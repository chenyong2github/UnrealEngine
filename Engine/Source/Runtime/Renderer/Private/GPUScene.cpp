// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GPUScene.cpp
=============================================================================*/

#include "GPUScene.h"
#include "CoreMinimal.h"
#include "RHI.h"
#include "SceneUtils.h"
#include "ScenePrivate.h"
#include "UnifiedBuffer.h"
#include "SpriteIndexBuffer.h"
#include "SceneFilterRendering.h"
#include "ClearQuad.h"
#include "RendererModule.h"
#include "Rendering/NaniteResources.h"
#include "Async/ParallelFor.h"
#include "VirtualShadowMaps/VirtualShadowMapCacheManager.h"
#include "NaniteSceneProxy.h"
#include "HAL/LowLevelMemTracker.h"
#include "HAL/LowLevelMemStats.h"

int32 GGPUSceneUploadEveryFrame = 0;
FAutoConsoleVariableRef CVarGPUSceneUploadEveryFrame(
	TEXT("r.GPUScene.UploadEveryFrame"),
	GGPUSceneUploadEveryFrame,
	TEXT("Whether to upload the entire scene's primitive data every frame.  Useful for debugging."),
	ECVF_RenderThreadSafe
);

int32 GGPUSceneValidatePrimitiveBuffer = 0;
FAutoConsoleVariableRef CVarGPUSceneValidatePrimitiveBuffer(
	TEXT("r.GPUScene.ValidatePrimitiveBuffer"),
	GGPUSceneValidatePrimitiveBuffer,
	TEXT("Whether to readback the GPU primitive data and assert if it doesn't match the RT primitive data.  Useful for debugging."),
	ECVF_RenderThreadSafe
);

int32 GGPUSceneValidateInstanceBuffer = 0;
FAutoConsoleVariableRef CVarGPUSceneValidateInstanceBuffer(
	TEXT("r.GPUScene.ValidateInstanceBuffer"),
	GGPUSceneValidateInstanceBuffer,
	TEXT("Whether to readback the GPU instance data and assert if it doesn't match the RT primitive data.  Useful for debugging."),
	ECVF_RenderThreadSafe
);

int32 GGPUSceneMaxPooledUploadBufferSize = 256000;
FAutoConsoleVariableRef CVarGGPUSceneMaxPooledUploadBufferSize(
	TEXT("r.GPUScene.MaxPooledUploadBufferSize"),
	GGPUSceneMaxPooledUploadBufferSize,
	TEXT("Maximum size of GPU Scene upload buffer size to pool."),
	ECVF_RenderThreadSafe
);

int32 GGPUSceneParallelUpdate = 0;
FAutoConsoleVariableRef CVarGPUSceneParallelUpdate(
	TEXT("r.GPUScene.ParallelUpdate"),
	GGPUSceneParallelUpdate,
	TEXT(""),
	ECVF_RenderThreadSafe
);

int32 GGPUSceneInstanceBVH = 0;
FAutoConsoleVariableRef CVarGPUSceneInstanceBVH(
	TEXT("r.GPUScene.InstanceBVH"),
	GGPUSceneInstanceBVH,
	TEXT("Add instances to BVH. (WIP)"),
	ECVF_RenderThreadSafe | ECVF_ReadOnly
);

LLM_DECLARE_TAG_API(GPUScene, RENDERER_API);
DECLARE_LLM_MEMORY_STAT(TEXT("GPUScene"), STAT_GPUSceneLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("GPUScene"), STAT_GPUSceneSummaryLLM, STATGROUP_LLM);
LLM_DEFINE_TAG(GPUScene, NAME_None, NAME_None, GET_STATFNAME(STAT_GPUSceneLLM), GET_STATFNAME(STAT_GPUSceneSummaryLLM));

static int32 GetMaxPrimitivesUpdate(uint32 NumUploads, uint32 InStrideInFloat4s)
{
	return FMath::Min((uint32)(GetMaxBufferDimension() / InStrideInFloat4s), NumUploads);
}

struct FParallelUpdateRange
{
	int32 ItemStart;
	int32 ItemCount;
};

struct FParallelUpdateRanges
{
	FParallelUpdateRange Range[4];
};

// TODO: Improve and move to shared utility location.
static int32 PartitionUpdateRanges(FParallelUpdateRanges& Ranges, int32 ItemCount, bool bAllowParallel)
{
	if (ItemCount < 256 || !bAllowParallel)
	{
		Ranges.Range[0].ItemStart = 0;
		Ranges.Range[0].ItemCount = ItemCount;
		return 1;
	}

	const int32 RangeCount = Align(ItemCount, 4) >> 2;

	Ranges.Range[0].ItemCount = RangeCount;
	Ranges.Range[1].ItemCount = RangeCount;
	Ranges.Range[2].ItemCount = RangeCount;

	Ranges.Range[0].ItemStart = 0;
	Ranges.Range[1].ItemStart = RangeCount;
	Ranges.Range[2].ItemStart = RangeCount * 2;
	Ranges.Range[3].ItemStart = RangeCount * 3;
	Ranges.Range[3].ItemCount = ItemCount - Ranges.Range[3].ItemStart;

	return Ranges.Range[3].ItemCount > 0 ? 4 : 3;
}

void FGPUScenePrimitiveCollector::Commit()
{
	ensure(!bCommitted);
	if (UploadData)
	{
		PrimitiveIdRange = GPUSceneDynamicContext->GPUScene.CommitPrimitiveCollector(*this);
	}
	bCommitted = true;
}

FGPUScenePrimitiveCollector::FUploadData* FGPUScenePrimitiveCollector::AllocateUploadData()
{
	return GPUSceneDynamicContext->AllocateDynamicPrimitiveData();
}

struct FBVHNode
{
	uint32		ChildIndexes[4];
	FVector4	ChildMin[3];
	FVector4	ChildMax[3];
};

/**
 * Info needed by the uploader to update a primitive.
 */
struct FPrimitiveUploadInfo
{
	/** Required */
	FPrimitiveSceneShaderData PrimitiveSceneData;
	int32 PrimitiveID = INDEX_NONE;

	/** Optional */
	int32 InstanceSceneDataOffset = INDEX_NONE;
	int32 InstanceSceneDataUploads = 0;
	int32 LightmapUploadCount = 0;

	/** NaniteSceneProxy must be set if the proxy is a Nanite proxy */
	const Nanite::FSceneProxyBase* NaniteSceneProxy = nullptr;
	const FPrimitiveSceneInfo* PrimitiveSceneInfo = nullptr;
};

/**
 * Info required by the uploader to update the instances that belong to a primitive.
 */
struct FInstanceUploadInfo
{
	TConstArrayView<FPrimitiveInstance> PrimitiveInstances;
	int32 InstanceSceneDataOffset = INDEX_NONE;

	// Optional per-instance data views
	TConstArrayView<FPrimitiveInstanceDynamicData> InstanceDynamicData;
	TConstArrayView<FVector4> InstanceLightShadowUVBias;
	TConstArrayView<float> InstanceCustomData;
	TConstArrayView<float> InstanceRandomID;
	
	// Used for primitives that need to create a dummy instance (they do not have instance data in the proxy)
	FPrimitiveInstance DummyInstance;

	FRenderTransform PrimitiveToWorld;
	FRenderTransform PrevPrimitiveToWorld;
	int32 PrimitiveID = INDEX_NONE;
	uint32 LastUpdateSceneFrameNumber = ~uint32(0);
};

/**
 * Info required by the uploader to update the lightmap data for a primitive.
 */
struct FLightMapUploadInfo
{
	FPrimitiveSceneProxy::FLCIArray LCIs;
	int32 LightmapDataOffset = 0;
};

/**
 * Implements a thin data abstraction such that the UploadGeneral function can upload primitive data from
 * both scene primitives and dynamic primitives (which are not stored in the same way). 
 * Note: handling of Nanite material table upload data is not abstracted (since at present it can only come via the scene primitives).
 */
struct FUploadDataSourceAdapterScenePrimitives
{
	static constexpr bool bUpdateNaniteMaterialTables = true;

	FUploadDataSourceAdapterScenePrimitives(FScene& InScene, uint32 InSceneFrameNumber, TArray<int32> InPrimitivesToUpdate, TArray<EPrimitiveDirtyState> InPrimitiveDirtyState)
		: Scene(InScene)
		, SceneFrameNumber(InSceneFrameNumber)
		, PrimitivesToUpdate(MoveTemp(InPrimitivesToUpdate))
		, PrimitiveDirtyState(MoveTemp(InPrimitiveDirtyState))
	{}

	/**
	 * Return the number of primitives to upload N, GetPrimitiveInfo will be called with ItemIndex in [0,N).
	 */
	FORCEINLINE int32 NumPrimitivesToUpload() const 
	{ 
		return PrimitivesToUpdate.Num(); 
	}

	/**
	 * Populate the primitive info for a given item index.
	 * 
	 */
	FORCEINLINE bool GetPrimitiveInfo(int32 ItemIndex, FPrimitiveUploadInfo& PrimitiveUploadInfo) const
	{
		int32 PrimitiveID = PrimitivesToUpdate[ItemIndex];
		if (PrimitiveID < Scene.PrimitiveSceneProxies.Num())
		{
			const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy = Scene.PrimitiveSceneProxies[PrimitiveID];
			const FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveSceneProxy->GetPrimitiveSceneInfo();

			PrimitiveUploadInfo.PrimitiveID = PrimitiveID;;
			PrimitiveUploadInfo.InstanceSceneDataOffset = INDEX_NONE;;
			PrimitiveUploadInfo.InstanceSceneDataUploads = 0;
			PrimitiveUploadInfo.LightmapUploadCount = PrimitiveSceneInfo->GetNumLightmapDataEntries();
			PrimitiveUploadInfo.NaniteSceneProxy = PrimitiveSceneProxy->IsNaniteMesh() ? static_cast<const Nanite::FSceneProxyBase*>(PrimitiveSceneProxy) : nullptr;
			PrimitiveUploadInfo.PrimitiveSceneInfo = PrimitiveSceneInfo;
			// Count all primitive instances represented in the instance data buffer.
			if (PrimitiveSceneProxy->SupportsInstanceDataBuffer())
			{
				const TConstArrayView<FPrimitiveInstance> InstanceSceneData = PrimitiveSceneProxy->GetInstanceSceneData();
				PrimitiveUploadInfo.InstanceSceneDataOffset = PrimitiveSceneProxy->GetPrimitiveSceneInfo()->GetInstanceSceneDataOffset();
				PrimitiveUploadInfo.InstanceSceneDataUploads = InstanceSceneData.Num();
			}
			else
			{
				PrimitiveUploadInfo.InstanceSceneDataOffset = PrimitiveSceneProxy->GetPrimitiveSceneInfo()->GetInstanceSceneDataOffset();
				PrimitiveUploadInfo.InstanceSceneDataUploads = 1;
			}
			PrimitiveUploadInfo.PrimitiveSceneData = FPrimitiveSceneShaderData(PrimitiveSceneProxy);

			return true;
		}

		return false;
	}

