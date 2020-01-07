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

static void UpdateGPUSceneBuffer(FRHICommandListImmediate& RHICmdList, FScene& Scene)
{
	if (UseGPUScene(GMaxRHIShaderPlatform, Scene.GetFeatureLevel()))
	{
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
		{
			const int32 NumPrimitiveEntries = Scene.Primitives.Num();
			const uint32 PrimitiveSceneNumFloat4s = NumPrimitiveEntries * FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s;
			// Reserve enough space
			bResizedPrimitiveData = ResizeBufferFloat4(RHICmdList, Scene.GPUScene.PrimitiveBuffer, FMath::RoundUpToPowerOfTwo(PrimitiveSceneNumFloat4s) * sizeof(FVector4), TEXT("PrimitiveData"));
		}
		
		{
			const int32 NumLightmapDataEntries = Scene.GPUScene.LightmapDataAllocator.GetMaxSize();
			const uint32 LightmapDataNumFloat4s = NumLightmapDataEntries * FLightmapSceneShaderData::LightmapDataStrideInFloat4s;
			bResizedLightmapData = ResizeBufferFloat4(RHICmdList, Scene.GPUScene.LightmapDataBuffer, FMath::RoundUpToPowerOfTwo(LightmapDataNumFloat4s) * sizeof(FVector4), TEXT("LightmapData"));
		}

		const int32 NumPrimitiveDataUploads = Scene.GPUScene.PrimitivesToUpdate.Num();

		int32 NumLightmapDataUploads = 0;
		if (NumPrimitiveDataUploads > 0)
		{
			SCOPED_DRAW_EVENTF(RHICmdList, UpdateGPUScene, TEXT("UpdateGPUScene PrimitivesToUpdate = %u"), NumPrimitiveDataUploads);

			Scene.GPUScene.PrimitiveUploadBuffer.Init( NumPrimitiveDataUploads, sizeof( FPrimitiveSceneShaderData::Data ), true, TEXT("PrimitiveUploadBuffer"));

			for (int32 Index : Scene.GPUScene.PrimitivesToUpdate)
			{
				// PrimitivesToUpdate may contain a stale out of bounds index, as we don't remove update request on primitive removal from scene.
				if (Index < Scene.PrimitiveSceneProxies.Num())
				{
					FPrimitiveSceneProxy* PrimitiveSceneProxy = Scene.PrimitiveSceneProxies[Index];
					NumLightmapDataUploads += PrimitiveSceneProxy->GetPrimitiveSceneInfo()->GetNumLightmapDataEntries();

					FPrimitiveSceneShaderData PrimitiveSceneData(PrimitiveSceneProxy);
					Scene.GPUScene.PrimitiveUploadBuffer.Add(Index, &PrimitiveSceneData.Data[0]);
				}

				Scene.GPUScene.PrimitivesMarkedToUpdate[Index] = false;
			}

			if (bResizedPrimitiveData)
			{
				RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, Scene.GPUScene.PrimitiveBuffer.UAV);
			}
			else
			{
				RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EGfxToCompute, Scene.GPUScene.PrimitiveBuffer.UAV);
			}

			Scene.GPUScene.PrimitiveUploadBuffer.UploadToBuffer(RHICmdList, Scene.GPUScene.PrimitiveBuffer.UAV, true);

			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, Scene.GPUScene.PrimitiveBuffer.UAV);
		}

		if (GGPUSceneValidatePrimitiveBuffer && Scene.GPUScene.PrimitiveBuffer.NumBytes > 0)
		{
			//UE_LOG(LogRenderer, Warning, TEXT("r.GPUSceneValidatePrimitiveBuffer enabled, doing slow readback from GPU"));
			FPrimitiveSceneShaderData* InstanceBufferCopy = (FPrimitiveSceneShaderData*)RHILockStructuredBuffer(Scene.GPUScene.PrimitiveBuffer.Buffer, 0, Scene.GPUScene.PrimitiveBuffer.NumBytes, RLM_ReadOnly);

			for (int32 Index = 0; Index < Scene.PrimitiveSceneProxies.Num(); Index++)
			{
				FPrimitiveSceneShaderData PrimitiveSceneData(Scene.PrimitiveSceneProxies[Index]);
				for (int i = 0; i < FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s; i++)
				{
					check(PrimitiveSceneData.Data[i] == InstanceBufferCopy[Index].Data[i]);
				}
			}

			RHIUnlockStructuredBuffer(Scene.GPUScene.PrimitiveBuffer.Buffer);
		}

		if (NumPrimitiveDataUploads > 0)
		{
			if (NumLightmapDataUploads > 0)
			{
				Scene.GPUScene.LightmapUploadBuffer.Init( NumLightmapDataUploads, sizeof( FLightmapSceneShaderData::Data ), true, TEXT("LightmapUploadBuffer"));

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
					RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, Scene.GPUScene.LightmapDataBuffer.UAV);
				}
				else
				{
					RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EGfxToCompute, Scene.GPUScene.LightmapDataBuffer.UAV);
				}

				Scene.GPUScene.LightmapUploadBuffer.UploadToBuffer(RHICmdList, Scene.GPUScene.LightmapDataBuffer.UAV,false);

				RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, Scene.GPUScene.LightmapDataBuffer.UAV);
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

