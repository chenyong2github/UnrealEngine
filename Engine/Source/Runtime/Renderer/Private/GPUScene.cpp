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

FRWBufferStructured* GetMirrorGPU(FGPUScene& GPUScene)
{
	return &GPUScene.PrimitiveBuffer;
}

/**
 * Returns a pointer to the underlying storage, 
 * Only used for debugging at present.
 */
void* LockResource(FRWBufferStructured& Resource)
{
	return RHILockBuffer(Resource.Buffer, 0, Resource.NumBytes, RLM_ReadOnly);
}

void UnlockResourceGPUScene(FRWBufferStructured& Resource)
{
	RHIUnlockBuffer(Resource.Buffer);
}

void UpdateUniformResource(FViewInfo& View, FGPUScene& GPUScene)
{
	View.CachedViewUniformShaderParameters->PrimitiveSceneData = GetMirrorGPU(GPUScene)->SRV;
}

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
	int32 InstanceDataOffset = INDEX_NONE;
	int32 InstanceUploadCount = 0;
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
	/**
	 * Transform of the primitive from local to world (not the instances).
	 */
	FMatrix PrimitiveLocalToWorld;
	FMatrix PreviousPrimitiveLocalToWorld;
	TArrayView<FPrimitiveInstance> PrimitiveInstances;
	int32 InstanceDataOffset = INDEX_NONE;

	int32 PrimitiveID = INDEX_NONE;

	// Used for primitives that need to create a dummy instance (they do not have instance data in the proxy)
	FPrimitiveInstance DummyInstance;

	bool bHasPrevInstanceTransform = false;
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

	FUploadDataSourceAdapterScenePrimitives(FGPUScene& InGPUScene, FScene& InScene) : GPUScene(InGPUScene), Scene(InScene) {}

	/**
	 * Return the number of primitives to upload N, GetPrimitiveInfo will be called with ItemIndex in [0,N).
	 */
	FORCEINLINE int32 NumPrimitivesToUpload() const 
	{ 
		return GPUScene.PrimitivesToUpdate.Num(); 
	}

	/**
	 * Populate the primitive info for a given item index.
	 * 
	 */
	FORCEINLINE bool GetPrimitiveInfo(int32 ItemIndex, FPrimitiveUploadInfo& PrimitiveUploadInfo) const
	{
		int32 PrimitiveID = GPUScene.PrimitivesToUpdate[ItemIndex];
		if (PrimitiveID < Scene.PrimitiveSceneProxies.Num())
		{
			const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy = Scene.PrimitiveSceneProxies[PrimitiveID];
			const FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveSceneProxy->GetPrimitiveSceneInfo();

			PrimitiveUploadInfo.PrimitiveID = PrimitiveID;;
			PrimitiveUploadInfo.InstanceDataOffset = INDEX_NONE;;
			PrimitiveUploadInfo.InstanceUploadCount = 0;
			PrimitiveUploadInfo.LightmapUploadCount = PrimitiveSceneInfo->GetNumLightmapDataEntries();
			PrimitiveUploadInfo.NaniteSceneProxy = PrimitiveSceneProxy->IsNaniteMesh() ? static_cast<const Nanite::FSceneProxyBase*>(PrimitiveSceneProxy) : nullptr;
			PrimitiveUploadInfo.PrimitiveSceneInfo = PrimitiveSceneInfo;
			// Count all primitive instances represented in the instance data buffer.
			if (PrimitiveSceneProxy->SupportsInstanceDataBuffer())
			{
				const TArray<FPrimitiveInstance>* PrimitiveInstances = PrimitiveSceneProxy->GetPrimitiveInstances();
				PrimitiveUploadInfo.InstanceDataOffset = PrimitiveSceneProxy->GetPrimitiveSceneInfo()->GetInstanceDataOffset();
				PrimitiveUploadInfo.InstanceUploadCount = PrimitiveInstances->Num();
			}
#if defined(GPUCULL_TODO)
			else
			{
				PrimitiveUploadInfo.InstanceDataOffset = PrimitiveSceneProxy->GetPrimitiveSceneInfo()->GetInstanceDataOffset();
				PrimitiveUploadInfo.InstanceUploadCount = 1;
			}
#endif //defined(GPUCULL_TODO)
			PrimitiveUploadInfo.PrimitiveSceneData = FPrimitiveSceneShaderData(PrimitiveSceneProxy);

			return true;
		}

		return false;
	}

	FORCEINLINE bool GetInstanceInfo(int32 ItemIndex, FInstanceUploadInfo& InstanceUploadInfo)
	{
		const int32 PrimitiveID = GPUScene.PrimitivesToUpdate[ItemIndex];
		if (PrimitiveID < Scene.PrimitiveSceneProxies.Num())
		{
			InstanceUploadInfo.PrimitiveID = PrimitiveID;
			FPrimitiveSceneProxy* PrimitiveSceneProxy = Scene.PrimitiveSceneProxies[PrimitiveID];
			const FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveSceneProxy->GetPrimitiveSceneInfo();

			InstanceUploadInfo.bHasPrevInstanceTransform = PrimitiveSceneProxy->HasPrevInstanceTransforms();

			if (PrimitiveSceneProxy->SupportsInstanceDataBuffer())
			{
				TArray<FPrimitiveInstance>* PrimitiveInstancesPtr = PrimitiveSceneProxy->GetPrimitiveInstances();
				check(PrimitiveInstancesPtr);

				InstanceUploadInfo.PrimitiveInstances = TArrayView<FPrimitiveInstance>(*PrimitiveInstancesPtr);
			}
#if defined(GPUCULL_TODO)
			else
			{
				// We always create an instance to ensure that we can always use the same code paths in the shader
				// In the future we should remove redundant data from the primitive, and then the instances should be
				// provided by the proxy. However, this is a lot of work before we can just enable it in the base proxy class.
				FPrimitiveInstance &DummyInstance = InstanceUploadInfo.DummyInstance;
				if (!PrimitiveSceneProxy->SupportsInstanceDataBuffer())
				{
					DummyInstance.InstanceToLocal = FMatrix::Identity;
					DummyInstance.LocalToInstance = FMatrix::Identity;
					DummyInstance.LocalToWorld = FMatrix::Identity;
					DummyInstance.PrevLocalToWorld = FMatrix::Identity;
					DummyInstance.NonUniformScale = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
					DummyInstance.InvNonUniformScaleAndDeterminantSign = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
					DummyInstance.RenderBounds = PrimitiveSceneProxy->GetLocalBounds();
					DummyInstance.LocalBounds = DummyInstance.RenderBounds;;
					DummyInstance.PrimitiveId = 0xFFFFFFFF;
					DummyInstance.LastUpdateSceneFrameNumber = 0xFFFFFFFF;
				}

				InstanceUploadInfo.PrimitiveInstances = TArrayView<FPrimitiveInstance>(&DummyInstance, 1);
			}
#endif // defined(GPUCULL_TODO)

			check(InstanceUploadInfo.PrimitiveInstances.Num() == PrimitiveSceneInfo->GetNumInstanceDataEntries());
			if (InstanceUploadInfo.PrimitiveInstances.Num() == 0)
			{
				return false;
			}

			InstanceUploadInfo.PrimitiveLocalToWorld = PrimitiveSceneProxy->GetLocalToWorld();
			
			InstanceUploadInfo.InstanceDataOffset = PrimitiveSceneProxy->GetPrimitiveSceneInfo()->GetInstanceDataOffset();
			check(InstanceUploadInfo.InstanceDataOffset != INDEX_NONE);
			check(PrimitiveSceneInfo->GetIndex() == PrimitiveID);

			// Dummy data that we don't actually need
			bool bHasPrecomputedVolumetricLightmap = false;
			int32 SingleCaptureIndex = 0;
			bool bOutputVelocity = false;

			Scene.GetPrimitiveUniformShaderParameters_RenderThread(
				PrimitiveSceneInfo,
				bHasPrecomputedVolumetricLightmap,
				InstanceUploadInfo.PreviousPrimitiveLocalToWorld,
				SingleCaptureIndex,
				bOutputVelocity
			);

			return true;
		}

		return false;
	}

	FORCEINLINE bool GetLightMapInfo(int32 ItemIndex, FLightMapUploadInfo &UploadInfo) const
	{
		const int32 PrimitiveID = GPUScene.PrimitivesToUpdate[ItemIndex];
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

	FGPUScene& GPUScene;
	FScene& Scene;
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

void FGPUScene::UpdateInternal(FRHICommandListImmediate& RHICmdList, FScene& Scene)
{
	ensure(bInBeginEndBlock);
	ensure(bIsEnabled == UseGPUScene(GMaxRHIShaderPlatform, Scene.GetFeatureLevel()));
	ensure(NumScenePrimitives == Scene.Primitives.Num());
	ensure(DynamicPrimitivesOffset >= Scene.Primitives.Num());
	{
		SCOPED_NAMED_EVENT( STAT_UpdateGPUScene, FColor::Green );
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(UpdateGPUScene);
		QUICK_SCOPE_CYCLE_COUNTER(STAT_UpdateGPUScene);
		SCOPE_CYCLE_COUNTER(STAT_UpdateGPUSceneTime);

		// Store in GPU-scene to enable validation that update has been carried out.
		SceneFrameNumber = Scene.GetFrameNumber();

		if (GGPUSceneUploadEveryFrame || bUpdateAllPrimitives)
		{
			PrimitivesMarkedToUpdate.Init(false, PrimitivesMarkedToUpdate.Num());
			PrimitivesToUpdate.Reset();

			for (int32 Index = 0; Index < Scene.Primitives.Num(); ++Index)
			{
				PrimitivesToUpdate.Add(Index);
			}

			// Clear the full instance data range, except primitives that use a slot (they will unset the bits).
			InstanceDataToClear.Init(true, InstanceDataToClear.Num());

			// Set entire instance range for possible clearing.
			for (int32 Index = 0; Index < InstanceDataToClear.Num(); ++Index)
			{
				InstanceClearList.Add(Index);
			}

			bUpdateAllPrimitives = false;
		}

		FUploadDataSourceAdapterScenePrimitives Adapter(*this, Scene);
		UploadGeneral<FUploadDataSourceAdapterScenePrimitives>(RHICmdList, &Scene, Adapter);

		PrimitivesMarkedToUpdate.Init(false, PrimitivesMarkedToUpdate.Num());


#if DO_CHECK
		// Validate the scene primitives are identical to the uploaded data (not the dynamic ones).
		if (GGPUSceneValidatePrimitiveBuffer && PrimitiveBuffer.NumBytes > 0)
		{
			FRWBufferStructured* MirrorResourceGPU = GetMirrorGPU(*this);
			//UE_LOG(LogRenderer, Warning, TEXT("r.GPUSceneValidatePrimitiveBuffer enabled, doing slow readback from GPU"));
			const FPrimitiveSceneShaderData* PrimitiveBufferPtr = reinterpret_cast<const FPrimitiveSceneShaderData*>(LockResource(*MirrorResourceGPU));
			ensureMsgf(PrimitiveBufferPtr != nullptr, TEXT("Validation not implemented for Texture2D buffer type as layout is opaque."));

			if (PrimitiveBufferPtr != nullptr)
			{
				const int32 TotalNumberPrimitives = Scene.PrimitiveSceneProxies.Num();
				check(MirrorResourceGPU->NumBytes >= TotalNumberPrimitives * sizeof(FPrimitiveSceneShaderData));

				int32 MaxPrimitivesUploads = GetMaxPrimitivesUpdate(TotalNumberPrimitives, FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s);
				for (int32 IndexOffset = 0; IndexOffset < TotalNumberPrimitives; IndexOffset += MaxPrimitivesUploads)
				{
					for (int32 Index = 0; (Index < MaxPrimitivesUploads) && ((Index + IndexOffset) < TotalNumberPrimitives); ++Index)
					{
						FPrimitiveSceneShaderData PrimitiveSceneData(Scene.PrimitiveSceneProxies[Index + IndexOffset]);
						const FPrimitiveSceneShaderData& Item = PrimitiveBufferPtr[Index + IndexOffset];
						for (int32 i = 0; i < FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s; i++)
						{
							check(PrimitiveSceneData.Data[i] == Item.Data[i]);
						}
					}
				}
			}
			UnlockResourceGPUScene(*MirrorResourceGPU);
		}
#endif // DO_CHECK

		PrimitivesToUpdate.Reset();

		// Clear the flags that mark newly added primitives.
		AddedPrimitiveFlags.Init(false, AddedPrimitiveFlags.Num());

		// Dynamic primitives are allocated after the regular ones, this needs to match the value passed in in BeginRender
		ensure(DynamicPrimitivesOffset >= Scene.Primitives.Num());
	}

	checkSlow(PrimitivesToUpdate.Num() == 0);
}

template<typename FUploadDataSourceAdapter>
void FGPUScene::UploadGeneral(FRHICommandListImmediate& RHICmdList, FScene *Scene, FUploadDataSourceAdapter &UploadDataSourceAdapter)
{
	ensure(bInBeginEndBlock);
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

		FRWBufferStructured* MirrorResourceGPU = GetMirrorGPU(*this);

		const uint32 SizeReserve = FMath::RoundUpToPowerOfTwo(FMath::Max(DynamicPrimitivesOffset, 256));
		const bool bResizedPrimitiveData = ResizeResourceIfNeeded(RHICmdList, *MirrorResourceGPU, SizeReserve * sizeof(FPrimitiveSceneShaderData::Data), TEXT("PrimitiveData"));

		const uint32 InstanceDataNumArrays = FInstanceSceneShaderData::InstanceDataStrideInFloat4s;
		const uint32 InstanceDataSizeReserve = FMath::RoundUpToPowerOfTwo(FMath::Max(InstanceDataAllocator.GetMaxSize(), 256));
		const bool bResizedInstanceData = ResizeResourceSOAIfNeeded(RHICmdList, InstanceDataBuffer, InstanceDataSizeReserve * sizeof(FInstanceSceneShaderData::Data), InstanceDataNumArrays, TEXT("InstanceData"));
		InstanceDataSOAStride = InstanceDataSizeReserve;

		if (Scene != nullptr)
		{
			const uint32 NumNodes = FMath::RoundUpToPowerOfTwo( FMath::Max( Scene->InstanceBVH.GetNumNodes(), 256 ) );
			ResizeResourceIfNeeded( RHICmdList, InstanceBVHBuffer, NumNodes * sizeof( FBVHNode ), TEXT("InstanceBVH") );
		}

		const uint32 LightMapDataBufferSize = FMath::RoundUpToPowerOfTwo(FMath::Max(LightmapDataAllocator.GetMaxSize(), 256));
		const bool bResizedLightmapData = ResizeResourceIfNeeded(RHICmdList, LightmapDataBuffer, LightMapDataBufferSize * sizeof(FLightmapSceneShaderData::Data), TEXT("LightmapData"));

		// These should always be in sync with each other.
		check(InstanceDataToClear.Num() == InstanceDataAllocator.GetMaxSize());

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
		int32 NumInstanceDataUploads = 0;

		static FCriticalSection PrimitiveUploadBufferCS;
		static FCriticalSection MaterialTableUploadCS;

		FParallelUpdateRanges ParallelRanges;

		if (NumPrimitiveDataUploads > 0)
		{
			auto ProcessPrimitiveFn = [this, &UploadDataSourceAdapter, Scene, &NumLightmapDataUploads, &NumInstanceDataUploads, bNaniteEnabled](int32 ItemIndex, bool bThreaded)
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
						if (UploadInfo.InstanceUploadCount > 0)
						{
							check(UploadInfo.InstanceDataOffset != INDEX_NONE);
							InstanceDataToClear.SetRange(UploadInfo.InstanceDataOffset, UploadInfo.InstanceUploadCount, false);
						}

						NumLightmapDataUploads += UploadInfo.LightmapUploadCount; // Not thread safe
						NumInstanceDataUploads += UploadInfo.InstanceUploadCount; // Not thread safe

						void* UploadDst = PrimitiveUploadBuffer.Add_GetRef(UploadInfo.PrimitiveID); // Not thread safe

						if (bThreaded)
						{
							PrimitiveUploadBufferCS.Unlock();
						}


						FVector4* DstData = static_cast<FVector4*>(UploadDst);
						for (uint32 VectorIndex = 0; VectorIndex < FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s; ++VectorIndex)
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
							check(NaniteSceneProxy->GetMaterialSections().Num() == PassMaterialIds.Num());

							if (bThreaded)
							{
								MaterialTableUploadCS.Lock();
							}

							void* DepthTable = PassMaterialTables.GetDepthTablePtr(UploadInfo.PrimitiveID, PassMaterialIds.Num());
						#if WITH_EDITOR
							const uint32 HitProxyEntryCount = (NaniteMeshPass == ENaniteMeshPass::BasePass) ? PrimitiveSceneInfo->NaniteHitProxyIds.Num() : NANITE_MAX_MATERIALS;
							void* HitProxyTable = PassMaterialTables.GetHitProxyTablePtr(UploadInfo.PrimitiveID, HitProxyEntryCount);
						#endif

							if (bThreaded)
							{
								MaterialTableUploadCS.Unlock();
							}

							uint32* DepthEntry = static_cast<uint32*>(DepthTable);
							for (int32 Entry = 0; Entry < PassMaterialIds.Num(); ++Entry)
							{
								DepthEntry[Entry] = PassMaterialIds[Entry];
							}

						#if WITH_EDITOR
							if (NaniteMeshPass == ENaniteMeshPass::BasePass)
							{
								uint32* HitProxyEntry = static_cast<uint32*>(HitProxyTable);
								for (uint32 Entry = 0; Entry < HitProxyEntryCount; ++Entry)
								{
									HitProxyEntry[Entry] = PrimitiveSceneInfo->NaniteHitProxyIds[Entry];
								}
							}
							else
							{
								// Other passes don't use hit proxies. TODO: Shouldn't even need to do this.
								uint64* DualHitProxyEntry = static_cast<uint64*>(HitProxyTable);
								for (uint32 DualEntry = 0; DualEntry < HitProxyEntryCount >> 1; ++DualEntry)
								{
									DualHitProxyEntry[DualEntry] = 0;
								}
							}
						#endif
						}
					}
				}
			};

			ERHIAccess CurrentAccess = ERHIAccess::Unknown;

			const int32 MaxPrimitivesUploads = GetMaxPrimitivesUpdate(NumPrimitiveDataUploads, FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s);
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

				RHICmdList.Transition(FRHITransitionInfo(MirrorResourceGPU->UAV, CurrentAccess, ERHIAccess::UAVCompute));
				CurrentAccess = ERHIAccess::UAVCompute;

				PrimitiveUploadBuffer.ResourceUploadTo(RHICmdList, *MirrorResourceGPU, true);
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

					RHICmdList.Transition(FRHITransitionInfo(MirrorResourceGPU->UAV, CurrentAccess, ERHIAccess::UAVCompute));
					CurrentAccess = ERHIAccess::UAVCompute;

					{
						QUICK_SCOPE_CYCLE_COUNTER(UploadTo);
						PrimitiveUploadBuffer.ResourceUploadTo(RHICmdList, *MirrorResourceGPU, true);
					}
				}
			}

			RHICmdList.Transition(FRHITransitionInfo(MirrorResourceGPU->UAV, CurrentAccess, ERHIAccess::SRVMask));
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
			for (int32 InstanceIndex = 0; InstanceIndex < InstanceDataAllocator.GetMaxSize(); ++InstanceIndex)
			{
				if (InstanceDataToClear[InstanceIndex])
				{
					check(InstanceClearList.Contains(InstanceIndex));
				}
			}
		}
	#endif

		for (uint32 InstanceIndex : InstanceClearList)
		{
			// Any clear bits set after enumerating the primitives being updated are
			// stale sections of the instance data buffer, so they should be reset to
			// an invalid state and skipped on the GPU.
			if (InstanceDataToClear[InstanceIndex])
			{
				InstanceDataToClear[InstanceIndex] = false;
				InstancesToClear.Add(InstanceIndex);
			}
		}

		InstanceClearList.Reset();


		// Clears count toward the total instance data uploads - batched together for efficiency.
		NumInstanceDataUploads += InstancesToClear.Num();

		// GPUCULL_TODO: May this not skip clears? E.g. if something is removed?
		//ensure(NumPrimitiveDataUploads == 0 ? NumInstanceDataUploads == 0 : true);
		{
			// Upload instancing data for the scene.
			if (NumInstanceDataUploads > 0)
			{
				InstanceUploadBuffer.Init(NumInstanceDataUploads * InstanceDataNumArrays, sizeof(FVector4), true, TEXT("InstanceUploadBuffer"));

				int32 RangeCount = PartitionUpdateRanges(ParallelRanges, InstancesToClear.Num(), bExecuteInParallel);

				// Reset any instance slots marked for clearing.
				ParallelFor(RangeCount,
					[this, &InstancesToClear, &ParallelRanges, RangeCount, InstanceDataNumArrays, InstanceDataSizeReserve](int32 RangeIndex)
					{
						for (int32 ItemIndex = ParallelRanges.Range[RangeIndex].ItemStart; ItemIndex < ParallelRanges.Range[RangeIndex].ItemStart + ParallelRanges.Range[RangeIndex].ItemCount; ++ItemIndex)
						{
							const int32 Index = InstancesToClear[ItemIndex];
							FPrimitiveInstance PrimitiveInstance;
							PrimitiveInstance.PrimitiveId = ~uint32(0);
							FInstanceSceneShaderData InstanceSceneData(PrimitiveInstance);

							void* DstRefs[FInstanceSceneShaderData::InstanceDataStrideInFloat4s];
							if (RangeCount > 1)
							{
								PrimitiveUploadBufferCS.Lock();
							}

							for (uint32 RefIndex = 0; RefIndex < InstanceDataNumArrays; ++RefIndex)
							{
								DstRefs[RefIndex] = InstanceUploadBuffer.Add_GetRef(RefIndex * InstanceDataSizeReserve + Index);
							}

							if (RangeCount > 1)
							{
								PrimitiveUploadBufferCS.Unlock();
							}

							for (uint32 RefIndex = 0; RefIndex < InstanceDataNumArrays; ++RefIndex) //TODO: make a SOA version of InstanceUploadBuffer.Add
							{
								FVector4* DstVector = static_cast<FVector4*>(DstRefs[RefIndex]);
								*DstVector = InstanceSceneData.Data[RefIndex];
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
						[&UploadDataSourceAdapter, this, &ParallelRanges, RangeCount, InstanceDataNumArrays, InstanceDataSizeReserve](int32 RangeIndex)
						{
							for (int32 ItemIndex = ParallelRanges.Range[RangeIndex].ItemStart; ItemIndex < ParallelRanges.Range[RangeIndex].ItemStart + ParallelRanges.Range[RangeIndex].ItemCount; ++ItemIndex)
							{
								FInstanceUploadInfo UploadInfo;
								if (UploadDataSourceAdapter.GetInstanceInfo(ItemIndex, UploadInfo))
								{
									// These are the bounds covering all primitives (which we don't want to use for per-instance culling).
									//const FBoxSphereBounds& PrimitiveBounds = PrimitiveSceneProxy->GetLocalBounds();

									// Update each primitive instance with current data.
									for (int32 InstanceIndex = 0; InstanceIndex < UploadInfo.PrimitiveInstances.Num(); ++InstanceIndex)
									{
										FPrimitiveInstance& PrimitiveInstance = UploadInfo.PrimitiveInstances[InstanceIndex];
										PrimitiveInstance.PrimitiveId = UploadInfo.PrimitiveID;
										PrimitiveInstance.LocalBounds = PrimitiveInstance.RenderBounds;// .TransformBy(PrimitiveInstance.LocalToWorld);
										PrimitiveInstance.LocalToWorld = PrimitiveInstance.InstanceToLocal * UploadInfo.PrimitiveLocalToWorld;
										// TODO: KevinO cleanup
										const FMatrix& PrevInstanceToLocal = UploadInfo.bHasPrevInstanceTransform ? PrimitiveInstance.PrevInstanceToLocal : PrimitiveInstance.InstanceToLocal;
										PrimitiveInstance.PrevLocalToWorld = PrevInstanceToLocal * UploadInfo.PreviousPrimitiveLocalToWorld;
										PrimitiveInstance.LastUpdateSceneFrameNumber = SceneFrameNumber;

										{
											// Extract per axis scales from InstanceToWorld transform
											FVector4 WorldX = FVector4(PrimitiveInstance.LocalToWorld.M[0][0], PrimitiveInstance.LocalToWorld.M[0][1], PrimitiveInstance.LocalToWorld.M[0][2], 0);
											FVector4 WorldY = FVector4(PrimitiveInstance.LocalToWorld.M[1][0], PrimitiveInstance.LocalToWorld.M[1][1], PrimitiveInstance.LocalToWorld.M[1][2], 0);
											FVector4 WorldZ = FVector4(PrimitiveInstance.LocalToWorld.M[2][0], PrimitiveInstance.LocalToWorld.M[2][1], PrimitiveInstance.LocalToWorld.M[2][2], 0);

											const float ScaleX = FVector(WorldX).Size();
											const float ScaleY = FVector(WorldY).Size();
											const float ScaleZ = FVector(WorldZ).Size();

											PrimitiveInstance.NonUniformScale = FVector4(
												ScaleX, ScaleY, ScaleZ,
												FMath::Max3(FMath::Abs(ScaleX), FMath::Abs(ScaleY), FMath::Abs(ScaleZ))
											);

											PrimitiveInstance.InvNonUniformScaleAndDeterminantSign = FVector4(
												ScaleX > KINDA_SMALL_NUMBER ? 1.0f / ScaleX : 0.0f,
												ScaleY > KINDA_SMALL_NUMBER ? 1.0f / ScaleY : 0.0f,
												ScaleZ > KINDA_SMALL_NUMBER ? 1.0f / ScaleZ : 0.0f,
												FMath::FloatSelect(PrimitiveInstance.LocalToWorld.RotDeterminant(), 1.0f, -1.0f)
											);
										}

										FInstanceSceneShaderData InstanceSceneData(PrimitiveInstance);

										void* DstRefs[FInstanceSceneShaderData::InstanceDataStrideInFloat4s];
										if (RangeCount > 1)
										{
											PrimitiveUploadBufferCS.Lock();
										}

										for (uint32 RefIndex = 0; RefIndex < InstanceDataNumArrays; ++RefIndex)
										{
											DstRefs[RefIndex] = InstanceUploadBuffer.Add_GetRef(RefIndex * InstanceDataSizeReserve + UploadInfo.InstanceDataOffset + InstanceIndex);
										}

										if (RangeCount > 1)
										{
											PrimitiveUploadBufferCS.Unlock();
										}

										for (uint32 RefIndex = 0; RefIndex < InstanceDataNumArrays; ++RefIndex) //TODO: make a SOA version of InstanceUploadBuffer.Add
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
				RHICmdList.Transition(FRHITransitionInfo(InstanceDataBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
				InstanceUploadBuffer.ResourceUploadTo(RHICmdList, InstanceDataBuffer, false);
				RHICmdList.Transition(FRHITransitionInfo(InstanceDataBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
			}
			else if (bResizedInstanceData)
			{
				RHICmdList.Transition(FRHITransitionInfo(InstanceDataBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVMask));
			}

			if( Scene != nullptr && Scene->InstanceBVH.GetNumDirty() > 0 )
			{
				InstanceUploadBuffer.Init( Scene->InstanceBVH.GetNumDirty(), sizeof( FBVHNode ), true, TEXT("InstanceUploadBuffer") );

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

						InstanceUploadBuffer.Add( NodeIndex, &GPUNode );
					} );

				RHICmdList.Transition( FRHITransitionInfo( InstanceBVHBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute ) );
				InstanceUploadBuffer.ResourceUploadTo( RHICmdList, InstanceBVHBuffer, false );
				RHICmdList.Transition( FRHITransitionInfo( InstanceBVHBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask ) );
			}

			if (NumLightmapDataUploads > 0)
			{
				ERHIAccess CurrentAccess = ERHIAccess::Unknown;

				// GPUCULL_TODO: This code is wrong: the intention is to break it up into batches such that the uploaded data fits in the max buffer size
				//               However, what it does do is break it up into batches of MaxLightmapsUploads (while iterating over primitives). This is bad
				//               because it a) makes more batches than needed, b) does not AFAICT guarantee that we don't overflow (as each prim may have 
				//				 multiple LCIs - so all may belong to the first 1/8th of primitives).
				const int32 MaxLightmapsUploads = GetMaxPrimitivesUpdate(NumLightmapDataUploads, FLightmapSceneShaderData::LightmapDataStrideInFloat4s);
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

					RHICmdList.Transition(FRHITransitionInfo(LightmapDataBuffer.UAV, CurrentAccess, ERHIAccess::UAVCompute));
					CurrentAccess = ERHIAccess::UAVCompute;

					LightmapUploadBuffer.ResourceUploadTo(RHICmdList, LightmapDataBuffer, false);
				}

				RHICmdList.Transition(FRHITransitionInfo(LightmapDataBuffer.UAV, CurrentAccess, ERHIAccess::SRVMask));
			}

			if (PrimitiveUploadBuffer.GetNumBytes() > (uint32)GGPUSceneMaxPooledUploadBufferSize)
			{
				PrimitiveUploadBuffer.Release();
			}

			if (InstanceUploadBuffer.GetNumBytes() > (uint32)GGPUSceneMaxPooledUploadBufferSize)
			{
				InstanceUploadBuffer.Release();
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

	FUploadDataSourceAdapterDynamicPrimitives(const TArray<FPrimitiveUniformShaderParameters, TInlineAllocator<8>>& InPrimitiveShaderData, int32 InPrimitiveIDStartOffset, int32 InInstanceIDStartOffset) :
		PrimitiveShaderData(InPrimitiveShaderData), 
		PrimitiveIDStartOffset(InPrimitiveIDStartOffset), 
		InstanceIDStartOffset(InInstanceIDStartOffset) {}

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
			// Needed to ensure the link back to instace list is up to date
			FPrimitiveUniformShaderParameters Tmp = PrimitiveShaderData[ItemIndex];
#if defined(GPUCULL_TODO)
			Tmp.InstanceDataOffset = InstanceIDStartOffset + ItemIndex;
			Tmp.NumInstanceDataEntries = 1;

			PrimitiveUploadInfo.InstanceDataOffset = InstanceIDStartOffset + ItemIndex;
			PrimitiveUploadInfo.InstanceUploadCount = 1;
#else // !defined(GPUCULL_TODO)
			PrimitiveUploadInfo.InstanceDataOffset = INDEX_NONE;
			PrimitiveUploadInfo.InstanceUploadCount = 0;
#endif // defined(GPUCULL_TODO)
			PrimitiveUploadInfo.PrimitiveID = PrimitiveIDStartOffset + ItemIndex;
			PrimitiveUploadInfo.PrimitiveSceneData = FPrimitiveSceneShaderData(Tmp);

			return true;
		}
		return false;
	}

	FORCEINLINE bool GetInstanceInfo(int32 ItemIndex, FInstanceUploadInfo& InstanceUploadInfo)
	{	

#if defined(GPUCULL_TODO)
		if (ItemIndex < PrimitiveShaderData.Num())
		{
			InstanceUploadInfo.PrimitiveLocalToWorld = PrimitiveShaderData[ItemIndex].LocalToWorld;
			InstanceUploadInfo.PreviousPrimitiveLocalToWorld = PrimitiveShaderData[ItemIndex].PreviousLocalToWorld;
			InstanceUploadInfo.InstanceDataOffset = InstanceIDStartOffset + ItemIndex;
			InstanceUploadInfo.PrimitiveID = PrimitiveIDStartOffset + ItemIndex;;

			// We always create an instance to ensure that we can always use the same code paths in the shader
			// In the future we should remove redundant data from the primitive, and then the instances should be
			// provided by the proxy. However, this is a lot of work before we can just enable it in the base proxy class.
			FPrimitiveInstance& DummyInstance = InstanceUploadInfo.DummyInstance;
			DummyInstance.InstanceToLocal = FMatrix::Identity;
			DummyInstance.LocalToInstance = FMatrix::Identity;
			DummyInstance.LocalToWorld = FMatrix::Identity;
			DummyInstance.PrevLocalToWorld = FMatrix::Identity;
			DummyInstance.NonUniformScale = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
			DummyInstance.InvNonUniformScaleAndDeterminantSign = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
			DummyInstance.RenderBounds = FBoxSphereBounds(FBox(PrimitiveShaderData[ItemIndex].LocalObjectBoundsMin, PrimitiveShaderData[ItemIndex].LocalObjectBoundsMax));
			DummyInstance.LocalBounds = DummyInstance.RenderBounds;;
			DummyInstance.PrimitiveId = 0xFFFFFFFF;
			DummyInstance.LastUpdateSceneFrameNumber = 0xFFFFFFFF;

			InstanceUploadInfo.PrimitiveInstances = TArrayView<FPrimitiveInstance>(&DummyInstance, 1);

			return true;
		}
#endif // defined(GPUCULL_TODO)
		return false;
	}

	/*FORCEINLINE*/ bool GetLightMapInfo(int32 ItemIndex, FLightMapUploadInfo& UploadInfo) const
	{
		return false;
	}

	const TArray<FPrimitiveUniformShaderParameters, TInlineAllocator<8>> &PrimitiveShaderData;
	int32 PrimitiveIDStartOffset;
	int32 InstanceIDStartOffset;
};

void FGPUScene::UploadDynamicPrimitiveShaderDataForViewInternal(FRHICommandListImmediate& RHICmdList, FScene *Scene, FViewInfo& View)
{
	ensure(bInBeginEndBlock);
	ensure(Scene == nullptr || DynamicPrimitivesOffset >= Scene->Primitives.Num());
	
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(UploadDynamicPrimitiveShaderData);
	QUICK_SCOPE_CYCLE_COUNTER(STAT_UploadDynamicPrimitiveShaderData);

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
		Collector.UploadData->bIsUploaded = true;
		/** Tracks dynamic primitive data for upload to GPU Scene, when enabled. */
		auto &DynamicPrimitiveShaderData = Collector.UploadData->PrimitiveShaderData;

		int32 UploadIdStart = Collector.GetPrimitiveIdRange().GetLowerBoundValue();
		ensure(UploadIdStart < DynamicPrimitivesOffset);
#if defined(GPUCULL_TODO)
		ensure(Collector.UploadData->InstanceDataOffset != INDEX_NONE);
#endif // defined(GPUCULL_TODO)

		FUploadDataSourceAdapterDynamicPrimitives UploadAdapter(DynamicPrimitiveShaderData, UploadIdStart, Collector.UploadData->InstanceDataOffset);
		UploadGeneral<FUploadDataSourceAdapterDynamicPrimitives>(RHICmdList, Scene, UploadAdapter);
	}

	UpdateUniformResource(View, *this);

	// Update view uniform buffer
	View.CachedViewUniformShaderParameters->InstanceSceneData = InstanceDataBuffer.SRV;
	View.CachedViewUniformShaderParameters->LightmapSceneData = LightmapDataBuffer.SRV;
	View.CachedViewUniformShaderParameters->InstanceDataSOAStride = InstanceDataSOAStride;

	View.ViewUniformBuffer.UpdateUniformBufferImmediate(*View.CachedViewUniformShaderParameters);
}

void AddPrimitiveToUpdateGPU(FScene& Scene, int32 PrimitiveId)
{
	Scene.GPUScene.AddPrimitiveToUpdate(PrimitiveId);
}

void FGPUScene::AddPrimitiveToUpdate(int32 PrimitiveId)
{
	if (bIsEnabled)
	{
		if (PrimitiveId + 1 > PrimitivesMarkedToUpdate.Num())
		{
			const int32 NewSize = Align(PrimitiveId + 1, 64);
			PrimitivesMarkedToUpdate.Add(0, NewSize - PrimitivesMarkedToUpdate.Num());
		}

		// Make sure we aren't updating same primitive multiple times.
		if (!PrimitivesMarkedToUpdate[PrimitiveId])
		{
			PrimitivesToUpdate.Add(PrimitiveId);
			PrimitivesMarkedToUpdate[PrimitiveId] = true;
		}
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
		
		UpdateInternal(GraphBuilder.RHICmdList, Scene);
	}
}

void FGPUScene::UploadDynamicPrimitiveShaderDataForView(FRHICommandListImmediate& RHICmdList, FScene *Scene, FViewInfo& View)
{
	if (bIsEnabled)
	{
		UploadDynamicPrimitiveShaderDataForViewInternal(RHICmdList, Scene, View);
	}
}

int32 FGPUScene::AllocateInstanceSlots(int32 NumInstanceDataEntries)
{
	if (bIsEnabled)
	{
		if (NumInstanceDataEntries > 0)
		{
			int32 InstanceDataOffset = InstanceDataAllocator.Allocate(NumInstanceDataEntries);

			// Allocate enough storage space, if needed.
			const int32 NewSize = InstanceDataOffset + NumInstanceDataEntries;
			if (NewSize >= InstanceDataToClear.Num())
			{
				InstanceDataToClear.Add(false, NewSize - InstanceDataToClear.Num());
			}

			InstanceClearList.Reserve(InstanceDataToClear.Num());

			// Unset all bits associated with newly allocated instance data.
			InstanceDataToClear.SetRange(InstanceDataOffset, NumInstanceDataEntries, false);
			check(InstanceDataToClear.Num() == InstanceDataAllocator.GetMaxSize());

			return InstanceDataOffset;
		}
	}
	return INDEX_NONE;
}


void FGPUScene::FreeInstanceSlots(int InstanceDataOffset, int32 NumInstanceDataEntries)
{
	if (bIsEnabled)
	{
		InstanceDataAllocator.Free(InstanceDataOffset, NumInstanceDataEntries);
		InstanceDataToClear.SetRange(InstanceDataOffset, NumInstanceDataEntries, true);
		InstanceClearList.Reserve(InstanceDataToClear.Num());
		for (int32 AddIndex = 0; AddIndex < NumInstanceDataEntries; ++AddIndex)
		{
			InstanceClearList.Add(InstanceDataOffset + AddIndex);
		}

		// Resize bit arrays to match new high watermark
		if (InstanceDataToClear.Num() > InstanceDataAllocator.GetMaxSize())
		{
			const int32 OldBitCount = InstanceDataToClear.Num();
			const int32 NewBitCount = InstanceDataAllocator.GetMaxSize();
			const int32 RemBitCount = OldBitCount - NewBitCount;
			InstanceDataToClear.RemoveAt(NewBitCount, RemBitCount);
			check(InstanceDataToClear.Num() == InstanceDataAllocator.GetMaxSize());
		}
	}
}


void FGPUScene::MarkPrimitiveAdded(int32 PrimitiveId)
{
	if (bIsEnabled)
	{
		check(PrimitiveId >= 0);

		if (PrimitiveId >= AddedPrimitiveFlags.Num())
		{
			AddedPrimitiveFlags.Add(false, PrimitiveId + 1 - AddedPrimitiveFlags.Num());
		}
		AddedPrimitiveFlags[PrimitiveId] = true;
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

#if defined(GPUCULL_TODO)
	PrimitiveCollector.UploadData->InstanceDataOffset = AllocateInstanceSlots(PrimitiveCollector.UploadData->PrimitiveShaderData.Num());
#endif // defined(GPUCULL_TODO)

	return TRange<int32>(StartOffset, DynamicPrimitivesOffset);
}


FGPUSceneDynamicContext::~FGPUSceneDynamicContext()
{
	for (auto UploadData : DymamicPrimitiveUploadData)
	{
#if defined(GPUCULL_TODO)
		check(UploadData->InstanceDataOffset != INDEX_NONE);
		GPUScene.FreeInstanceSlots(UploadData->InstanceDataOffset, UploadData->PrimitiveShaderData.Num());
#endif // defined(GPUCULL_TODO)
		delete UploadData;
	}
	DymamicPrimitiveUploadData.Empty();
}


FGPUScenePrimitiveCollector::FUploadData* FGPUSceneDynamicContext::AllocateDynamicPrimitiveData()
{
	FGPUScenePrimitiveCollector::FUploadData* UploadData = new FGPUScenePrimitiveCollector::FUploadData;
	DymamicPrimitiveUploadData.Add(UploadData);
	return UploadData;
}
