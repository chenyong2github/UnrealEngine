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

// Allocate a range.  Returns allocated StartOffset.
int32 FGrowOnlySpanAllocator::Allocate(int32 Num)
{
	const int32 FoundIndex = SearchFreeList(Num);

	// Use an existing free span if one is found
	if (FoundIndex != INDEX_NONE)
	{
		FLinearAllocation FreeSpan = FreeSpans[FoundIndex];

		if (FreeSpan.Num > Num)
		{
			// Update existing free span with remainder
			FreeSpans[FoundIndex] = FLinearAllocation(FreeSpan.StartOffset + Num, FreeSpan.Num - Num);
		}
		else
		{
			// Fully consumed the free span
			FreeSpans.RemoveAtSwap(FoundIndex);
		}

		return FreeSpan.StartOffset;
	}

	// New allocation
	int32 StartOffset = MaxSize;
	MaxSize = MaxSize + Num;

	return StartOffset;
}

// Free an already allocated range.  
void FGrowOnlySpanAllocator::Free(int32 BaseOffset, int32 Num)
{
	check(BaseOffset + Num <= MaxSize);

	FLinearAllocation NewFreeSpan(BaseOffset, Num);

#if DO_CHECK
	// Detect double delete
	for (int32 i = 0; i < FreeSpans.Num(); i++)
	{
		FLinearAllocation CurrentSpan = FreeSpans[i];
		check(!(CurrentSpan.Contains(NewFreeSpan)));
	}
#endif

	bool bMergedIntoExisting = false;

	int32 SpanBeforeIndex = INDEX_NONE;
	int32 SpanAfterIndex = INDEX_NONE;

	// Search for existing free spans we can merge with
	for (int32 i = 0; i < FreeSpans.Num(); i++)
	{
		FLinearAllocation CurrentSpan = FreeSpans[i];

		if (CurrentSpan.StartOffset == NewFreeSpan.StartOffset + NewFreeSpan.Num)
		{
			SpanAfterIndex = i;
		}

		if (CurrentSpan.StartOffset + CurrentSpan.Num == NewFreeSpan.StartOffset)
		{
			SpanBeforeIndex = i;
		}
	}

	if (SpanBeforeIndex != INDEX_NONE)
	{
		// Merge span before with new free span
		FLinearAllocation& SpanBefore = FreeSpans[SpanBeforeIndex];
		SpanBefore.Num += NewFreeSpan.Num;

		if (SpanAfterIndex != INDEX_NONE)
		{
			// Also merge span after with span before
			FLinearAllocation SpanAfter = FreeSpans[SpanAfterIndex];
			SpanBefore.Num += SpanAfter.Num;
			FreeSpans.RemoveAtSwap(SpanAfterIndex);
		}
	}
	else if (SpanAfterIndex != INDEX_NONE)
	{
		// Merge span after with new free span
		FLinearAllocation& SpanAfter = FreeSpans[SpanAfterIndex];
		SpanAfter.StartOffset = NewFreeSpan.StartOffset;
		SpanAfter.Num += NewFreeSpan.Num;
	}
	else
	{
		// Couldn't merge, store new free span
		FreeSpans.Add(NewFreeSpan);
	}
}

int32 FGrowOnlySpanAllocator::SearchFreeList(int32 Num)
{
	// Search free list for first matching
	for (int32 i = 0; i < FreeSpans.Num(); i++)
	{
		FLinearAllocation CurrentSpan = FreeSpans[i];

		if (CurrentSpan.Num >= Num)
		{
			return i;
		}
	}

	return INDEX_NONE;
}

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

