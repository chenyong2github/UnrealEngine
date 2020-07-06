// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenSceneBVH.cpp
=============================================================================*/

#include "LumenSceneBVH.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "Net/Core/Misc/ResizableCircularQueue.h"

int32 GLumenGICardBVH = 1;
FAutoConsoleVariableRef CVarLumenGICardBVH(
	TEXT("r.LumenScene.CardBVH"),
	GLumenGICardBVH,
	TEXT("Whether to use BVH for Lumen card tracing."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenGICardBVHBuildBuckets = 12;
FAutoConsoleVariableRef CVarLumenGICardBVHBuildBuckets(
	TEXT("r.LumenScene.CardBVH.BuildBuckets"),
	GLumenGICardBVHBuildBuckets,
	TEXT("Number of buckets used for BVH building."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenGICardLinearBVH = 0;
FAutoConsoleVariableRef CVarLumenGICardLinearBVH(
	TEXT("r.LumenScene.CardLinearBVH"),
	GLumenGICardLinearBVH,
	TEXT("Whether to use LBVH (faster) or SAH (higher quality) based BVH building."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GDefaultMaxCulledNodesPerCell = 8;
FAutoConsoleVariableRef CVarMaxCulledNodesPerCell(
	TEXT("r.LumenScene.CardBVH.DefaultMaxCulledNodesPerCell"),
	GDefaultMaxCulledNodesPerCell,
	TEXT("Controls how much memory is allocated for temporary BVH nodes during BVH card culling."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GDefaultMaxCulledCardsPerCell = 64;
FAutoConsoleVariableRef CVarMaxCulledCardsPerCell(
	TEXT("r.LumenScene.CardBVH.DefaultMaxCulledCardsPerCell"),
	GDefaultMaxCulledCardsPerCell,
	TEXT("Controls how much memory is allocated for culled cards during BVH card culling."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

class FBuildNodeId
{
public:
	FBuildNodeId()
		: Value(-1)
	{
	}

	FBuildNodeId(int32 InValue)
		: Value(InValue)
	{
	}

	bool IsValid() const
	{
		return Value >= 0;
	}

	int32 Value;
};

class FBVHBuildNode
{
public:
	FBox BBox;
	uint32 LumenCardId;
	uint32 Hash;
	FBuildNodeId Children[BVH_WIDTH];

	bool HasAnyChild() const
	{
		for (int32 ChildIndex = 0; ChildIndex < UE_ARRAY_COUNT(Children); ++ChildIndex)
		{
			if (Children[ChildIndex].IsValid())
			{
				return true;
			}
		}

		return false;
	}
};

class FBVHBuildPrimitive
{
public:
	FBox BBox;
	uint32 LumenCardId;
	uint32 Hash;
};

class FBuildNodeManager
{
public:
	void Reserve(int32 NumNodes)
	{
		Nodes.Reserve(NumNodes);
	}

	FBuildNodeId Add(const FBVHBuildNode& NewNode)
	{
		Nodes.Add(NewNode);
		return FBuildNodeId(Nodes.Num() - 1);
	}

	const FBVHBuildNode& Get(FBuildNodeId Id) const
	{
		check(Id.Value >= 0 && Id.Value < Nodes.Num());
		return Nodes[Id.Value];
	}

	FBVHBuildNode& Get(FBuildNodeId Id)
	{
		check(Id.Value >= 0 && Id.Value < Nodes.Num());
		return Nodes[Id.Value];
	}

	int32 Num() const
	{
		return Nodes.Num();
	}

private:
	TArray<FBVHBuildNode, SceneRenderingAllocator> Nodes;
};

float BoxSurfaceArea(const FBox& Box)
{
	const FVector Extent = Box.GetExtent();
	return 2.0f * (Extent.X * Extent.Y + Extent.Y * Extent.Z + Extent.Z * Extent.X);
}

struct FMultiNode
{
	FMultiNode(int32 InPrimitiveRangeStart, int32 InPrimitiveRangeEnd, float InSAH)
		: PrimitiveRangeStart(InPrimitiveRangeStart)
		, PrimitiveRangeEnd(InPrimitiveRangeEnd)
		, SAH(InSAH)
	{
	}

	int32 PrimitiveRangeStart;
	int32 PrimitiveRangeEnd;
	float SAH;
};

typedef TArray<FBVHBuildPrimitive, SceneRenderingAllocator> FInputPrimitives;
typedef TArray<uint16, SceneRenderingAllocator> FInputPrimitiveIds;

void SplitBVHMultiNodes(
	TArray<FMultiNode, SceneRenderingAllocator>& MultiNodes, 
	int32 InputPrimitiveRangeStart,
	int32 InputPrimitiveRangeEnd,
	const FInputPrimitives& Primitives,
	FInputPrimitiveIds& PrimitiveIds,
	FBuildNodeManager& NodeManager)
{
	LLM_SCOPE(ELLMTag::Lumen);

	const int32 NumBuckets = FMath::Clamp(GLumenGICardBVHBuildBuckets, 1, 100);

	int32 NextMultiNodeIndex = 0;
	do
	{
		FMultiNode MultiNode = MultiNodes[NextMultiNodeIndex];
		MultiNodes.RemoveAtSwap(NextMultiNodeIndex);

		FBox CentroidBBox;
		CentroidBBox.Init();
		for (int32 PrimitiveIndex = MultiNode.PrimitiveRangeStart; PrimitiveIndex < MultiNode.PrimitiveRangeEnd; ++PrimitiveIndex)
		{
			const uint32 PrimitiveId = PrimitiveIds[PrimitiveIndex];
			CentroidBBox += Primitives[PrimitiveId].BBox.GetCenter();
		}
		const FVector CentroidBBoxExtent = CentroidBBox.GetExtent();

		// Select split axis based on the longest side of the current multinode's BBox.
		const float CentroidBBoxExtentComponentMax = CentroidBBoxExtent.GetMax();

		// By default split array in two equal halves.
		int32 PrimitiveNewRangeStart = MultiNode.PrimitiveRangeStart;
		int32 PrimitiveNewRangeMid = (MultiNode.PrimitiveRangeStart + MultiNode.PrimitiveRangeEnd) / 2;
		int32 PrimitiveNewRangeEnd = MultiNode.PrimitiveRangeEnd;

		float BestSplitSAH0 = PrimitiveNewRangeMid - PrimitiveNewRangeStart;
		float BestSplitSAH1 = PrimitiveNewRangeEnd - PrimitiveNewRangeMid;

		// No need to split if all boxes are in the same place.
		if (CentroidBBoxExtentComponentMax > 1.0f)
		{
			if (GLumenGICardLinearBVH)
			{ 
				// Split based on hashed bits.
				const uint32 FirstPrimitiveId = PrimitiveIds[MultiNode.PrimitiveRangeStart];
				const uint32 LastPrimitiveId = PrimitiveIds[MultiNode.PrimitiveRangeEnd - 1];
				const uint32 FirstHash = Primitives[FirstPrimitiveId].Hash;
				const uint32 LastHash = Primitives[LastPrimitiveId].Hash;

				if (FirstHash != LastHash)
				{
					// Binary search for best split.
					const uint32 CommonPrefix = FMath::CountLeadingZeros(FirstHash ^ LastHash);
					int32 BestSplit = MultiNode.PrimitiveRangeStart;
					int32 Step = MultiNode.PrimitiveRangeEnd - MultiNode.PrimitiveRangeStart;
					do
					{
						Step = (Step + 1) >> 1;
						int32 Split = BestSplit + Step;
						if (Split < MultiNode.PrimitiveRangeEnd)
						{
							const uint32 SplitPrimitiveId = PrimitiveIds[Split];
							uint32 SplitHash = Primitives[SplitPrimitiveId].Hash;
							uint32 SplitPrefix = FMath::CountLeadingZeros(FirstHash ^ SplitHash);
							if (SplitPrefix > CommonPrefix)
							{
								BestSplit = Split;
							}
						}
					} while (Step > 1);

					if (BestSplit > PrimitiveNewRangeStart && BestSplit < PrimitiveNewRangeEnd)
					{
						PrimitiveNewRangeMid = BestSplit;
						BestSplitSAH0 = PrimitiveNewRangeMid - PrimitiveNewRangeStart;
						BestSplitSAH1 = PrimitiveNewRangeEnd - PrimitiveNewRangeMid;
					}
				}
			}
			else
			{
				// Search for a split value with best SAH.
				float BestSplit = FLT_MAX;
				float BestSlitScore = FLT_MAX;

				const int32 SplitAxis = CentroidBBoxExtent.X >= CentroidBBoxExtentComponentMax ? 0 : (CentroidBBoxExtent.Y >= CentroidBBoxExtentComponentMax ? 1 : 2);

				for (int32 BucketIndex = 0; BucketIndex < NumBuckets; ++BucketIndex)
				{
					int32 NumSplitPrimitives[2] = { 0, 0 };
					FBox SplitBBox[2];
					SplitBBox[0].Init();
					SplitBBox[1].Init();

					const float SplitMin = CentroidBBox.Min[SplitAxis];
					const float SplitMax = CentroidBBox.Max[SplitAxis];
					const float Split = SplitMin + (SplitMax - SplitMin) * ((BucketIndex + 1) / float(NumBuckets + 1));

					for (int32 PrimitiveIndex = MultiNode.PrimitiveRangeStart; PrimitiveIndex < MultiNode.PrimitiveRangeEnd; ++PrimitiveIndex)
					{
						const uint32 PrimitiveId = PrimitiveIds[PrimitiveIndex];
						const FBVHBuildPrimitive& Primitive = Primitives[PrimitiveId];
						if (Primitive.BBox.GetCenter()[SplitAxis] <= Split)
						{
							++NumSplitPrimitives[0];
							SplitBBox[0] += Primitive.BBox;
						}
						else
						{
							++NumSplitPrimitives[1];
							SplitBBox[1] += Primitive.BBox;
						}
					}

					const float SAH0 = BoxSurfaceArea(SplitBBox[0]) * NumSplitPrimitives[0];
					const float SAH1 = BoxSurfaceArea(SplitBBox[1]) * NumSplitPrimitives[1];
					const float SplitScore = SAH0 + SAH1;
					if (SplitScore < BestSlitScore
						&& NumSplitPrimitives[0] > 0
						&& NumSplitPrimitives[1] > 0)
					{
						BestSlitScore = SplitScore;
						BestSplit = Split;
						BestSplitSAH0 = SAH0;
						BestSplitSAH1 = SAH1;
					}
				}

				// Split based on a found best split plane.
				if (BestSplit < FLT_MAX)
				{
					PrimitiveNewRangeStart = PrimitiveIds.Num();
					PrimitiveIds.Reserve(PrimitiveIds.Num() + MultiNode.PrimitiveRangeEnd - MultiNode.PrimitiveRangeStart);
					for (int32 PrimitiveIndex = MultiNode.PrimitiveRangeStart; PrimitiveIndex < MultiNode.PrimitiveRangeEnd; ++PrimitiveIndex)
					{
						const uint32 PrimitiveId = PrimitiveIds[PrimitiveIndex];
						if (Primitives[PrimitiveId].BBox.GetCenter()[SplitAxis] <= BestSplit)
						{
							PrimitiveIds.Add(PrimitiveId);
						}
					}
					PrimitiveNewRangeMid = PrimitiveIds.Num();
					for (int32 PrimitiveIndex = MultiNode.PrimitiveRangeStart; PrimitiveIndex < MultiNode.PrimitiveRangeEnd; ++PrimitiveIndex)
					{
						const uint32 PrimitiveId = PrimitiveIds[PrimitiveIndex];
						if (Primitives[PrimitiveId].BBox.GetCenter()[SplitAxis] > BestSplit)
						{
							PrimitiveIds.Add(PrimitiveId);
						}
					}
					PrimitiveNewRangeEnd = PrimitiveIds.Num();
				}
			}
		}

		check(PrimitiveNewRangeStart < PrimitiveNewRangeMid && PrimitiveNewRangeMid < PrimitiveNewRangeEnd);
		MultiNodes.Add(FMultiNode(PrimitiveNewRangeStart, PrimitiveNewRangeMid, BestSplitSAH0));
		MultiNodes.Add(FMultiNode(PrimitiveNewRangeMid, PrimitiveNewRangeEnd, BestSplitSAH1));

		// Find element with largest SAH which needs to be splitted.
		float NextMultiNodeSAH = -1.0f;
		NextMultiNodeIndex = -1;
		for (int32 MultiNodeIndex = 0; MultiNodeIndex < MultiNodes.Num(); ++MultiNodeIndex)
		{
			MultiNode = MultiNodes[MultiNodeIndex];
			if (NextMultiNodeSAH < MultiNode.SAH)
			{
				const int32 PrimitiveRangeSize = MultiNode.PrimitiveRangeEnd - MultiNode.PrimitiveRangeStart;
				if (PrimitiveRangeSize > BVH_WIDTH)
				{
					NextMultiNodeSAH = MultiNode.SAH;
					NextMultiNodeIndex = MultiNodeIndex;
				}
			}
		}
	} while (MultiNodes.Num() < BVH_WIDTH && NextMultiNodeIndex >= 0);
}

struct FLumenCardBVHNodeData
{
	// Must match usf
	enum { LumenCardBVHNodeDataStrideInFloat4s = (2 * BVH_WIDTH) };

	FVector4 Data[LumenCardBVHNodeDataStrideInFloat4s];

	FLumenCardBVHNodeData(const class FLumenSceneCardBVHNode& RESTRICT Node);
};

FLumenCardBVHNodeData::FLumenCardBVHNodeData(const FLumenSceneCardBVHNode& RESTRICT Node)
{
	// Note: layout must match GetLumenCardBVHNodeData in usf
	for (int32 ChildIndex = 0; ChildIndex < BVH_WIDTH; ++ChildIndex)
	{
		Data[ChildIndex * 2 + 0] = FVector4(Node.Children[ChildIndex].BBoxCenter, 0.0f);
		Data[ChildIndex * 2 + 0].W = *(const float*)&Node.Children[ChildIndex].LumenCardId;

		Data[ChildIndex * 2 + 1] = FVector4(Node.Children[ChildIndex].BBoxExtent, 0.0f);
		Data[ChildIndex * 2 + 1].W = *(const float*)&Node.Children[ChildIndex].ChildId;
	}
}

void InitNullCardBVHData(FRWBufferStructured& CardBVHData)
{
	LLM_SCOPE(ELLMTag::Lumen);

	EPixelFormat BufferFormat = PF_A32B32G32R32F;
	uint32 BytesPerElement = GPixelFormats[BufferFormat].BlockBytes;

	CardBVHData.Initialize(BytesPerElement, FLumenCardBVHNodeData::LumenCardBVHNodeDataStrideInFloat4s, 0, TEXT("NullCardBVHData"));

	FLumenSceneCardBVHNode NullBVHNode;
	FLumenCardBVHNodeData* BVHNodeData = (FLumenCardBVHNodeData*)RHILockStructuredBuffer(CardBVHData.Buffer, 0, BytesPerElement * FLumenCardBVHNodeData::LumenCardBVHNodeDataStrideInFloat4s, EResourceLockMode::RLM_WriteOnly);
	*BVHNodeData = FLumenCardBVHNodeData(NullBVHNode);
	RHIUnlockStructuredBuffer(CardBVHData.Buffer);
}

FBuildNodeId BuildBVH(
	int32 InputPrimitiveRangeStart,
	int32 InputPrimitiveRangeEnd,
	const FInputPrimitives& Primitives,
	FInputPrimitiveIds& PrimitiveIds,
	FBuildNodeManager& NodeManager)
{
	LLM_SCOPE(ELLMTag::Lumen);

	struct FStackElement
	{
		FStackElement(int32 InPrimitiveRangeStart, int32 InPrimitiveRangeEnd, FBuildNodeId InParentNodeId, int32 InParentChildId)
			: PrimitiveRangeStart(InPrimitiveRangeStart)
			, PrimitiveRangeEnd(InPrimitiveRangeEnd)
			, ParentNodeId(InParentNodeId)
			, ParentChildId(InParentChildId)
		{
		}

		int32 PrimitiveRangeStart;
		int32 PrimitiveRangeEnd;
		FBuildNodeId ParentNodeId;
		int32 ParentChildId;
	};

	FBuildNodeId RootNodeId;
	TResizableCircularQueue<FStackElement, SceneRenderingAllocator> BuildNodeStack(64);
	BuildNodeStack.Enqueue(FStackElement(InputPrimitiveRangeStart, InputPrimitiveRangeEnd, FBuildNodeId(), -1));

	TArray<FMultiNode, SceneRenderingAllocator> MultiNodeStack;

	while (BuildNodeStack.Count() > 0)
	{
		FBuildNodeId NodeId;
		FBVHBuildNode Node;
		Node.BBox.Init();
		Node.LumenCardId = INVALID_PROXY_CARD_ID;

		const FStackElement CurrNode = BuildNodeStack.Peek();
		BuildNodeStack.Pop();

		const int32 PrimitiveRangeStart = CurrNode.PrimitiveRangeStart;
		const int32 PrimitiveRangeEnd = CurrNode.PrimitiveRangeEnd;

		for (int32 PrimitiveIndex = PrimitiveRangeStart; PrimitiveIndex < PrimitiveRangeEnd; ++PrimitiveIndex)
		{
			const uint32 PrimitiveId = PrimitiveIds[PrimitiveIndex];
			Node.BBox += Primitives[PrimitiveId].BBox;
		}

		const int32 PrimitiveRangeSize = PrimitiveRangeEnd - PrimitiveRangeStart;
		if (PrimitiveRangeSize == 0)
		{
			NodeId = NodeManager.Add(Node);
		}
		else if (PrimitiveRangeSize == 1)
		{
			const uint32 PrimitiveId = PrimitiveIds[PrimitiveRangeStart];
			Node.LumenCardId = Primitives[PrimitiveId].LumenCardId;
			NodeId = NodeManager.Add(Node);
		}
		else if (PrimitiveRangeSize <= BVH_WIDTH)
		{
			Node.BBox.Init();
			for (int32 ChildIndex = 0; ChildIndex < PrimitiveRangeSize; ++ChildIndex)
			{
				FBVHBuildNode ChildNode;
				const uint32 PrimitiveId = PrimitiveIds[PrimitiveRangeStart + ChildIndex];
				ChildNode.BBox = Primitives[PrimitiveId].BBox;
				ChildNode.LumenCardId = Primitives[PrimitiveId].LumenCardId;

				Node.BBox += ChildNode.BBox;
				Node.Children[ChildIndex] = NodeManager.Add(ChildNode);
			}
			NodeId = NodeManager.Add(Node);
		}
		else if (PrimitiveRangeSize > BVH_WIDTH)
		{
			MultiNodeStack.Reset();
			MultiNodeStack.Add(FMultiNode(PrimitiveRangeStart, PrimitiveRangeEnd, FLT_MAX));

			SplitBVHMultiNodes(MultiNodeStack, PrimitiveRangeStart, PrimitiveRangeEnd, Primitives, PrimitiveIds, NodeManager);

			NodeId = NodeManager.Add(Node);

			int32 NextChildrenIndex = 0;
			for (int32 MultiNodeIndex = 0; MultiNodeIndex < MultiNodeStack.Num(); ++MultiNodeIndex)
			{
				const FMultiNode& MultiNode = MultiNodeStack[MultiNodeIndex];
				if (MultiNode.PrimitiveRangeStart < MultiNode.PrimitiveRangeEnd)
				{
					BuildNodeStack.Enqueue(FStackElement(MultiNode.PrimitiveRangeStart, MultiNode.PrimitiveRangeEnd, NodeId, NextChildrenIndex));
					++NextChildrenIndex;
				}
			}
		}

		if (NodeId.IsValid())
		{
			if (CurrNode.ParentNodeId.IsValid())
			{
				// When child node is ready we can link parent to it.
				FBVHBuildNode& ParentNode = NodeManager.Get(CurrNode.ParentNodeId);
				ParentNode.Children[CurrNode.ParentChildId] = NodeId;
			}
			else
			{
				RootNodeId = NodeId;
			}
		}
	}

	return RootNodeId;
}

void FlattenBVH(TArray<FLumenSceneCardBVHNode>& OutCardBVH, int32& OutBVHDepth, const FBuildNodeManager& NodeManager, FBuildNodeId RootNodeId)
{
	LLM_SCOPE(ELLMTag::Lumen);

	OutBVHDepth = 0;

	struct FStackElement
	{
		FStackElement(FBuildNodeId InNodeId, uint32 InFlattenedNodeId, int32 InBVHLevel)
			: NodeId(InNodeId)
			, FlattenedNodeId(InFlattenedNodeId)
			, BVHLevel(InBVHLevel)
		{
		}

		FBuildNodeId NodeId;
		uint32 FlattenedNodeId;
		int32 BVHLevel;
	};

	TResizableCircularQueue<FStackElement, SceneRenderingAllocator> BuildNodeStack(64);
	check(RootNodeId.IsValid());
	BuildNodeStack.Enqueue(FStackElement(RootNodeId, OutCardBVH.Add(FLumenSceneCardBVHNode()), 0));
	
	while (BuildNodeStack.Count() > 0)
	{
		const FStackElement CurrNode = BuildNodeStack.Peek();
		BuildNodeStack.Pop();

		OutBVHDepth = FMath::Max(OutBVHDepth, CurrNode.BVHLevel + 1);

		// Make sure we can add all children nodes without invalidating parent one.
		OutCardBVH.Reserve(BVH_WIDTH);

		FLumenSceneCardBVHNode& FlattenedNode = OutCardBVH[CurrNode.FlattenedNodeId];
		const FBVHBuildNode& BuildNode = NodeManager.Get(CurrNode.NodeId);

		// First write out all the nodes with children.
		int32 NextChildIndex = 0;
		for (int32 ChildIndex = 0; ChildIndex < BVH_WIDTH; ++ChildIndex)
		{
			if (BuildNode.Children[ChildIndex].IsValid())
			{
				const FBVHBuildNode& ChildNode = NodeManager.Get(BuildNode.Children[ChildIndex]);
				if (ChildNode.HasAnyChild())
				{
					const int32 ChildFlattenedId = OutCardBVH.Add(FLumenSceneCardBVHNode());
					checkf(ChildFlattenedId < INVALID_BVH_NODE_ID, TEXT("Too many BVH nodes. Code assumes uint16 is enough to store a BVH node id."));

					FlattenedNode.Children[NextChildIndex].BBoxCenter = ChildNode.BBox.GetCenter();
					FlattenedNode.Children[NextChildIndex].BBoxExtent = ChildNode.BBox.GetExtent();
					FlattenedNode.Children[NextChildIndex].LumenCardId = ChildNode.LumenCardId;
					FlattenedNode.Children[NextChildIndex].ChildId = ChildFlattenedId;
					++NextChildIndex;
					
					BuildNodeStack.Enqueue(FStackElement(BuildNode.Children[ChildIndex], ChildFlattenedId, CurrNode.BVHLevel + 1));
				}
			}
		}

		// Finally write out all the leaf node.
		for (int32 ChildIndex = 0; ChildIndex < BVH_WIDTH; ++ChildIndex)
		{
			if (BuildNode.Children[ChildIndex].IsValid())
			{
				const FBVHBuildNode& ChildNode = NodeManager.Get(BuildNode.Children[ChildIndex]);
				if (!ChildNode.HasAnyChild())
				{
					FlattenedNode.Children[NextChildIndex].BBoxCenter = ChildNode.BBox.GetCenter();
					FlattenedNode.Children[NextChildIndex].BBoxExtent = ChildNode.BBox.GetExtent();
					FlattenedNode.Children[NextChildIndex].LumenCardId = ChildNode.LumenCardId;
					++NextChildIndex;
				}
			}
		}
	}
}

uint32 ComputeNodeHash(const FBox& WorldBBox, const FBox& NodeBBox)
{
	FVector Pos = NodeBBox.GetCenter();

	// Rescale to [0;1023]
	Pos = ((Pos - WorldBBox.Min * 1023.0f)) / (WorldBBox.Max - WorldBBox.Min);
	const int32 X = FMath::Clamp((int)Pos.X, 0, 1023);
	const int32 Y = FMath::Clamp((int)Pos.Y, 0, 1023);
	const int32 Z = FMath::Clamp((int)Pos.Z, 0, 1023);

	const uint32 NodeHash = FGenericPlatformMath::MortonCode3(X) | (FGenericPlatformMath::MortonCode3(Y) << 1) | (FGenericPlatformMath::MortonCode3(Z) << 2);
	return NodeHash;
}

void UpdateCardBVH(bool bUseBVH, FLumenSceneData& SceneData, FRHICommandListImmediate& RHICmdList)
{
	LLM_SCOPE(ELLMTag::Lumen);

	SceneData.CardBVH.Empty();
	SceneData.BVHDepth = 0;

	if (bUseBVH)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UpdateCardBVH);

		const double StartTime = FPlatformTime::Seconds();

		FBuildNodeManager NodeManager;
		NodeManager.Reserve(2 * SceneData.Cards.Num());

		FInputPrimitives InputPrimitives;
		InputPrimitives.Reserve(SceneData.Cards.Num());

		FInputPrimitiveIds InputPrimitiveIds;
		InputPrimitiveIds.Reserve(8 * SceneData.Cards.Num());

		// Build proxy card LOD hierarchy subtrees.
		checkf(SceneData.Cards.Num() < INVALID_PROXY_CARD_ID, TEXT("Too many proxy cards. Shader traversal and C++ builder code assumes uint16 is enough to store a proxy card id."));

		FBox WorldBBox;
		WorldBBox.Init();
		for (int32 CardIndex = 0; CardIndex < SceneData.Cards.Num(); ++CardIndex)
		{
			if (SceneData.Cards[CardIndex].bVisible)
			{
				FBVHBuildPrimitive BuildPrimitive;
				BuildPrimitive.BBox = SceneData.Cards[CardIndex].WorldBounds;
				BuildPrimitive.LumenCardId = CardIndex;
				WorldBBox += BuildPrimitive.BBox;
				InputPrimitiveIds.Add(InputPrimitives.Num());
				InputPrimitives.Add(BuildPrimitive);
			}
		}

		if (GLumenGICardLinearBVH)
		{
			struct FComparePrimitivesByHash
			{
				FComparePrimitivesByHash(const FInputPrimitives& InInputPrimitives)
					: InputPrimitives(InInputPrimitives)
				{
				}

				const FInputPrimitives& InputPrimitives;

				FORCEINLINE bool operator()(const uint32& IndexA, const uint32& IndexB) const
				{
					return InputPrimitives[IndexA].Hash < InputPrimitives[IndexB].Hash;
				}
			};

			for (int32 PrimitiveIndex = 0; PrimitiveIndex < InputPrimitives.Num(); ++PrimitiveIndex)
			{
				FBVHBuildPrimitive& Primitive = InputPrimitives[PrimitiveIndex];
				Primitive.Hash = ComputeNodeHash(WorldBBox, Primitive.BBox);
			}

			InputPrimitiveIds.Sort(FComparePrimitivesByHash(InputPrimitives));
		}

		FBuildNodeId RootId = BuildBVH(0, InputPrimitives.Num(), InputPrimitives, InputPrimitiveIds, NodeManager);

		// Root node will be inlined by FlattenBVH so need to add a new one if root contains data.
		const FBVHBuildNode& RootNode = NodeManager.Get(RootId);
		if (RootNode.LumenCardId != INVALID_PROXY_CARD_ID)
		{
			FBVHBuildNode NewRootNode;
			NewRootNode.BBox = RootNode.BBox;
			NewRootNode.Children[0] = RootId;
			RootId = NodeManager.Add(NewRootNode);
		}

		SceneData.CardBVH.Reserve(NodeManager.Num());
		FlattenBVH(SceneData.CardBVH, SceneData.BVHDepth, NodeManager, RootId);

		const float TimeElapsed = FPlatformTime::Seconds() - StartTime;

		if (TimeElapsed > .02f)
		{
			UE_LOG(LogRenderer, Log, TEXT("UpdateCardBVH NumCards %u NumVisible %u NumNodes %u Depth %u %.2fs"), SceneData.Cards.Num(), InputPrimitives.Num(), SceneData.CardBVH.Num(), SceneData.BVHDepth, TimeElapsed);
		}
	}
	else
	{
		// Just fill an empty BVH
		FLumenSceneCardBVHNode NullRootNode;
		SceneData.CardBVH.Add(NullRootNode);
	}

	const uint32 NumBVHNodes = SceneData.CardBVH.Num();
	const uint32 BVHNumFloat4s = FMath::RoundUpToPowerOfTwo(NumBVHNodes * FLumenCardBVHNodeData::LumenCardBVHNodeDataStrideInFloat4s);
	const uint32 BVHNumBytes = BVHNumFloat4s * sizeof(float) * 4;

	ResizeResourceIfNeeded(RHICmdList, SceneData.CardBVHBuffer, BVHNumBytes, TEXT("CardBVH"));

	FLumenCardBVHNodeData* BVHNodeData = (FLumenCardBVHNodeData*)RHILockStructuredBuffer(SceneData.CardBVHBuffer.Buffer, 0, BVHNumBytes, RLM_WriteOnly);

	for (uint32 NodeIndex = 0; NodeIndex < NumBVHNodes; ++NodeIndex)
	{
		BVHNodeData[NodeIndex] = FLumenCardBVHNodeData(SceneData.CardBVH[NodeIndex]);
	}

	RHIUnlockStructuredBuffer(SceneData.CardBVHBuffer.Buffer);
}

class FInitBVHQueryUAVCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FInitBVHQueryUAVCS)
	SHADER_USE_PARAMETER_STRUCT(FInitBVHQueryUAVCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWBVHQueryNum)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 64;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FInitBVHQueryUAVCS, "/Engine/Private/Lumen/LumenCardBVH.usf", "InitBVHQueryUAVCS", SF_Compute);

class FCompactCardListIntoGridCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCompactCardListIntoGridCS)
	SHADER_USE_PARAMETER_STRUCT(FCompactCardListIntoGridCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCulledCardGridHeader)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCulledCardGridData)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCulledCardGridNext)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWUsedCardData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CulledCardLinkHeadGrid)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CulledCardLinkData)
		SHADER_PARAMETER(FIntVector, CullGridSize)
		SHADER_PARAMETER(uint32, CulledCardDataGridSize)
	END_SHADER_PARAMETER_STRUCT()

	class FCullUnusedLinks : SHADER_PERMUTATION_BOOL("CULL_UNUSED_LINKS");
	using FPermutationDomain = TShaderPermutationDomain<FCullUnusedLinks>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 4;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FCompactCardListIntoGridCS, "/Engine/Private/Lumen/LumenCardBVH.usf", "CompactCardListIntoGridCS", SF_Compute);

