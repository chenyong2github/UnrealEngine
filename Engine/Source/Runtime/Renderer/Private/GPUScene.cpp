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
ResourceType* GetMirrorGPU(FScene& Scene);
template<>
FRWBufferStructured* GetMirrorGPU<FRWBufferStructured>(FScene& Scene)
{
	return &Scene.GPUScene.PrimitiveBuffer;
}
template<>
FTextureRWBuffer2D* GetMirrorGPU<FTextureRWBuffer2D>(FScene& Scene)
{
	return &Scene.GPUScene.PrimitiveTexture;
}

template<typename ResourceType>
ResourceType& GetViewStateResourceRef(FViewInfo& View, bool bSingle);
template<>
FRWBufferStructured& GetViewStateResourceRef(FViewInfo& View, bool bSingle)
{
	return bSingle ? View.OneFramePrimitiveShaderDataBuffer: View.ViewState->PrimitiveShaderDataBuffer;
}
template<>
FTextureRWBuffer2D& GetViewStateResourceRef(FViewInfo& View, bool bSingle)
{
	return bSingle ? View.OneFramePrimitiveShaderDataTexture: View.ViewState->PrimitiveShaderDataTexture;
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
void UpdateUniformResource(FViewInfo& View, FScene& Scene, bool bDynamicPrimitives);
template<>
void UpdateUniformResource<FTextureRWBuffer2D>(FViewInfo& View, FScene& Scene, bool bDynamicPrimitives)
{
	View.CachedViewUniformShaderParameters->PrimitiveSceneDataTexture = bDynamicPrimitives ? GetViewStateResourceRef<FTextureRWBuffer2D>(View, View.ViewState == nullptr).Buffer: GetMirrorGPU<FTextureRWBuffer2D>(Scene)->Buffer;
}
template<>
void UpdateUniformResource<FRWBufferStructured>(FViewInfo& View, FScene& Scene, bool bDynamicPrimitives)
{
	View.CachedViewUniformShaderParameters->PrimitiveSceneData = bDynamicPrimitives ? GetViewStateResourceRef<FRWBufferStructured>(View, View.ViewState == nullptr).SRV: GetMirrorGPU<FRWBufferStructured>(Scene)->SRV;
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

template<typename ResourceType>
void UpdateGPUSceneInternal(FRHICommandListImmediate& RHICmdList, FScene& Scene)
{
	if (UseGPUScene(GMaxRHIShaderPlatform, Scene.GetFeatureLevel()))
	{
		SCOPED_NAMED_EVENT( STAT_UpdateGPUScene, FColor::Green );
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(UpdateGPUScene);
		QUICK_SCOPE_CYCLE_COUNTER(STAT_UpdateGPUScene);
		SCOPE_CYCLE_COUNTER(STAT_UpdateGPUSceneTime);

		// Multi-GPU support : Updating on all GPUs is inefficient for AFR. Work is wasted
		// for any primitives that update on consecutive frames.
		SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::All());

		const uint32 SceneFrameNumber = Scene.GetFrameNumber();
		// Store in GPU-scene to enable validation that update has been carried out.
		Scene.GPUScene.SceneFrameNumber = SceneFrameNumber;

		const bool bNaniteEnabled = DoesPlatformSupportNanite(GMaxRHIShaderPlatform);

		const bool bExecuteInParallel = GGPUSceneParallelUpdate != 0 && FApp::ShouldUseThreadingForPerformance();

		if (GGPUSceneUploadEveryFrame || Scene.GPUScene.bUpdateAllPrimitives)
		{
			Scene.GPUScene.PrimitivesMarkedToUpdate.Init(false, Scene.GPUScene.PrimitivesMarkedToUpdate.Num());
			Scene.GPUScene.PrimitivesToUpdate.Reset();

			for (int32 Index = 0; Index < Scene.Primitives.Num(); ++Index)
			{
				Scene.GPUScene.PrimitivesToUpdate.Add(Index);
			}

			// Clear the full instance data range, except primitives that use a slot (they will unset the bits).
			Scene.GPUScene.InstanceDataToClear.Init(true, Scene.GPUScene.InstanceDataToClear.Num());

			// Set entire instance range for possible clearing.
			if (GGPUSceneInstanceClearList != 0)
			{
				for (int32 Index = 0; Index < Scene.GPUScene.InstanceDataToClear.Num(); ++Index)
				{
					Scene.GPUScene.InstanceClearList.Add(Index);
				}
			}

			Scene.GPUScene.bUpdateAllPrimitives = false;
		}

		bool bResizedPrimitiveData = false;
		bool bResizedInstanceData = false;
		bool bResizedLightmapData = false;

		ResourceType* MirrorResourceGPU = GetMirrorGPU<ResourceType>(Scene);
		{
			const uint32 SizeReserve = FMath::RoundUpToPowerOfTwo( FMath::Max( Scene.Primitives.Num(), 256 ) );
			bResizedPrimitiveData = ResizeResourceIfNeeded(RHICmdList, *MirrorResourceGPU, SizeReserve * sizeof(FPrimitiveSceneShaderData::Data), TEXT("PrimitiveData"));
		}

		const uint32 InstanceDataNumArrays = FInstanceSceneShaderData::InstanceDataStrideInFloat4s;
		const uint32 InstanceDataSizeReserve = FMath::RoundUpToPowerOfTwo(FMath::Max(Scene.GPUScene.InstanceDataAllocator.GetMaxSize(), 256));
		bResizedInstanceData = ResizeResourceSOAIfNeeded(RHICmdList, Scene.GPUScene.InstanceDataBuffer, InstanceDataSizeReserve * sizeof(FInstanceSceneShaderData::Data), InstanceDataNumArrays, TEXT("InstanceData"));
		Scene.GPUScene.InstanceDataSOAStride = InstanceDataSizeReserve;
		
		{
			const uint32 SizeReserve = FMath::RoundUpToPowerOfTwo( FMath::Max( Scene.GPUScene.LightmapDataAllocator.GetMaxSize(), 256 ) );
			bResizedLightmapData = ResizeResourceIfNeeded(RHICmdList, Scene.GPUScene.LightmapDataBuffer, SizeReserve * sizeof(FLightmapSceneShaderData::Data), TEXT("LightmapData"));
		}

		// These should always be in sync with each other.
		check(Scene.GPUScene.InstanceDataToClear.Num() == Scene.GPUScene.InstanceDataAllocator.GetMaxSize());

		const int32 NumPrimitiveDataUploads = Scene.GPUScene.PrimitivesToUpdate.Num();

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
			auto ProcessPrimitiveFn = [&Scene, &NumLightmapDataUploads, &NumInstanceDataUploads, bNaniteEnabled](int32 PrimitiveIndex, bool bThreaded)
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
							Scene.GPUScene.InstanceDataToClear.SetRange(TaskInstanceDataOffset, TaskInstanceUploadCount, false);
						}

						NumLightmapDataUploads += TaskLightmapUploadCount; // Not thread safe
						NumInstanceDataUploads += TaskInstanceUploadCount; // Not thread safe

						void* UploadDst = Scene.GPUScene.PrimitiveUploadBuffer.Add_GetRef(PrimitiveIndex); // Not thread safe

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

				Scene.GPUScene.PrimitiveUploadBuffer.Init(NumPrimitiveDataUploads, sizeof(FPrimitiveSceneShaderData::Data), true, TEXT("PrimitiveUploadBuffer"));

				int32 RangeCount = PartitionUpdateRanges(ParallelRanges, NumPrimitiveDataUploads, bExecuteInParallel);

				ParallelFor(RangeCount,
					[&ProcessPrimitiveFn, &ParallelRanges, &Scene, RangeCount](int32 RangeIndex)
					{
						for (int32 ItemIndex = ParallelRanges.Range[RangeIndex].ItemStart; ItemIndex < ParallelRanges.Range[RangeIndex].ItemStart + ParallelRanges.Range[RangeIndex].ItemCount; ++ItemIndex)
						{
							const int32 Index = Scene.GPUScene.PrimitivesToUpdate[ItemIndex];
							ProcessPrimitiveFn(Index, RangeCount > 1);
						}
					},
					RangeCount == 1
				);

				RHICmdList.Transition(FRHITransitionInfo(MirrorResourceGPU->UAV, CurrentAccess, ERHIAccess::UAVCompute));
				CurrentAccess = ERHIAccess::UAVCompute;

				Scene.GPUScene.PrimitiveUploadBuffer.ResourceUploadTo(RHICmdList, *MirrorResourceGPU, true);
			}
			else
			{
				// Break into multiple batches
				for (int32 PrimitiveOffset = 0; PrimitiveOffset < NumPrimitiveDataUploads; PrimitiveOffset += MaxPrimitivesUploads)
				{
					SCOPED_DRAW_EVENTF(RHICmdList, UpdateGPUScene, TEXT("UpdateGPUScene PrimitivesToUpdate and Offset = %u %u"), NumPrimitiveDataUploads, PrimitiveOffset);

					Scene.GPUScene.PrimitiveUploadBuffer.Init(MaxPrimitivesUploads, sizeof(FPrimitiveSceneShaderData::Data), true, TEXT("PrimitiveUploadBuffer"));

					for (int32 IndexUpdate = 0; (IndexUpdate < MaxPrimitivesUploads) && ((IndexUpdate + PrimitiveOffset) < NumPrimitiveDataUploads); ++IndexUpdate)
					{
						const int32 Index = Scene.GPUScene.PrimitivesToUpdate[IndexUpdate + PrimitiveOffset];
						ProcessPrimitiveFn(Index, false /* threaded */);
					}

					RHICmdList.Transition(FRHITransitionInfo(MirrorResourceGPU->UAV, CurrentAccess, ERHIAccess::UAVCompute));
					CurrentAccess = ERHIAccess::UAVCompute;

					{
						QUICK_SCOPE_CYCLE_COUNTER(UploadTo);
						Scene.GPUScene.PrimitiveUploadBuffer.ResourceUploadTo(RHICmdList, *MirrorResourceGPU, true);
					}
				}
			}

			RHICmdList.Transition(FRHITransitionInfo(MirrorResourceGPU->UAV, CurrentAccess, ERHIAccess::SRVMask));
		}

		Scene.GPUScene.PrimitivesMarkedToUpdate.Init(false, Scene.GPUScene.PrimitivesMarkedToUpdate.Num());

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
				for (int32 InstanceIndex = 0; InstanceIndex < Scene.GPUScene.InstanceDataAllocator.GetMaxSize(); ++InstanceIndex)
				{
					if (Scene.GPUScene.InstanceDataToClear[InstanceIndex])
					{
						check(Scene.GPUScene.InstanceClearList.Contains(InstanceIndex));
					}
				}
			}
		#endif

			for (uint32 InstanceIndex : Scene.GPUScene.InstanceClearList)
			{
				// Any clear bits set after enumerating the primitives being updated are
				// stale sections of the instance data buffer, so they should be reset to
				// an invalid state and skipped on the GPU.
				if (Scene.GPUScene.InstanceDataToClear[InstanceIndex])
				{
					Scene.GPUScene.InstanceDataToClear[InstanceIndex] = false;
					InstancesToClear.Add(InstanceIndex);
				}
			}

			Scene.GPUScene.InstanceClearList.Reset();
		}
		else
		{
			for (int32 InstanceIndex = 0; InstanceIndex < Scene.GPUScene.InstanceDataAllocator.GetMaxSize(); ++InstanceIndex)
			{
				// Any clear bits set after enumerating the primitives being updated are
				// stale sections of the instance data buffer, so they should be reset to
				// an invalid state and skipped on the GPU.
				if (Scene.GPUScene.InstanceDataToClear[InstanceIndex])
				{
					InstancesToClear.Add(InstanceIndex);
					Scene.GPUScene.InstanceDataToClear[InstanceIndex] = false;
				}
			}
		}

		// Clears count toward the total instance data uploads - batched together for efficiency.
		NumInstanceDataUploads += InstancesToClear.Num();

		if (GGPUSceneValidatePrimitiveBuffer && (Scene.GPUScene.PrimitiveBuffer.NumBytes > 0 || Scene.GPUScene.PrimitiveTexture.NumBytes > 0))
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
				Scene.GPUScene.InstanceUploadBuffer.Init(NumInstanceDataUploads * InstanceDataNumArrays, sizeof( FVector4 ), true, TEXT("InstanceUploadBuffer"));

				int32 RangeCount = PartitionUpdateRanges(ParallelRanges, InstancesToClear.Num(), bExecuteInParallel);

				// Reset any instance slots marked for clearing.
				ParallelFor(RangeCount,
					[&InstancesToClear, &ParallelRanges, &Scene, RangeCount, InstanceDataNumArrays, InstanceDataSizeReserve](int32 RangeIndex)
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
								DstRefs[RefIndex] = Scene.GPUScene.InstanceUploadBuffer.Add_GetRef(RefIndex * InstanceDataSizeReserve + Index);
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

				RangeCount = PartitionUpdateRanges(ParallelRanges, Scene.GPUScene.PrimitivesToUpdate.Num(), bExecuteInParallel);

				// Upload any out of data instance slots.
				ParallelFor(RangeCount,
					[&Scene, &ParallelRanges, RangeCount, InstanceDataNumArrays, InstanceDataSizeReserve, SceneFrameNumber](int32 RangeIndex)
					{
						for (int32 ItemIndex = ParallelRanges.Range[RangeIndex].ItemStart; ItemIndex < ParallelRanges.Range[RangeIndex].ItemStart + ParallelRanges.Range[RangeIndex].ItemCount; ++ItemIndex)
						{
							const int32 Index = Scene.GPUScene.PrimitivesToUpdate[ItemIndex];

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
										DstRefs[RefIndex] = Scene.GPUScene.InstanceUploadBuffer.Add_GetRef(RefIndex * InstanceDataSizeReserve + InstanceDataOffset + InstanceIndex);
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

				RHICmdList.Transition(FRHITransitionInfo(Scene.GPUScene.InstanceDataBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
				Scene.GPUScene.InstanceUploadBuffer.ResourceUploadTo(RHICmdList, Scene.GPUScene.InstanceDataBuffer, false);
				RHICmdList.Transition(FRHITransitionInfo(Scene.GPUScene.InstanceDataBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
			}
			else
			{
				RHICmdList.Transition(FRHITransitionInfo(Scene.GPUScene.InstanceDataBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVMask));
			}

			if (NumLightmapDataUploads > 0)
			{
				ERHIAccess CurrentAccess = ERHIAccess::Unknown;

				const int32 MaxLightmapsUploads = GetMaxPrimitivesUpdate(NumLightmapDataUploads, FLightmapSceneShaderData::LightmapDataStrideInFloat4s);
				for (int32 PrimitiveOffset = 0; PrimitiveOffset < NumPrimitiveDataUploads; PrimitiveOffset += MaxLightmapsUploads)
				{
					Scene.GPUScene.LightmapUploadBuffer.Init(MaxLightmapsUploads, sizeof(FLightmapSceneShaderData::Data), true, TEXT("LightmapUploadBuffer"));

					for (int32 IndexUpdate = 0; (IndexUpdate < MaxLightmapsUploads) && ((IndexUpdate + PrimitiveOffset) < NumPrimitiveDataUploads); ++IndexUpdate)
					{
						int32 Index = Scene.GPUScene.PrimitivesToUpdate[IndexUpdate + PrimitiveOffset];
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
								Scene.GPUScene.LightmapUploadBuffer.Add(LightmapDataOffset + i, &LightmapSceneData.Data[0]);
							}
						}
					}

					RHICmdList.Transition(FRHITransitionInfo(Scene.GPUScene.LightmapDataBuffer.UAV, CurrentAccess, ERHIAccess::UAVCompute));
					CurrentAccess = ERHIAccess::UAVCompute;

					Scene.GPUScene.LightmapUploadBuffer.ResourceUploadTo(RHICmdList, Scene.GPUScene.LightmapDataBuffer, false);
				}

				RHICmdList.Transition(FRHITransitionInfo(Scene.GPUScene.LightmapDataBuffer.UAV, CurrentAccess, ERHIAccess::SRVMask));
			}

			Scene.GPUScene.PrimitivesToUpdate.Reset();
			
			if (Scene.GPUScene.PrimitiveUploadBuffer.GetNumBytes() > (uint32)GGPUSceneMaxPooledUploadBufferSize)
			{
				Scene.GPUScene.PrimitiveUploadBuffer.Release();
			}

			if (Scene.GPUScene.InstanceUploadBuffer.GetNumBytes() > (uint32)GGPUSceneMaxPooledUploadBufferSize)
			{
				Scene.GPUScene.InstanceUploadBuffer.Release();
			}

			if (Scene.GPUScene.LightmapUploadBuffer.GetNumBytes() > (uint32)GGPUSceneMaxPooledUploadBufferSize)
			{
				Scene.GPUScene.LightmapUploadBuffer.Release();
			}
		}
		// Clear the flags that mark newly added primitives.
		Scene.GPUScene.AddedPrimitiveFlags.Init(false, Scene.GPUScene.AddedPrimitiveFlags.Num());
	}

	checkSlow(Scene.GPUScene.PrimitivesToUpdate.Num() == 0);
}

template void UpdateGPUSceneInternal<FRWBufferStructured>(FRHICommandListImmediate& RHICmdList, FScene& Scene);
template void UpdateGPUSceneInternal<FTextureRWBuffer2D>(FRHICommandListImmediate& RHICmdList, FScene& Scene);

template<typename ResourceType>
void UploadDynamicPrimitiveShaderDataForViewInternal(FRHICommandListImmediate& RHICmdList, FScene& Scene, FViewInfo& View)
{
	if (UseGPUScene(GMaxRHIShaderPlatform, Scene.GetFeatureLevel()))
	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(UploadDynamicPrimitiveShaderData);
		QUICK_SCOPE_CYCLE_COUNTER(STAT_UploadDynamicPrimitiveShaderData);

		const int32 NumPrimitiveDataUploads = View.DynamicPrimitiveShaderData.Num();
		if (NumPrimitiveDataUploads > 0)
		{
			ResourceType& ViewPrimitiveShaderDataResource = GetViewStateResourceRef<ResourceType>(View, View.ViewState == nullptr);

			const int32 NumPrimitiveEntries = Scene.Primitives.Num() + View.DynamicPrimitiveShaderData.Num();
			const uint32 PrimitiveSceneNumFloat4s = NumPrimitiveEntries * FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s;

			uint32 ViewPrimitiveSceneNumFloat4s = FMath::RoundUpToPowerOfTwo(PrimitiveSceneNumFloat4s);
			uint32 BytesPerElement = GPixelFormats[PF_A32B32G32R32F].BlockBytes;

			ERHIAccess CurrentAccess = ERHIAccess::Unknown;

			// Reserve enough space
			if (ViewPrimitiveSceneNumFloat4s * BytesPerElement != ViewPrimitiveShaderDataResource.NumBytes)
			{
				ViewPrimitiveShaderDataResource.Release();
				ResizeResourceIfNeeded(RHICmdList, ViewPrimitiveShaderDataResource, ViewPrimitiveSceneNumFloat4s * BytesPerElement, TEXT("ViewPrimitiveShaderDataBuffer"));
			}

			// Copy scene primitive data into view primitive data
			{
				RHICmdList.Transition(FRHITransitionInfo(ViewPrimitiveShaderDataResource.UAV, CurrentAccess, ERHIAccess::UAVCompute));
				MemcpyResource(RHICmdList, ViewPrimitiveShaderDataResource, *GetMirrorGPU<ResourceType>(Scene), Scene.Primitives.Num() * sizeof(FPrimitiveSceneShaderData::Data), 0, 0);
				RHICmdList.Transition(FRHITransitionInfo(ViewPrimitiveShaderDataResource.UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));
				CurrentAccess = ERHIAccess::UAVCompute;
			}

			// Append View.DynamicPrimitiveShaderData to the end of the view primitive data resource
			if (NumPrimitiveDataUploads > 0)
			{
				int32 MaxPrimitivesUploads = GetMaxPrimitivesUpdate(NumPrimitiveDataUploads, FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s);
				for (int32 PrimitiveOffset = 0; PrimitiveOffset < NumPrimitiveDataUploads; PrimitiveOffset += MaxPrimitivesUploads)
				{
					Scene.GPUScene.PrimitiveUploadViewBuffer.Init( MaxPrimitivesUploads, sizeof( FPrimitiveSceneShaderData::Data ), true, TEXT("PrimitiveUploadViewBuffer") );

					for (int32 IndexUpdate = 0; (IndexUpdate < MaxPrimitivesUploads) && ((IndexUpdate + PrimitiveOffset) < NumPrimitiveDataUploads); ++IndexUpdate)
					{
						int32 DynamicUploadIndex = IndexUpdate + PrimitiveOffset;
						FPrimitiveSceneShaderData PrimitiveSceneData(View.DynamicPrimitiveShaderData[DynamicUploadIndex]);
						// Place dynamic primitive shader data just after scene primitive data
						Scene.GPUScene.PrimitiveUploadViewBuffer.Add(Scene.Primitives.Num() + DynamicUploadIndex, &PrimitiveSceneData.Data[0]);
					}

					{
						RHICmdList.Transition(FRHITransitionInfo(ViewPrimitiveShaderDataResource.UAV, CurrentAccess, ERHIAccess::UAVCompute));
						CurrentAccess = ERHIAccess::UAVCompute;

						Scene.GPUScene.PrimitiveUploadViewBuffer.ResourceUploadTo(RHICmdList, ViewPrimitiveShaderDataResource, false);
					}
				}
			}

			if (Scene.GPUScene.PrimitiveUploadViewBuffer.GetNumBytes() > (uint32)GGPUSceneMaxPooledUploadBufferSize)
			{
				Scene.GPUScene.PrimitiveUploadViewBuffer.Release();
			}

			RHICmdList.Transition(FRHITransitionInfo(ViewPrimitiveShaderDataResource.UAV, CurrentAccess, ERHIAccess::SRVMask));

			if (GGPUSceneValidatePrimitiveBuffer && (Scene.GPUScene.PrimitiveBuffer.NumBytes > 0 || Scene.GPUScene.PrimitiveTexture.NumBytes > 0))
			{
				uint32 Stride = 0;
				FPrimitiveSceneShaderData* InstanceBufferCopy = (FPrimitiveSceneShaderData*)LockResource(ViewPrimitiveShaderDataResource, Stride);

				const int32 TotalNumberPrimitives = Scene.PrimitiveSceneProxies.Num() + View.DynamicPrimitiveShaderData.Num();
				int32 MaxPrimitivesUploads = GetMaxPrimitivesUpdate(TotalNumberPrimitives, FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s);
				for (int32 IndexOffset = 0; IndexOffset < TotalNumberPrimitives; IndexOffset += MaxPrimitivesUploads)
				{
					for (int32 Index = 0; (Index < MaxPrimitivesUploads) && ((Index + IndexOffset) < TotalNumberPrimitives); ++Index)
					{
						FPrimitiveSceneShaderData PrimitiveSceneData;
						if ((Index + IndexOffset) < Scene.PrimitiveSceneProxies.Num())
						{
							PrimitiveSceneData = (Scene.PrimitiveSceneProxies[Index + IndexOffset]);
						}
						else
						{
							PrimitiveSceneData = FPrimitiveSceneShaderData(View.DynamicPrimitiveShaderData[Index + IndexOffset - Scene.PrimitiveSceneProxies.Num()]);
						}

						check(FMemory::Memcmp(&InstanceBufferCopy[Index + IndexOffset], &PrimitiveSceneData, sizeof(FPrimitiveSceneShaderData)) == 0);
					}
					InstanceBufferCopy += Stride / sizeof(FPrimitiveSceneShaderData);
				}

				UnlockResourceGPUScene(ViewPrimitiveShaderDataResource);
			}

		}

		UpdateUniformResource<ResourceType>(View, Scene, NumPrimitiveDataUploads > 0);

		// Update view uniform buffer
		View.CachedViewUniformShaderParameters->InstanceSceneData = Scene.GPUScene.InstanceDataBuffer.SRV;
		View.CachedViewUniformShaderParameters->LightmapSceneData = Scene.GPUScene.LightmapDataBuffer.SRV;
		View.ViewUniformBuffer.UpdateUniformBufferImmediate(*View.CachedViewUniformShaderParameters);
	}
}

template void UploadDynamicPrimitiveShaderDataForViewInternal<FRWBufferStructured>(FRHICommandListImmediate& RHICmdList, FScene& Scene, FViewInfo& View);
template void UploadDynamicPrimitiveShaderDataForViewInternal<FTextureRWBuffer2D>(FRHICommandListImmediate& RHICmdList, FScene& Scene, FViewInfo& View);

void AddPrimitiveToUpdateGPU(FScene& Scene, int32 PrimitiveId)
{
	if (UseGPUScene(GMaxRHIShaderPlatform, Scene.GetFeatureLevel()))
	{ 
		if (PrimitiveId + 1 > Scene.GPUScene.PrimitivesMarkedToUpdate.Num())
		{
			const int32 NewSize = Align(PrimitiveId + 1, 64);
			Scene.GPUScene.PrimitivesMarkedToUpdate.Add(0, NewSize - Scene.GPUScene.PrimitivesMarkedToUpdate.Num());
		}

		// Make sure we aren't updating same primitive multiple times.
		if (!Scene.GPUScene.PrimitivesMarkedToUpdate[PrimitiveId])
		{
			Scene.GPUScene.PrimitivesToUpdate.Add(PrimitiveId);
			Scene.GPUScene.PrimitivesMarkedToUpdate[PrimitiveId] = true;
		}
	}
}
void UpdateGPUScene(FRDGBuilder& GraphBuilder, FScene& Scene)
{
	// Invoke the cache manager to invalidate the previous location of all instances that are to be updated, 
	// must be done prior to update of GPU-side data to use the previous transforms.
	if (Scene.VirtualShadowMapArrayCacheManager)
	{
		Scene.VirtualShadowMapArrayCacheManager->ProcessPrimitivesToUpdate(GraphBuilder, Scene);
	}
	if (GPUSceneUseTexture2D(Scene.GetShaderPlatform()))
	{
		UpdateGPUSceneInternal<FTextureRWBuffer2D>(GraphBuilder.RHICmdList, Scene);
	}
	else
	{
		UpdateGPUSceneInternal<FRWBufferStructured>(GraphBuilder.RHICmdList, Scene);
	}
}

void UploadDynamicPrimitiveShaderDataForView(FRHICommandListImmediate& RHICmdList, FScene& Scene, FViewInfo& View)
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



int32 FGPUScene::AllocateInstanceSlots(int32 NumInstanceDataEntries)
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

	return INDEX_NONE;
}


void FGPUScene::FreeInstanceSlots(int InstanceDataOffset, int32 NumInstanceDataEntries)
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


void FGPUScene::MarkPrimitiveAdded(int32 PrimitiveId)
{
	check(PrimitiveId >= 0);

	if (PrimitiveId >= AddedPrimitiveFlags.Num())
	{
		AddedPrimitiveFlags.Add(false, PrimitiveId + 1 - AddedPrimitiveFlags.Num());
	}
	AddedPrimitiveFlags[PrimitiveId] = true;
}
