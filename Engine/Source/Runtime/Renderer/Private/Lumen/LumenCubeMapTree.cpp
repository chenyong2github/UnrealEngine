// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenCubeMapTree.cpp
=============================================================================*/

#include "LumenCubeMapTree.h"
#include "RendererPrivate.h"
#include "MeshCardRepresentation.h"
#include "ComponentRecreateRenderStateContext.h"
#include "LumenSceneUtils.h"

#define LUMEN_LOG_HITCHES 0

#define INVALID_CUBE_MAP_TREE_ID 0x7fffffff

FLumenCubeMapTreeLUTAtlas GLumenCubeMapTreeLUTAtlas;

int32 GLumenSceneMaxInstanceAddsPerFrame = 5000;
FAutoConsoleVariableRef CVarLumenSceneMaxInstanceAddsPerFrame(
	TEXT("r.LumenScene.MaxInstanceAddsPerFrame"),
	GLumenSceneMaxInstanceAddsPerFrame,
	TEXT("Max number of instanced allowed to be added per frame, remainder deferred to subsequent frames. (default 5000)"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenCubeMapTreeMinSize = 100.0f;
FAutoConsoleVariableRef CVarLumenCubeMapTreeMinSize(
	TEXT("r.LumenScene.CubeMapTreeMinSize"),
	GLumenCubeMapTreeMinSize,
	TEXT("Min mesh size to be included in the Lumen cube map tree."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenCubeMapTreeLUTAtlasSizeXY = 512;
FAutoConsoleVariableRef CVarLumeCubeMapTreeLUTAtlasSizeXY(
	TEXT("r.LumenScene.CubeMapTreeLUTAtlasSizeXY"),
	GLumenCubeMapTreeLUTAtlasSizeXY,
	TEXT("Max size of the cube map tree lookup volumes in X and Y."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenCubeMapTreeLUTAtlasSizeZ = 512;
FAutoConsoleVariableRef CVarLumeCubeMapTreeLUTAtlasSizeZ(
	TEXT("r.LumenScene.CubeMapTreeLUTAtlasSizeZ"),
	GLumenCubeMapTreeLUTAtlasSizeZ,
	TEXT("Max size of the cube map tree lookup volumes in Z."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenCubeMapTreeMergeInstances = 1;
FAutoConsoleVariableRef CVarLumenCubeMapTreeMergeInstances(
	TEXT("r.LumenScene.CubeMapTreeMergeInstances"),
	GLumenCubeMapTreeMergeInstances,
	TEXT("Whether to merge all instances of a Instanced Static Mesh Component into a single CubeMapTree."),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
	{
		FGlobalComponentRecreateRenderStateContext Context;
	}),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenCubeMapTreeMergeInstancesMaxSurfaceAreaRatio = 1.7f;
FAutoConsoleVariableRef CVarLumenCubeMapTreeMergeInstancesMaxSurfaceAreaRatio(
	TEXT("r.LumenScene.CubeMapTreeMergeInstancesMaxSurfaceAreaRatio"),
	GLumenCubeMapTreeMergeInstancesMaxSurfaceAreaRatio,
	TEXT("Only merge if the (combined box surface area) / (summed instance box surface area) < MaxSurfaceAreaRatio"),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
	{
		FGlobalComponentRecreateRenderStateContext Context;
	}),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenCubeMapTreeMergedResolutionScale = .3f;
FAutoConsoleVariableRef CVarLumenCubeMapTreeMergedResolutionScale(
	TEXT("r.LumenScene.CubeMapTreeMergedResolutionScale"),
	GLumenCubeMapTreeMergedResolutionScale,
	TEXT("Scale on the resolution calculation for a merged CubeMapTree.  This compensates for the merged box getting a higher resolution assigned due to being closer to the viewer."),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
	{
		FGlobalComponentRecreateRenderStateContext Context;
	}),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenCubeMapTreeMergedMaxWorldSize = 10000.0f;
FAutoConsoleVariableRef CVarLumenCubeMapTreeMergedMaxWorldSize(
	TEXT("r.LumenScene.CubeMapTreeMergedMaxWorldSize"),
	GLumenCubeMapTreeMergedMaxWorldSize,
	TEXT("Only merged bounds less than this size on any axis are considered, since Lumen Scene streaming relies on object granularity."),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
	{
		FGlobalComponentRecreateRenderStateContext Context;
	}),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenCubeMapTreeCullFaces = 1;
FAutoConsoleVariableRef CVarLumenCubeMapTreeCullFaces(
	TEXT("r.LumenScene.CubeMapTreeCullFaces"),
	GLumenCubeMapTreeCullFaces,
	TEXT(""),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
	{
		FGlobalComponentRecreateRenderStateContext Context;
	}),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

bool IsPrimitiveToDFObjectMappingRequired()
{
	return IsRayTracingEnabled();
}

void FLumenCubeMapTreeGPUData::FillData(const class FLumenCubeMapTree& RESTRICT CubeMapTree, FVector4* RESTRICT OutData)
{
	// Note: layout must match GetLumenCubeMapTreeData in usf

	const FMatrix WorldToLocal = CubeMapTree.LocalToWorld.Inverse();

	const FMatrix TransposedWorldToLocal = WorldToLocal.GetTransposed();

	OutData[0] = *(FVector4*)&TransposedWorldToLocal.M[0];
	OutData[1] = *(FVector4*)&TransposedWorldToLocal.M[1];
	OutData[2] = *(FVector4*)&TransposedWorldToLocal.M[2];
	
	const FVector LocalToLUTAtlasScale = FVector(CubeMapTree.SizeInLUTAtlas) / (CubeMapTree.LUTVolumeBounds.Max - CubeMapTree.LUTVolumeBounds.Min);

	FMatrix LocalToLUTAtlasCoord;
	LocalToLUTAtlasCoord.SetIdentity();
	LocalToLUTAtlasCoord.M[0][0] = LocalToLUTAtlasScale.X;
	LocalToLUTAtlasCoord.M[1][1] = LocalToLUTAtlasScale.Y;
	LocalToLUTAtlasCoord.M[2][2] = LocalToLUTAtlasScale.Z;
	LocalToLUTAtlasCoord.SetOrigin(-CubeMapTree.LUTVolumeBounds.Min * LocalToLUTAtlasScale + FVector(CubeMapTree.MinInLUTAtlas));

	const FMatrix TransposedWorldToLUTAtlasCoord = (WorldToLocal * LocalToLUTAtlasCoord).GetTransposed();

	OutData[3] = *(FVector4*)&TransposedWorldToLUTAtlasCoord.M[0];
	OutData[4] = *(FVector4*)&TransposedWorldToLUTAtlasCoord.M[1];
	OutData[5] = *(FVector4*)&TransposedWorldToLUTAtlasCoord.M[2];

	const FIntVector MinInLUTAtlas = CubeMapTree.SizeInLUTAtlas.IsZero() ? FIntVector::ZeroValue : CubeMapTree.MinInLUTAtlas;
	const FIntVector MaxInLUTAtlas = CubeMapTree.SizeInLUTAtlas.IsZero() ? FIntVector::ZeroValue : CubeMapTree.MinInLUTAtlas + CubeMapTree.SizeInLUTAtlas - FIntVector(1, 1, 1);

	OutData[6].X = *(const float*)&MinInLUTAtlas.X;
	OutData[6].Y = *(const float*)&MinInLUTAtlas.Y;
	OutData[6].Z = *(const float*)&MinInLUTAtlas.Z;
	OutData[6].W = *(const float*)&CubeMapTree.FirstCubeMapIndex;
	OutData[7].X = *(const float*)&MaxInLUTAtlas.X;
	OutData[7].Y = *(const float*)&MaxInLUTAtlas.Y;
	OutData[7].Z = *(const float*)&MaxInLUTAtlas.Z;
	OutData[7].W = 0.0f;

	static_assert(DataStrideInFloat4s == 8, "Data stride doesn't match");
}

void FLumenCubeMapGPUData::FillData(const class FLumenCubeMap& RESTRICT CubeMap, FVector4* RESTRICT OutData)
{
	// Note: layout must match GetLumenCubeMapData in usf

	OutData[0].X = *(const float*)&CubeMap.FaceCardIndices[0];
	OutData[0].Y = *(const float*)&CubeMap.FaceCardIndices[1];
	OutData[0].Z = *(const float*)&CubeMap.FaceCardIndices[2];
	OutData[0].W = *(const float*)&CubeMap.FaceCardIndices[3];

	OutData[1].X = *(const float*)&CubeMap.FaceCardIndices[4];
	OutData[1].Y = *(const float*)&CubeMap.FaceCardIndices[5];
	OutData[1].Z = 0.0f;
	OutData[1].W = 0.0f;

	static_assert(DataStrideInFloat4s == 2, "Data stride doesn't match");
}

void LumenUpdateDFObjectIndex(FScene* Scene, int32 DFObjectIndex)
{
	Scene->LumenSceneData->DFObjectIndicesToUpdateInBuffer.Add(DFObjectIndex);
}

void UpdateLumenCubeMapTrees(const FDistanceFieldSceneData& DistanceFieldSceneData, FLumenSceneData& LumenSceneData, FRHICommandListImmediate& RHICmdList, int32 NumScenePrimitives)
{
	LLM_SCOPE(ELLMTag::Lumen);
	QUICK_SCOPE_CYCLE_COUNTER(UpdateLumenCubeMapTrees);

	checkf(LumenSceneData.CubeMapTreeBounds.Num() == LumenSceneData.CubeMapTrees.Num(),
		TEXT("CubeMapTrees and CubeMapTreeBounds arrays are expected to be fully in sync, as they are accessed using the same index"));

	extern int32 GLumenSceneUploadCubeMapTreeBufferEveryFrame;
	if (GLumenSceneUploadCubeMapTreeBufferEveryFrame)
	{
		LumenSceneData.CubeMapTreeIndicesToUpdateInBuffer.Reset();
		LumenSceneData.CubeMapIndicesToUpdateInBuffer.Reset();

		for (int32 i = 0; i < LumenSceneData.CubeMapTrees.Num(); i++)
		{
			LumenSceneData.CubeMapTreeIndicesToUpdateInBuffer.Add(i);
		}

		for (int32 i = 0; i < LumenSceneData.CubeMaps.Num(); i++)
		{
			LumenSceneData.CubeMapIndicesToUpdateInBuffer.Add(i);
		}
	}

	// Upload Cube Map Tree Allocations
	if (LumenSceneData.CubeMapTreeIndicesToAllocate.Num() > 0)
	{
		QUICK_SCOPE_CYCLE_COUNTER(UpdateAllocations);

		GLumenCubeMapTreeLUTAtlas.Allocate(LumenSceneData.CubeMapTrees, LumenSceneData.CubeMapTreeIndicesToAllocate);
	}

	// Upload Cube Map Trees
	{
		QUICK_SCOPE_CYCLE_COUNTER(UpdateCubeMapTrees);

		const uint32 NumCubeMapTrees = LumenSceneData.CubeMapTrees.Num();
		const uint32 CubeMapTreeNumFloat4s = FMath::RoundUpToPowerOfTwo(NumCubeMapTrees * FLumenCubeMapTreeGPUData::DataStrideInFloat4s);
		const uint32 CubeMapTreeNumBytes = CubeMapTreeNumFloat4s * sizeof(FVector4);
		const bool bResizedCubeMapTreeData = ResizeResourceIfNeeded(RHICmdList, LumenSceneData.CubeMapTreeBuffer, CubeMapTreeNumBytes, TEXT("LumenCubeMapTrees"));

		const int32 NumCubeMapTreeUploads = LumenSceneData.CubeMapTreeIndicesToUpdateInBuffer.Num();

		if (NumCubeMapTreeUploads > 0)
		{
			FLumenCubeMapTree NullCubeMapTree;

			LumenSceneData.UploadCubeMapTreeBuffer.Init(NumCubeMapTreeUploads, FLumenCubeMapTreeGPUData::DataStrideInBytes, true, TEXT("LumenSceneUploadCubeMapTreeBuffer"));

			for (int32 Index : LumenSceneData.CubeMapTreeIndicesToUpdateInBuffer)
			{
				if (Index < LumenSceneData.CubeMapTrees.Num())
				{
					const FLumenCubeMapTree& CubeMapTree = LumenSceneData.CubeMapTrees.IsAllocated(Index) ? LumenSceneData.CubeMapTrees[Index] : NullCubeMapTree;

					FVector4* Data = (FVector4*) LumenSceneData.UploadCubeMapTreeBuffer.Add_GetRef(Index);
					FLumenCubeMapTreeGPUData::FillData(CubeMapTree, Data);
				}
			}

			if (bResizedCubeMapTreeData)
			{
				RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, LumenSceneData.CubeMapTreeBuffer.UAV);
			}
			else
			{
				RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EGfxToCompute, LumenSceneData.CubeMapTreeBuffer.UAV);
			}

			LumenSceneData.UploadCubeMapTreeBuffer.ResourceUploadTo(RHICmdList, LumenSceneData.CubeMapTreeBuffer, false);

			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, LumenSceneData.CubeMapTreeBuffer.UAV);
		}
	}

	// Upload Cube Maps
	{
		QUICK_SCOPE_CYCLE_COUNTER(UpdateCubeMaps);

		const uint32 NumCubeMaps = LumenSceneData.CubeMaps.Num();
		const uint32 CubeMapNumFloat4s = FMath::RoundUpToPowerOfTwo(NumCubeMaps * FLumenCubeMapGPUData::DataStrideInFloat4s);
		const uint32 CubeMapNumBytes = CubeMapNumFloat4s * sizeof(FVector4);
		const bool bResizedCubeMapData = ResizeResourceIfNeeded(RHICmdList, LumenSceneData.CubeMapBuffer, CubeMapNumBytes, TEXT("LumenCubeMaps"));

		const int32 NumCubeMapUploads = LumenSceneData.CubeMapIndicesToUpdateInBuffer.Num();

		if (NumCubeMapUploads > 0)
		{
			FLumenCubeMap NullCubeMap;

			LumenSceneData.UploadCubeMapBuffer.Init(NumCubeMapUploads, FLumenCubeMapGPUData::DataStrideInBytes, true, TEXT("LumenSceneUploadCubeMapBuffer"));
			
			for (int32 Index : LumenSceneData.CubeMapIndicesToUpdateInBuffer)
			{
				if (Index < LumenSceneData.CubeMaps.Num())
				{
					const FLumenCubeMap& CubeMap = LumenSceneData.CubeMaps.IsAllocated(Index) ? LumenSceneData.CubeMaps[Index] : NullCubeMap;

					FVector4* Data = (FVector4*)LumenSceneData.UploadCubeMapBuffer.Add_GetRef(Index);
					FLumenCubeMapGPUData::FillData(CubeMap, Data);
				}
			}

			if (bResizedCubeMapData)
			{
				RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, LumenSceneData.CubeMapBuffer.UAV);
			}
			else
			{
				RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EGfxToCompute, LumenSceneData.CubeMapBuffer.UAV);
			}

			LumenSceneData.UploadCubeMapBuffer.ResourceUploadTo(RHICmdList, LumenSceneData.CubeMapBuffer, false);

			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, LumenSceneData.CubeMapBuffer.UAV);
		}
	}

	// Upload mesh SDF to cube map tree index buffer
	{
		QUICK_SCOPE_CYCLE_COUNTER(UpdateDFObjectToCubeMapTreeIndices);

		extern int32 GLumenSceneUploadDFObjectToCubeMapTreeIndexBufferEveryFrame;
		if (GLumenSceneUploadDFObjectToCubeMapTreeIndexBufferEveryFrame)
		{
			LumenSceneData.DFObjectIndicesToUpdateInBuffer.Reset();

			for (int32 DFObjectIndex = 0; DFObjectIndex < DistanceFieldSceneData.PrimitiveInstanceMapping.Num(); ++DFObjectIndex)
			{
				LumenSceneData.DFObjectIndicesToUpdateInBuffer.Add(DFObjectIndex);
			}
		}

		const int32 NumIndices = FMath::RoundUpToPowerOfTwo(DistanceFieldSceneData.NumObjectsInBuffer);
		const uint32 IndexSizeInBytes = GPixelFormats[PF_R32_UINT].BlockBytes;
		const uint32 IndicesSizeInBytes = FMath::DivideAndRoundUp<int32>(NumIndices * IndexSizeInBytes, 16) * 16; // Round to multiple of 16 bytes
		const bool bResizedIndexElements = ResizeResourceIfNeeded(RHICmdList, LumenSceneData.DFObjectToCubeMapTreeIndexBuffer, IndicesSizeInBytes, TEXT("DFObjectToCubeMapTreeIndices"));

		const int32 NumIndexUploads = LumenSceneData.DFObjectIndicesToUpdateInBuffer.Num();

		if (NumIndexUploads > 0)
		{
			LumenSceneData.ByteBufferUploadBuffer.Init(NumIndexUploads, IndexSizeInBytes, false, TEXT("LumenSceneUploadBuffer"));

			for (int32 DFObjectIndex : LumenSceneData.DFObjectIndicesToUpdateInBuffer)
			{
				if (DFObjectIndex < DistanceFieldSceneData.PrimitiveInstanceMapping.Num())
				{
					const FPrimitiveAndInstance& Mapping = DistanceFieldSceneData.PrimitiveInstanceMapping[DFObjectIndex];

					int32 CubeMapTreeIndex = -1;

					if (Mapping.InstanceIndex < Mapping.Primitive->LumenCubeMapTreeInstanceIndices.Num())
					{
						CubeMapTreeIndex = Mapping.Primitive->LumenCubeMapTreeInstanceIndices[Mapping.InstanceIndex];
					}
					// When instances are merged, only one entry is added to LumenCubeMapTreeInstanceIndices
					else if (Mapping.Primitive->LumenCubeMapTreeInstanceIndices.Num() == 1)
					{
						CubeMapTreeIndex = Mapping.Primitive->LumenCubeMapTreeInstanceIndices[0];
					}

					LumenSceneData.ByteBufferUploadBuffer.Add(DFObjectIndex, &CubeMapTreeIndex);
				}
			}

			if (bResizedIndexElements)
			{
				RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, LumenSceneData.DFObjectToCubeMapTreeIndexBuffer.UAV);
			}
			else
			{
				RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EGfxToCompute, LumenSceneData.DFObjectToCubeMapTreeIndexBuffer.UAV);
			}

			LumenSceneData.ByteBufferUploadBuffer.ResourceUploadTo(RHICmdList, LumenSceneData.DFObjectToCubeMapTreeIndexBuffer, false);

			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, LumenSceneData.DFObjectToCubeMapTreeIndexBuffer.UAV);
		}
	}
	
	// Upload primitive index to DFObject index mapping
	{
		QUICK_SCOPE_CYCLE_COUNTER(UpdatePrimitiveToDFIndexBufferMapping);

		extern int32 GLumenSceneUploadDFObjectToCubeMapTreeIndexBufferEveryFrame;
		if (GLumenSceneUploadDFObjectToCubeMapTreeIndexBufferEveryFrame)
		{
			LumenSceneData.PrimitiveToDFObjectIndexBufferSize = 0;
		}
		
		const bool bShouldUpdatePrimitiveToDFIndexBufferMapping = IsPrimitiveToDFObjectMappingRequired();
		const int NumPrimitiveElements = bShouldUpdatePrimitiveToDFIndexBufferMapping ? NumScenePrimitives : 1;
		const int32 NumPrimitiveIndices = FMath::RoundUpToPowerOfTwo(NumPrimitiveElements);
		const uint32 IndexSizeInBytes = GPixelFormats[PF_R32_UINT].BlockBytes;
		const uint32 IndicesSizeInBytesForPrimitives = FMath::DivideAndRoundUp<int32>(NumPrimitiveIndices * IndexSizeInBytes, 16) * 16; // Round to multiple of 16 bytes
		const bool bResizedPrimitiveIndexElements = ResizeResourceIfNeeded(RHICmdList, LumenSceneData.PrimitiveToDFObjectIndexBuffer, IndicesSizeInBytesForPrimitives, TEXT("PritimitiveToDFObjectIndices"));
		
		const int32 DFObjectIndexInvalid = INVALID_CUBE_MAP_TREE_ID;
		const bool bBufferResized = IndicesSizeInBytesForPrimitives > LumenSceneData.PrimitiveToDFObjectIndexBufferSize;

		if (bBufferResized)
		{
			const uint32 DeltaIndicesSizeInBytesForPrimitives = IndicesSizeInBytesForPrimitives - LumenSceneData.PrimitiveToDFObjectIndexBufferSize;
			const uint32 DstOffset = LumenSceneData.PrimitiveToDFObjectIndexBufferSize;
			MemsetResource(RHICmdList, LumenSceneData.PrimitiveToDFObjectIndexBuffer, DFObjectIndexInvalid, DeltaIndicesSizeInBytesForPrimitives, DstOffset);
		}

		LumenSceneData.PrimitiveToDFObjectIndexBufferSize = IndicesSizeInBytesForPrimitives;

		const int32 NumIndexUploads = bShouldUpdatePrimitiveToDFIndexBufferMapping? LumenSceneData.DFObjectIndicesToUpdateInBuffer.Num():0;

		// Update the primitive to DFObject index mapping.
		if(NumIndexUploads > 0)
		{
			LumenSceneData.UploadPrimitiveBuffer.Init(NumIndexUploads, IndexSizeInBytes, false, TEXT("UploadPrimitiveBuffer"));

			for (int32 DFObjectIndex : LumenSceneData.DFObjectIndicesToUpdateInBuffer)
			{
				if (DFObjectIndex < DistanceFieldSceneData.PrimitiveInstanceMapping.Num())
				{
					const FPrimitiveAndInstance& Mapping = DistanceFieldSceneData.PrimitiveInstanceMapping[DFObjectIndex];

					int32 CubeMapTreeIndex = -1;

					if (Mapping.InstanceIndex < Mapping.Primitive->LumenCubeMapTreeInstanceIndices.Num())
					{
						CubeMapTreeIndex = Mapping.Primitive->LumenCubeMapTreeInstanceIndices[Mapping.InstanceIndex];
					}
					// When instances are merged, only one entry is added to LumenCubeMapTreeInstanceIndices
					else if (Mapping.Primitive->LumenCubeMapTreeInstanceIndices.Num() == 1)
					{
						CubeMapTreeIndex = Mapping.Primitive->LumenCubeMapTreeInstanceIndices[0];
					}
					//@TODO: Instancing is not supported at this moment.
					if (CubeMapTreeIndex != -1)
					{
						const int32 PrimitiveIndex = Mapping.Primitive->GetIndex();
						LumenSceneData.UploadPrimitiveBuffer.Add(PrimitiveIndex, &DFObjectIndex);
					}
				}
			}

			if (bBufferResized)
			{
				RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, LumenSceneData.PrimitiveToDFObjectIndexBuffer.UAV);
			}
			else
			{
				RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EGfxToCompute, LumenSceneData.PrimitiveToDFObjectIndexBuffer.UAV);
			}

			LumenSceneData.UploadPrimitiveBuffer.ResourceUploadTo(RHICmdList, LumenSceneData.PrimitiveToDFObjectIndexBuffer, false);

			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, LumenSceneData.PrimitiveToDFObjectIndexBuffer.UAV);
		}

	}

	// Reset arrays, but keep allocated memory for 1024 elements
	LumenSceneData.DFObjectIndicesToUpdateInBuffer.Empty(1024);
	LumenSceneData.CubeMapTreeIndicesToUpdateInBuffer.Empty(1024);
	LumenSceneData.CubeMapTreeIndicesToAllocate.Empty(1024);
	LumenSceneData.CubeMapIndicesToUpdateInBuffer.Empty(1024);
}