void FBVHCulling::Init(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, FIntVector InCullGridSize, int32 InMaxCulledNodesPerCell, int32 InMaxCulledCardsPerCell)
{
	LLM_SCOPE(ELLMTag::Lumen);

	CullGridSize = InCullGridSize;
	NumCullGridCells = CullGridSize.X * CullGridSize.Y * CullGridSize.Z;
	ensureMsgf(NumCullGridCells < MAX_uint16, TEXT("BVH: too many cull cells: %u.  Grid cell index is packed into 16 bits in the shader"), NumCullGridCells);
	MaxCulledNodesPerCell = InMaxCulledNodesPerCell > 0 ? InMaxCulledNodesPerCell : GDefaultMaxCulledNodesPerCell;
	MaxCulledCardsPerCell = InMaxCulledCardsPerCell > 0 ? InMaxCulledCardsPerCell : GDefaultMaxCulledCardsPerCell;

	// Temporary buffers for the BVH traversal.
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const uint32 MaxTemporaryBVHQueries = NumCullGridCells * MaxCulledNodesPerCell;

		BVHQueryArray[Index] = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), MaxTemporaryBVHQueries), Index == 0 ? TEXT("BVHQueryArray0") : TEXT("BVHQueryArray1"));
		BVHQueryNum[Index] = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(4), Index == 0 ? TEXT("BVHQueryNum0") : TEXT("BVHQueryNum1"));

		BVHQueryArrayUAV[Index] = GraphBuilder.CreateUAV(BVHQueryArray[Index], PF_R32_UINT);
		BVHQueryNumUAV[Index] = GraphBuilder.CreateUAV(BVHQueryNum[Index], PF_R32_UINT);

		BVHQueryArraySRV[Index] = GraphBuilder.CreateSRV(BVHQueryArray[Index], PF_R32_UINT);
		BVHQueryNumSRV[Index] = GraphBuilder.CreateSRV(BVHQueryNum[Index], PF_R32_UINT);
	}

	// Linked list for temporary culled cards.
	CulledCardLinkData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumCullGridCells * MaxCulledCardsPerCell * CulledCardLinkStride), TEXT("CulledCardLinkData"));
	CulledCardLinkNext = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("CulledCardLinkNext"));
	CulledCardLinkHeadGrid = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumCullGridCells), TEXT("CulledCardLinkHeadGrid"));
	CulledCardLinkDataUAV = GraphBuilder.CreateUAV(CulledCardLinkData, PF_R32_UINT);
	CulledCardLinkNextUAV = GraphBuilder.CreateUAV(CulledCardLinkNext, PF_R32_UINT);
	CulledCardLinkHeadGridUAV = GraphBuilder.CreateUAV(CulledCardLinkHeadGrid, PF_R32_UINT);
	CulledCardLinkDataSRV = GraphBuilder.CreateSRV(CulledCardLinkData, PF_R32_UINT);
	CulledCardLinkHeadGridSRV = GraphBuilder.CreateSRV(CulledCardLinkHeadGrid, PF_R32_UINT);

	// Compacted grid of culled cards.
	CulledCardGridHeader = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumCullGridCells * CulledCardGridHeaderStride), TEXT("CulledCardGridHeader"));
	CulledCardGridData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumCullGridCells * MaxCulledCardsPerCell), TEXT("CulledCardGridData"));
	CulledCardGridNext = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("CulledCardGridNext"));
	CulledCardGridHeaderUAV = GraphBuilder.CreateUAV(CulledCardGridHeader, PF_R32_UINT);
	CulledCardGridDataUAV = GraphBuilder.CreateUAV(CulledCardGridData, PF_R32_UINT);
	CulledCardGridNextUAV = GraphBuilder.CreateUAV(CulledCardGridNext, PF_R32_UINT);
	CulledCardGridHeaderSRV = GraphBuilder.CreateSRV(CulledCardGridHeader, PF_R32_UINT);
	CulledCardGridDataSRV = GraphBuilder.CreateSRV(CulledCardGridData, PF_R32_UINT);

	FComputeShaderUtils::ClearUAV(GraphBuilder, ShaderMap, CulledCardLinkHeadGridUAV, 0xFFFFFFFF);
	FComputeShaderUtils::ClearUAV(GraphBuilder, ShaderMap, CulledCardLinkNextUAV, 0);
	FComputeShaderUtils::ClearUAV(GraphBuilder, ShaderMap, CulledCardGridNextUAV, 0);
}