	FORCEINLINE bool GetInstanceInfo(int32 ItemIndex, FInstanceUploadInfo& InstanceUploadInfo) const
	{
		const int32 PrimitiveID = PrimitivesToUpdate[ItemIndex];
		if (PrimitiveID < Scene.PrimitiveSceneProxies.Num() && PrimitiveDirtyState[PrimitiveID] != EPrimitiveDirtyState::ChangedId)
		{
			FPrimitiveSceneProxy* PrimitiveSceneProxy = Scene.PrimitiveSceneProxies[PrimitiveID];
			const FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveSceneProxy->GetPrimitiveSceneInfo();

			InstanceUploadInfo.InstanceSceneDataOffset = PrimitiveSceneInfo->GetInstanceSceneDataOffset();
			InstanceUploadInfo.LastUpdateSceneFrameNumber = SceneFrameNumber;
			InstanceUploadInfo.PrimitiveID = PrimitiveID;
			InstanceUploadInfo.PrimitiveToWorld = PrimitiveSceneProxy->GetLocalToWorld();

			{
				bool bHasPrecomputedVolumetricLightmap{};
				bool bOutputVelocity{};
				int32 SingleCaptureIndex{};

				FMatrix PreviousLocalToWorld;
				Scene.GetPrimitiveUniformShaderParameters_RenderThread(PrimitiveSceneInfo, bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);
				InstanceUploadInfo.PrevPrimitiveToWorld = PreviousLocalToWorld;
			}

			bool bPerformUpload = true;

			if (PrimitiveSceneProxy->SupportsInstanceDataBuffer())
			{
				InstanceUploadInfo.PrimitiveInstances = PrimitiveSceneProxy->GetInstanceSceneData();
				InstanceUploadInfo.InstanceDynamicData = PrimitiveSceneProxy->GetInstanceDynamicData();
				InstanceUploadInfo.InstanceLightShadowUVBias = PrimitiveSceneProxy->GetInstanceLightShadowUVBias();
				InstanceUploadInfo.InstanceCustomData = PrimitiveSceneProxy->GetInstanceCustomData();
				InstanceUploadInfo.InstanceRandomID = PrimitiveSceneProxy->GetInstanceRandomID();

				// Only trigger upload if this primitive has instances
				bPerformUpload = InstanceUploadInfo.PrimitiveInstances.Num() > 0;
			}
			else
			{
				// We always create an instance to ensure that we can always use the same code paths in the shader
				// In the future we should remove redundant data from the primitive, and then the instances should be
				// provided by the proxy. However, this is a lot of work before we can just enable it in the base proxy class.
				InstanceUploadInfo.DummyInstance.LocalToPrimitive.SetIdentity();
				InstanceUploadInfo.DummyInstance.LocalBounds = PrimitiveSceneProxy->GetLocalBounds();
				InstanceUploadInfo.DummyInstance.NaniteHierarchyOffset = NANITE_INVALID_HIERARCHY_OFFSET;

				// TODO: Set INSTANCE_SCENE_DATA_FLAG_CAST_SHADOWS when appropriate
				InstanceUploadInfo.DummyInstance.Flags = 0;

				InstanceUploadInfo.PrimitiveInstances = TConstArrayView<FPrimitiveInstance>(&InstanceUploadInfo.DummyInstance, 1);
				InstanceUploadInfo.InstanceDynamicData = TConstArrayView<FPrimitiveInstanceDynamicData>((FPrimitiveInstanceDynamicData*)nullptr, 0);
				InstanceUploadInfo.InstanceLightShadowUVBias = TConstArrayView<FVector4>((FVector4*)nullptr, 0);
				InstanceUploadInfo.InstanceCustomData = TConstArrayView<float>((float*)nullptr, 0);
				InstanceUploadInfo.InstanceRandomID = TConstArrayView<float>((float*)nullptr, 0);
			}

			return bPerformUpload;
		}

		return false;
	}

	FORCEINLINE bool GetLightMapInfo(int32 ItemIndex, FLightMapUploadInfo &UploadInfo) const
	{
		const int32 PrimitiveID = PrimitivesToUpdate[ItemIndex];
		if (PrimitiveID < Scene.PrimitiveSceneProxies.Num())
		{
			FPrimitiveSceneProxy* PrimitiveSceneProxy = Scene.PrimitiveSceneProxies[PrimitiveID];

			PrimitiveSceneProxy->GetLCIs(UploadInfo.LCIs);
			check(UploadInfo.LCIs.Num() == PrimitiveSceneProxy->GetPrimitiveSceneInfo()->GetNumLightmapDataEntries());
			UploadInfo.LightmapDataOffset = PrimitiveSceneProxy->GetPrimitiveSceneInfo()->GetLightmapDataOffset();
			return true;
		}

		return false;
	}

	FScene& Scene;
	const uint32 SceneFrameNumber;
	TArray<int> PrimitivesToUpdate;
	TArray<EPrimitiveDirtyState> PrimitiveDirtyState;
};

void FGPUScene::SetEnabled(ERHIFeatureLevel::Type InFeatureLevel)
{
	FeatureLevel = InFeatureLevel;
	bIsEnabled = UseGPUScene(GMaxRHIShaderPlatform, FeatureLevel);
}

FGPUScene::~FGPUScene()
{
}

void FGPUScene::BeginRender(const FScene* Scene, FGPUSceneDynamicContext &GPUSceneDynamicContext)
{
	ensure(!bInBeginEndBlock);
	ensure(CurrentDynamicContext == nullptr);
	if (Scene != nullptr)
	{
		ensure(bIsEnabled == UseGPUScene(GMaxRHIShaderPlatform, Scene->GetFeatureLevel()));
		NumScenePrimitives = Scene->Primitives.Num();
	}
	else
	{
		NumScenePrimitives = 0;
	}
	CurrentDynamicContext = &GPUSceneDynamicContext;
	DynamicPrimitivesOffset = NumScenePrimitives;
	bInBeginEndBlock = true;
}

void FGPUScene::EndRender()
{
	ensure(bInBeginEndBlock);
	ensure(CurrentDynamicContext != nullptr);
	DynamicPrimitivesOffset = -1;
	bInBeginEndBlock = false;
	CurrentDynamicContext = nullptr;
}