class FLUTAtlasAllocationRequest
{
public:
	FLumenCubeMapTree* CubeMapTree;
	FIntVector MinInAtlas;
	FIntVector SizeInAtlas;

	int32 GetAllocationVolume() const
	{
		return SizeInAtlas.X * SizeInAtlas.Y * SizeInAtlas.Z;
	}
};

class FLUTAtlasUpload
{
public:
	const FCardRepresentationData* CardRepresentationData;
	FIntVector MinInAtlas;
	FIntVector SizeInAtlas;
};

struct FCompareLUTAtlasAllocationRequests
{
	FORCEINLINE bool operator()(const FLUTAtlasAllocationRequest& A, const FLUTAtlasAllocationRequest& B) const
	{
		return A.GetAllocationVolume() > B.GetAllocationVolume();
	}
};

FLumenCubeMapTreeLUTAtlas::FLumenCubeMapTreeLUTAtlas()
	: VolumeFormat(PF_R8_UINT)
	, BlockAllocator(0, 0, 0, 0, 0, 0, false, false)
{
	const int32 AtlasSizeXY = FMath::Clamp(GLumenCubeMapTreeLUTAtlasSizeXY, 16, 2048);
	const int32 AtlasSizeZ = FMath::Clamp(GLumenCubeMapTreeLUTAtlasSizeZ, 16, 2048);

	BlockAllocator = FTextureLayout3d(0, 0, 0, AtlasSizeXY, AtlasSizeXY, AtlasSizeZ, false, false);
}

