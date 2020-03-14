// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineSurfaceReader.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "RHIStaticStates.h"
#include "CommonRenderResources.h"
#include "RenderTargetPool.h"
#include "ScreenRendering.h"
#include "GlobalShader.h"
#include "Shader.h"
#include "StaticBoundShaderState.h"
#include "Modules/ModuleManager.h"
#include "MovieRenderPipelineCoreModule.h"

FMoviePipelineSurfaceReader::FMoviePipelineSurfaceReader(EPixelFormat InPixelFormat, FIntPoint InSurfaceSize)
{
	AvailableEvent = nullptr;
	ReadbackTexture = nullptr;
	PixelFormat = InPixelFormat;
	bQueuedForCapture = false;
	
	Resize(InSurfaceSize.X, InSurfaceSize.Y);
}

FMoviePipelineSurfaceReader::~FMoviePipelineSurfaceReader()
{
	BlockUntilAvailable();

	ReadbackTexture.SafeRelease();
}

void FMoviePipelineSurfaceReader::Initialize()
{
	// Initialize shouldn't be called if we're currently not available.
	check(!AvailableEvent);
	AvailableEvent = FPlatformProcess::GetSynchEventFromPool();
}

void FMoviePipelineSurfaceReader::Resize(uint32 Width, uint32 Height)
{
	ReadbackTexture.SafeRelease();

	// TexCreate_CPUReadback is important to make this texture available for reading later.
	FMoviePipelineSurfaceReader* This = this;
	ENQUEUE_RENDER_COMMAND(CreateCaptureFrameTexture)(
		[Width, Height, This](FRHICommandListImmediate& RHICmdList)
		{
			FRHIResourceCreateInfo CreateInfo;

			This->ReadbackTexture = RHICreateTexture2D(
				Width,
				Height,
				This->PixelFormat,
				1,
				1,
				TexCreate_CPUReadback,
				CreateInfo
				);
		});
}

void FMoviePipelineSurfaceReader::BlockUntilAvailable()
{
	if (AvailableEvent)
	{
		// Make this thread wait until another thread (ie: Rendering Thread) calls Trigger on the event.
		AvailableEvent->Wait(~0);

		// Return the sync event until we call Initialize again.
		FPlatformProcess::ReturnSynchEventToPool(AvailableEvent);
		AvailableEvent = nullptr;
	}
}

void FMoviePipelineSurfaceReader::Reset()
{
	if (AvailableEvent)
	{
		AvailableEvent->Trigger();
	}
	BlockUntilAvailable();
	bQueuedForCapture = false;
}

void FMoviePipelineSurfaceReader::ResolveSampleToReadbackTexture_RenderThread(const FTexture2DRHIRef& SourceSurfaceSample)
{
	// We use GetModuleChecked here to avoid accidentally loading the module from the non-main thread.
	static const FName RendererModuleName("Renderer");
	IRendererModule* RendererModule = &FModuleManager::GetModuleChecked<IRendererModule>(RendererModuleName);

	bQueuedForCapture = true;
	FRHICommandListImmediate& RHICmdList = GetImmediateCommandList_ForRenderCommand();

	// Retrieve a temporary render target that matches the destination surface type/size.
	const FIntPoint TargetSize(ReadbackTexture->GetSizeX(), ReadbackTexture->GetSizeY());

	FPooledRenderTargetDesc OutputDesc = FPooledRenderTargetDesc::Create2DDesc(
		TargetSize,
		ReadbackTexture->GetFormat(),
		FClearValueBinding::None,
		TexCreate_None,
		TexCreate_RenderTargetable,
		false);
	TRefCountPtr<IPooledRenderTarget> ResampleTexturePooledRenderTarget;
	GRenderTargetPool.FindFreeElement(RHICmdList, OutputDesc, ResampleTexturePooledRenderTarget, TEXT("ResampleTexture"));
	check(ResampleTexturePooledRenderTarget);

	// Enqueue a render pass which uses a simple shader and a fullscreen quad to copy the SourceSurfaceSample to this SurfaceReader's cpu-readback enabled texture.
	// This RenderPass resolves to our ReadbackTexture 
	FRHIRenderPassInfo RPInfo(ResampleTexturePooledRenderTarget->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::Load_Store, ReadbackTexture);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("MoviePipelineSurfaceResolveRenderTarget"));
	{
		RHICmdList.SetViewport(0, 0, 0.0f, TargetSize.X, TargetSize.Y, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		const ERHIFeatureLevel::Type FeatureLevel = GMaxRHIFeatureLevel;

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);
		TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
		TShaderMapRef<FScreenPS> PixelShader(ShaderMap);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		// Bind the SourceSurfaceSample to our texture sampler with a point sample (since the resolutions match).
		PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Point>::GetRHI(), SourceSurfaceSample);

		RendererModule->DrawRectangle(
			RHICmdList,
			0, 0,									// Dest X, Y
			TargetSize.X,							// Dest Width
			TargetSize.Y,							// Dest Height
			0, 0,									// Source U, V
			1, 1,									// Source USize, VSize
			TargetSize,								// Target buffer size
			FIntPoint(1, 1),						// Source texture size
			VertexShader,
			EDRF_Default);
	}
	RHICmdList.EndRenderPass();
}