void FGPUScene::UpdateInternal(FRDGBuilder& GraphBuilder, FScene& Scene)
{
	LLM_SCOPE_BYTAG(GPUScene);

	ensure(bInBeginEndBlock);
	ensure(bIsEnabled == UseGPUScene(GMaxRHIShaderPlatform, Scene.GetFeatureLevel()));
	ensure(NumScenePrimitives == Scene.Primitives.Num());
	ensure(DynamicPrimitivesOffset >= Scene.Primitives.Num());

	if (GGPUSceneUploadEveryFrame || bUpdateAllPrimitives)
	{
		PrimitivesToUpdate.Reset();

		for (int32 Index = 0; Index < Scene.Primitives.Num(); ++Index)
		{
			PrimitiveDirtyState[Index] |= EPrimitiveDirtyState::ChangedAll;
			PrimitivesToUpdate.Add(Index);
		}

		// Clear the full instance data range, except primitives that use a slot (they will unset the bits).
		InstanceSceneDataToClear.Init(true, InstanceSceneDataToClear.Num());

		// Set entire instance range for possible clearing.
		for (int32 Index = 0; Index < InstanceSceneDataToClear.Num(); ++Index)
		{
			InstanceSceneDataClearList.Add(Index);
		}

		bUpdateAllPrimitives = false;
	}

	// Store in GPU-scene to enable validation that update has been carried out.
	SceneFrameNumber = Scene.GetFrameNumber();

	FUploadDataSourceAdapterScenePrimitives Adapter(Scene, SceneFrameNumber, MoveTemp(PrimitivesToUpdate), MoveTemp(PrimitiveDirtyState));
	FGPUSceneBufferState BufferState = UpdateBufferState(GraphBuilder, &Scene, Adapter);

	// Pull out instances needing only primitive ID update, they still have to go to the general update such that the primitive gets updated (as it moved)
	{
		FInstanceGPULoadBalancer IdOnlyUpdateData;
		for (int32 Index = 0; Index < Adapter.PrimitivesToUpdate.Num(); ++Index)
		{
			int32 PrimitiveId = Adapter.PrimitivesToUpdate[Index];

			if (PrimitiveId < Scene.PrimitiveSceneProxies.Num() && Adapter.PrimitiveDirtyState[PrimitiveId] == EPrimitiveDirtyState::ChangedId)
			{
				const FPrimitiveSceneInfo* PrimitiveSceneInfo = Scene.Primitives[PrimitiveId];
				if (PrimitiveSceneInfo->GetInstanceSceneDataOffset() >= 0)
				{
					InstanceSceneDataToClear.SetRange(PrimitiveSceneInfo->GetInstanceSceneDataOffset(), PrimitiveSceneInfo->GetNumInstanceSceneDataEntries(), false);
					IdOnlyUpdateData.Add(PrimitiveSceneInfo->GetInstanceSceneDataOffset(), PrimitiveSceneInfo->GetNumInstanceSceneDataEntries(), PrimitiveId);
				}
			}
		}
		AddUpdatePrimitiveIdsPass(GraphBuilder, IdOnlyUpdateData);
	}

	// The adapter copies the IDs of primitives to update such that any that are (incorrectly) marked for update after are not lost.
	PrimitivesToUpdate.Reset();
	PrimitiveDirtyState.Init(EPrimitiveDirtyState::None, PrimitiveDirtyState.Num());

	AddPass(GraphBuilder, RDG_EVENT_NAME("GPUSceneUpdate"), 
		[this, &Scene, Adapter = MoveTemp(Adapter), BufferState = MoveTemp(BufferState)](FRHICommandListImmediate& RHICmdList)
	{
		SCOPED_NAMED_EVENT(STAT_UpdateGPUScene, FColor::Green);
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(UpdateGPUScene);
		QUICK_SCOPE_CYCLE_COUNTER(STAT_UpdateGPUScene);
		SCOPE_CYCLE_COUNTER(STAT_UpdateGPUSceneTime);

		UploadGeneral<FUploadDataSourceAdapterScenePrimitives>(RHICmdList, &Scene, Adapter, BufferState);

#if DO_CHECK
		// Validate the scene primitives are identical to the uploaded data (not the dynamic ones).
		if (GGPUSceneValidatePrimitiveBuffer && BufferState.PrimitiveBuffer.NumBytes > 0)
		{
			//UE_LOG(LogRenderer, Warning, TEXT("r.GPUSceneValidatePrimitiveBuffer enabled, doing slow readback from GPU"));
			const FPrimitiveSceneShaderData* PrimitiveBufferPtr = reinterpret_cast<const FPrimitiveSceneShaderData*>(RHILockBuffer(BufferState.PrimitiveBuffer.Buffer, 0, BufferState.PrimitiveBuffer.NumBytes, RLM_ReadOnly));

			if (PrimitiveBufferPtr != nullptr)
			{
				const int32 TotalNumberPrimitives = Scene.PrimitiveSceneProxies.Num();
				check(BufferState.PrimitiveBuffer.NumBytes >= TotalNumberPrimitives * sizeof(FPrimitiveSceneShaderData));

				int32 MaxPrimitivesUploads = GetMaxPrimitivesUpdate(TotalNumberPrimitives, FPrimitiveSceneShaderData::DataStrideInFloat4s);
				for (int32 IndexOffset = 0; IndexOffset < TotalNumberPrimitives; IndexOffset += MaxPrimitivesUploads)
				{
					for (int32 Index = 0; (Index < MaxPrimitivesUploads) && ((Index + IndexOffset) < TotalNumberPrimitives); ++Index)
					{
						FPrimitiveSceneShaderData PrimitiveSceneData(Scene.PrimitiveSceneProxies[Index + IndexOffset]);
						const FPrimitiveSceneShaderData& Item = PrimitiveBufferPtr[Index + IndexOffset];
						for (int32 DataIndex = 0; DataIndex < FPrimitiveSceneShaderData::DataStrideInFloat4s; ++DataIndex)
						{
							check(PrimitiveSceneData.Data[DataIndex] == Item.Data[DataIndex]);
						}
					}
				}
			}
			RHIUnlockBuffer(BufferState.PrimitiveBuffer.Buffer);
		}


		// Validate the scene instances are identical to the uploaded data (not the dynamic ones).
		if (GGPUSceneValidateInstanceBuffer && BufferState.InstanceSceneDataBuffer.NumBytes > 0)
		{
			//UE_LOG(LogRenderer, Warning, TEXT("r.GPUSceneValidatePrimitiveBuffer enabled, doing slow readback from GPU"));
			const FVector4* InstanceSceneDataBufferPtr = reinterpret_cast<const FVector4*>(RHILockBuffer(BufferState.InstanceSceneDataBuffer.Buffer, 0, BufferState.InstanceSceneDataBuffer.NumBytes, RLM_ReadOnly));

			struct FInstanceSceneDataDebug
			{
				FRenderTransform  LocalToWorld;
				FRenderTransform  PrevLocalToWorld;
#if 0
				// Derived data, not checked: 
				FMatrix44f  WorldToLocal;
				FVector4    NonUniformScale;
				FVector3f   InvNonUniformScale;
				float       DeterminantSign;
				uint32      NaniteRuntimeResourceID;
#endif 
				FVector3f   LocalBoundsCenter;
				uint32      PrimitiveId;
				uint32      PayloadDataOffset; // TODO: Implement payload data
				FVector3f   LocalBoundsExtent;
				uint32      LastUpdateSceneFrameNumber;
				uint32      NaniteRuntimeResourceID;
				uint32      NaniteHierarchyOffset;
				bool        NaniteHasImposter;
				float       PerInstanceRandom;
				FVector4    LightMapAndShadowMapUVBias;
				bool        ValidInstance;
				uint32      Flags;
			};

			auto GetInstanceSceneData = [](uint32 InstanceId, uint32 SOAStride, const FVector4* InstanceSceneDataBufferInner) -> FInstanceSceneDataDebug
			{
				auto LoadInstanceSceneDataElement = [InstanceSceneDataBufferInner](uint32 Index) -> FVector4
				{
					return InstanceSceneDataBufferInner[Index];
				};

				auto asuint = [](const float& Value) -> uint32
				{
					return *reinterpret_cast<const uint32*>(&Value);
				};

				auto LoadRenderTransform = [&](uint32 InstanceId, uint32 StartOffset) -> FRenderTransform
				{
					FVector4 V0 = LoadInstanceSceneDataElement((StartOffset + 0) * SOAStride + InstanceId);
					FVector4 V1 = LoadInstanceSceneDataElement((StartOffset + 1) * SOAStride + InstanceId);
					FVector4 V2 = LoadInstanceSceneDataElement((StartOffset + 2) * SOAStride + InstanceId);

					FRenderTransform Result;
					Result.TransformRows[0] = FVector3f(V0.X, V1.X, V2.X);
					Result.TransformRows[1] = FVector3f(V0.Y, V1.Y, V2.Y);
					Result.TransformRows[2] = FVector3f(V0.Z, V1.Z, V2.Z);
					Result.Origin = FVector3f(V0.W, V1.W, V2.W);

					return Result;
				};

				FInstanceSceneDataDebug InstanceSceneData;

				InstanceSceneData.Flags = asuint(LoadInstanceSceneDataElement(0 * SOAStride + InstanceId).X);
				InstanceSceneData.PrimitiveId = asuint(LoadInstanceSceneDataElement(0 * SOAStride + InstanceId).Y);
				InstanceSceneData.NaniteHierarchyOffset = asuint(LoadInstanceSceneDataElement(0 * SOAStride + InstanceId).Z);
				InstanceSceneData.LastUpdateSceneFrameNumber = asuint(LoadInstanceSceneDataElement(0 * SOAStride + InstanceId).W);

				// Only process valid instances
				InstanceSceneData.ValidInstance = InstanceSceneData.PrimitiveId != 0xFFFFFFFFu;

				if (InstanceSceneData.ValidInstance)
				{
					InstanceSceneData.LocalToWorld = LoadRenderTransform(InstanceId, 1);
					InstanceSceneData.PrevLocalToWorld = LoadRenderTransform(InstanceId, 4);

					InstanceSceneData.LocalBoundsCenter = LoadInstanceSceneDataElement(7 * SOAStride + InstanceId);
					InstanceSceneData.LocalBoundsExtent.X = LoadInstanceSceneDataElement(7 * SOAStride + InstanceId).W;

					InstanceSceneData.LocalBoundsExtent.Y = LoadInstanceSceneDataElement(8 * SOAStride + InstanceId).X;
					InstanceSceneData.LocalBoundsExtent.Z = LoadInstanceSceneDataElement(8 * SOAStride + InstanceId).Y;
					InstanceSceneData.PayloadDataOffset = asuint(LoadInstanceSceneDataElement(8 * SOAStride + InstanceId).Z);
					InstanceSceneData.PerInstanceRandom = LoadInstanceSceneDataElement(8 * SOAStride + InstanceId).W;

					InstanceSceneData.LightMapAndShadowMapUVBias = LoadInstanceSceneDataElement(9 * SOAStride + InstanceId);

					InstanceSceneData.NaniteHasImposter = (InstanceSceneData.Flags & INSTANCE_SCENE_DATA_FLAG_HAS_IMPOSTER);
				}

				return InstanceSceneData;
			};

			if (InstanceSceneDataBufferPtr != nullptr)
			{
				//const int32 TotalNumberInstances = InstanceSceneDataAllocator.GetMaxSize();
				check(BufferState.InstanceSceneDataBuffer.NumBytes >= BufferState.InstanceSceneDataSOAStride * sizeof(FInstanceSceneShaderData::Data));

				for (int32 Index = 0; Index < InstanceSceneDataAllocator.GetMaxSize(); ++Index)
				{
					FInstanceSceneDataDebug InstanceSceneDataGPU = GetInstanceSceneData(uint32(Index), BufferState.InstanceSceneDataSOAStride, InstanceSceneDataBufferPtr);

					if (InstanceSceneDataGPU.ValidInstance)
					{
						check(!InstanceSceneDataAllocator.IsFree(Index));
						if (InstanceSceneDataGPU.PrimitiveId < uint32(Scene.Primitives.Num()))
						{
							const FPrimitiveSceneProxy* PrimitiveSceneProxy = Scene.PrimitiveSceneProxies[InstanceSceneDataGPU.PrimitiveId];
							const FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveSceneProxy->GetPrimitiveSceneInfo();
							check(Index >= PrimitiveSceneInfo->GetInstanceSceneDataOffset() && Index < PrimitiveSceneInfo->GetInstanceSceneDataOffset() + PrimitiveSceneInfo->GetNumInstanceSceneDataEntries());
							FPrimitiveInstance PrimitiveInstance;

							float RandomID = 0.0f; // TODO: Temporary
							FVector4 LightMapAndShadowMapUVBias = FVector4(ForceInitToZero); // TODO: Temporary
							FRenderTransform PrevLocalToPrimitive = FRenderTransform::Identity; // TODO: Temporary

							// TODO: Temporary code to de-interleave optional data on the CPU, but prior to doing the same on the GPU
							if (PrimitiveSceneProxy->SupportsInstanceDataBuffer())
							{
								const int32 InstanceDataIndex = Index - PrimitiveSceneInfo->GetInstanceSceneDataOffset();

								const TConstArrayView<FPrimitiveInstance> InstanceSceneData = PrimitiveSceneProxy->GetInstanceSceneData();
								PrimitiveInstance = InstanceSceneData[InstanceDataIndex];

								const TConstArrayView<FPrimitiveInstanceDynamicData> InstanceDynamicData = PrimitiveSceneProxy->GetInstanceDynamicData();
								if (InstanceDynamicData.Num() == InstanceSceneData.Num())
								{
									PrevLocalToPrimitive = InstanceDynamicData[InstanceDataIndex].PrevLocalToPrimitive;
									check((PrimitiveInstance.Flags & INSTANCE_SCENE_DATA_FLAG_HAS_DYNAMIC_DATA) != 0);
								}
								else
								{
									check((PrimitiveInstance.Flags & INSTANCE_SCENE_DATA_FLAG_HAS_DYNAMIC_DATA) == 0);
								}
								
								const TConstArrayView<float> InstanceRandomID = PrimitiveSceneProxy->GetInstanceRandomID();
								if (InstanceRandomID.Num() == InstanceSceneData.Num())
								{
									RandomID = InstanceRandomID[InstanceDataIndex];
									check((PrimitiveInstance.Flags & INSTANCE_SCENE_DATA_FLAG_HAS_RANDOM) != 0);
								}
								else
								{
									check((PrimitiveInstance.Flags & INSTANCE_SCENE_DATA_FLAG_HAS_RANDOM) == 0);
								}

								const TConstArrayView<float> InstanceCustomData = PrimitiveSceneProxy->GetInstanceCustomData();
								if (InstanceCustomData.Num() == InstanceSceneData.Num())
								{
									// TODO: Implement
									check((PrimitiveInstance.Flags & INSTANCE_SCENE_DATA_FLAG_HAS_CUSTOM_DATA) != 0);
								}
								else
								{
									check((PrimitiveInstance.Flags & INSTANCE_SCENE_DATA_FLAG_HAS_CUSTOM_DATA) == 0);
								}

								const TConstArrayView<FVector4> InstanceLightShadowUVBias = PrimitiveSceneProxy->GetInstanceLightShadowUVBias();
								if (InstanceLightShadowUVBias.Num() == InstanceSceneData.Num())
								{
									LightMapAndShadowMapUVBias = InstanceLightShadowUVBias[InstanceDataIndex];
									check((PrimitiveInstance.Flags & INSTANCE_SCENE_DATA_FLAG_HAS_LIGHTSHADOW_UV_BIAS) != 0);
								}
								else
								{
									check((PrimitiveInstance.Flags & INSTANCE_SCENE_DATA_FLAG_HAS_LIGHTSHADOW_UV_BIAS) == 0);
								}
							}
							else
							{
								PrimitiveInstance.LocalToPrimitive.SetIdentity();
								PrimitiveInstance.LocalBounds = PrimitiveSceneProxy->GetLocalBounds();
								PrimitiveInstance.NaniteHierarchyOffset = NANITE_INVALID_HIERARCHY_OFFSET;
								// TODO: Set INSTANCE_SCENE_DATA_FLAG_CAST_SHADOWS when appropriate
								PrimitiveInstance.Flags = 0;
							}

							FMatrix PrimitiveToWorld = PrimitiveSceneProxy->GetLocalToWorld();
							bool bHasPrecomputedVolumetricLightmap{};
							bool bOutputVelocity{};
							int32 SingleCaptureIndex{};

							FMatrix PrevPrimitiveToWorld;
							Scene.GetPrimitiveUniformShaderParameters_RenderThread(PrimitiveSceneInfo, bHasPrecomputedVolumetricLightmap, PrevPrimitiveToWorld, SingleCaptureIndex, bOutputVelocity);

							// Pack down then info just as for upload.
							const FInstanceSceneShaderData InstanceSceneData(
								PrimitiveInstance,
								InstanceSceneDataGPU.PrimitiveId,
								PrimitiveToWorld,
								PrevPrimitiveToWorld,
								PrevLocalToPrimitive, // TODO: Temporary
								LightMapAndShadowMapUVBias, // TODO: Temporary
								RandomID, // TODO: Temporary
								0.0f, /* Custom Data Float0 */ // TODO: Temporary Hack!
								SceneFrameNumber
							);

							// unpack again, just as on GPU...
							FInstanceSceneDataDebug InstanceDataHost = GetInstanceSceneData(0U, 1U, InstanceSceneData.Data.GetData());

							check(InstanceDataHost.LocalToWorld.Equals(InstanceSceneDataGPU.LocalToWorld));
							// Check disabled for now as PrevLocalToWorld handling is a bit odd and is subject to change.
							// check(PrimitiveInstance.PrevLocalToWorld.Equals(InstanceDataGPU.PrevLocalToWorld));
							checkSlow(FMath::IsFinite(InstanceSceneDataGPU.PerInstanceRandom));
							check(InstanceDataHost.PerInstanceRandom == InstanceSceneDataGPU.PerInstanceRandom);
							// Can't validate this because it is transient and can't be known at this point
							// check(PrimitiveInstance.LastUpdateSceneFrameNumber == InstanceDataGPU.LastUpdateSceneFrameNumber);
							check(InstanceDataHost.LocalBoundsCenter.Equals(InstanceSceneDataGPU.LocalBoundsCenter));
							check(InstanceDataHost.LocalBoundsExtent.Equals(InstanceSceneDataGPU.LocalBoundsExtent));
							check(InstanceDataHost.LightMapAndShadowMapUVBias == InstanceSceneDataGPU.LightMapAndShadowMapUVBias);
							check(InstanceDataHost.NaniteHierarchyOffset == InstanceSceneDataGPU.NaniteHierarchyOffset);
							check(InstanceDataHost.Flags == InstanceSceneDataGPU.Flags);
						}
					}
				}
			}
			RHIUnlockBuffer(BufferState.InstanceSceneDataBuffer.Buffer);
		}

#endif // DO_CHECK
	});
}

