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

// Always a benefit unless the game is doing tons of add/remove instance
// calls, which isn't advisable anyways.
int32 GGPUSceneInstanceClearList = 1;
FAutoConsoleVariableRef CVarGPUSceneInstanceClearList(
	TEXT("r.GPUScene.InstanceClearList"),
	GGPUSceneInstanceClearList,
	TEXT("Whether to use instance clear indirection list."),
	ECVF_RenderThreadSafe
);


template<typename ResourceType>
ResourceType* GetMirrorGPU(FGPUScene& GPUScene);

template<>
FRWBufferStructured* GetMirrorGPU<FRWBufferStructured>(FGPUScene& GPUScene)
{
	return &GPUScene.PrimitiveBuffer;
}


template<>
FTextureRWBuffer2D* GetMirrorGPU<FTextureRWBuffer2D>(FGPUScene& GPUScene)
{
	return &GPUScene.PrimitiveTexture;
}

template<typename ResourceType>
void* LockResource(ResourceType& Resource, uint32& Stride);
template<>
void* LockResource<FRWBufferStructured>(FRWBufferStructured& Resource, uint32& Stride)
{
	Stride = 0;
	return RHILockStructuredBuffer(Resource.Buffer, 0, Resource.NumBytes, RLM_ReadOnly);
}
template<>
void* LockResource<FTextureRWBuffer2D>(FTextureRWBuffer2D& Resource, uint32& Stride)
{
	return RHILockTexture2D(Resource.Buffer, 0, RLM_ReadOnly, Stride, false);
}

template<typename ResourceType>
void UnlockResourceGPUScene(ResourceType& Resource);
template<>
void UnlockResourceGPUScene<FRWBufferStructured>(FRWBufferStructured& Resource)
{
	RHIUnlockStructuredBuffer(Resource.Buffer);
}
template<>
void UnlockResourceGPUScene<FTextureRWBuffer2D>(FTextureRWBuffer2D& Resource)
{
	RHIUnlockTexture2D(Resource.Buffer, 0, false);
}

template<typename ResourceType>
void UpdateUniformResource(FViewInfo& View, FGPUScene& GPUScene);