void FLumenCubeMapTreeLUTAtlas::Allocate(TSparseSpanArray<FLumenCubeMapTree>& CubeMapTrees, const TArray<int32>& CubeMapTreeIndicesToAllocate)
{
	LLM_SCOPE(ELLMTag::Lumen);

	TRACE_CPUPROFILER_EVENT_SCOPE(CubeMapTreeLUTAtlasAllocate);

	TArray<FLUTAtlasAllocationRequest, SceneRenderingAllocator> AllocationRequests;

	for (int32 CubeMapTreeIndex : CubeMapTreeIndicesToAllocate)
	{
		if (CubeMapTrees.IsAllocated(CubeMapTreeIndex))
		{
			FLumenCubeMapTree& CubeMapTree = CubeMapTrees[CubeMapTreeIndex];

			if (!CubeMapTree.LUTAtlasAllocationId.IsValid())
			{
				const FCardRepresentationData* CardRepresentationData = CubeMapTree.PrimitiveSceneInfo->Proxy->GetMeshCardRepresentation();

				FLUTAtlasAllocationRequest Request;
				Request.CubeMapTree = &CubeMapTree;
				Request.SizeInAtlas = CardRepresentationData->CubeMapTreeBuildData.LUTVolumeResolution;
				AllocationRequests.Add(Request);
			}
		}
	}

	// Sort largest to smallest for best packing
	AllocationRequests.Sort(FCompareLUTAtlasAllocationRequests());

	TArray<FLUTAtlasUpload, SceneRenderingAllocator> AtlasUploads;
	AtlasUploads.Reserve(AllocationRequests.Num());

	for (int32 RequestIndex = 0; RequestIndex < AllocationRequests.Num(); ++RequestIndex)
	{
		FLUTAtlasAllocationRequest& Request = AllocationRequests[RequestIndex];
		const FCardRepresentationData* CardRepresentationData = Request.CubeMapTree->PrimitiveSceneInfo->Proxy->GetMeshCardRepresentation();
		
		// First try to find existing allocation.
		FAllocation* Allocation = AllocationMap.Find(CardRepresentationData->CardRepresentationDataId);
		if (Allocation)
		{
			checkSlow(Allocation->SizeInAtlas == Request.SizeInAtlas);
			++Allocation->NumRefs;

			Request.CubeMapTree->LUTAtlasAllocationId = CardRepresentationData->CardRepresentationDataId;
			Request.CubeMapTree->MinInLUTAtlas = Allocation->MinInAtlas;
			Request.CubeMapTree->SizeInLUTAtlas = Allocation->SizeInAtlas;
		}
		else
		{
			// Try to add a new allocation.
			if (BlockAllocator.AddElement((uint32&)Request.MinInAtlas.X, (uint32&)Request.MinInAtlas.Y, (uint32&)Request.MinInAtlas.Z,
				Request.SizeInAtlas.X, Request.SizeInAtlas.Y, Request.SizeInAtlas.Z))
			{
				FAllocation NewAllocation;
				NewAllocation.NumRefs = 1;
				NewAllocation.SizeInAtlas = Request.SizeInAtlas;
				NewAllocation.MinInAtlas = Request.MinInAtlas;
				AllocationMap.Add(CardRepresentationData->CardRepresentationDataId, NewAllocation);

				FLUTAtlasUpload AtlasUpload;
				AtlasUpload.CardRepresentationData = CardRepresentationData;
				AtlasUpload.SizeInAtlas = Request.SizeInAtlas;
				AtlasUpload.MinInAtlas = Request.MinInAtlas;
				AtlasUploads.Add(AtlasUpload);

				Request.CubeMapTree->LUTAtlasAllocationId = CardRepresentationData->CardRepresentationDataId;
				Request.CubeMapTree->MinInLUTAtlas = NewAllocation.MinInAtlas;
				Request.CubeMapTree->SizeInLUTAtlas = NewAllocation.SizeInAtlas;
			}
			else
			{
				UE_LOG(LogRenderer, Error, TEXT("Failed to allocate %ux%ux%u in Lumen cube map tree lookup atlas"), Request.SizeInAtlas.X, Request.SizeInAtlas.Y, Request.SizeInAtlas.Z);
				AllocationRequests.RemoveAt(RequestIndex);
				--RequestIndex;
			}
		}
	}

	if (!VolumeTextureRHI)
	{
		FRHIResourceCreateInfo CreateInfo;
		CreateInfo.DebugName = TEXT("CubeMapTreeLookupAtlas");

		FIntVector VolumeTextureSize = FIntVector(BlockAllocator.GetMaxSizeX(), BlockAllocator.GetMaxSizeY(), BlockAllocator.GetMaxSizeZ());

		VolumeTextureRHI = RHICreateTexture3D(
			VolumeTextureSize.X,
			VolumeTextureSize.Y,
			VolumeTextureSize.Z,
			VolumeFormat,
			1,
			TexCreate_ShaderResource,
			CreateInfo);
	}

	// Upload new data
	const int32 NumUploads = AtlasUploads.Num();
	if (NumUploads > 0)
	{
		TArray<FUpdateTexture3DData> UpdateDataArray;
		UpdateDataArray.Empty(NumUploads);
		UpdateDataArray.AddUninitialized(NumUploads);

		// Fill upload buffers
		for (int32 UploadIndex = 0; UploadIndex < AtlasUploads.Num(); ++UploadIndex)
		{
			FLUTAtlasUpload& Upload = AtlasUploads[UploadIndex];
			const FCubeMapTreeBuildData& CubeMapTreeBuildData = Upload.CardRepresentationData->CubeMapTreeBuildData;

			const FUpdateTextureRegion3D UpdateRegion(Upload.MinInAtlas, FIntVector::ZeroValue, Upload.SizeInAtlas);

			FUpdateTexture3DData& UpdateData = UpdateDataArray[UploadIndex];
			UpdateData = RHIBeginUpdateTexture3D(VolumeTextureRHI, 0, UpdateRegion);

			const int32 FormatSize = GPixelFormats[VolumeFormat].BlockBytes;

			check(!!UpdateData.Data);
			check(static_cast<int32>(UpdateData.RowPitch) >= Upload.SizeInAtlas.X * FormatSize);
			check(static_cast<int32>(UpdateData.DepthPitch) >= Upload.SizeInAtlas.X * Upload.SizeInAtlas.Y * FormatSize);

			const uint32 SrcRowPitch = Upload.SizeInAtlas.X * FormatSize;
			const uint32 SrcDepthPitch = Upload.SizeInAtlas.Y * SrcRowPitch;
			const bool bRowByRowCopy = SrcRowPitch != UpdateData.RowPitch || SrcDepthPitch != UpdateData.DepthPitch;

			const uint8* SrcData = CubeMapTreeBuildData.LookupVolumeData.GetData();
			uint32 SrcDataSize = CubeMapTreeBuildData.LookupVolumeData.Num() * sizeof(CubeMapTreeBuildData.LookupVolumeData[0]);

			if (bRowByRowCopy)
			{
				const uint32 NumRows = UpdateData.DepthPitch / UpdateData.RowPitch;
				uint8* DstSliceData = UpdateData.Data;
				const uint8* SrcSliceData = SrcData;
				for (uint32 SliceIdx = 0; SliceIdx < UpdateData.UpdateRegion.Depth; ++SliceIdx)
				{
					uint8* DstRowData = DstSliceData;
					const uint8* SrcRowData = SrcSliceData;
					for (uint32 RowIdx = 0; RowIdx < NumRows; ++RowIdx)
					{
						FMemory::Memcpy(DstRowData, SrcRowData, SrcRowPitch);
						DstRowData += UpdateData.RowPitch;
						SrcRowData += SrcRowPitch;
					}
					DstSliceData += UpdateData.DepthPitch;
					SrcSliceData += SrcDepthPitch;
				}
			}
			else
			{
				FMemory::Memcpy(UpdateData.Data, SrcData, SrcDataSize);
			}
		}

		RHIEndMultiUpdateTexture3D(UpdateDataArray);
	}
}