namespace 
{

	class FUAVTransitionStateScopeHelper
	{
	public:
		FUAVTransitionStateScopeHelper(FRHICommandListImmediate& InRHICmdList, const FUnorderedAccessViewRHIRef& InUAV, ERHIAccess InitialState, ERHIAccess InFinalState = ERHIAccess::None) :
			RHICmdList(InRHICmdList),
			UAV(InUAV),
			CurrentState(InitialState),
			FinalState(InFinalState)
		{
		}

		~FUAVTransitionStateScopeHelper()
		{
			if (FinalState != ERHIAccess::None)
			{
				TransitionTo(FinalState);
			}
		}

		void TransitionTo(ERHIAccess NewState)
		{
			if (CurrentState != NewState)
			{
				RHICmdList.Transition(FRHITransitionInfo(UAV, CurrentState, NewState));
				CurrentState = NewState;
			}

		}

		FRHICommandListImmediate& RHICmdList;
		FUnorderedAccessViewRHIRef UAV;
		ERHIAccess CurrentState;
		ERHIAccess FinalState;
	};

};

template<typename FUploadDataSourceAdapter>
FGPUSceneBufferState FGPUScene::UpdateBufferState(FRDGBuilder& GraphBuilder, FScene* Scene, const FUploadDataSourceAdapter& UploadDataSourceAdapter)
{
	LLM_SCOPE_BYTAG(GPUScene);

	FGPUSceneBufferState BufferState;
	ensure(bInBeginEndBlock);
	if (Scene != nullptr)
	{
		ensure(bIsEnabled == UseGPUScene(GMaxRHIShaderPlatform, Scene->GetFeatureLevel()));
		ensure(NumScenePrimitives == Scene->Primitives.Num());
	}

	// Multi-GPU support : Updating on all GPUs is inefficient for AFR. Work is wasted
	// for any primitives that update on consecutive frames.
	RDG_GPU_MASK_SCOPE(GraphBuilder, FRHIGPUMask::All());

	constexpr int32 InitialBufferSize = 256;

	const uint32 SizeReserve = FMath::RoundUpToPowerOfTwo(FMath::Max(DynamicPrimitivesOffset, InitialBufferSize));
	BufferState.bResizedPrimitiveData = ResizeResourceIfNeeded(GraphBuilder, PrimitiveBuffer, SizeReserve * sizeof(FPrimitiveSceneShaderData::Data), TEXT("GPUScene.PrimitiveData"));
	BufferState.PrimitiveBuffer = PrimitiveBuffer;

	const uint32 InstanceSceneDataNumArrays = FInstanceSceneShaderData::DataStrideInFloat4s;
	const uint32 InstanceSceneDataSizeReserve = FMath::RoundUpToPowerOfTwo(FMath::Max(InstanceSceneDataAllocator.GetMaxSize(), InitialBufferSize));
	BufferState.bResizedInstanceSceneData = ResizeResourceSOAIfNeeded(GraphBuilder, InstanceSceneDataBuffer, InstanceSceneDataSizeReserve * sizeof(FInstanceSceneShaderData::Data), InstanceSceneDataNumArrays, TEXT("GPUScene.InstanceSceneData"));
	BufferState.InstanceSceneDataBuffer = InstanceSceneDataBuffer;
	InstanceSceneDataSOAStride = InstanceSceneDataSizeReserve;
	BufferState.InstanceSceneDataSOAStride = InstanceSceneDataSizeReserve;

	if (Scene != nullptr)
	{
		const uint32 NumNodes = FMath::RoundUpToPowerOfTwo(FMath::Max(Scene->InstanceBVH.GetNumNodes(), InitialBufferSize));
		ResizeResourceIfNeeded(GraphBuilder, InstanceBVHBuffer, NumNodes * sizeof(FBVHNode), TEXT("InstanceBVH"));
		BufferState.InstanceBVHBuffer = InstanceBVHBuffer;

		const bool bNaniteEnabled = DoesPlatformSupportNanite(GMaxRHIShaderPlatform);
		if (UploadDataSourceAdapter.bUpdateNaniteMaterialTables && bNaniteEnabled)
		{
			for (int32 NaniteMeshPassIndex = 0; NaniteMeshPassIndex < ENaniteMeshPass::Num; ++NaniteMeshPassIndex)
			{
				Scene->MaterialTables[NaniteMeshPassIndex].UpdateBufferState(GraphBuilder, Scene->Primitives.Num());
			}
		}
	}

	const uint32 LightMapDataBufferSize = FMath::RoundUpToPowerOfTwo(FMath::Max(LightmapDataAllocator.GetMaxSize(), InitialBufferSize));
	BufferState.bResizedLightmapData = ResizeResourceIfNeeded(GraphBuilder, LightmapDataBuffer, LightMapDataBufferSize * sizeof(FLightmapSceneShaderData::Data), TEXT("GPUScene.LightmapData"));
	BufferState.LightmapDataBuffer = LightmapDataBuffer;
	BufferState.LightMapDataBufferSize = LightMapDataBufferSize;

	return BufferState;
}