void FMoviePipelineSurfaceReader::CopyReadbackTexture_RenderThread(const FMoviePipelineRenderPassMetrics& InSampleState, const FMoviePipelineSampleReady& InCallback)
{
	// We use GetModuleChecked here to avoid accidentally loading the module from the non-main thread.
	static const FName RendererModuleName("Renderer");
	IRendererModule* RendererModule = &FModuleManager::GetModuleChecked<IRendererModule>(RendererModuleName);

	FRHICommandListImmediate& RHICmdList = GetImmediateCommandList_ForRenderCommand();
	{
		void* ColorDataBuffer = nullptr;

		int32 ActualSizeX = 0, ActualSizeY = 0;
		RHICmdList.MapStagingSurface(ReadbackTexture, ColorDataBuffer, ActualSizeX, ActualSizeY);

		TArray<FFloat16Color> OutputPixels;

		int32 ExpectedSizeX = InSampleState.BackbufferSize.X;
		int32 ExpectedSizeY = InSampleState.BackbufferSize.Y;
		OutputPixels.SetNumUninitialized(ExpectedSizeX * ExpectedSizeY);

		// Due to padding, the actual size might be larger than the expected size. If they are the same, do a block copy. Otherwise copy
		// line by line.
		if (ExpectedSizeX == ActualSizeX && ExpectedSizeY == ActualSizeY)
		{
			FMemory::BigBlockMemcpy(OutputPixels.GetData(), ColorDataBuffer, (ExpectedSizeX * ExpectedSizeY) * sizeof(FFloat16Color));
		}
		else
		{
			UE_LOG(LogMovieRenderPipeline, Log, TEXT("Unexpected size in FMoviePipelineSurfaceReader::CopyReadbackTexture_RenderThread."));
			UE_LOG(LogMovieRenderPipeline, Log, TEXT("    Tile size:     %d x %d"), InSampleState.TileSize.X, InSampleState.TileSize.Y);
			UE_LOG(LogMovieRenderPipeline, Log, TEXT("    Expected size: %d x %d"), ExpectedSizeX, ExpectedSizeY);
			UE_LOG(LogMovieRenderPipeline, Log, TEXT("    Actual size:   %d x %d"), ActualSizeX, ActualSizeY);

			// Make sure the target is larger than expected size.
			check(ExpectedSizeX <= ActualSizeX);
			check(ExpectedSizeY <= ActualSizeY);

			int32 SrcPitchElem = ActualSizeX;
			int32 DstPitchElem = ExpectedSizeX;

			const FFloat16Color * SrcColorData = (const FFloat16Color*)ColorDataBuffer;
			FFloat16Color * DstColorData = (FFloat16Color*)OutputPixels.GetData();

			// Copy one line at a time
			for (int32 RowIndex = 0; RowIndex < ExpectedSizeY; RowIndex++)
			{
				const FFloat16Color * SrcPtr = &SrcColorData[RowIndex * SrcPitchElem];
				FFloat16Color * DstPtr = &DstColorData[RowIndex * DstPitchElem];
				FMemory::Memcpy(DstPtr, SrcPtr, DstPitchElem * sizeof(FFloat16Color));
			}
		}

		// Enqueue the Unmap before we broadcast the resulting pixels, though the broadcast shouldn't do anything blocking.
		RHICmdList.UnmapStagingSurface(ReadbackTexture);

		InCallback.Broadcast(OutputPixels, InSampleState);
		
		// Now that we've successfully used the surface, we trigger the Available event so that we can reuse this surface. This
		// triggers the Available event and then returns it to the pool.
		Reset();
	}
}