void FLumenCubeMapTreeLUTAtlas::RemoveAllocation(FLumenCubeMapTree& CubeMapTree)
{
	LLM_SCOPE(ELLMTag::Lumen);

	if (CubeMapTree.LUTAtlasAllocationId.IsValid() && !CubeMapTree.SizeInLUTAtlas.IsZero())
	{
		FAllocation* Allocation = AllocationMap.Find(CubeMapTree.LUTAtlasAllocationId);
		check(Allocation);

		--Allocation->NumRefs;
		check(Allocation->NumRefs >= 0);

		if (Allocation->NumRefs == 0)
		{
			verify(BlockAllocator.RemoveElement(Allocation->MinInAtlas.X, Allocation->MinInAtlas.Y, Allocation->MinInAtlas.Z, Allocation->SizeInAtlas.X, Allocation->SizeInAtlas.Y, Allocation->SizeInAtlas.Z));
			AllocationMap.Remove(CubeMapTree.LUTAtlasAllocationId);
		}

		CubeMapTree.MinInLUTAtlas = FIntVector::ZeroValue;
		CubeMapTree.SizeInLUTAtlas = FIntVector::ZeroValue;
	}
}

bool IsMatrixOrthogonal(const FMatrix& Matrix)
{
	const FVector MatrixScale = Matrix.GetScaleVector();

	if (MatrixScale.GetAbsMin() >= KINDA_SMALL_NUMBER)
	{
		FVector AxisX;
		FVector AxisY;
		FVector AxisZ;
		Matrix.GetUnitAxes(AxisX, AxisY, AxisZ);

		return FMath::Abs(AxisX | AxisY) < KINDA_SMALL_NUMBER
			&& FMath::Abs(AxisX | AxisZ) < KINDA_SMALL_NUMBER
			&& FMath::Abs(AxisY | AxisZ) < KINDA_SMALL_NUMBER;
	}

	return false;
}