template<typename FUploadDataSourceAdapter>
void FGPUScene::UploadGeneral(FRHICommandListImmediate& RHICmdList, FScene *Scene, const FUploadDataSourceAdapter& UploadDataSourceAdapter, const FGPUSceneBufferState& BufferState)
{
	LLM_SCOPE_BYTAG(GPUScene);

	if (Scene != nullptr)
	{
		ensure(bIsEnabled == UseGPUScene(GMaxRHIShaderPlatform, Scene->GetFeatureLevel()));
		ensure(NumScenePrimitives == Scene->Primitives.Num());
	}
	{
		// Multi-GPU support : Updating on all GPUs is inefficient for AFR. Work is wasted
		// for any primitives that update on consecutive frames.
		SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::All());

		const bool bExecuteInParallel = GGPUSceneParallelUpdate != 0 && FApp::ShouldUseThreadingForPerformance();
		const bool bNaniteEnabled = DoesPlatformSupportNanite(GMaxRHIShaderPlatform);

		const uint32 InstanceSceneDataNumArrays = FInstanceSceneShaderData::DataStrideInFloat4s;
		FUAVTransitionStateScopeHelper InstanceSceneDataTransitionHelper(RHICmdList, BufferState.InstanceSceneDataBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVMask);

		const uint32 LightMapDataBufferSize = BufferState.LightMapDataBufferSize;

		// These should always be in sync with each other.
		check(InstanceSceneDataToClear.Num() == InstanceSceneDataAllocator.GetMaxSize());

		const int32 NumPrimitiveDataUploads = UploadDataSourceAdapter.NumPrimitivesToUpload();

		if (Scene != nullptr)
		{
			if (UploadDataSourceAdapter.bUpdateNaniteMaterialTables && bNaniteEnabled)
			{
				for (int32 NaniteMeshPassIndex = 0; NaniteMeshPassIndex < ENaniteMeshPass::Num; ++NaniteMeshPassIndex)
				{
					Scene->MaterialTables[NaniteMeshPassIndex].Begin(RHICmdList, Scene->Primitives.Num(), NumPrimitiveDataUploads);
				}
			}
		}

		int32 NumLightmapDataUploads = 0;
		int32 NumInstanceSceneDataUploads = 0;

		static FCriticalSection PrimitiveUploadBufferCS;
		static FCriticalSection MaterialTableUploadCS;

		FParallelUpdateRanges ParallelRanges;

		if (NumPrimitiveDataUploads > 0)
		{
			auto ProcessPrimitiveFn = [this, &UploadDataSourceAdapter, Scene, &NumLightmapDataUploads, &NumInstanceSceneDataUploads, bNaniteEnabled](int32 ItemIndex, bool bThreaded)
			{
				FPrimitiveUploadInfo UploadInfo;
				if (UploadDataSourceAdapter.GetPrimitiveInfo(ItemIndex, UploadInfo))
				{
					{
						if (bThreaded)
						{
							PrimitiveUploadBufferCS.Lock();
						}

						// Cancel out any pending clear bits for these instances.
						if (UploadInfo.InstanceSceneDataUploads > 0)
						{
							check(UploadInfo.InstanceSceneDataOffset != INDEX_NONE);
							InstanceSceneDataToClear.SetRange(UploadInfo.InstanceSceneDataOffset, UploadInfo.InstanceSceneDataUploads, false);
						}

						NumLightmapDataUploads += UploadInfo.LightmapUploadCount; // Not thread safe
						NumInstanceSceneDataUploads += UploadInfo.InstanceSceneDataUploads; // Not thread safe

						void* UploadDst = PrimitiveUploadBuffer.Add_GetRef(UploadInfo.PrimitiveID); // Not thread safe

						if (bThreaded)
						{
							PrimitiveUploadBufferCS.Unlock();
						}

						FVector4* DstData = static_cast<FVector4*>(UploadDst);
						for (uint32 VectorIndex = 0; VectorIndex < FPrimitiveSceneShaderData::DataStrideInFloat4s; ++VectorIndex)
						{
							DstData[VectorIndex] = UploadInfo.PrimitiveSceneData.Data[VectorIndex];
						}
					}

					// Update Nanite material tables associated with this primitive index.
					// GPUCULL_TODO: Abstract this also if we ever need Nanite in dynamic primitives
					if (Scene != nullptr && bNaniteEnabled && UploadInfo.NaniteSceneProxy != nullptr)
					{
						check(UploadDataSourceAdapter.bUpdateNaniteMaterialTables);
						check(UploadInfo.PrimitiveSceneInfo != nullptr);
						const FPrimitiveSceneInfo* PrimitiveSceneInfo = UploadInfo.PrimitiveSceneInfo;
						const Nanite::FSceneProxyBase* NaniteSceneProxy = UploadInfo.NaniteSceneProxy;

						// Update material depth and hit proxy ID remapping tables.
						for (int32 NaniteMeshPass = 0; NaniteMeshPass < ENaniteMeshPass::Num; ++NaniteMeshPass)
						{
							FNaniteMaterialTables& PassMaterialTables = Scene->MaterialTables[NaniteMeshPass];
							const TArray<uint32>& PassMaterialIds = PrimitiveSceneInfo->NaniteMaterialIds[NaniteMeshPass];
							const TArray<Nanite::FSceneProxyBase::FMaterialSection>& PassMaterials = NaniteSceneProxy->GetMaterialSections();
							check(PassMaterials.Num() == PassMaterialIds.Num());

							if (bThreaded)
							{
								MaterialTableUploadCS.Lock();
							}

							const uint32 TableEntryCount = uint32(NaniteSceneProxy->GetMaterialMaxIndex() + 1);
							check(TableEntryCount >= uint32(PassMaterials.Num()));

							void* DepthTable = PassMaterialTables.GetDepthTablePtr(UploadInfo.PrimitiveID, TableEntryCount);
						#if WITH_EDITOR
							const uint32 HitProxyEntryCount = (NaniteMeshPass == ENaniteMeshPass::BasePass) ? TableEntryCount : NANITE_MAX_MATERIALS;
							void* HitProxyTable = PassMaterialTables.GetHitProxyTablePtr(UploadInfo.PrimitiveID, HitProxyEntryCount);
						#endif

							if (bThreaded)
							{
								MaterialTableUploadCS.Unlock();
							}

							uint32* DepthEntry = static_cast<uint32*>(DepthTable);
							for (int32 Entry = 0; Entry < PassMaterialIds.Num(); ++Entry)
							{
								DepthEntry[PassMaterials[Entry].MaterialIndex] = PassMaterialIds[Entry];
							}

						#if WITH_EDITOR
							if (NaniteMeshPass == ENaniteMeshPass::BasePass)
							{
								const TArray<uint32>& PassHitProxyIds = PrimitiveSceneInfo->NaniteHitProxyIds;

								uint32* HitProxyEntry = static_cast<uint32*>(HitProxyTable);
								for (int32 Entry = 0; Entry < PassHitProxyIds.Num(); ++Entry)
								{
									HitProxyEntry[PassMaterials[Entry].MaterialIndex] = PassHitProxyIds[Entry];
								}
							}
							else
							{
								// Other passes don't use hit proxies. TODO: Shouldn't even need to do this.
								uint64* DualHitProxyEntry = static_cast<uint64*>(HitProxyTable);
								for (uint32 DualEntry = 0; DualEntry < NANITE_MAX_MATERIALS >> 1; ++DualEntry)
								{
									DualHitProxyEntry[DualEntry] = 0;
								}
							}
						#endif
						}
					}
				}
			};

			FUAVTransitionStateScopeHelper PrimitiveDataTransitionHelper(RHICmdList, BufferState.PrimitiveBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVMask);

			const int32 MaxPrimitivesUploads = GetMaxPrimitivesUpdate(NumPrimitiveDataUploads, FPrimitiveSceneShaderData::DataStrideInFloat4s);
			if (MaxPrimitivesUploads == NumPrimitiveDataUploads)
			{
				// One large batch
				SCOPED_DRAW_EVENTF(RHICmdList, UpdateGPUScene, TEXT("UpdateGPUScene NumPrimitiveDataUploads %u"), NumPrimitiveDataUploads);

				PrimitiveUploadBuffer.Init(NumPrimitiveDataUploads, sizeof(FPrimitiveSceneShaderData::Data), true, TEXT("PrimitiveUploadBuffer"));

				int32 RangeCount = PartitionUpdateRanges(ParallelRanges, NumPrimitiveDataUploads, bExecuteInParallel);

				ParallelFor(RangeCount,
					[&ProcessPrimitiveFn, &ParallelRanges, RangeCount](int32 RangeIndex)
					{
						for (int32 ItemIndex = ParallelRanges.Range[RangeIndex].ItemStart; ItemIndex < ParallelRanges.Range[RangeIndex].ItemStart + ParallelRanges.Range[RangeIndex].ItemCount; ++ItemIndex)
						{
							ProcessPrimitiveFn(ItemIndex, RangeCount > 1);
						}
					},
					RangeCount == 1
				);

				PrimitiveDataTransitionHelper.TransitionTo(ERHIAccess::UAVCompute);
				PrimitiveUploadBuffer.ResourceUploadTo(RHICmdList, BufferState.PrimitiveBuffer, true);
			}
			else
			{
				// Break into multiple batches
				for (int32 PrimitiveOffset = 0; PrimitiveOffset < NumPrimitiveDataUploads; PrimitiveOffset += MaxPrimitivesUploads)
				{
					SCOPED_DRAW_EVENTF(RHICmdList, UpdateGPUScene, TEXT("UpdateGPUScene NumPrimitiveDataUploads and Offset = %u %u"), NumPrimitiveDataUploads, PrimitiveOffset);

					PrimitiveUploadBuffer.Init(MaxPrimitivesUploads, sizeof(FPrimitiveSceneShaderData::Data), true, TEXT("PrimitiveUploadBuffer"));

					for (int32 IndexUpdate = 0; (IndexUpdate < MaxPrimitivesUploads) && ((IndexUpdate + PrimitiveOffset) < NumPrimitiveDataUploads); ++IndexUpdate)
					{
						const int32 ItemIndex = IndexUpdate + PrimitiveOffset;
						ProcessPrimitiveFn(ItemIndex, false /* threaded */);
					}

					PrimitiveDataTransitionHelper.TransitionTo(ERHIAccess::UAVCompute);

					{
						QUICK_SCOPE_CYCLE_COUNTER(UploadTo);
						PrimitiveUploadBuffer.ResourceUploadTo(RHICmdList, BufferState.PrimitiveBuffer, true);
					}
				}
			}
		}

		if (Scene != nullptr)
		{
			if (UploadDataSourceAdapter.bUpdateNaniteMaterialTables && bNaniteEnabled)
			{
				for (int32 NaniteMeshPassIndex = 0; NaniteMeshPassIndex < ENaniteMeshPass::Num; ++NaniteMeshPassIndex)
				{
					Scene->MaterialTables[NaniteMeshPassIndex].Finish(RHICmdList);
				}
			}
		}
		// Make sure instance buffer always has valid or properly reset entries.
		TArray<uint32, TInlineAllocator<64, SceneRenderingAllocator>> InstancesToClear;

	#if !UE_BUILD_SHIPPING
		static const bool bVerifyClearList = false;
		if (bVerifyClearList)
		{
			// We need to make sure that every set clear bit in the total list is
			// represented by an entry in the clear list. We can safely ignore unset
			// bits - such as clear list contains an entry from an earlier removal,
			// but clear bit was unset prior to clearing because a slot was reused
			// by an instance added.
			for (int32 InstanceIndex = 0; InstanceIndex < InstanceSceneDataAllocator.GetMaxSize(); ++InstanceIndex)
			{
				if (InstanceSceneDataToClear[InstanceIndex])
				{
					check(InstanceSceneDataClearList.Contains(InstanceIndex));
				}
			}
		}
	#endif

		TSet<uint32> InstanceSceneDataClearOverflow;
		for (uint32 InstanceIndex : InstanceSceneDataClearList)
		{
			// Any clear bits set after enumerating the primitives being updated are
			// stale sections of the instance data buffer, so they should be reset to
			// an invalid state and skipped on the GPU.
			if (InstanceSceneDataToClear[InstanceIndex])
			{
				if (InstanceIndex < BufferState.InstanceSceneDataSOAStride)
				{
					InstanceSceneDataToClear[InstanceIndex] = false;
					InstancesToClear.Add(InstanceIndex);
				}
				else
				{
					InstanceSceneDataClearOverflow.Add(InstanceIndex);
				}
			}
		}

		InstanceSceneDataClearList.Reset();

		Swap(InstanceSceneDataClearOverflow, InstanceSceneDataClearList);

#if DO_GUARD_SLOW
		// Verify that if InstanceClearList is empty all the upload bits are cleared as well.
		if (InstanceSceneDataClearList.Num() == 0)
		{
			ensure(InstanceSceneDataToClear.Find(true) == INDEX_NONE);
		}
#endif

		// Clears count toward the total instance data uploads - batched together for efficiency.
		NumInstanceSceneDataUploads += InstancesToClear.Num();

		// GPUCULL_TODO: May this not skip clears? E.g. if something is removed?
		//ensure(NumPrimitiveDataUploads == 0 ? NumInstanceDataUploads == 0 : true);
		{
			// Upload instancing data for the scene.
			if (NumInstanceSceneDataUploads > 0)
			{
				InstanceSceneUploadBuffer.Init(NumInstanceSceneDataUploads * InstanceSceneDataNumArrays, sizeof(FVector4), true, TEXT("InstanceSceneUploadBuffer"));

				int32 RangeCount = PartitionUpdateRanges(ParallelRanges, InstancesToClear.Num(), bExecuteInParallel);

				const FInstanceSceneShaderData& ClearedShaderData = GetDummyInstanceSceneShaderData();

				// Reset any instance slots marked for clearing.
				ParallelFor(RangeCount,
					[this, &InstancesToClear, &ParallelRanges, RangeCount, InstanceSceneDataNumArrays, &ClearedShaderData , &BufferState](int32 RangeIndex)
					{
						for (int32 ItemIndex = ParallelRanges.Range[RangeIndex].ItemStart; ItemIndex < ParallelRanges.Range[RangeIndex].ItemStart + ParallelRanges.Range[RangeIndex].ItemCount; ++ItemIndex)
						{
							const int32 Index = InstancesToClear[ItemIndex];

							void* DstRefs[FInstanceSceneShaderData::DataStrideInFloat4s];
							if (RangeCount > 1)
							{
								PrimitiveUploadBufferCS.Lock();
							}

							for (uint32 RefIndex = 0; RefIndex < InstanceSceneDataNumArrays; ++RefIndex)
							{
								DstRefs[RefIndex] = InstanceSceneUploadBuffer.Add_GetRef(RefIndex * BufferState.InstanceSceneDataSOAStride + Index);
							}

							if (RangeCount > 1)
							{
								PrimitiveUploadBufferCS.Unlock();
							}

							// TODO: This is silly, use a custom shader to splat the identity shader data over multiple output locations - way more efficient bandwidth and memory usage.
							for (uint32 RefIndex = 0; RefIndex < InstanceSceneDataNumArrays; ++RefIndex) //TODO: make a SOA version of InstanceUploadBuffer.Add
							{
								FVector4* DstVector = static_cast<FVector4*>(DstRefs[RefIndex]);
								*DstVector = ClearedShaderData.Data[RefIndex];
							}
						}
					},
					RangeCount == 1
				);

				if (NumPrimitiveDataUploads > 0)
				{
					// Note: we iterate over the primitives, whether they have instances or not (which is a bit wasteful) but this is the way we currently get to the instance data.
					// GPUCULL_TODO: move instance data ownership to GPU-scene such that it can be put in a compact list or something, and be tracked independent of primitives?
					RangeCount = PartitionUpdateRanges(ParallelRanges, NumPrimitiveDataUploads, bExecuteInParallel);

					// Upload any out of date instance slots.
					ParallelFor(RangeCount,
						[&UploadDataSourceAdapter, this, &ParallelRanges, RangeCount, InstanceSceneDataNumArrays, &BufferState](int32 RangeIndex)
						{
							for (int32 ItemIndex = ParallelRanges.Range[RangeIndex].ItemStart; ItemIndex < ParallelRanges.Range[RangeIndex].ItemStart + ParallelRanges.Range[RangeIndex].ItemCount; ++ItemIndex)
							{
								FInstanceUploadInfo UploadInfo;
								if (UploadDataSourceAdapter.GetInstanceInfo(ItemIndex, UploadInfo))
								{
									// TODO: Temporary hack!
									uint32 NumCustomDataFloats = 0;
									if (UploadInfo.PrimitiveInstances.Num() > 0 && UploadInfo.InstanceCustomData.Num() > 0)
									{
										NumCustomDataFloats = UploadInfo.InstanceCustomData.Num() / UploadInfo.PrimitiveInstances.Num();
										check(UploadInfo.PrimitiveInstances.Num() * NumCustomDataFloats == UploadInfo.InstanceCustomData.Num()); // Temp sanity check
									}

									// Update each primitive instance with current data.
									for (int32 InstanceIndex = 0; InstanceIndex < UploadInfo.PrimitiveInstances.Num(); ++InstanceIndex)
									{
										const FPrimitiveInstance& SceneData = UploadInfo.PrimitiveInstances[InstanceIndex];

										// TODO: Temporary
										const FRenderTransform& PrevLocalToPrimitive = UploadInfo.InstanceDynamicData.Num() == UploadInfo.PrimitiveInstances.Num() ? UploadInfo.InstanceDynamicData[InstanceIndex].PrevLocalToPrimitive : FRenderTransform::Identity;
										FVector4 LightMapShadowMapUVBias = UploadInfo.InstanceLightShadowUVBias.Num() == UploadInfo.PrimitiveInstances.Num() ? UploadInfo.InstanceLightShadowUVBias[InstanceIndex] : FVector4(ForceInitToZero);
										float RandomID = UploadInfo.InstanceRandomID.Num() == UploadInfo.PrimitiveInstances.Num() ? UploadInfo.InstanceRandomID[InstanceIndex] : 0.0f;

										// TODO: Temporary hack!
										float CustomDataFloat0 = 0.0f;
										if (NumCustomDataFloats > 0)
										{
											check(SceneData.Flags & INSTANCE_SCENE_DATA_FLAG_HAS_CUSTOM_DATA);
											CustomDataFloat0 = UploadInfo.InstanceCustomData[InstanceIndex * NumCustomDataFloats];
										}

										const FInstanceSceneShaderData InstanceSceneData(
											SceneData,
											UploadInfo.PrimitiveID,
											UploadInfo.PrimitiveToWorld,
											UploadInfo.PrevPrimitiveToWorld,
											PrevLocalToPrimitive, // TODO: Temporary
											LightMapShadowMapUVBias, // TODO: Temporary
											RandomID, // TODO: Temporary
											CustomDataFloat0, // TODO: Temporary Hack!
											UploadInfo.LastUpdateSceneFrameNumber
										);

										void* DstRefs[FInstanceSceneShaderData::DataStrideInFloat4s];
										if (RangeCount > 1)
										{
											PrimitiveUploadBufferCS.Lock();
										}

										for (uint32 RefIndex = 0; RefIndex < InstanceSceneDataNumArrays; ++RefIndex)
										{
											DstRefs[RefIndex] = InstanceSceneUploadBuffer.Add_GetRef(RefIndex * BufferState.InstanceSceneDataSOAStride + UploadInfo.InstanceSceneDataOffset + InstanceIndex);
										}

										if (RangeCount > 1)
										{
											PrimitiveUploadBufferCS.Unlock();
										}

										for (uint32 RefIndex = 0; RefIndex < InstanceSceneDataNumArrays; ++RefIndex) //TODO: make a SOA version of InstanceUploadBuffer.Add
										{
											FVector4* DstVector = static_cast<FVector4*>(DstRefs[RefIndex]);
											*DstVector = InstanceSceneData.Data[RefIndex];
										}
									}
								}
							}
						},
						RangeCount == 1
					);
				}
				InstanceSceneDataTransitionHelper.TransitionTo(ERHIAccess::UAVCompute);
				InstanceSceneUploadBuffer.ResourceUploadTo(RHICmdList, BufferState.InstanceSceneDataBuffer, false);
			}

			if( Scene != nullptr && Scene->InstanceBVH.GetNumDirty() > 0 )
			{
				InstanceSceneUploadBuffer.Init( Scene->InstanceBVH.GetNumDirty(), sizeof( FBVHNode ), true, TEXT("InstanceSceneUploadBuffer") );

				Scene->InstanceBVH.ForAllDirty(
					[&]( uint32 NodeIndex, const auto& Node )
					{
						FBVHNode GPUNode;
						for( int i = 0; i < 4; i++ )
						{
							GPUNode.ChildIndexes[i] = Node.ChildIndexes[i];

							GPUNode.ChildMin[0][i] = Node.ChildBounds[i].Min.X;
							GPUNode.ChildMin[1][i] = Node.ChildBounds[i].Min.Y;
							GPUNode.ChildMin[2][i] = Node.ChildBounds[i].Min.Z;

							GPUNode.ChildMax[0][i] = Node.ChildBounds[i].Max.X;
							GPUNode.ChildMax[1][i] = Node.ChildBounds[i].Max.Y;
							GPUNode.ChildMax[2][i] = Node.ChildBounds[i].Max.Z;
						}

						InstanceSceneUploadBuffer.Add( NodeIndex, &GPUNode );
					} );

				RHICmdList.Transition( FRHITransitionInfo(BufferState.InstanceBVHBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute ) );
				InstanceSceneUploadBuffer.ResourceUploadTo( RHICmdList, BufferState.InstanceBVHBuffer, false );
				RHICmdList.Transition( FRHITransitionInfo(BufferState.InstanceBVHBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask ) );
			}

			if (NumLightmapDataUploads > 0)
			{
				FUAVTransitionStateScopeHelper LightMapTransitionHelper(RHICmdList, BufferState.LightmapDataBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVMask);

				// GPUCULL_TODO: This code is wrong: the intention is to break it up into batches such that the uploaded data fits in the max buffer size
				//               However, what it does do is break it up into batches of MaxLightmapsUploads (while iterating over primitives). This is bad
				//               because it a) makes more batches than needed, b) does not AFAICT guarantee that we don't overflow (as each prim may have 
				//               multiple LCIs - so all may belong to the first 1/8th of primitives).
				const int32 MaxLightmapsUploads = GetMaxPrimitivesUpdate(NumLightmapDataUploads, FLightmapSceneShaderData::DataStrideInFloat4s);
				for (int32 PrimitiveOffset = 0; PrimitiveOffset < NumPrimitiveDataUploads; PrimitiveOffset += MaxLightmapsUploads)
				{
					LightmapUploadBuffer.Init(MaxLightmapsUploads, sizeof(FLightmapSceneShaderData::Data), true, TEXT("LightmapUploadBuffer"));

					for (int32 IndexUpdate = 0; (IndexUpdate < MaxLightmapsUploads) && ((IndexUpdate + PrimitiveOffset) < NumPrimitiveDataUploads); ++IndexUpdate)
					{
						const int32 ItemIndex = IndexUpdate + PrimitiveOffset;
						FLightMapUploadInfo UploadInfo;
						if (UploadDataSourceAdapter.GetLightMapInfo(ItemIndex, UploadInfo))
						{
							for (int32 LCIIndex = 0; LCIIndex < UploadInfo.LCIs.Num(); LCIIndex++)
							{
								FLightmapSceneShaderData LightmapSceneData(UploadInfo.LCIs[LCIIndex], FeatureLevel);
								LightmapUploadBuffer.Add(UploadInfo.LightmapDataOffset + LCIIndex, &LightmapSceneData.Data[0]);
							}
						}
					}

					LightMapTransitionHelper.TransitionTo(ERHIAccess::UAVCompute);
					LightmapUploadBuffer.ResourceUploadTo(RHICmdList, BufferState.LightmapDataBuffer, false);
				}
			}

			if (PrimitiveUploadBuffer.GetNumBytes() > (uint32)GGPUSceneMaxPooledUploadBufferSize)
			{
				PrimitiveUploadBuffer.Release();
			}

			if (InstanceSceneUploadBuffer.GetNumBytes() > (uint32)GGPUSceneMaxPooledUploadBufferSize)
			{
				InstanceSceneUploadBuffer.Release();
			}

			if (LightmapUploadBuffer.GetNumBytes() > (uint32)GGPUSceneMaxPooledUploadBufferSize)
			{
				LightmapUploadBuffer.Release();
			}
		}
	}
}