static int32 GetMaxPrimitivesUpdate( uint32 NumUploads, uint32 InStrideInFloat4s )
{
	return GMaxTextureBufferSize == 0 ? NumUploads : FMath::Min( (uint32)( GMaxTextureBufferSize / InStrideInFloat4s ), NumUploads );
}

static void UpdateGPUSceneTexture(FRHICommandListImmediate& RHICmdList, FScene& Scene)
{
	if (UseGPUScene(GMaxRHIShaderPlatform, Scene.GetFeatureLevel()))
	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(UpdateGPUSceneTexture);
		QUICK_SCOPE_CYCLE_COUNTER(STAT_UpdateGPUSceneTexture);

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

		bool bResizedLightmapData = false;
		bool bResizedPrimitiveTextureData = false;

		{
			const int32 NumPrimitiveEntries = Scene.Primitives.Num();
			const uint32 PrimitiveSceneNumFloat4s = NumPrimitiveEntries * FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s;
			// Reserve enough space
			bResizedPrimitiveTextureData = ResizeTexture(RHICmdList, Scene.GPUScene.PrimitiveTexture, PrimitiveSceneNumFloat4s * sizeof(FVector4), sizeof(FPrimitiveSceneShaderData::Data) * FPrimitiveSceneShaderData::GetPrimitivesPerTextureLine() );
		}

		{
			const int32 NumLightmapDataEntries = Scene.GPUScene.LightmapDataAllocator.GetMaxSize();
			const uint32 LightmapDataNumFloat4s = NumLightmapDataEntries * FLightmapSceneShaderData::LightmapDataStrideInFloat4s;
			bResizedLightmapData = ResizeBufferFloat4(RHICmdList, Scene.GPUScene.LightmapDataBuffer, FMath::RoundUpToPowerOfTwo(LightmapDataNumFloat4s) * sizeof(FVector4), TEXT("LightmapData"));
		}

		const int32 NumPrimitiveDataUploads = Scene.GPUScene.PrimitivesToUpdate.Num();

		int32 NumLightmapDataUploads = 0;

		if (NumPrimitiveDataUploads > 0)
		{
			const int32 MaxPrimitivesUploads = GetMaxPrimitivesUpdate(NumPrimitiveDataUploads, FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s);
			for (int32 PrimitiveOffset = 0; PrimitiveOffset < NumPrimitiveDataUploads; PrimitiveOffset += MaxPrimitivesUploads)
			{
				SCOPED_DRAW_EVENTF(RHICmdList, UpdateGPUScene, TEXT("UpdateGPUScene PrimitivesToUpdate and Offset = %u %u"), NumPrimitiveDataUploads, PrimitiveOffset);

				Scene.GPUScene.PrimitiveUploadBuffer.Init( MaxPrimitivesUploads, sizeof( FPrimitiveSceneShaderData::Data ), true, TEXT("PrimitiveUploadBuffer") );

				for (int32 IndexUpdate = 0; (IndexUpdate < MaxPrimitivesUploads) && ((IndexUpdate + PrimitiveOffset) < NumPrimitiveDataUploads); ++IndexUpdate)
				{
					int32 Index = Scene.GPUScene.PrimitivesToUpdate[IndexUpdate + PrimitiveOffset];
					// PrimitivesToUpdate may contain a stale out of bounds index, as we don't remove update request on primitive removal from scene.
					if (Index < Scene.PrimitiveSceneProxies.Num())
					{
						FPrimitiveSceneProxy* PrimitiveSceneProxy = Scene.PrimitiveSceneProxies[Index];
						NumLightmapDataUploads += PrimitiveSceneProxy->GetPrimitiveSceneInfo()->GetNumLightmapDataEntries();

						FPrimitiveSceneShaderData PrimitiveSceneData(PrimitiveSceneProxy);
						Scene.GPUScene.PrimitiveUploadBuffer.Add(Index, &PrimitiveSceneData.Data[0]);
					}
					Scene.GPUScene.PrimitivesMarkedToUpdate[Index] = false;
				}

				{
					if (bResizedPrimitiveTextureData)
					{
						RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, Scene.GPUScene.PrimitiveTexture.UAV);
					}
					else
					{
						RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EGfxToCompute, Scene.GPUScene.PrimitiveTexture.UAV);
					}
				}

				{
					uint16 PrimitivesPerTextureLine = FPrimitiveSceneShaderData::GetPrimitivesPerTextureLine();
					Scene.GPUScene.PrimitiveUploadBuffer.UploadToTexture(RHICmdList, Scene.GPUScene.PrimitiveTexture, PrimitivesPerTextureLine * sizeof( FPrimitiveSceneShaderData::Data ), true);
				}
			}
			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, Scene.GPUScene.PrimitiveTexture.UAV);
		}

		
		if (NumPrimitiveDataUploads > 0)
		{
			if (NumLightmapDataUploads > 0)
			{
				const int32 MaxLightmapsUploads = GetMaxPrimitivesUpdate(NumLightmapDataUploads, FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s);
				for (int32 PrimitiveOffset = 0; PrimitiveOffset < NumPrimitiveDataUploads; PrimitiveOffset += MaxLightmapsUploads)
				{
					Scene.GPUScene.LightmapUploadBuffer.Init( NumLightmapDataUploads, sizeof( FLightmapSceneShaderData::Data ), true, TEXT("LightmapUploadBuffer") );

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

					if (bResizedLightmapData)
					{
						RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, Scene.GPUScene.LightmapDataBuffer.UAV);
					}
					else
					{
						RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EGfxToCompute, Scene.GPUScene.LightmapDataBuffer.UAV);
					}

					Scene.GPUScene.LightmapUploadBuffer.UploadToBuffer(RHICmdList, Scene.GPUScene.LightmapDataBuffer.UAV, false);
	
				}
				RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, Scene.GPUScene.LightmapDataBuffer.UAV);
			}

			Scene.GPUScene.PrimitivesToUpdate.Reset();
			
			if( Scene.GPUScene.PrimitiveUploadBuffer.GetNumBytes() > (uint32)GGPUSceneMaxPooledUploadBufferSize )
			{
				Scene.GPUScene.PrimitiveUploadBuffer.Release();
			}

			if( Scene.GPUScene.LightmapUploadBuffer.GetNumBytes() > (uint32)GGPUSceneMaxPooledUploadBufferSize )
			{
				Scene.GPUScene.LightmapUploadBuffer.Release();
			}
		}
	}

	checkSlow(Scene.GPUScene.PrimitivesToUpdate.Num() == 0);
}