void AddCubeMapTreeForInstance(
	FPrimitiveSceneInfo* PrimitiveSceneInfo,
	int32 InstanceIndexOrMergedFlag,
	float ResolutionScale,
	const FCardRepresentationData* CardRepresentationData,
	const FMatrix& LocalToWorld,
	FLumenSceneData& LumenSceneData)
{
	const FCubeMapTreeBuildData& CubeMapTreeBuildData = CardRepresentationData->CubeMapTreeBuildData;

	const FVector LocalToWorldScale = LocalToWorld.GetScaleVector();
	const FVector ScaledBoundSize = CubeMapTreeBuildData.LUTVolumeBounds.GetSize() * LocalToWorldScale;
	const FVector FaceSurfaceArea(ScaledBoundSize.Y * ScaledBoundSize.Z, ScaledBoundSize.X * ScaledBoundSize.Z, ScaledBoundSize.Y * ScaledBoundSize.X);
	const float LargestFaceArea = FaceSurfaceArea.GetMax();
	const float MinFaceSurfaceArea = GLumenCubeMapTreeMinSize * GLumenCubeMapTreeMinSize;

	if (LargestFaceArea > MinFaceSurfaceArea
		&& IsMatrixOrthogonal(LocalToWorld)) // #lumen_todo: implement card capture for non orthogonal local to world transforms
	{
		const int32 NumBuildDataCards = CubeMapTreeBuildData.FaceBuiltData.Num();

		TArray<int32, TInlineAllocator<6>> BuildFaceToCulledFaceIndexBuffer;
		BuildFaceToCulledFaceIndexBuffer.SetNum(NumBuildDataCards);

		int32 NumCards = 0;

		for (int32 FaceIndex = 0; FaceIndex < NumBuildDataCards; FaceIndex++)
		{
			const FLumenCubeMapFaceBuildData& CubeMapFaceBuildData = CubeMapTreeBuildData.FaceBuiltData[FaceIndex];
			const int32 AxisIndex = CubeMapFaceBuildData.Orientation / 2;
			const float AxisSurfaceArea = FaceSurfaceArea[AxisIndex];

			if (!GLumenCubeMapTreeCullFaces || AxisSurfaceArea > MinFaceSurfaceArea)
			{
				BuildFaceToCulledFaceIndexBuffer[FaceIndex] = NumCards;
				NumCards++;
			}
			else
			{
				BuildFaceToCulledFaceIndexBuffer[FaceIndex] = -1;
			}
		}

		if (NumCards > 0)
		{
			const int32 FirstCardIndex = LumenSceneData.Cards.AddSpan(NumCards);

			int32 CardIndex = 0;

			for (int32 FaceIndex = 0; FaceIndex < NumBuildDataCards; FaceIndex++)
			{
				const FLumenCubeMapFaceBuildData& CubeMapFaceBuildData = CubeMapTreeBuildData.FaceBuiltData[FaceIndex];
				const int32 AxisIndex = CubeMapFaceBuildData.Orientation / 2;
				const float AxisSurfaceArea = FaceSurfaceArea[AxisIndex];

				if (!GLumenCubeMapTreeCullFaces || AxisSurfaceArea > MinFaceSurfaceArea)
				{
					LumenSceneData.Cards[FirstCardIndex + CardIndex].Initialize(PrimitiveSceneInfo, InstanceIndexOrMergedFlag, ResolutionScale, LocalToWorld, CubeMapFaceBuildData, FaceIndex);
					LumenSceneData.CardIndicesToUpdateInBuffer.Add(FirstCardIndex + CardIndex);
					CardIndex++;
				}
			}

			const int32 NumCubeMaps = CubeMapTreeBuildData.CubeMapBuiltData.Num();

			int32 FirstCubeMapIndex = LumenSceneData.CubeMaps.AddSpan(NumCubeMaps);

			for (int32 CubeMapIndex = 0; CubeMapIndex < CubeMapTreeBuildData.CubeMapBuiltData.Num(); ++CubeMapIndex)
			{
				const FLumenCubeMapBuildData& CubeMapBuildData = CubeMapTreeBuildData.CubeMapBuiltData[CubeMapIndex];

				LumenSceneData.CubeMaps[FirstCubeMapIndex + CubeMapIndex].Initialize(CubeMapBuildData, BuildFaceToCulledFaceIndexBuffer, FirstCardIndex);
				LumenSceneData.CubeMapIndicesToUpdateInBuffer.Add(FirstCubeMapIndex + CubeMapIndex);
			}

			checkf(LumenSceneData.CubeMapTreeBounds.Num() == LumenSceneData.CubeMapTrees.Num(),
				TEXT("CubeMapTrees and CubeMapTreeBounds arrays are expected to be fully in sync, as they are accessed using the same index"));

			const int32 CubeMapTreeIndex = LumenSceneData.CubeMapTrees.AddSpan(1);

			LumenSceneData.CubeMapTrees[CubeMapTreeIndex].Initialize(PrimitiveSceneInfo, 
				InstanceIndexOrMergedFlag, 
				LocalToWorld, 
				FirstCardIndex, 
				NumCards, 
				FirstCubeMapIndex, 
				NumCubeMaps, 
				CubeMapTreeBuildData.LUTVolumeBounds);

			LumenSceneData.CubeMapTreeBounds.AddSpan(1);
			LumenSceneData.CubeMapTreeBounds[CubeMapTreeIndex].InitFromCubeMapTree(LumenSceneData.CubeMapTrees[CubeMapTreeIndex], LumenSceneData.Cards);

			LumenSceneData.CubeMapTreeIndicesToUpdateInBuffer.Add(CubeMapTreeIndex);
			LumenSceneData.CubeMapTreeIndicesToAllocate.Add(CubeMapTreeIndex);

			for (int32 i = FirstCardIndex; i < FirstCardIndex+NumCards; ++i)
			{
				LumenSceneData.Cards[i].CubeMapTreeIndex = CubeMapTreeIndex;
			}

			if (InstanceIndexOrMergedFlag >= 0)
			{
				PrimitiveSceneInfo->LumenCubeMapTreeInstanceIndices[InstanceIndexOrMergedFlag] = CubeMapTreeIndex;
				if (InstanceIndexOrMergedFlag < PrimitiveSceneInfo->DistanceFieldInstanceIndices.Num())
				{
					LumenSceneData.DFObjectIndicesToUpdateInBuffer.Add(PrimitiveSceneInfo->DistanceFieldInstanceIndices[InstanceIndexOrMergedFlag]);
				}
			}
			else
			{
				PrimitiveSceneInfo->LumenCubeMapTreeInstanceIndices[0] = CubeMapTreeIndex;

				const TArray<FPrimitiveInstance>* PrimitiveInstances = PrimitiveSceneInfo->Proxy->GetPrimitiveInstances();
				const int32 NumInstances = PrimitiveInstances->Num();

				for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; InstanceIndex++)
				{
					if (InstanceIndex < PrimitiveSceneInfo->DistanceFieldInstanceIndices.Num())
					{
						LumenSceneData.DFObjectIndicesToUpdateInBuffer.Add(PrimitiveSceneInfo->DistanceFieldInstanceIndices[InstanceIndex]);
					}
				}
			}
		}
	}
}