struct FUploadDataSourceAdapterDynamicPrimitives
{
	static constexpr bool bUpdateNaniteMaterialTables = false;

	FUploadDataSourceAdapterDynamicPrimitives(
		const TArray<FPrimitiveUniformShaderParameters, TInlineAllocator<8>>& InPrimitiveShaderData,
		int32 InPrimitiveIDStartOffset,
		int32 InInstanceIDStartOffset,
		uint32 InSceneFrameNumber)
		: PrimitiveShaderData(InPrimitiveShaderData)
		, PrimitiveIDStartOffset(InPrimitiveIDStartOffset)
		, InstanceIDStartOffset(InInstanceIDStartOffset)
		, SceneFrameNumber(InSceneFrameNumber)
	{}

	FORCEINLINE int32 NumPrimitivesToUpload() const
	{ 
		return PrimitiveShaderData.Num();
	}

	FORCEINLINE bool GetPrimitiveInfo(int32 ItemIndex, FPrimitiveUploadInfo& PrimitiveUploadInfo) const
	{
		PrimitiveUploadInfo.LightmapUploadCount = 0;
		PrimitiveUploadInfo.NaniteSceneProxy = nullptr;
		PrimitiveUploadInfo.PrimitiveSceneInfo = nullptr;

		if (ItemIndex < PrimitiveShaderData.Num())
		{
			// Needed to ensure the link back to instance list is up to date
			FPrimitiveUniformShaderParameters Tmp = PrimitiveShaderData[ItemIndex];
			Tmp.InstanceSceneDataOffset = InstanceIDStartOffset + ItemIndex;
			Tmp.NumInstanceSceneDataEntries = 1;

			PrimitiveUploadInfo.InstanceSceneDataOffset = InstanceIDStartOffset + ItemIndex;
			PrimitiveUploadInfo.InstanceSceneDataUploads = 1;
			PrimitiveUploadInfo.PrimitiveID = PrimitiveIDStartOffset + ItemIndex;
			PrimitiveUploadInfo.PrimitiveSceneData = FPrimitiveSceneShaderData(Tmp);

			return true;
		}

		return false;
	}