void FBVHCulling::InitNextPass(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, int32 BVHLevel)
{
	LLM_SCOPE(ELLMTag::Lumen);

	const bool bFirstPass = BVHLevel == 0;
	const int32 SrcBufferIndex = (BVHLevel + 0) % 2;
	const int32 DstBufferIndex = (BVHLevel + 1) % 2;
	FRDGBufferRef SrcBVHQueryNum = BVHQueryNum[SrcBufferIndex];
	FRDGBufferSRVRef SrcBVHQueryNumSRV = BVHQueryNumSRV[SrcBufferIndex];
	FRDGBufferSRVRef SrcBVHQueryArraySRV = BVHQueryArraySRV[SrcBufferIndex];
	FRDGBufferUAVRef DstBVHQueryNumUAV = BVHQueryNumUAV[DstBufferIndex];
	FRDGBufferUAVRef DstBVHQueryArrayUAV = BVHQueryArrayUAV[DstBufferIndex];

	// Clear BVHQueryNumUAV which also serves as indirect dispatch arguments for the next pass.
	{
		FInitBVHQueryUAVCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FInitBVHQueryUAVCS::FParameters>();
		PassParameters->RWBVHQueryNum = DstBVHQueryNumUAV;

		auto ComputeShader = ShaderMap->GetShader<FInitBVHQueryUAVCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("InitBVHQueryNum"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1));
	}

	BVHCullingParameters.RWBVHQueryArray = DstBVHQueryArrayUAV;
	BVHCullingParameters.RWBVHQueryNum = DstBVHQueryNumUAV;
	BVHCullingParameters.RWCulledCardLinkNext = CulledCardLinkNextUAV;
	BVHCullingParameters.RWCulledCardLinkHeadGrid = CulledCardLinkHeadGridUAV;
	BVHCullingParameters.RWCulledCardLinkData = CulledCardLinkDataUAV;
	BVHCullingParameters.InputBVHQueryArray = bFirstPass ? nullptr : SrcBVHQueryArraySRV;
	BVHCullingParameters.InputBVHQueryNum = bFirstPass ? nullptr : SrcBVHQueryNumSRV;
	BVHCullingParameters.IndirectDispatchArgsBuffer = bFirstPass ? nullptr : SrcBVHQueryNum;
	BVHCullingParameters.CullGridSize = CullGridSize;
	BVHCullingParameters.NumCullGridCells = NumCullGridCells;
	BVHCullingParameters.MaxCulledCardLinks = NumCullGridCells * MaxCulledCardsPerCell;
	BVHCullingParameters.MaxBVHQueries = NumCullGridCells * MaxCulledNodesPerCell;
}