double BoxSurfaceArea(FVector Extent)
{
	return 2.0 * (Extent.X * Extent.Y + Extent.Y * Extent.Z + Extent.Z * Extent.X);
}

struct FAddCubeMapTreeResult
{
	int32 NumAdded = 0;
};

FAddCubeMapTreeResult AddCubeMapTreeForPrimitive(FLumenPrimitiveAddInfo& AddInfo, FLumenSceneData& LumenSceneData, int32 MaxInstancesToAdd = INT32_MAX)
{
	FPrimitiveSceneInfo* PrimitiveSceneInfo = AddInfo.Primitive;
	FAddCubeMapTreeResult Result;

	const FCardRepresentationData* CardRepresentationData = PrimitiveSceneInfo->Proxy->GetMeshCardRepresentation();

	if (CardRepresentationData)
	{
		if (PrimitiveSceneInfo->HasLumenCaptureMeshPass())
		{
			const FBox& WorldBounds = PrimitiveSceneInfo->Proxy->GetBounds().GetBox();
			const TArray<FPrimitiveInstance>* PrimitiveInstances = PrimitiveSceneInfo->Proxy->GetPrimitiveInstances();
			const int32 NumInstances = PrimitiveInstances ? PrimitiveInstances->Num() : 1;

			bool bMergeInstances = false;
			float ResolutionScale = 1.0f;

			if (GLumenCubeMapTreeMergeInstances 
				&& NumInstances > 1
				&& WorldBounds.GetSize().GetMax() < GLumenCubeMapTreeMergedMaxWorldSize
				&& !AddInfo.IsProcessing())
			{
				FBox LocalBounds;
				LocalBounds.Init();
				double TotalInstanceSurfaceArea = 0;

				for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; ++InstanceIndex)
				{
					const FPrimitiveInstance& Instance = (*PrimitiveInstances)[InstanceIndex];
					const FBox InstanceLocalBounds = Instance.RenderBounds.GetBox().TransformBy(Instance.InstanceToLocal);
					LocalBounds += InstanceLocalBounds;
					const double InstanceSurfaceArea = BoxSurfaceArea(InstanceLocalBounds.GetExtent());
					TotalInstanceSurfaceArea += InstanceSurfaceArea;
				}

				const double BoundsSurfaceArea = BoxSurfaceArea(LocalBounds.GetExtent());
				const float SurfaceAreaRatio = BoundsSurfaceArea / TotalInstanceSurfaceArea;

				if (SurfaceAreaRatio < GLumenCubeMapTreeMergeInstancesMaxSurfaceAreaRatio)
				{
					bMergeInstances = true;
					ResolutionScale = FMath::Sqrt(1.0f / SurfaceAreaRatio) * GLumenCubeMapTreeMergedResolutionScale;
				}

				/*
				UE_LOG(LogRenderer, Log, TEXT("AddCubeMapTreeForPrimitive %s: Instances: %u, Merged: %u, SurfaceAreaRatio: %.1f"),
					*PrimitiveSceneInfo->Proxy->GetOwnerName().ToString(),
					NumInstances,
					bMergeInstances ? 1 : 0,
					SurfaceAreaRatio);*/
			}

			if (bMergeInstances)
			{
				PrimitiveSceneInfo->LumenCubeMapTreeInstanceIndices.SetNum(1);
				PrimitiveSceneInfo->LumenCubeMapTreeInstanceIndices[0] = -1;

				FBox LocalBounds;
				LocalBounds.Init();

				for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; ++InstanceIndex)
				{
					const FPrimitiveInstance& Instance = (*PrimitiveInstances)[InstanceIndex];
					LocalBounds += Instance.RenderBounds.GetBox().TransformBy(Instance.InstanceToLocal);
				}

				const FCubeMapTreeBuildData& CubeMapTreeBuildData = CardRepresentationData->CubeMapTreeBuildData;
				FMatrix LocalToWorld = PrimitiveSceneInfo->Proxy->GetLocalToWorld();
				LocalToWorld = FTranslationMatrix(-CubeMapTreeBuildData.LUTVolumeBounds.GetCenter()) 
					* FScaleMatrix(FVector(1.0f) / CubeMapTreeBuildData.LUTVolumeBounds.GetExtent()) 
					* FScaleMatrix(LocalBounds.GetExtent()) 
					* FTranslationMatrix(LocalBounds.GetCenter()) 
					* LocalToWorld;

				const int32 InstanceIndexOrMergedFlag = -1;
				AddCubeMapTreeForInstance(PrimitiveSceneInfo, InstanceIndexOrMergedFlag, ResolutionScale, CardRepresentationData, LocalToWorld, LumenSceneData);
				Result.NumAdded++;

				AddInfo.MarkComplete();
			}
			else
			{
				check(AddInfo.NumInstances == NumInstances);
				check(MaxInstancesToAdd > 0);

				if (!AddInfo.IsProcessing())
				{
					PrimitiveSceneInfo->LumenCubeMapTreeInstanceIndices.SetNumUninitialized(NumInstances);
					for (int32& Index : PrimitiveSceneInfo->LumenCubeMapTreeInstanceIndices)
					{
						Index = -1;
					}
				}

				while (!AddInfo.IsComplete() && MaxInstancesToAdd != 0)
				{
					int32 InstanceIndex = AddInfo.NumProcessedInstances;

					FMatrix LocalToWorld = PrimitiveSceneInfo->Proxy->GetLocalToWorld();

					if (PrimitiveInstances)
					{
						LocalToWorld = (*PrimitiveInstances)[InstanceIndex].InstanceToLocal * LocalToWorld;
					}

					AddCubeMapTreeForInstance(PrimitiveSceneInfo, InstanceIndex, ResolutionScale, CardRepresentationData, LocalToWorld, LumenSceneData);
					Result.NumAdded++;

					AddInfo.NumProcessedInstances++;
					MaxInstancesToAdd--;
				}
			}
		}
		else
		{
			AddInfo.MarkComplete();
		}
	}
	else
	{
		AddInfo.MarkComplete();
	}

	return Result;
}