void UpdateGPUScene(FRHICommandListImmediate& RHICmdList, FScene& Scene)
{
	if (!GPUSceneUseTexture2D(Scene.GetShaderPlatform()))
	{
		UpdateGPUSceneBuffer(RHICmdList, Scene);
	}
	else
	{
		UpdateGPUSceneTexture(RHICmdList, Scene);
	}
}

static void UploadDynamicPrimitiveShaderDataBufferForView(FRHICommandListImmediate& RHICmdList, FScene& Scene, FViewInfo& View)
{
	if (UseGPUScene(GMaxRHIShaderPlatform, Scene.GetFeatureLevel()))
	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(UploadDynamicPrimitiveShaderDataForView);
		QUICK_SCOPE_CYCLE_COUNTER(STAT_UploadDynamicPrimitiveShaderDataForView);

		const int32 NumPrimitiveDataUploads = View.DynamicPrimitiveShaderData.Num();
		if (NumPrimitiveDataUploads > 0)
		{
			FRWBufferStructured& ViewPrimitiveShaderDataBuffer = View.ViewState ? View.ViewState->PrimitiveShaderDataBuffer : View.OneFramePrimitiveShaderDataBuffer;

			const int32 NumPrimitiveEntries = Scene.Primitives.Num() + View.DynamicPrimitiveShaderData.Num();
			const uint32 PrimitiveSceneNumFloat4s = NumPrimitiveEntries * FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s;

			uint32 ViewPrimitiveSceneNumFloat4s = FMath::RoundUpToPowerOfTwo(PrimitiveSceneNumFloat4s);
			uint32 BytesPerElement = GPixelFormats[PF_A32B32G32R32F].BlockBytes;

			// Reserve enough space
			if (ViewPrimitiveSceneNumFloat4s * BytesPerElement != ViewPrimitiveShaderDataBuffer.NumBytes)
			{
				ViewPrimitiveShaderDataBuffer.Release();
				ViewPrimitiveShaderDataBuffer.Initialize(BytesPerElement, ViewPrimitiveSceneNumFloat4s, 0, TEXT("ViewPrimitiveShaderDataBuffer"));
			}

			// Copy scene primitive data into view primitive data
			RHICmdList.TransitionResource( EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EGfxToCompute, ViewPrimitiveShaderDataBuffer.UAV );
			MemcpyBufferFloat4(RHICmdList, ViewPrimitiveShaderDataBuffer, Scene.GPUScene.PrimitiveBuffer, Scene.Primitives.Num() * sizeof(FPrimitiveSceneShaderData::Data));
			RHICmdList.TransitionResource( EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, ViewPrimitiveShaderDataBuffer.UAV );

			// Append View.DynamicPrimitiveShaderData to the end of the view primitive data buffer
			if (NumPrimitiveDataUploads > 0)
			{
				Scene.GPUScene.PrimitiveUploadBuffer.Init( NumPrimitiveDataUploads, sizeof( FPrimitiveSceneShaderData::Data ), true, TEXT("PrimitiveUploadBuffer") );

				for (int32 DynamicUploadIndex = 0; DynamicUploadIndex < View.DynamicPrimitiveShaderData.Num(); DynamicUploadIndex++)
				{
					FPrimitiveSceneShaderData PrimitiveSceneData(View.DynamicPrimitiveShaderData[DynamicUploadIndex]);
					// Place dynamic primitive shader data just after scene primitive data
					Scene.GPUScene.PrimitiveUploadBuffer.Add(Scene.Primitives.Num() + DynamicUploadIndex, &PrimitiveSceneData.Data[0]);
				}

				RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, ViewPrimitiveShaderDataBuffer.UAV);

				Scene.GPUScene.PrimitiveUploadBuffer.UploadToBuffer(RHICmdList, ViewPrimitiveShaderDataBuffer.UAV, false);
			}

			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, ViewPrimitiveShaderDataBuffer.UAV);

			View.CachedViewUniformShaderParameters->PrimitiveSceneData = ViewPrimitiveShaderDataBuffer.SRV;
		}
		else
		{
			// No dynamic primitives for this view, we just use Scene.GPUScene.PrimitiveBuffer.
			View.CachedViewUniformShaderParameters->PrimitiveSceneData = Scene.GPUScene.PrimitiveBuffer.SRV;
		}

		// Update view uniform buffer
		View.CachedViewUniformShaderParameters->LightmapSceneData = Scene.GPUScene.LightmapDataBuffer.SRV;
		View.ViewUniformBuffer.UpdateUniformBufferImmediate(*View.CachedViewUniformShaderParameters);
	}
}