template<>
void UpdateUniformResource<FTextureRWBuffer2D>(FViewInfo& View, FGPUScene& GPUScene)
{
	View.CachedViewUniformShaderParameters->PrimitiveSceneDataTexture = GetMirrorGPU<FTextureRWBuffer2D>(GPUScene)->Buffer;
}
template<>
void UpdateUniformResource<FRWBufferStructured>(FViewInfo& View, FGPUScene& GPUScene)
{
	View.CachedViewUniformShaderParameters->PrimitiveSceneData = GetMirrorGPU<FRWBufferStructured>(GPUScene)->SRV;
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


FGPUScene::~FGPUScene()
{
}

void FGPUScene::BeginRender(FScene& Scene, FGPUSceneDynamicContext &GPUSceneDynamicContext)
{
	ensure(!bInBeginEndBlock);
	ensure(CurrentDynamicContext == nullptr);
	ensure(bIsEnabled == UseGPUScene(GMaxRHIShaderPlatform, Scene.GetFeatureLevel()));
	CurrentDynamicContext = &GPUSceneDynamicContext;
	NumScenePrimitives = Scene.Primitives.Num();
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

template<typename ResourceType>
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

		// Multi-GPU support : Updating on all GPUs is inefficient for AFR. Work is wasted
		// for any primitives that update on consecutive frames.
		SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::All());

		// Store in GPU-scene to enable validation that update has been carried out.
		SceneFrameNumber = Scene.GetFrameNumber();

		const bool bNaniteEnabled = DoesPlatformSupportNanite(GMaxRHIShaderPlatform);

		const bool bExecuteInParallel = GGPUSceneParallelUpdate != 0 && FApp::ShouldUseThreadingForPerformance();

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
			if (GGPUSceneInstanceClearList != 0)
			{
				for (int32 Index = 0; Index < InstanceDataToClear.Num(); ++Index)
				{
					InstanceClearList.Add(Index);
				}
			}

			bUpdateAllPrimitives = false;
		}

		bool bResizedPrimitiveData = false;
		bool bResizedInstanceData = false;
		bool bResizedLightmapData = false;

		ResourceType* MirrorResourceGPU = GetMirrorGPU<ResourceType>(*this);
		{
			const uint32 SizeReserve = FMath::RoundUpToPowerOfTwo( FMath::Max(DynamicPrimitivesOffset, 256 ) );
			bResizedPrimitiveData = ResizeResourceIfNeeded(RHICmdList, *MirrorResourceGPU, SizeReserve * sizeof(FPrimitiveSceneShaderData::Data), TEXT("PrimitiveData"));
		}

		const uint32 InstanceDataNumArrays = FInstanceSceneShaderData::InstanceDataStrideInFloat4s;
		const uint32 InstanceDataSizeReserve = FMath::RoundUpToPowerOfTwo(FMath::Max(InstanceDataAllocator.GetMaxSize(), 256));
		bResizedInstanceData = ResizeResourceSOAIfNeeded(RHICmdList, InstanceDataBuffer, InstanceDataSizeReserve * sizeof(FInstanceSceneShaderData::Data), InstanceDataNumArrays, TEXT("InstanceData"));
		InstanceDataSOAStride = InstanceDataSizeReserve;
		
		{
			const uint32 SizeReserve = FMath::RoundUpToPowerOfTwo( FMath::Max( LightmapDataAllocator.GetMaxSize(), 256 ) );
			bResizedLightmapData = ResizeResourceIfNeeded(RHICmdList, LightmapDataBuffer, SizeReserve * sizeof(FLightmapSceneShaderData::Data), TEXT("LightmapData"));
		}

		// These should always be in sync with each other.
		check(InstanceDataToClear.Num() == InstanceDataAllocator.GetMaxSize());

		const int32 NumPrimitiveDataUploads = PrimitivesToUpdate.Num();

		if (bNaniteEnabled)
		{
			for (int32 NaniteMeshPassIndex = 0; NaniteMeshPassIndex < ENaniteMeshPass::Num; ++NaniteMeshPassIndex)
			{
				Scene.MaterialTables[NaniteMeshPassIndex].Begin(RHICmdList, Scene.Primitives.Num(), NumPrimitiveDataUploads);
			}
		}

		int32 NumLightmapDataUploads = 0;
		int32 NumInstanceDataUploads = 0;

		static FCriticalSection PrimitiveUploadBufferCS;
		static FCriticalSection MaterialTableUploadCS;

		FParallelUpdateRanges ParallelRanges;

		if (NumPrimitiveDataUploads > 0)
		{
			auto ProcessPrimitiveFn = [this, &Scene, &NumLightmapDataUploads, &NumInstanceDataUploads, bNaniteEnabled](int32 PrimitiveIndex, bool bThreaded)
			{
				// PrimitivesToUpdate may contain a stale out of bounds index, as we don't remove update request on primitive removal from scene.
				if (PrimitiveIndex < Scene.PrimitiveSceneProxies.Num())
				{
					const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy = Scene.PrimitiveSceneProxies[PrimitiveIndex];
					const FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveSceneProxy->GetPrimitiveSceneInfo();

					int32 TaskInstanceDataOffset  = 0;
					int32 TaskInstanceUploadCount = 0;
					int32 TaskLightmapUploadCount = PrimitiveSceneInfo->GetNumLightmapDataEntries();

					// Count all primitive instances represented in the instance data buffer.
					if (PrimitiveSceneProxy->SupportsInstanceDataBuffer())
					{
						const TArray<FPrimitiveInstance>* PrimitiveInstances = PrimitiveSceneProxy->GetPrimitiveInstances();
						TaskInstanceDataOffset  = PrimitiveSceneProxy->GetPrimitiveSceneInfo()->GetInstanceDataOffset();
						TaskInstanceUploadCount = PrimitiveInstances->Num();
					}

					{
						if (bThreaded)
						{
							PrimitiveUploadBufferCS.Lock();
						}

						// Cancel out any pending clear bits for these instances.
						if (TaskInstanceUploadCount > 0)
						{
							InstanceDataToClear.SetRange(TaskInstanceDataOffset, TaskInstanceUploadCount, false);
						}

						NumLightmapDataUploads += TaskLightmapUploadCount; // Not thread safe
						NumInstanceDataUploads += TaskInstanceUploadCount; // Not thread safe

						void* UploadDst = PrimitiveUploadBuffer.Add_GetRef(PrimitiveIndex); // Not thread safe

						if (bThreaded)
						{
							PrimitiveUploadBufferCS.Unlock();
						}

						FPrimitiveSceneShaderData PrimitiveSceneData(PrimitiveSceneProxy);
						FVector4* DstData = static_cast<FVector4*>(UploadDst);
						for (uint32 VectorIndex = 0; VectorIndex < FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s; ++VectorIndex)
						{
							DstData[VectorIndex] = PrimitiveSceneData.Data[VectorIndex];
						}
					}

					// Update Nanite material tables associated with this primitive index.
					if (bNaniteEnabled && PrimitiveSceneProxy->IsNaniteMesh())
					{
						const Nanite::FSceneProxyBase* NaniteSceneProxy = static_cast<const Nanite::FSceneProxyBase*>(PrimitiveSceneProxy);

						// Update material depth and hit proxy ID remapping tables.
						for (int32 NaniteMeshPass = 0; NaniteMeshPass < ENaniteMeshPass::Num; ++NaniteMeshPass)
						{
							FNaniteMaterialTables& PassMaterialTables = Scene.MaterialTables[NaniteMeshPass];
							const TArray<uint32>& PassMaterialIds = PrimitiveSceneInfo->NaniteMaterialIds[NaniteMeshPass];
							check(NaniteSceneProxy->GetMaterialSections().Num() == PassMaterialIds.Num());

							if (bThreaded)
							{
								MaterialTableUploadCS.Lock();
							}

							void* DepthTable = PassMaterialTables.GetDepthTablePtr(PrimitiveIndex, PassMaterialIds.Num());
						#if WITH_EDITOR
							const uint32 HitProxyEntryCount = (NaniteMeshPass == ENaniteMeshPass::BasePass) ? PrimitiveSceneInfo->NaniteHitProxyIds.Num() : NANITE_MAX_MATERIALS;
							void* HitProxyTable = PassMaterialTables.GetHitProxyTablePtr(PrimitiveIndex, HitProxyEntryCount);
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
				SCOPED_DRAW_EVENTF(RHICmdList, UpdateGPUScene, TEXT("UpdateGPUScene PrimitivesToUpdate %u"), NumPrimitiveDataUploads);

				PrimitiveUploadBuffer.Init(NumPrimitiveDataUploads, sizeof(FPrimitiveSceneShaderData::Data), true, TEXT("PrimitiveUploadBuffer"));

				int32 RangeCount = PartitionUpdateRanges(ParallelRanges, NumPrimitiveDataUploads, bExecuteInParallel);

				ParallelFor(RangeCount,
					[this, &ProcessPrimitiveFn, &ParallelRanges, &Scene, RangeCount](int32 RangeIndex)
					{
						for (int32 ItemIndex = ParallelRanges.Range[RangeIndex].ItemStart; ItemIndex < ParallelRanges.Range[RangeIndex].ItemStart + ParallelRanges.Range[RangeIndex].ItemCount; ++ItemIndex)
						{
							const int32 Index = PrimitivesToUpdate[ItemIndex];
							ProcessPrimitiveFn(Index, RangeCount > 1);
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
					SCOPED_DRAW_EVENTF(RHICmdList, UpdateGPUScene, TEXT("UpdateGPUScene PrimitivesToUpdate and Offset = %u %u"), NumPrimitiveDataUploads, PrimitiveOffset);

					PrimitiveUploadBuffer.Init(MaxPrimitivesUploads, sizeof(FPrimitiveSceneShaderData::Data), true, TEXT("PrimitiveUploadBuffer"));

					for (int32 IndexUpdate = 0; (IndexUpdate < MaxPrimitivesUploads) && ((IndexUpdate + PrimitiveOffset) < NumPrimitiveDataUploads); ++IndexUpdate)
					{
						const int32 Index = PrimitivesToUpdate[IndexUpdate + PrimitiveOffset];
						ProcessPrimitiveFn(Index, false /* threaded */);
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

		PrimitivesMarkedToUpdate.Init(false, PrimitivesMarkedToUpdate.Num());

		if (bNaniteEnabled)
		{
			for (int32 NaniteMeshPassIndex = 0; NaniteMeshPassIndex < ENaniteMeshPass::Num; ++NaniteMeshPassIndex)
			{
				Scene.MaterialTables[NaniteMeshPassIndex].Finish(RHICmdList);
			}
		}

		// Make sure instance buffer always has valid or properly reset entries.
		TArray<uint32, TInlineAllocator<64, SceneRenderingAllocator>> InstancesToClear;

		if (GGPUSceneInstanceClearList != 0)
		{
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
		}
		else
		{
			for (int32 InstanceIndex = 0; InstanceIndex < InstanceDataAllocator.GetMaxSize(); ++InstanceIndex)
			{
				// Any clear bits set after enumerating the primitives being updated are
				// stale sections of the instance data buffer, so they should be reset to
				// an invalid state and skipped on the GPU.
				if (InstanceDataToClear[InstanceIndex])
				{
					InstancesToClear.Add(InstanceIndex);
					InstanceDataToClear[InstanceIndex] = false;
				}
			}
		}

		// Clears count toward the total instance data uploads - batched together for efficiency.
		NumInstanceDataUploads += InstancesToClear.Num();

		if (GGPUSceneValidatePrimitiveBuffer && (PrimitiveBuffer.NumBytes > 0 || PrimitiveTexture.NumBytes > 0))
		{
			//UE_LOG(LogRenderer, Warning, TEXT("r.GPUSceneValidatePrimitiveBuffer enabled, doing slow readback from GPU"));
			uint32 Stride = 0;
			FPrimitiveSceneShaderData* InstanceBufferCopy = (FPrimitiveSceneShaderData*)(FPrimitiveSceneShaderData*)LockResource(*MirrorResourceGPU, Stride);

			const int32 TotalNumberPrimitives = Scene.PrimitiveSceneProxies.Num();
			int32 MaxPrimitivesUploads = GetMaxPrimitivesUpdate(TotalNumberPrimitives, FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s);
			for (int32 IndexOffset = 0; IndexOffset < TotalNumberPrimitives; IndexOffset += MaxPrimitivesUploads)
			{
				for (int32 Index = 0; (Index < MaxPrimitivesUploads) && ((Index + IndexOffset) < TotalNumberPrimitives); ++Index)
				{
					FPrimitiveSceneShaderData PrimitiveSceneData(Scene.PrimitiveSceneProxies[Index + IndexOffset]);

					for (int32 i = 0; i < FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s; i++)
					{
						check(PrimitiveSceneData.Data[i] == InstanceBufferCopy[Index].Data[i]);
					}
				}
				InstanceBufferCopy += Stride / sizeof(FPrimitiveSceneShaderData);
			}

			UnlockResourceGPUScene(*MirrorResourceGPU);
		}

		if (NumPrimitiveDataUploads > 0)
		{
			// Upload instancing data for the scene.
			if (NumInstanceDataUploads > 0)
			{
				InstanceUploadBuffer.Init(NumInstanceDataUploads * InstanceDataNumArrays, sizeof( FVector4 ), true, TEXT("InstanceUploadBuffer"));

				int32 RangeCount = PartitionUpdateRanges(ParallelRanges, InstancesToClear.Num(), bExecuteInParallel);

				// Reset any instance slots marked for clearing.
				ParallelFor(RangeCount,
					[this, &InstancesToClear, &ParallelRanges, &Scene, RangeCount, InstanceDataNumArrays, InstanceDataSizeReserve](int32 RangeIndex)
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

				RangeCount = PartitionUpdateRanges(ParallelRanges, PrimitivesToUpdate.Num(), bExecuteInParallel);

				// Upload any out of data instance slots.
				ParallelFor(RangeCount,
					[this, &Scene, &ParallelRanges, RangeCount, InstanceDataNumArrays, InstanceDataSizeReserve](int32 RangeIndex)
					{
						for (int32 ItemIndex = ParallelRanges.Range[RangeIndex].ItemStart; ItemIndex < ParallelRanges.Range[RangeIndex].ItemStart + ParallelRanges.Range[RangeIndex].ItemCount; ++ItemIndex)
						{
							const int32 Index = PrimitivesToUpdate[ItemIndex];

							// PrimitivesToUpdate may contain a stale out of bounds index, as we don't remove update request on primitive removal from scene.
							if (Index < Scene.PrimitiveSceneProxies.Num())
							{
								FPrimitiveSceneProxy* PrimitiveSceneProxy = Scene.PrimitiveSceneProxies[Index];
								TArray<FPrimitiveInstance>* PrimitiveInstancesPtr = PrimitiveSceneProxy->GetPrimitiveInstances();
								if (!PrimitiveSceneProxy->SupportsInstanceDataBuffer() || !PrimitiveInstancesPtr)
								{
									continue;
								}

								TArray<FPrimitiveInstance>& PrimitiveInstances = *PrimitiveInstancesPtr;
								if (PrimitiveInstances.Num() == 0)
								{
									continue;
								}

								const FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveSceneProxy->GetPrimitiveSceneInfo();

								check(PrimitiveInstances.Num() == PrimitiveSceneInfo->GetNumInstanceDataEntries());
								const int32 InstanceDataOffset = PrimitiveSceneInfo->GetInstanceDataOffset();
								check(InstanceDataOffset != INDEX_NONE);

								// These are the bounds covering all primitives (which we don't want to use for per-instance culling).
								//const FBoxSphereBounds& PrimitiveBounds = PrimitiveSceneProxy->GetLocalBounds();

								// Update each primitive instance with current data.
								for (int32 InstanceIndex = 0; InstanceIndex < PrimitiveInstances.Num(); ++InstanceIndex)
								{
									bool bOutHasPrecomputedVolumetricLightmap;
									FMatrix OutPreviousLocalToWorld;
									int32 OutSingleCaptureIndex;
									bool bOutOutputVelocity;

									Scene.GetPrimitiveUniformShaderParameters_RenderThread(
										PrimitiveSceneInfo,
										bOutHasPrecomputedVolumetricLightmap,
										OutPreviousLocalToWorld,
										OutSingleCaptureIndex,
										bOutOutputVelocity
									);

									FPrimitiveInstance& PrimitiveInstance = PrimitiveInstances[InstanceIndex];
									PrimitiveInstance.PrimitiveId = uint32(PrimitiveSceneInfo->GetIndex());
									PrimitiveInstance.LocalBounds = PrimitiveInstance.RenderBounds;// .TransformBy(PrimitiveInstance.LocalToWorld);
									PrimitiveInstance.LocalToWorld = PrimitiveInstance.InstanceToLocal * PrimitiveSceneProxy->GetLocalToWorld();
									PrimitiveInstance.PrevLocalToWorld = PrimitiveInstance.InstanceToLocal * OutPreviousLocalToWorld;
									PrimitiveInstance.WorldToLocal = PrimitiveInstance.LocalToWorld.Inverse();
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
										DstRefs[RefIndex] = InstanceUploadBuffer.Add_GetRef(RefIndex * InstanceDataSizeReserve + InstanceDataOffset + InstanceIndex);
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

				RHICmdList.Transition(FRHITransitionInfo(InstanceDataBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
				InstanceUploadBuffer.ResourceUploadTo(RHICmdList, InstanceDataBuffer, false);
				RHICmdList.Transition(FRHITransitionInfo(InstanceDataBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
			}
			else if (bResizedInstanceData)
			{
				RHICmdList.Transition(FRHITransitionInfo(InstanceDataBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVMask));
			 }

			if (NumLightmapDataUploads > 0)
			{
				ERHIAccess CurrentAccess = ERHIAccess::Unknown;

				const int32 MaxLightmapsUploads = GetMaxPrimitivesUpdate(NumLightmapDataUploads, FLightmapSceneShaderData::LightmapDataStrideInFloat4s);
				for (int32 PrimitiveOffset = 0; PrimitiveOffset < NumPrimitiveDataUploads; PrimitiveOffset += MaxLightmapsUploads)
				{
					LightmapUploadBuffer.Init(MaxLightmapsUploads, sizeof(FLightmapSceneShaderData::Data), true, TEXT("LightmapUploadBuffer"));

					for (int32 IndexUpdate = 0; (IndexUpdate < MaxLightmapsUploads) && ((IndexUpdate + PrimitiveOffset) < NumPrimitiveDataUploads); ++IndexUpdate)
					{
						int32 Index = PrimitivesToUpdate[IndexUpdate + PrimitiveOffset];
						// PrimitivesToUpdate may contain a stale out of bounds index, as we don't remove update request on primitive removal from scene.
						if (Index < Scene.PrimitiveSceneProxies.Num())
						{
							FPrimitiveSceneProxy* PrimitiveSceneProxy = Scene.PrimitiveSceneProxies[Index];

							FPrimitiveSceneProxy::FLCIArray LCIs;
							PrimitiveSceneProxy->GetLCIs(LCIs);

							check(LCIs.Num() == PrimitiveSceneProxy->GetPrimitiveSceneInfo()->GetNumLightmapDataEntries());
							const int32 LightmapDataOffset = PrimitiveSceneProxy->GetPrimitiveSceneInfo()->GetLightmapDataOffset();

							for (int32 i = 0; i < LCIs.Num(); i++)
							{
								FLightmapSceneShaderData LightmapSceneData(LCIs[i], Scene.GetFeatureLevel());
								LightmapUploadBuffer.Add(LightmapDataOffset + i, &LightmapSceneData.Data[0]);
							}
						}
					}

					RHICmdList.Transition(FRHITransitionInfo(LightmapDataBuffer.UAV, CurrentAccess, ERHIAccess::UAVCompute));
					CurrentAccess = ERHIAccess::UAVCompute;

					LightmapUploadBuffer.ResourceUploadTo(RHICmdList, LightmapDataBuffer, false);
				}

				RHICmdList.Transition(FRHITransitionInfo(LightmapDataBuffer.UAV, CurrentAccess, ERHIAccess::SRVMask));
			}

			PrimitivesToUpdate.Reset();
			
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
		// Clear the flags that mark newly added primitives.
		AddedPrimitiveFlags.Init(false, AddedPrimitiveFlags.Num());

		// Dynamic primitives are allocated after the regular ones, this needs to match the value passed in in BeginRender
		ensure(DynamicPrimitivesOffset >= Scene.Primitives.Num());
	}

	checkSlow(PrimitivesToUpdate.Num() == 0);
}

template<typename ResourceType>
void FGPUScene::UploadDynamicPrimitiveShaderDataForViewInternal(FRHICommandListImmediate& RHICmdList, FScene& Scene, FViewInfo& View)
{
	ensure(bInBeginEndBlock);
	ensure(DynamicPrimitivesOffset >= Scene.Primitives.Num());
	
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(UploadDynamicPrimitiveShaderData);
	QUICK_SCOPE_CYCLE_COUNTER(STAT_UploadDynamicPrimitiveShaderData);

	FGPUScenePrimitiveCollector& Collector = View.DynamicPrimitiveCollector;

	const int32 NumPrimitiveDataUploads = Collector.Num();
	ensure(Collector.GetPrimitiveIdRange().Size<int32>() == NumPrimitiveDataUploads);
	
	// Make sure we are not trying to upload data that lives in a different context.
	ensure(Collector.UploadData == nullptr || CurrentDynamicContext->DymamicPrimitiveUploadData.Find(Collector.UploadData) != INDEX_NONE);
	
	// Skip uploading empty & already uploaded data
	if (NumPrimitiveDataUploads > 0 && !Collector.UploadData->bIsUploaded)
	{
		Collector.UploadData->bIsUploaded = true;
		/** Tracks dynamic primitive data for upload to GPU Scene, when enabled. */
		auto &DynamicPrimitiveShaderData = Collector.UploadData->PrimitiveShaderData;

		int32 UploadIdStart = Collector.GetPrimitiveIdRange().GetLowerBoundValue();
		ensure(UploadIdStart < DynamicPrimitivesOffset);

		ResourceType* MirrorResourceGPU = GetMirrorGPU<ResourceType>(*this);
		{
			// Work out space requirements for all dynamic primitives
			const int32 TotalNumberPrimitives = DynamicPrimitivesOffset;
			const uint32 PrimitiveSceneNumFloat4s = TotalNumberPrimitives * FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s;

			const uint32 SizeReserve = FMath::RoundUpToPowerOfTwo(FMath::Max(TotalNumberPrimitives, 256));

			ERHIAccess CurrentAccess = ERHIAccess::SRVMask;
			// Reserve enough space
			if (ResizeResourceIfNeeded(RHICmdList, *MirrorResourceGPU, SizeReserve * sizeof(FPrimitiveSceneShaderData::Data), TEXT("PrimitiveData")))
			{
				CurrentAccess = ERHIAccess::Unknown;
			}
			RHICmdList.Transition(FRHITransitionInfo(MirrorResourceGPU->UAV, CurrentAccess, ERHIAccess::UAVCompute));
			CurrentAccess = ERHIAccess::UAVCompute;

			// Upload DynamicPrimitiveShaderData to the allocated range in the primitive data resource
			int32 MaxPrimitivesUploads = GetMaxPrimitivesUpdate(NumPrimitiveDataUploads, FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s);
			for (int32 BatchStartIndex = 0; BatchStartIndex < NumPrimitiveDataUploads; BatchStartIndex += MaxPrimitivesUploads)
			{
				PrimitiveUploadViewBuffer.Init(MaxPrimitivesUploads, sizeof(FPrimitiveSceneShaderData::Data), true, TEXT("PrimitiveUploadViewBuffer"));

				for (int32 IndexUpdate = 0; (IndexUpdate < MaxPrimitivesUploads) && ((IndexUpdate + BatchStartIndex) < NumPrimitiveDataUploads); ++IndexUpdate)
				{
					int32 DynamicUploadIndex = BatchStartIndex + IndexUpdate;
					FPrimitiveSceneShaderData PrimitiveSceneData(DynamicPrimitiveShaderData[DynamicUploadIndex]);
					// Place dynamic primitive shader data after any previous batches of dynamic primitive data
					PrimitiveUploadViewBuffer.Add(UploadIdStart + DynamicUploadIndex, &PrimitiveSceneData.Data[0]);
				}

				PrimitiveUploadViewBuffer.ResourceUploadTo(RHICmdList, *MirrorResourceGPU, false);
			}
			RHICmdList.Transition(FRHITransitionInfo(MirrorResourceGPU->UAV, CurrentAccess, ERHIAccess::SRVMask));
		}
		if (PrimitiveUploadViewBuffer.GetNumBytes() > (uint32)GGPUSceneMaxPooledUploadBufferSize)
		{
			PrimitiveUploadViewBuffer.Release();
		}


		if (GGPUSceneValidatePrimitiveBuffer && (PrimitiveBuffer.NumBytes > 0 || PrimitiveTexture.NumBytes > 0))
		{
			uint32 Stride = 0;
			FPrimitiveSceneShaderData* InstanceBufferCopy = (FPrimitiveSceneShaderData*)LockResource(*MirrorResourceGPU, Stride);

			const int32 TotalNumberPrimitives = DynamicPrimitivesOffset;
			int32 MaxPrimitivesUploads = GetMaxPrimitivesUpdate(TotalNumberPrimitives, FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s);
			for (int32 IndexOffset = 0; IndexOffset < TotalNumberPrimitives; IndexOffset += MaxPrimitivesUploads)
			{
				for (int32 Index = 0; (Index < MaxPrimitivesUploads) && ((Index + IndexOffset) < TotalNumberPrimitives); ++Index)
				{
					if ((Index + IndexOffset) < Scene.PrimitiveSceneProxies.Num())
					{
						const FPrimitiveSceneShaderData &PrimitiveSceneData = Scene.PrimitiveSceneProxies[Index + IndexOffset];
						check(FMemory::Memcmp(&InstanceBufferCopy[Index + IndexOffset], &PrimitiveSceneData, sizeof(FPrimitiveSceneShaderData)) == 0);
					}
					else if (Collector.GetPrimitiveIdRange().Contains(Index + IndexOffset))
					{
						const FPrimitiveSceneShaderData& PrimitiveSceneData = FPrimitiveSceneShaderData(DynamicPrimitiveShaderData[Index + IndexOffset - Scene.PrimitiveSceneProxies.Num()]);
						check(FMemory::Memcmp(&InstanceBufferCopy[Index + IndexOffset], &PrimitiveSceneData, sizeof(FPrimitiveSceneShaderData)) == 0);
					}

				}
				InstanceBufferCopy += Stride / sizeof(FPrimitiveSceneShaderData);
			}

			UnlockResourceGPUScene(*MirrorResourceGPU);
		}
	}

	UpdateUniformResource<ResourceType>(View, *this);

	// Update view uniform buffer
	View.CachedViewUniformShaderParameters->InstanceSceneData = InstanceDataBuffer.SRV;
	View.CachedViewUniformShaderParameters->LightmapSceneData = LightmapDataBuffer.SRV;
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
		if (GPUSceneUseTexture2D(Scene.GetShaderPlatform()))
		{
			UpdateInternal<FTextureRWBuffer2D>(GraphBuilder.RHICmdList, Scene);
		}
		else
		{
			UpdateInternal<FRWBufferStructured>(GraphBuilder.RHICmdList, Scene);
		}
	}
}

void FGPUScene::UploadDynamicPrimitiveShaderDataForView(FRHICommandListImmediate& RHICmdList, FScene& Scene, FViewInfo& View)
{
	if (bIsEnabled)
	{
		if (GPUSceneUseTexture2D(Scene.GetShaderPlatform()))
		{
			UploadDynamicPrimitiveShaderDataForViewInternal<FTextureRWBuffer2D>(RHICmdList, Scene, View);
		}
		else
		{
			UploadDynamicPrimitiveShaderDataForViewInternal<FRWBufferStructured>(RHICmdList, Scene, View);
		}
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

			if (GGPUSceneInstanceClearList != 0)
			{
				InstanceClearList.Reserve(InstanceDataToClear.Num());
			}

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

		if (GGPUSceneInstanceClearList != 0)
		{
			InstanceClearList.Reserve(InstanceDataToClear.Num());
			for (int32 AddIndex = 0; AddIndex < NumInstanceDataEntries; ++AddIndex)
			{
				InstanceClearList.Add(InstanceDataOffset + AddIndex);
			}
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

	return TRange<int32>(StartOffset, DynamicPrimitivesOffset);
}


FGPUSceneDynamicContext::~FGPUSceneDynamicContext()
{
	for (auto UploadData : DymamicPrimitiveUploadData)
	{
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