	FORCEINLINE bool GetInstanceInfo(int32 ItemIndex, FInstanceUploadInfo& InstanceUploadInfo) const
	{
		if (ItemIndex < PrimitiveShaderData.Num())
		{
			const FPrimitiveUniformShaderParameters& PrimitiveData = PrimitiveShaderData[ItemIndex];
			InstanceUploadInfo.PrimitiveID = PrimitiveIDStartOffset + ItemIndex;
			InstanceUploadInfo.PrimitiveToWorld = PrimitiveData.LocalToWorld;
			InstanceUploadInfo.PrevPrimitiveToWorld = InstanceUploadInfo.PrimitiveToWorld;
			
			InstanceUploadInfo.DummyInstance.LocalToPrimitive.SetIdentity();
			InstanceUploadInfo.DummyInstance.LocalBounds = FRenderBounds(PrimitiveData.LocalObjectBoundsMin, PrimitiveData.LocalObjectBoundsMax);
			InstanceUploadInfo.DummyInstance.NaniteHierarchyOffset = NANITE_INVALID_HIERARCHY_OFFSET;

			// TODO: Set INSTANCE_SCENE_DATA_FLAG_CAST_SHADOWS when appropriate
			InstanceUploadInfo.DummyInstance.Flags = 0;

			InstanceUploadInfo.PrimitiveInstances = TConstArrayView<FPrimitiveInstance>(&InstanceUploadInfo.DummyInstance, 1);
			InstanceUploadInfo.InstanceDynamicData = TConstArrayView<FPrimitiveInstanceDynamicData>((FPrimitiveInstanceDynamicData*)nullptr, 0);
			InstanceUploadInfo.InstanceLightShadowUVBias = TConstArrayView<FVector4>((FVector4*)nullptr, 0);
			InstanceUploadInfo.InstanceCustomData = TConstArrayView<float>((float*)nullptr, 0);
			InstanceUploadInfo.InstanceRandomID = TConstArrayView<float>((float*)nullptr, 0);
			InstanceUploadInfo.InstanceSceneDataOffset = InstanceIDStartOffset + ItemIndex;
			return true;
		}
		return false;
	}

	FORCEINLINE bool GetLightMapInfo(int32 ItemIndex, FLightMapUploadInfo& UploadInfo) const
	{
		return false;
	}

	const TArray<FPrimitiveUniformShaderParameters, TInlineAllocator<8>> &PrimitiveShaderData;
	const int32 PrimitiveIDStartOffset;
	const int32 InstanceIDStartOffset;
	const uint32 SceneFrameNumber;
};

void FGPUScene::UploadDynamicPrimitiveShaderDataForViewInternal(FRDGBuilder& GraphBuilder, FScene *Scene, FViewInfo& View)
{
	LLM_SCOPE_BYTAG(GPUScene);

	ensure(bInBeginEndBlock);
	ensure(Scene == nullptr || DynamicPrimitivesOffset >= Scene->Primitives.Num());

	FGPUScenePrimitiveCollector& Collector = View.DynamicPrimitiveCollector;

	// Auto-commit if not done (should usually not be done, but sometimes the UploadDynamicPrimitiveShaderDataForViewInternal is called to ensure the 
	// CachedViewUniformShaderParameters is set on the view.
	if (!Collector.bCommitted)
	{
		Collector.Commit();
	}

	const int32 NumPrimitiveDataUploads = Collector.Num();
	ensure(Collector.GetPrimitiveIdRange().Size<int32>() == NumPrimitiveDataUploads);

	// Make sure we are not trying to upload data that lives in a different context.
	ensure(Collector.UploadData == nullptr || CurrentDynamicContext->DymamicPrimitiveUploadData.Find(Collector.UploadData) != INDEX_NONE);

	// Skip uploading empty & already uploaded data
	if (Collector.UploadData != nullptr && NumPrimitiveDataUploads > 0 && !Collector.UploadData->bIsUploaded)
	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(UploadDynamicPrimitiveShaderData);
		QUICK_SCOPE_CYCLE_COUNTER(STAT_UploadDynamicPrimitiveShaderData);

		Collector.UploadData->bIsUploaded = true;

		int32 UploadIdStart = Collector.GetPrimitiveIdRange().GetLowerBoundValue();
		ensure(UploadIdStart < DynamicPrimitivesOffset);
		ensure(Collector.UploadData->InstanceSceneDataOffset != INDEX_NONE);

		FUploadDataSourceAdapterDynamicPrimitives UploadAdapter(Collector.UploadData->PrimitiveShaderData, UploadIdStart, Collector.UploadData->InstanceSceneDataOffset, SceneFrameNumber);
		FGPUSceneBufferState BufferState = UpdateBufferState(GraphBuilder, Scene, UploadAdapter);

		AddPass(GraphBuilder, RDG_EVENT_NAME("UploadDynamicPrimitiveShaderDataForView"),
			[this, Scene, UploadAdapter, BufferState = MoveTemp(BufferState)](FRHICommandListImmediate& RHICmdList)
		{
			UploadGeneral<FUploadDataSourceAdapterDynamicPrimitives>(RHICmdList, Scene, UploadAdapter, BufferState);
		});
	}

	// Update view uniform buffer
	View.CachedViewUniformShaderParameters->PrimitiveSceneData = PrimitiveBuffer.SRV;
	View.CachedViewUniformShaderParameters->LightmapSceneData = LightmapDataBuffer.SRV;
	View.CachedViewUniformShaderParameters->InstanceSceneData = InstanceSceneDataBuffer.SRV;
	View.CachedViewUniformShaderParameters->InstanceSceneDataSOAStride = InstanceSceneDataSOAStride;

	View.ViewUniformBuffer.UpdateUniformBufferImmediate(*View.CachedViewUniformShaderParameters);
}

void AddPrimitiveToUpdateGPU(FScene& Scene, int32 PrimitiveId)
{
	Scene.GPUScene.AddPrimitiveToUpdate(PrimitiveId, EPrimitiveDirtyState::ChangedAll);
}

void FGPUScene::AddPrimitiveToUpdate(int32 PrimitiveId, EPrimitiveDirtyState DirtyState)
{
	LLM_SCOPE_BYTAG(GPUScene);

	if (bIsEnabled)
	{
		ResizeDirtyState(PrimitiveId + 1);

		// Make sure we aren't updating same primitive multiple times.
		if (PrimitiveDirtyState[PrimitiveId] == EPrimitiveDirtyState::None)
		{
			PrimitivesToUpdate.Add(PrimitiveId);
		}
		
		PrimitiveDirtyState[PrimitiveId] |= DirtyState;
	}
}


void FGPUScene::Update(FRDGBuilder& GraphBuilder, FScene& Scene)
{
	if (bIsEnabled)
	{
		ensure(bInBeginEndBlock);
		// Invoke the cache manager to invalidate the previous location of all instances that are to be updated, 
		// must be done prior to update of GPU-side data to use the previous transforms.
		if (Scene.VirtualShadowMapArrayCacheManager)
		{
			Scene.VirtualShadowMapArrayCacheManager->ProcessPrimitivesToUpdate(GraphBuilder, Scene);
		}
		
		UpdateInternal(GraphBuilder, Scene);
	}
}

void FGPUScene::UploadDynamicPrimitiveShaderDataForView(FRDGBuilder& GraphBuilder, FScene *Scene, FViewInfo& View)
{
	if (bIsEnabled)
	{
		UploadDynamicPrimitiveShaderDataForViewInternal(GraphBuilder, Scene, View);
	}
}