static void UploadDynamicPrimitiveShaderDataTextureForView(FRHICommandListImmediate& RHICmdList, FScene& Scene, FViewInfo& View)
{
	uint16 PrimitivesPerTextureLine = FPrimitiveSceneShaderData::GetPrimitivesPerTextureLine();

	if (UseGPUScene(GMaxRHIShaderPlatform, Scene.GetFeatureLevel()))
	{
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(UploadDynamicPrimitiveShaderDataTextureForView);
		QUICK_SCOPE_CYCLE_COUNTER(STAT_UploadDynamicPrimitiveShaderDataTextureForView);

		const int32 NumPrimitiveDataUploads = View.DynamicPrimitiveShaderData.Num();
		if (NumPrimitiveDataUploads > 0)
		{
			FTextureRWBuffer2D& ViewPrimitiveShaderDataTexture = View.ViewState ? View.ViewState->PrimitiveShaderDataTexture : View.OneFramePrimitiveShaderDataTexture;

			const int32 NumPrimitiveEntries = Scene.Primitives.Num() + View.DynamicPrimitiveShaderData.Num();
			const uint32 PrimitiveSceneNumFloat4s = NumPrimitiveEntries * FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s;

			uint32 ViewPrimitiveSceneNumFloat4s = FMath::RoundUpToPowerOfTwo(PrimitiveSceneNumFloat4s);
			uint32 BytesPerElement = GPixelFormats[PF_A32B32G32R32F].BlockBytes;

			// Reserve enough space
			if (ViewPrimitiveSceneNumFloat4s * BytesPerElement != ViewPrimitiveShaderDataTexture.NumBytes)
			{
				ViewPrimitiveShaderDataTexture.Release();
				ViewPrimitiveShaderDataTexture.Initialize(BytesPerElement, FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s * PrimitivesPerTextureLine, NumPrimitiveEntries / PrimitivesPerTextureLine + 1, PF_A32B32G32R32F, TexCreate_RenderTargetable | TexCreate_UAV);
			}

			// Copy scene primitive data into view primitive data
			{
				MemcpyTextureToTexture(RHICmdList, Scene.GPUScene.PrimitiveTexture, ViewPrimitiveShaderDataTexture, 0, 0, Scene.Primitives.Num() * sizeof(FPrimitiveSceneShaderData::Data), PrimitivesPerTextureLine * sizeof(FPrimitiveSceneShaderData::Data));
			}

			// Append View.DynamicPrimitiveShaderData to the end of the view primitive data texture
			if (NumPrimitiveDataUploads > 0)
			{
				int32 MaxPrimitivesUploads = GetMaxPrimitivesUpdate(NumPrimitiveDataUploads, FPrimitiveSceneShaderData::PrimitiveDataStrideInFloat4s);
				for (int32 PrimitiveOffset = 0; PrimitiveOffset < NumPrimitiveDataUploads; PrimitiveOffset += MaxPrimitivesUploads)
				{
					Scene.GPUScene.PrimitiveUploadBuffer.Init( MaxPrimitivesUploads, sizeof( FPrimitiveSceneShaderData::Data ), true, TEXT("PrimitiveUploadBuffer") );

					for (int32 IndexUpdate = 0; (IndexUpdate < MaxPrimitivesUploads) && ((IndexUpdate + PrimitiveOffset) < NumPrimitiveDataUploads); ++IndexUpdate)
					{
						int32 DynamicUploadIndex = IndexUpdate + PrimitiveOffset;
						FPrimitiveSceneShaderData PrimitiveSceneData(View.DynamicPrimitiveShaderData[DynamicUploadIndex]);
						// Place dynamic primitive shader data just after scene primitive data
						Scene.GPUScene.PrimitiveUploadBuffer.Add(Scene.Primitives.Num() + DynamicUploadIndex, &PrimitiveSceneData.Data[0]);
					}

					{
						RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, ViewPrimitiveShaderDataTexture.UAV);
						Scene.GPUScene.PrimitiveUploadBuffer.UploadToTexture(RHICmdList, ViewPrimitiveShaderDataTexture, PrimitivesPerTextureLine * sizeof( FPrimitiveSceneShaderData::Data ), false);
					}
				}
			}

			{
				RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, ViewPrimitiveShaderDataTexture.UAV);
				View.CachedViewUniformShaderParameters->PrimitiveSceneDataTexture = ViewPrimitiveShaderDataTexture.Buffer;
			}

		}
		else
		{
			// No dynamic primitives for this view, we just use Scene.GPUScene.PrimitiveTexture.
			{
				View.CachedViewUniformShaderParameters->PrimitiveSceneDataTexture = Scene.GPUScene.PrimitiveTexture.Buffer;
			}

		}

		// Update view uniform buffer
		View.CachedViewUniformShaderParameters->LightmapSceneData = Scene.GPUScene.LightmapDataBuffer.SRV;
		View.ViewUniformBuffer.UpdateUniformBufferImmediate(*View.CachedViewUniformShaderParameters);
	}
}

void UploadDynamicPrimitiveShaderDataForView(FRHICommandListImmediate& RHICmdList, FScene& Scene, FViewInfo& View)
{
	if (!GPUSceneUseTexture2D(Scene.GetShaderPlatform()))
	{
		UploadDynamicPrimitiveShaderDataBufferForView(RHICmdList, Scene, View);
	}
	else
	{
		UploadDynamicPrimitiveShaderDataTextureForView(RHICmdList, Scene, View);
	}
}

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