void UpdateCubeMapTreeForInstance(
	int32 CubeMapTreeIndex,
	const FCubeMapTreeBuildData& CubeMapTreeBuildData,
	const FMatrix& LocalToWorld,
	FLumenSceneData& LumenSceneData)
{
	if (CubeMapTreeIndex >= 0 && IsMatrixOrthogonal(LocalToWorld))
	{
		FLumenCubeMapTree& CubeMapTree = LumenSceneData.CubeMapTrees[CubeMapTreeIndex];
		CubeMapTree.SetTransform(LocalToWorld);
		LumenSceneData.CubeMapTreeIndicesToUpdateInBuffer.Add(CubeMapTreeIndex);

		for (int32 RelativeCardIndex = 0; RelativeCardIndex < CubeMapTree.NumCards; RelativeCardIndex++)
		{
			const int32 CardIndex = RelativeCardIndex + CubeMapTree.FirstCardIndex;
			FCardSourceData& Card = LumenSceneData.Cards[CardIndex];

			const FLumenCubeMapFaceBuildData& CubeMapFaceBuildData = CubeMapTreeBuildData.FaceBuiltData[Card.FaceIndexInCubeMapTree];
			Card.SetTransform(LocalToWorld, CubeMapFaceBuildData);
			LumenSceneData.CardIndicesToUpdateInBuffer.Add(CardIndex);
		}

		// Intentionally accessed using CubeMapTreeIndex
		LumenSceneData.CubeMapTreeBounds[CubeMapTreeIndex].UpdateBounds(CubeMapTree, LumenSceneData.Cards);
	}
}

void UpdateCubeMapTreeForPrimitive(FPrimitiveSceneInfo* PrimitiveSceneInfo, FLumenSceneData& LumenSceneData)
{
	const FCardRepresentationData* CardRepresentationData = PrimitiveSceneInfo->Proxy->GetMeshCardRepresentation();

	if (CardRepresentationData)
	{
		if (PrimitiveSceneInfo->HasLumenCaptureMeshPass())
		{
			const TArray<FPrimitiveInstance>* PrimitiveInstances = PrimitiveSceneInfo->Proxy->GetPrimitiveInstances();
			const int32 NumInstances = PrimitiveInstances ? PrimitiveInstances->Num() : 1;

			if (PrimitiveSceneInfo->LumenCubeMapTreeInstanceIndices.Num() == NumInstances)
			{
				const FCubeMapTreeBuildData& CubeMapTreeBuildData = CardRepresentationData->CubeMapTreeBuildData;

				for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; ++InstanceIndex)
				{
					FMatrix LocalToWorld = PrimitiveSceneInfo->Proxy->GetLocalToWorld();

					if (PrimitiveInstances)
					{
						LocalToWorld = (*PrimitiveInstances)[InstanceIndex].InstanceToLocal * LocalToWorld;
					}

					const int32 CubeMapTreeIndex = PrimitiveSceneInfo->LumenCubeMapTreeInstanceIndices[InstanceIndex];
					UpdateCubeMapTreeForInstance(CubeMapTreeIndex, CubeMapTreeBuildData, LocalToWorld, LumenSceneData);
				}
			}
			else if (PrimitiveSceneInfo->LumenCubeMapTreeInstanceIndices.Num() == 1 && PrimitiveInstances)
			{
				FBox LocalBounds;
				LocalBounds.Init();

				for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; ++InstanceIndex)
				{
					const FPrimitiveInstance& Instance = (*PrimitiveInstances)[InstanceIndex];
					LocalBounds += Instance.RenderBounds.GetBox().TransformBy(Instance.InstanceToLocal);
				}

				const FCubeMapTreeBuildData& CubeMapTreeBuildData = CardRepresentationData->CubeMapTreeBuildData;
				FMatrix LocalToWorld = PrimitiveSceneInfo->Proxy->GetLocalToWorld();
				LocalToWorld = FTranslationMatrix(-CubeMapTreeBuildData.LUTVolumeBounds.GetCenter()) 
					* FScaleMatrix(FVector(1.0f) / CubeMapTreeBuildData.LUTVolumeBounds.GetExtent()) 
					* FScaleMatrix(LocalBounds.GetExtent()) 
					* FTranslationMatrix(LocalBounds.GetCenter()) 
					* LocalToWorld;

				const int32 CubeMapTreeIndex = PrimitiveSceneInfo->LumenCubeMapTreeInstanceIndices[0];
				UpdateCubeMapTreeForInstance(CubeMapTreeIndex, CubeMapTreeBuildData, LocalToWorld, LumenSceneData);
			}
		}
	}
}

void RemoveCubeMapTreeForPrimitive(
	FLumenSceneData& LumenSceneData, 
	const FPrimitiveSceneInfo* PrimitiveSceneInfo,
	const TArray<int32, TInlineAllocator<1>>& CubeMapTreeInstanceIndices)
{
	// Can't dereference the PrimitiveSceneInfo here, it has already been deleted

	for (int32 InstanceIndex = 0; InstanceIndex < CubeMapTreeInstanceIndices.Num(); ++InstanceIndex)
	{
		const int32 CubeMapTreeIndex = CubeMapTreeInstanceIndices[InstanceIndex];

		if (CubeMapTreeIndex >= 0)
		{
			FLumenCubeMapTree& CubeMapTree = LumenSceneData.CubeMapTrees[CubeMapTreeIndex];

			checkSlow(CubeMapTree.PrimitiveSceneInfo == PrimitiveSceneInfo);

			GLumenCubeMapTreeLUTAtlas.RemoveAllocation(CubeMapTree);

			for (int32 CardIndex = CubeMapTree.FirstCardIndex; CardIndex < CubeMapTree.FirstCardIndex + CubeMapTree.NumCards; ++CardIndex)
			{
				LumenSceneData.RemoveCardFromVisibleCardList(CardIndex);
				LumenSceneData.Cards[CardIndex].RemoveFromAtlas(LumenSceneData);
				LumenSceneData.CardIndicesToUpdateInBuffer.Add(CardIndex);
			}

			for (int32 CubeMapIndex = CubeMapTree.FirstCubeMapIndex; CubeMapIndex < CubeMapTree.FirstCubeMapIndex + CubeMapTree.NumCubeMaps; ++CubeMapIndex)
			{
				LumenSceneData.CubeMapIndicesToUpdateInBuffer.Add(CubeMapIndex);
			}

			checkf(LumenSceneData.CubeMapTreeBounds.Num() == LumenSceneData.CubeMapTrees.Num(),
				TEXT("CubeMapTrees and CubeMapTreeBounds arrays are expected to be fully in sync, as they are accessed using the same index"));

			LumenSceneData.Cards.RemoveSpan(CubeMapTree.FirstCardIndex, CubeMapTree.NumCards);
			LumenSceneData.CubeMaps.RemoveSpan(CubeMapTree.FirstCubeMapIndex, CubeMapTree.NumCubeMaps);
			LumenSceneData.CubeMapTrees.RemoveSpan(CubeMapTreeIndex, 1);
			LumenSceneData.CubeMapTreeBounds.RemoveSpan(CubeMapTreeIndex, 1); // Intentionally accessed using CubeMapTreeIndex

			LumenSceneData.CubeMapTreeIndicesToUpdateInBuffer.Add(CubeMapTreeIndex);
		}
	}
}