void FBVHCulling::CompactListIntoGrid(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, FRDGBufferUAVRef UsedCardDataUAV)
{
	LLM_SCOPE(ELLMTag::Lumen);

	FCompactCardListIntoGridCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCompactCardListIntoGridCS::FParameters>();
	PassParameters->RWCulledCardGridHeader = CulledCardGridHeaderUAV;
	PassParameters->RWCulledCardGridData = CulledCardGridDataUAV;
	PassParameters->RWCulledCardGridNext = CulledCardGridNextUAV;
	PassParameters->RWUsedCardData = UsedCardDataUAV;

	PassParameters->CulledCardLinkHeadGrid = CulledCardLinkHeadGridSRV;
	PassParameters->CulledCardLinkData = CulledCardLinkDataSRV;
	PassParameters->CullGridSize = CullGridSize;
	PassParameters->CulledCardDataGridSize = NumCullGridCells * MaxCulledCardsPerCell;

	FCompactCardListIntoGridCS::FPermutationDomain PermutationVector;
	PermutationVector.Set< FCompactCardListIntoGridCS::FCullUnusedLinks >(UsedCardDataUAV != nullptr);
	auto ComputeShader = ShaderMap->GetShader<FCompactCardListIntoGridCS>(PermutationVector);

	const FIntVector GroupSize = FIntVector::DivideAndRoundUp(CullGridSize, FCompactCardListIntoGridCS::GetGroupSize());

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("CompactCulledCardListIntoGrid"),
		ComputeShader,
		PassParameters,
		GroupSize);
}