FMoviePipelineSurfaceQueue::FMoviePipelineSurfaceQueue(FIntPoint InSurfaceSize, EPixelFormat InPixelFormat, uint32 InNumSurfaces)
{
	// TargetSize = InSurfaceSize;
	CurrentFrameIndex = 0;
	check(InNumSurfaces > 0);

	// The Surface array can't be reallocated as we're storing indexes into it.
	Surfaces.Reserve(InNumSurfaces);
	for (uint32 Index = 0; Index < InNumSurfaces; Index++)
	{
		Surfaces.Emplace(InPixelFormat, InSurfaceSize);
	}

	FrameResolveLatency = 1;
}

FMoviePipelineSurfaceQueue::~FMoviePipelineSurfaceQueue()
{
}

void FMoviePipelineSurfaceQueue::BlockUntilAnyAvailable()
{
	bool bAnyAvailable = false;
	for(FResolveSurface& ResolveSurface : Surfaces)
	{
		if (ResolveSurface.Surface.IsAvailable())
		{
			bAnyAvailable = true;
			break;
		}
	}

	// We only wait if none of them are available, and the oldest one is the most likely one to
	// be available. 
	if (!bAnyAvailable)
	{
		const int32 OldestIndex = (CurrentFrameIndex + 1) % Surfaces.Num();
		Surfaces[OldestIndex].Surface.BlockUntilAvailable();
	}
}

void FMoviePipelineSurfaceQueue::Shutdown()
{
	for (int32 Index = 0; Index < Surfaces.Num(); Index++)
	{
		FResolveSurface* ResolveSurface = &Surfaces[Index];
		ENQUEUE_RENDER_COMMAND(PerformReadback)(
			[ResolveSurface](FRHICommandListImmediate& RHICmdList)
			{
				// Ensure that all surfaces have queued up their readback.
				if (ResolveSurface->Surface.WasEverQueued())
				{
					ResolveSurface->Surface.CopyReadbackTexture_RenderThread(ResolveSurface->SampleState, ResolveSurface->Callback);
				}
			});
	}

	// Flush the rendering thread so that the readbacks happen and trigger AvailableEvents.
	FlushRenderingCommands();

	for (FResolveSurface& ResolveSurface : Surfaces)
	{
		// These should now all be available.
		ensureMsgf(ResolveSurface.Surface.IsAvailable(), TEXT("Flushed rendering commands but surface reader didn't perform the readback on a surface!"));
	}
}


void FMoviePipelineSurfaceQueue::OnRenderTargetReady_RenderThread(const FTexture2DRHIRef InRenderTarget, const FMoviePipelineRenderPassMetrics& InSampleState, const FMoviePipelineSampleReady& InCallback)
{
	ensure(IsInRenderingThread());
	
	// Pick the next destination surface and ensure it's available.
	FResolveSurface* NextResolveTarget = &Surfaces[CurrentFrameIndex];
	if(!NextResolveTarget->Surface.IsAvailable())
	{
		// BlockUntilAnyAvailable should have been called before submitting more work. We can't block
		// until a surface is available in this callback because we'd be waiting on the render thread
		// from the render thread.
		check(false);
	}
	
	NextResolveTarget->Surface.Initialize();
	NextResolveTarget->SampleState = InSampleState;
	NextResolveTarget->Callback = InCallback;

	// Queue this sample to be copied to the target surface.
	NextResolveTarget->Surface.ResolveSampleToReadbackTexture_RenderThread(InRenderTarget);
	
	// By the time we get to this point, our oldest surface should have successfully been rendered to, and no longer be in use by the GPU.
	// We can now safely map the surface and copy the data out of it without causing a GPU stall.
	{
		const int32 PrevCaptureIndexOffset = FMath::Clamp(FrameResolveLatency, 0, Surfaces.Num() - 1);

		// Get PrevCaptureIndexOffset surfaces back, handling wraparound at 0.
		const int32 PrevCaptureIndex = (CurrentFrameIndex - PrevCaptureIndexOffset) < 0 ? Surfaces.Num() - (PrevCaptureIndexOffset - CurrentFrameIndex) : (CurrentFrameIndex - PrevCaptureIndexOffset);

		FResolveSurface* OldestResolveTarget = &Surfaces[PrevCaptureIndex];

		// Only try to do the readback if the target has ever been written to.
		if (OldestResolveTarget->Surface.WasEverQueued())
		{
			OldestResolveTarget->Surface.CopyReadbackTexture_RenderThread(OldestResolveTarget->SampleState, OldestResolveTarget->Callback);
		}
	}
	

	// Write to the next available surface next time.
	CurrentFrameIndex = (CurrentFrameIndex + 1) % Surfaces.Num();
}