void UpdateMeshCardRepresentations(FScene* Scene)
{
	LLM_SCOPE(ELLMTag::Lumen);
	TRACE_CPUPROFILER_EVENT_SCOPE(UpdateMeshCardRepresentations);
	QUICK_SCOPE_CYCLE_COUNTER(UpdateMeshCardRepresentations);
	const double StartTime = FPlatformTime::Seconds();

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RemoveCubeMapTrees);
		QUICK_SCOPE_CYCLE_COUNTER(RemoveCubeMapTrees);

		for (int32 RemoveIndex = 0; RemoveIndex < LumenSceneData.PendingRemoveOperations.Num(); RemoveIndex++)
		{
			FLumenPrimitiveRemoveInfo& RemoveInfo = LumenSceneData.PendingRemoveOperations[RemoveIndex];

			RemoveCubeMapTreeForPrimitive(
				LumenSceneData, 
				RemoveInfo.Primitive,
				RemoveInfo.CubeMapTreeInstanceIndices);
		}
	}

	int32 NumInstancesAdded = 0;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(AddCubeMapTrees);
		QUICK_SCOPE_CYCLE_COUNTER(AddCubeMapTrees);

		int32 MaxInstancesToAdd = GLumenSceneMaxInstanceAddsPerFrame > 0 ? GLumenSceneMaxInstanceAddsPerFrame : INT32_MAX;

		while (LumenSceneData.PendingAddOperations.Num() != 0)
		{
			FLumenPrimitiveAddInfo& AddInfo = LumenSceneData.PendingAddOperations.Last();
			FAddCubeMapTreeResult Result = AddCubeMapTreeForPrimitive(
				AddInfo,
				LumenSceneData,
				MaxInstancesToAdd);

			MaxInstancesToAdd -= Result.NumAdded;
			NumInstancesAdded += Result.NumAdded;

			if (AddInfo.IsComplete())
			{
				if (AddInfo.bPendingUpdate)
				{
					UpdateCubeMapTreeForPrimitive(AddInfo.Primitive, LumenSceneData);
				}
				LumenSceneData.PendingAddOperations.Pop(false);
			}

			if (MaxInstancesToAdd <= 0)
			{
				break;
			}
		}
	}

	static bool bUseUpdatePath = true;

	if (bUseUpdatePath)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UpdateCubeMapTrees);
		QUICK_SCOPE_CYCLE_COUNTER(UpdateCubeMapTrees);

		for (TSet<FPrimitiveSceneInfo*>::TIterator It(LumenSceneData.PendingUpdateOperations); It; ++It)
		{
			FPrimitiveSceneInfo* PrimitiveSceneInfo = *It;
			UpdateCubeMapTreeForPrimitive(PrimitiveSceneInfo, LumenSceneData);
		}
	}
	else
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UpdateCubeMapTrees);
		QUICK_SCOPE_CYCLE_COUNTER(UpdateCubeMapTrees);

		//@todo - implement fast update path which just updates transforms with no capture triggered
		// For now we are just removing / re-adding for update transform
		for (TSet<FPrimitiveSceneInfo*>::TConstIterator It(LumenSceneData.PendingUpdateOperations); It; ++It)
		{
			const FPrimitiveSceneInfo* PrimitiveSceneInfo = *It;
			RemoveCubeMapTreeForPrimitive(
				LumenSceneData,
				PrimitiveSceneInfo,
				PrimitiveSceneInfo->LumenCubeMapTreeInstanceIndices);
		}

		for (TSet<FPrimitiveSceneInfo*>::TIterator It(LumenSceneData.PendingUpdateOperations); It; ++It)
		{
			FPrimitiveSceneInfo* PrimitiveSceneInfo = *It;
			FLumenPrimitiveAddInfo AddInfo(PrimitiveSceneInfo);
			AddCubeMapTreeForPrimitive(AddInfo, LumenSceneData);
		}
	}

#if LUMEN_LOG_HITCHES
	const float TimeElapsed = FPlatformTime::Seconds() - StartTime;

	if (TimeElapsed > 0.01f)
	{
		uint32 NumInstancesToRemove = 0;
		uint32 NumInstancesToUpdate = 0;

		for (int32 RemoveIndex = 0; RemoveIndex < LumenSceneData.PendingRemoveOperations.Num(); RemoveIndex++)
		{
			FLumenPrimitiveRemoveInfo& RemoveInfo = LumenSceneData.PendingRemoveOperations[RemoveIndex];
			NumInstancesToRemove += RemoveInfo.CubeMapTreeInstanceIndices.Num();
		}


		for (TSet<FPrimitiveSceneInfo*>::TIterator It(LumenSceneData.PendingUpdateOperations); It; ++It)
		{
			const FPrimitiveSceneInfo* PrimitiveSceneInfo = *It;
			if (PrimitiveSceneInfo->Proxy->GetPrimitiveInstances() && PrimitiveSceneInfo->Proxy->GetPrimitiveInstances()->Num() > 0)
			{
				NumInstancesToUpdate += PrimitiveSceneInfo->Proxy->GetPrimitiveInstances()->Num();
			}
			else
			{
				NumInstancesToUpdate += 1;
			}
		}

		UE_LOG(LogRenderer, Log, TEXT("UpdateMeshCardRepresentations took %.1fms Remove:%u inst:%u, Add:%u inst:%u Update:%u inst:%u"), 
			TimeElapsed * 1000.0f,
			(uint32) LumenSceneData.PendingRemoveOperations.Num(),
			NumInstancesToRemove,
			(uint32)LumenSceneData.PendingAddOperations.Num(),
			NumInstancesAdded,
			(uint32) LumenSceneData.PendingUpdateOperations.Num(),
			NumInstancesToUpdate);
	}
#endif

	// Reset arrays, but keep allocated memory for 1024 elements
	LumenSceneData.PendingRemoveOperations.Empty(1024);
	LumenSceneData.PendingUpdateOperations.Empty(1024);
}

void FLumenCubeMapTreeBounds::InitFromCubeMapTree(const FLumenCubeMapTree& CubeMapTree, const TSparseSpanArray<FCardSourceData>& Cards)
{
	checkSlow(CubeMapTree.NumCards <= MaxCards);
	FirstCardIndex = CubeMapTree.FirstCardIndex;
	NumCards = uint8(CubeMapTree.NumCards);
	NumVisibleCards = 0;

	UpdateBounds(CubeMapTree, Cards);
}

void FLumenCubeMapTreeBounds::UpdateBounds(const FLumenCubeMapTree& CubeMapTree, const TSparseSpanArray<FCardSourceData>& Cards)
{
	WorldBoundsMin = FVector(FLT_MAX, FLT_MAX, FLT_MAX);
	WorldBoundsMax = -WorldBoundsMin;
	ResolutionScale = 0.0f;

	for (int32 i = 0; i < CubeMapTree.NumCards; ++i)
	{
		const int32 CardIndex = CubeMapTree.FirstCardIndex + i;
		const FCardSourceData& Card = Cards[CardIndex];
		WorldBoundsMin = FVector::Min(WorldBoundsMin, Card.WorldBounds.Min);
		WorldBoundsMax = FVector::Max(WorldBoundsMax, Card.WorldBounds.Max);
		ResolutionScale = FMath::Max(ResolutionScale, Card.ResolutionScale);
	}
}