template<typename ResourceType>
void UpdateGPUSceneInternal(FRHICommandListImmediate& RHICmdList, FScene& Scene)
{
	if (UseGPUScene(GMaxRHIShaderPlatform, Scene.GetFeatureLevel()))
	{
		SCOPED_NAMED_EVENT( STAT_UpdateGPUScene, FColor::Green );
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(UpdateGPUScene);
		QUICK_SCOPE_CYCLE_COUNTER(STAT_UpdateGPUScene);

		// Multi-GPU support : Updating on all GPUs is inefficient for AFR. Work is wasted
		// for any primitives that update on consecutive frames.
		SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::All());

		if (GGPUSceneUploadEveryFrame || Scene.GPUScene.bUpdateAllPrimitives)
		{
			for (int32 Index : Scene.GPUScene.PrimitivesToUpdate)
			{
				Scene.GPUScene.PrimitivesMarkedToUpdate[Index] = false;
			}
			Scene.GPUScene.PrimitivesToUpdate.Reset();

			for (int32 i = 0; i < Scene.Primitives.Num(); i++)
			{
				Scene.GPUScene.PrimitivesToUpdate.Add(i);
			}

			Scene.GPUScene.bUpdateAllPrimitives = false;
		}

		bool bResizedPrimitiveData = false;
		bool bResizedLightmapData = false;

		ResourceType* MirrorResourceGPU = GetMirrorGPU<ResourceType>(Scene);
		{
			const uint32 SizeReserve = FMath::RoundUpToPowerOfTwo( FMath::Max( Scene.Primitives.Num(), 256 ) );
			bResizedPrimitiveData = ResizeResourceIfNeeded(RHICmdList, *MirrorResourceGPU, SizeReserve * sizeof(FPrimitiveSceneShaderData::Data), TEXT("PrimitiveData"));
		}

		{
			const uint32 SizeReserve = FMath::RoundUpToPowerOfTwo( FMath::Max( Scene.GPUScene.LightmapDataAllocator.GetMaxSize(), 256 ) );
			bResizedLightmapData = ResizeResourceIfNeeded(RHICmdList, Scene.GPUScene.LightmapDataBuffer, SizeReserve * sizeof(FLightmapSceneShaderData::Data), TEXT("LightmapData"));
		}

		const int32 NumPrimitiveDataUploads = Scene.GPUScene.PrimitivesToUpdate.Num();

		int32 NumLightmapDataUploads = 0;

		if (NumPrimitiveDataUploads > 0)
		{
			const int32 MaxPrimitivesUploads = GetMaxPrimitivesUpdate(NumPrimitiveDataUploads, FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s);
			for (int32 PrimitiveOffset = 0; PrimitiveOffset < NumPrimitiveDataUploads; PrimitiveOffset += MaxPrimitivesUploads)
			{
				SCOPED_DRAW_EVENTF(RHICmdList, UpdateGPUScene, TEXT("UpdateGPUScene PrimitivesToUpdate and Offset = %u %u"), NumPrimitiveDataUploads, PrimitiveOffset);


				Scene.GPUScene.PrimitiveUploadBuffer.Init(MaxPrimitivesUploads, sizeof(FPrimitiveSceneShaderData::Data), true, TEXT("PrimitiveUploadBuffer"));

				for (int32 IndexUpdate = 0; (IndexUpdate < MaxPrimitivesUploads) && ((IndexUpdate + PrimitiveOffset) < NumPrimitiveDataUploads); ++IndexUpdate)
				{
					int32 Index = Scene.GPUScene.PrimitivesToUpdate[IndexUpdate + PrimitiveOffset];
					// PrimitivesToUpdate may contain a stale out of bounds index, as we don't remove update request on primitive removal from scene.
					if (Index < Scene.PrimitiveSceneProxies.Num())
					{
						FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy = Scene.PrimitiveSceneProxies[Index];
						NumLightmapDataUploads += PrimitiveSceneProxy->GetPrimitiveSceneInfo()->GetNumLightmapDataEntries();

						FPrimitiveSceneShaderData PrimitiveSceneData(PrimitiveSceneProxy);
						Scene.GPUScene.PrimitiveUploadBuffer.Add(Index, &PrimitiveSceneData.Data[0]);
					}
					Scene.GPUScene.PrimitivesMarkedToUpdate[Index] = false;
				}

				if (bResizedPrimitiveData)
				{
					RHICmdList.Transition(FRHITransitionInfo(MirrorResourceGPU->UAV, ERHIAccess::Unknown, ERHIAccess::ERWBarrier));
				}
				else
				{
					RHICmdList.Transition(FRHITransitionInfo(MirrorResourceGPU->UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
				}

				{
					Scene.GPUScene.PrimitiveUploadBuffer.ResourceUploadTo(RHICmdList, *MirrorResourceGPU, true);
				}
			}

			RHICmdList.Transition(FRHITransitionInfo(MirrorResourceGPU->UAV, ERHIAccess::Unknown, ERHIAccess::SRVMask));
		}


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

					for (int i = 0; i < FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s; i++)
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

			if (NumLightmapDataUploads > 0)
			{
				Scene.GPUScene.LightmapUploadBuffer.Init(NumLightmapDataUploads, sizeof(FLightmapSceneShaderData::Data), true, TEXT("LightmapUploadBuffer"));

				for (int32 Index : Scene.GPUScene.PrimitivesToUpdate)
				{
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

				if (bResizedLightmapData)
				{
					RHICmdList.Transition(FRHITransitionInfo(Scene.GPUScene.LightmapDataBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::ERWBarrier));
				}
				else
				{
					RHICmdList.Transition(FRHITransitionInfo(Scene.GPUScene.LightmapDataBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
				}

				Scene.GPUScene.LightmapUploadBuffer.ResourceUploadTo(RHICmdList, Scene.GPUScene.LightmapDataBuffer, false);

				RHICmdList.Transition(FRHITransitionInfo(Scene.GPUScene.LightmapDataBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::SRVMask));
			}

			Scene.GPUScene.PrimitivesToUpdate.Reset();
			
			if (Scene.GPUScene.PrimitiveUploadBuffer.GetNumBytes() > (uint32)GGPUSceneMaxPooledUploadBufferSize)
			{
				Scene.GPUScene.PrimitiveUploadBuffer.Release();
			}

			if (Scene.GPUScene.LightmapUploadBuffer.GetNumBytes() > (uint32)GGPUSceneMaxPooledUploadBufferSize)
			{
				Scene.GPUScene.LightmapUploadBuffer.Release();
			}
		}
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

			// Reserve enough space
			if (ViewPrimitiveSceneNumFloat4s * BytesPerElement != ViewPrimitiveShaderDataResource.NumBytes)
			{
				ViewPrimitiveShaderDataResource.Release();
				ResizeResourceIfNeeded(RHICmdList, ViewPrimitiveShaderDataResource, ViewPrimitiveSceneNumFloat4s * BytesPerElement, TEXT("ViewPrimitiveShaderDataBuffer"));
			}

			// Copy scene primitive data into view primitive data
			{
				RHICmdList.Transition(FRHITransitionInfo(ViewPrimitiveShaderDataResource.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
				MemcpyResource(RHICmdList, ViewPrimitiveShaderDataResource, *GetMirrorGPU<ResourceType>(Scene), Scene.Primitives.Num() * sizeof(FPrimitiveSceneShaderData::Data), 0, 0);
				RHICmdList.Transition(FRHITransitionInfo(ViewPrimitiveShaderDataResource.UAV, ERHIAccess::UAVCompute, ERHIAccess::ERWBarrier));
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
						RHICmdList.Transition(FRHITransitionInfo(ViewPrimitiveShaderDataResource.UAV, ERHIAccess::ERWBarrier, ERHIAccess::ERWBarrier));
						Scene.GPUScene.PrimitiveUploadViewBuffer.ResourceUploadTo(RHICmdList, ViewPrimitiveShaderDataResource, false);
					}
				}
			}

			if (Scene.GPUScene.PrimitiveUploadViewBuffer.GetNumBytes() > (uint32)GGPUSceneMaxPooledUploadBufferSize)
			{
				Scene.GPUScene.PrimitiveUploadViewBuffer.Release();
			}

			RHICmdList.Transition(FRHITransitionInfo(ViewPrimitiveShaderDataResource.UAV, ERHIAccess::ERWBarrier, ERHIAccess::SRVMask));

			
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
void UpdateGPUScene(FRHICommandListImmediate& RHICmdList, FScene& Scene)
{
	if (GPUSceneUseTexture2D(Scene.GetShaderPlatform()))
	{
		UpdateGPUSceneInternal<FTextureRWBuffer2D>(RHICmdList, Scene);
	}
	else
	{
		UpdateGPUSceneInternal<FRWBufferStructured>(RHICmdList, Scene);
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