int32 FGPUScene::AllocateInstanceSceneDataSlots(int32 NumInstanceSceneDataEntries)
{
	LLM_SCOPE_BYTAG(GPUScene);

	if (bIsEnabled)
	{
		if (NumInstanceSceneDataEntries > 0)
		{
			int32 InstanceSceneDataOffset = InstanceSceneDataAllocator.Allocate(NumInstanceSceneDataEntries);

			// Allocate enough storage space, if needed.
			const int32 NewSize = InstanceSceneDataOffset + NumInstanceSceneDataEntries;
			if (NewSize >= InstanceSceneDataToClear.Num())
			{
				InstanceSceneDataToClear.Add(false, NewSize - InstanceSceneDataToClear.Num());
			}

			// Set all bits associated with newly allocated instance data, otherwise deferred uploads will result in uninitialized instances (primarily for dynamic primitives).
			for (int32 AddIndex = 0; AddIndex < NumInstanceSceneDataEntries; ++AddIndex)
			{
				int32 InstanceIndex = InstanceSceneDataOffset + AddIndex;
				if (!InstanceSceneDataToClear[InstanceIndex])
				{
					// Note: we could keep ranges in InstanceSceneDataClearList to make it far more compact when many instances / primitive are used
					InstanceSceneDataClearList.Add(InstanceIndex);
				}
			}
			InstanceSceneDataToClear.SetRange(InstanceSceneDataOffset, NumInstanceSceneDataEntries, true);

			check(InstanceSceneDataToClear.Num() == InstanceSceneDataAllocator.GetMaxSize());

			return InstanceSceneDataOffset;
		}
	}
	return INDEX_NONE;
}


void FGPUScene::FreeInstanceSceneDataSlots(int32 InstanceSceneDataOffset, int32 NumInstanceSceneDataEntries)
{
	LLM_SCOPE_BYTAG(GPUScene);

	if (bIsEnabled)
	{
		InstanceSceneDataAllocator.Free(InstanceSceneDataOffset, NumInstanceSceneDataEntries);
		for (int32 AddIndex = 0; AddIndex < NumInstanceSceneDataEntries; ++AddIndex)
		{
			int32 InstanceIndex = InstanceSceneDataOffset + AddIndex;
			if (!InstanceSceneDataToClear[InstanceIndex])
			{
				// Note: we could keep ranges in InstanceSceneDataClearList to make it far more compact when many instances / primitive are used
				InstanceSceneDataClearList.Add(InstanceIndex);
			}
		}
		InstanceSceneDataToClear.SetRange(InstanceSceneDataOffset, NumInstanceSceneDataEntries, true);

		// Resize bit arrays to match new high watermark
		if (InstanceSceneDataToClear.Num() > InstanceSceneDataAllocator.GetMaxSize())
		{
			const int32 OldBitCount = InstanceSceneDataToClear.Num();
			const int32 NewBitCount = InstanceSceneDataAllocator.GetMaxSize();
			const int32 RemBitCount = OldBitCount - NewBitCount;
			InstanceSceneDataToClear.RemoveAt(NewBitCount, RemBitCount);
			check(InstanceSceneDataToClear.Num() == InstanceSceneDataAllocator.GetMaxSize());
		}
	}
}

int32 FGPUScene::AllocateInstancePayloadDataSlots(int32 NumInstancePayloadFloat4Entries)
{
	LLM_SCOPE_BYTAG(GPUScene);

	if (bIsEnabled)
	{
		if (NumInstancePayloadFloat4Entries > 0)
		{
			int32 InstancePayloadDataOffset = InstancePayloadDataAllocator.Allocate(NumInstancePayloadFloat4Entries);
			return InstancePayloadDataOffset;
		}
	}
	return INDEX_NONE;
}

void FGPUScene::FreeInstancePayloadDataSlots(int32 InstancePayloadDataOffset, int32 NumInstancePayloadFloat4Entries)
{
	LLM_SCOPE_BYTAG(GPUScene);

	if (bIsEnabled)
	{
		InstancePayloadDataAllocator.Free(InstancePayloadDataOffset, NumInstancePayloadFloat4Entries);
	}
}

TRange<int32> FGPUScene::CommitPrimitiveCollector(FGPUScenePrimitiveCollector& PrimitiveCollector)
{
	ensure(bInBeginEndBlock);
	ensure(CurrentDynamicContext != nullptr);

	// Make sure we are not trying to commit data that lives in a different context.
	ensure(CurrentDynamicContext == nullptr || CurrentDynamicContext->DymamicPrimitiveUploadData.Find(PrimitiveCollector.UploadData) != INDEX_NONE);

	int32 StartOffset = DynamicPrimitivesOffset;
	DynamicPrimitivesOffset += PrimitiveCollector.UploadData->PrimitiveShaderData.Num();

	PrimitiveCollector.UploadData->InstanceSceneDataOffset = AllocateInstanceSceneDataSlots(PrimitiveCollector.UploadData->PrimitiveShaderData.Num());

	return TRange<int32>(StartOffset, DynamicPrimitivesOffset);
}

FGPUSceneDynamicContext::~FGPUSceneDynamicContext()
{
	Release();
}

void FGPUSceneDynamicContext::Release()
{
	for (auto UploadData : DymamicPrimitiveUploadData)
	{
		check(UploadData->InstanceSceneDataOffset != INDEX_NONE);
		GPUScene.FreeInstanceSceneDataSlots(UploadData->InstanceSceneDataOffset, UploadData->PrimitiveShaderData.Num());
		delete UploadData;
	}
	DymamicPrimitiveUploadData.Empty();
}

FGPUScenePrimitiveCollector::FUploadData* FGPUSceneDynamicContext::AllocateDynamicPrimitiveData()
{
	LLM_SCOPE_BYTAG(GPUScene);

	FGPUScenePrimitiveCollector::FUploadData* UploadData = new FGPUScenePrimitiveCollector::FUploadData;
	DymamicPrimitiveUploadData.Add(UploadData);
	return UploadData;
}

/**
 * Call before accessing the GPU scene in a read/write pass.
 */
bool FGPUScene::BeginReadWriteAccess(FRDGBuilder& GraphBuilder)
{
	if (IsEnabled())
	{
		// TODO: Remove this when everything is properly RDG'd
		AddPass(GraphBuilder, RDG_EVENT_NAME("TransitionInstanceSceneDataBuffer"), [this](FRHICommandList& RHICmdList)
		{
			FRHITransitionInfo 	Transitions[2] =
			{
				FRHITransitionInfo(InstanceSceneDataBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
				FRHITransitionInfo(PrimitiveBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute)
			};

			RHICmdList.Transition(Transitions);
		});
		return true;
	}

	return false;
}

/**
 * Fills in the FGPUSceneWriterParameters to use for read/write access to the GPU Scene.
 */
void FGPUScene::GetWriteParameters(FGPUSceneWriterParameters& GPUSceneWriterParametersOut)
{
	GPUSceneWriterParametersOut.GPUSceneFrameNumber = SceneFrameNumber;
	GPUSceneWriterParametersOut.GPUSceneInstanceSceneDataSOAStride = InstanceSceneDataSOAStride;
	GPUSceneWriterParametersOut.GPUSceneNumAllocatedInstances = InstanceSceneDataAllocator.GetMaxSize();
	GPUSceneWriterParametersOut.GPUSceneNumAllocatedPrimitives = DynamicPrimitivesOffset;
	GPUSceneWriterParametersOut.GPUSceneInstanceSceneDataRW = InstanceSceneDataBuffer.UAV;
	GPUSceneWriterParametersOut.GPUScenePrimitiveSceneDataRW = PrimitiveBuffer.UAV;
}

/**
 * Call after accessing the GPU scene in a read/write pass. Ensures barriers are done.
 */
void FGPUScene::EndReadWriteAccess(FRDGBuilder& GraphBuilder, ERHIAccess FinalAccessState)
{
	if (IsEnabled())
	{
		// TODO: Remove this when everything is properly RDG'd
		AddPass(GraphBuilder, RDG_EVENT_NAME("TransitionInstanceSceneDataBuffer"), [this, FinalAccessState](FRHICommandList& RHICmdList)
		{
			FRHITransitionInfo 	Transitions[2] =
			{
				FRHITransitionInfo(InstanceSceneDataBuffer.UAV, ERHIAccess::UAVCompute, FinalAccessState),
				FRHITransitionInfo(PrimitiveBuffer.UAV, ERHIAccess::UAVCompute, FinalAccessState)
			};

			RHICmdList.Transition(Transitions);
		});
	}
}


/**
 * Compute shader to project and invalidate the rectangles of given instances.
 */
class FGPUSceneSetInstancePrimitiveIdCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGPUSceneSetInstancePrimitiveIdCS);
	SHADER_USE_PARAMETER_STRUCT(FGPUSceneSetInstancePrimitiveIdCS, FGlobalShader)
public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FGPUScene::FInstanceGPULoadBalancer::FShaderParameters, BatcherParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FGPUSceneWriterParameters, GPUSceneWriterParameters)
		END_SHADER_PARAMETER_STRUCT()

	static constexpr int32 NumThreadsPerGroup = FGPUScene::FInstanceGPULoadBalancer::ThreadGroupSize;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return UseGPUScene(Parameters.Platform, GetMaxSupportedFeatureLevel(Parameters.Platform));
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		FGPUScene::FInstanceGPULoadBalancer::SetShaderDefines(OutEnvironment);

		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FGPUSceneSetInstancePrimitiveIdCS, "/Engine/Private/GPUScene/GPUSceneDataManagement.usf", "GPUSceneSetInstancePrimitiveIdCS", SF_Compute);


void FGPUScene::AddUpdatePrimitiveIdsPass(FRDGBuilder& GraphBuilder, FInstanceGPULoadBalancer& IdOnlyUpdateItems)
{
	if (!IdOnlyUpdateItems.IsEmpty())
	{
		FGPUSceneSetInstancePrimitiveIdCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FGPUSceneSetInstancePrimitiveIdCS::FParameters>();

		IdOnlyUpdateItems.Upload(GraphBuilder).GetShaderParameters(GraphBuilder, PassParameters->BatcherParameters);

		BeginReadWriteAccess(GraphBuilder);

		GetWriteParameters(PassParameters->GPUSceneWriterParameters);

		auto ComputeShader = GetGlobalShaderMap(FeatureLevel)->GetShader<FGPUSceneSetInstancePrimitiveIdCS>();

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("GPUSceneSetInstancePrimitiveIdCS"),
			ComputeShader,
			PassParameters,
			IdOnlyUpdateItems.GetWrappedCsGroupCount()
		);

		EndReadWriteAccess(GraphBuilder, ERHIAccess::UAVCompute);
	}
}
