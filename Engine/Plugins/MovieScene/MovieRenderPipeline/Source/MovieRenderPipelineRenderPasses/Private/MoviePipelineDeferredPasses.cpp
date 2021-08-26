// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineDeferredPasses.h"
#include "MoviePipelineOutputBase.h"
#include "Engine/TextureRenderTarget2D.h"
#include "SceneView.h"
#include "MovieRenderPipelineDataTypes.h"
#include "GameFramework/PlayerController.h"
#include "MoviePipelineRenderPass.h"
#include "EngineModule.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget.h"
#include "MoviePipeline.h"
#include "Misc/FrameRate.h"
#include "MoviePipelineShotConfig.h"
#include "MovieRenderOverlappedImage.h"
#include "MovieRenderPipelineCoreModule.h"
#include "ImagePixelData.h"
#include "MoviePipelineOutputBuilder.h"
#include "BufferVisualizationData.h"
#include "Containers/Array.h"
#include "FinalPostProcessSettings.h"
#include "Materials/Material.h"
#include "MoviePipelineCameraSetting.h"
#include "MoviePipelineQueue.h"
#include "MoviePipelineAntiAliasingSetting.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelineBlueprintLibrary.h"
#include "MovieRenderPipelineCoreModule.h"
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "Engine/RendererSettings.h"
#include "ImageUtils.h"


DECLARE_CYCLE_STAT(TEXT("STAT_MoviePipeline_AccumulateSample_TT"), STAT_AccumulateSample_TaskThread, STATGROUP_MoviePipeline);

// Forward Declare
namespace MoviePipeline
{
	static void AccumulateSample_TaskThread(TUniquePtr<FImagePixelData>&& InPixelData, const MoviePipeline::FImageSampleAccumulationArgs& InParams);
}

FString UMoviePipelineDeferredPassBase::StencilLayerMaterialAsset = TEXT("/MovieRenderPipeline/Materials/MoviePipeline_StencilCutout.MoviePipeline_StencilCutout");
FString UMoviePipelineDeferredPassBase::DefaultDepthAsset = TEXT("/MovieRenderPipeline/Materials/MovieRenderQueue_WorldDepth.MovieRenderQueue_WorldDepth");
FString UMoviePipelineDeferredPassBase::DefaultMotionVectorsAsset = TEXT("/MovieRenderPipeline/Materials/MovieRenderQueue_MotionVectors.MovieRenderQueue_MotionVectors");

UMoviePipelineDeferredPassBase::UMoviePipelineDeferredPassBase() 
	: UMoviePipelineImagePassBase()
{
	PassIdentifier = FMoviePipelinePassIdentifier("FinalImage");

	// To help user knowledge we pre-seed the additional post processing materials with an array of potentially common passes.
	TArray<FString> DefaultPostProcessMaterials;
	DefaultPostProcessMaterials.Add(DefaultDepthAsset);
	DefaultPostProcessMaterials.Add(DefaultMotionVectorsAsset);

	for (FString& MaterialPath : DefaultPostProcessMaterials)
	{
		FMoviePipelinePostProcessPass& NewPass = AdditionalPostProcessMaterials.AddDefaulted_GetRef();
		NewPass.Material = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(MaterialPath));
		NewPass.bEnabled = false;
	}
	bUse32BitPostProcessMaterials = false;
	CurrentLayerIndex = INDEX_NONE;
}

void UMoviePipelineDeferredPassBase::MoviePipelineRenderShowFlagOverride(FEngineShowFlags& OutShowFlag)
{
	if (bDisableMultisampleEffects)
	{
		OutShowFlag.SetAntiAliasing(false);
		OutShowFlag.SetDepthOfField(false);
		OutShowFlag.SetMotionBlur(false);
		OutShowFlag.SetBloom(false);
		OutShowFlag.SetSceneColorFringe(false);
	}
}

void UMoviePipelineDeferredPassBase::SetupImpl(const MoviePipeline::FMoviePipelineRenderPassInitSettings& InPassInitSettings)
{
	Super::SetupImpl(InPassInitSettings);

	// [0] is FinalImage, [1] is Default Layer, [1+] is Stencil Layers. Not used by post processing materials
	// Render Target that the GBuffer is copied to
	int32 NumRenderTargets = (bAddDefaultLayer ? 1 : 0) + StencilLayers.Num() + 1;
	for (int32 Index = 0; Index < NumRenderTargets; Index++)
	{
		UTextureRenderTarget2D* NewTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
		NewTarget->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

		// Initialize to the tile size (not final size) and use a 16 bit back buffer to avoid precision issues when accumulating later
		NewTarget->InitCustomFormat(InPassInitSettings.BackbufferResolution.X, InPassInitSettings.BackbufferResolution.Y, EPixelFormat::PF_FloatRGBA, false);

		// OCIO: Since this is a manually created Render target we don't need Gamma to be applied.
		// We use this render target to render to via a display extension that utilizes Display Gamma
		// which has a default value of 2.2 (DefaultDisplayGamma), therefore we need to set Gamma on this render target to 2.2 to cancel out any unwanted effects.
		NewTarget->TargetGamma = FOpenColorIODisplayExtension::DefaultDisplayGamma;

		TileRenderTargets.Add(NewTarget);
	}


	if (GetPipeline()->GetPreviewTexture() == nullptr)
	{
		GetPipeline()->SetPreviewTexture(TileRenderTargets[0]);
	}

	{
		TSoftObjectPtr<UMaterialInterface> StencilMatRef = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(StencilLayerMaterialAsset));
		StencilLayerMaterial = StencilMatRef.LoadSynchronous();
		if (!StencilLayerMaterial)
		{
			UE_LOG(LogMovieRenderPipeline, Error, TEXT("Failed to load Stencil Mask material, stencil layers will be incorrect. Path: %s"), *StencilMatRef.ToString());
		}
	}

	for (FMoviePipelinePostProcessPass& AdditionalPass : AdditionalPostProcessMaterials)
	{
		if (AdditionalPass.bEnabled)
		{
			UMaterialInterface* Material = AdditionalPass.Material.LoadSynchronous();
			if (Material)
			{
				ActivePostProcessMaterials.Add(Material);
			}
		}
	}

	SurfaceQueue = MakeShared<FMoviePipelineSurfaceQueue>(InPassInitSettings.BackbufferResolution, EPixelFormat::PF_FloatRGBA, 3, true);

	// Each stencil layer uses its own view state to keep TAA history etc separate
	if (bAddDefaultLayer)
	{
		StencilLayerViewStates.AddDefaulted();
	}

	for (int32 Index = 0; Index < StencilLayers.Num(); Index++)
	{
		StencilLayerViewStates.AddDefaulted();
	}

	for (int32 Index = 0; Index < StencilLayerViewStates.Num(); Index++)
	{
		StencilLayerViewStates[Index].Allocate();
	}


	// We must have at least enough accumulators to render all of the requested post process materials, because work doesn't begin
	// until they're actually submitted to the render thread (which happens all at once) but we tie up an accumulator as we get ready to submit.
	// If there aren't enough accumulators then we block until one is free but since submission hasn't gone through they'll never be free.
	int32 PoolSize = (StencilLayerViewStates.Num() + ActivePostProcessMaterials.Num() + 1) * 3;
	AccumulatorPool = MakeShared<TAccumulatorPool<FImageOverlappedAccumulator>, ESPMode::ThreadSafe>(PoolSize);
	
	PreviousCustomDepthValue.Reset();
	PreviousDumpFramesValue.Reset();
	PreviousColorFormatValue.Reset();

	// This scene view extension will be released automatically as soon as Render Sequence is torn down.
	// One Extension per sequence, since each sequence has its own OCIO settings.
	OCIOSceneViewExtension = FSceneViewExtensions::NewExtension<FOpenColorIODisplayExtension>();

	if (StencilLayerViewStates.Num() > 0)
	{
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepth"));
		if (CVar)
		{
			PreviousCustomDepthValue = CVar->GetInt();
			const int32 CustomDepthWithStencil = 3;
			if (PreviousCustomDepthValue != CustomDepthWithStencil)
			{
				UE_LOG(LogMovieRenderPipeline, Log, TEXT("Overriding project custom depth/stencil value to support a stencil pass."));
				// We use ECVF_SetByProjectSetting otherwise once this is set once by rendering, the UI silently fails
				// if you try to change it afterwards. This SetByProjectSetting will fail if they have manipulated the cvar via the console
				// during their current session but it's less likely than changing the project settings.
				CVar->Set(CustomDepthWithStencil, EConsoleVariableFlags::ECVF_SetByProjectSetting);
			}
		}
	}
	
	if (bUse32BitPostProcessMaterials)
	{
		IConsoleVariable* DumpFramesCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.BufferVisualizationDumpFramesAsHDR"));
		if (DumpFramesCVar)
		{
			PreviousDumpFramesValue = DumpFramesCVar->GetInt();
			DumpFramesCVar->Set(1, EConsoleVariableFlags::ECVF_SetByConsole);
		}
		
		IConsoleVariable* ColorFormatCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PostProcessingColorFormat"));
		if (ColorFormatCVar)
		{
			PreviousColorFormatValue = ColorFormatCVar->GetInt();
			ColorFormatCVar->Set(1, EConsoleVariableFlags::ECVF_SetByConsole);
		}
	}
}

void UMoviePipelineDeferredPassBase::TeardownImpl()
{
	GetPipeline()->SetPreviewTexture(nullptr);

	// This may call FlushRenderingCommands if there are outstanding readbacks that need to happen.
	if (SurfaceQueue.IsValid())
	{
		SurfaceQueue->Shutdown();
	}

	// Stall until the task graph has completed any pending accumulations.
	FTaskGraphInterface::Get().WaitUntilTasksComplete(OutstandingTasks, ENamedThreads::GameThread);
	OutstandingTasks.Reset();

	ActivePostProcessMaterials.Reset();

	for (int32 Index = 0; Index < StencilLayerViewStates.Num(); Index++)
	{
		FSceneViewStateInterface* Ref = StencilLayerViewStates[Index].GetReference();
		if (Ref)
		{
			Ref->ClearMIDPool();
		}
		StencilLayerViewStates[Index].Destroy();
	}
	StencilLayerViewStates.Reset();
	CurrentLayerIndex = INDEX_NONE;
	TileRenderTargets.Reset();
	
	OCIOSceneViewExtension.Reset();
	OCIOSceneViewExtension = nullptr;

	if (PreviousCustomDepthValue.IsSet())
	{
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepth"));
		if (CVar)
		{
			if (CVar->GetInt() != PreviousCustomDepthValue.GetValue())
			{
				UE_LOG(LogMovieRenderPipeline, Log, TEXT("Restoring custom depth/stencil value to: %d"), PreviousCustomDepthValue.GetValue());
				CVar->Set(PreviousCustomDepthValue.GetValue(), EConsoleVariableFlags::ECVF_SetByProjectSetting);
			}
		}
	}
	
	if (PreviousDumpFramesValue.IsSet())
	{
		IConsoleVariable* DumpFramesCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.BufferVisualizationDumpFramesAsHDR"));
		if (DumpFramesCVar)
		{
			DumpFramesCVar->Set(PreviousDumpFramesValue.GetValue(),  EConsoleVariableFlags::ECVF_SetByConsole);
		}
		
		IConsoleVariable* ColorFormatCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PostProcessingColorFormat"));
		if (ColorFormatCVar)
		{
			ColorFormatCVar->Set(PreviousColorFormatValue.GetValue(), EConsoleVariableFlags::ECVF_SetByConsole);
		}
	}

	// Preserve our view state until the rendering thread has been flushed.
	Super::TeardownImpl();
}

void UMoviePipelineDeferredPassBase::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);

	UMoviePipelineDeferredPassBase& This = *CastChecked<UMoviePipelineDeferredPassBase>(InThis);
	for (int32 Index = 0; Index < This.StencilLayerViewStates.Num(); Index++)
	{
		FSceneViewStateInterface* Ref = This.StencilLayerViewStates[Index].GetReference();
		if (Ref)
		{
			Ref->AddReferencedObjects(Collector);
		}
	}
}

FSceneViewStateInterface* UMoviePipelineDeferredPassBase::GetSceneViewStateInterface()
{
	if (CurrentLayerIndex == INDEX_NONE)
	{
		return Super::GetSceneViewStateInterface();
	}
	else
	{
		return StencilLayerViewStates[CurrentLayerIndex].GetReference();
	}
}

UTextureRenderTarget2D* UMoviePipelineDeferredPassBase::GetViewRenderTarget() const
{
	if (CurrentLayerIndex == INDEX_NONE)
	{
		return TileRenderTargets[0];
	}
	else
	{
		return TileRenderTargets[CurrentLayerIndex + 1];
	}
}


void UMoviePipelineDeferredPassBase::GatherOutputPassesImpl(TArray<FMoviePipelinePassIdentifier>& ExpectedRenderPasses)
{
	// Add the default backbuffer
	Super::GatherOutputPassesImpl(ExpectedRenderPasses);

	TArray<FString> RenderPasses;
	for (UMaterialInterface* Material : ActivePostProcessMaterials)
	{
		if (Material)
		{
			RenderPasses.Add(Material->GetName());
		}
	}

	for (const FString& Pass : RenderPasses)
	{
		ExpectedRenderPasses.Add(FMoviePipelinePassIdentifier(PassIdentifier.Name + Pass));
	}

	if (bAddDefaultLayer)
	{
		ExpectedRenderPasses.Add(FMoviePipelinePassIdentifier(PassIdentifier.Name + TEXT("DefaultLayer")));
	}

	for (const FActorLayer& Layer : StencilLayers)
	{
		ExpectedRenderPasses.Add(FMoviePipelinePassIdentifier(PassIdentifier.Name + Layer.Name.ToString()));
	}
}

void UMoviePipelineDeferredPassBase::AddViewExtensions(FSceneViewFamilyContext& InContext, FMoviePipelineRenderPassMetrics& InOutSampleState)
{
	// OCIO Scene View Extension is a special case and won't be registered like other view extensions.
	if (InOutSampleState.OCIOConfiguration && InOutSampleState.OCIOConfiguration->bIsEnabled)
	{
		FOpenColorIODisplayConfiguration* OCIOConfigNew = const_cast<FMoviePipelineRenderPassMetrics&>(InOutSampleState).OCIOConfiguration;
		FOpenColorIODisplayConfiguration& OCIOConfigCurrent = OCIOSceneViewExtension->GetDisplayConfiguration();

		// We only need to set this once per render sequence.
		if (OCIOConfigNew->ColorConfiguration.ConfigurationSource && OCIOConfigNew->ColorConfiguration.ConfigurationSource != OCIOConfigCurrent.ColorConfiguration.ConfigurationSource)
		{
			OCIOSceneViewExtension->SetDisplayConfiguration(*OCIOConfigNew);
		}

		InContext.ViewExtensions.Add(OCIOSceneViewExtension.ToSharedRef());
	}
}

void UMoviePipelineDeferredPassBase::RenderSample_GameThreadImpl(const FMoviePipelineRenderPassMetrics& InSampleState)
{
	Super::RenderSample_GameThreadImpl(InSampleState);

	// Wait for a surface to be available to write to. This will stall the game thread while the RHI/Render Thread catch up.
	{
		SCOPE_CYCLE_COUNTER(STAT_MoviePipeline_WaitForAvailableSurface);
		SurfaceQueue->BlockUntilAnyAvailable();
	}


	// Main Render Pass
	{
		FMoviePipelineRenderPassMetrics InOutSampleState = InSampleState;

		CurrentLayerIndex = INDEX_NONE;
		TSharedPtr<FSceneViewFamilyContext> ViewFamily = CalculateViewFamily(InOutSampleState);

		// Add post-processing materials if needed
		FSceneView* View = const_cast<FSceneView*>(ViewFamily->Views[0]);
		View->FinalPostProcessSettings.BufferVisualizationOverviewMaterials.Empty();
		View->FinalPostProcessSettings.BufferVisualizationPipes.Empty();

		for (UMaterialInterface* Material : ActivePostProcessMaterials)
		{
			if (Material)
			{
				View->FinalPostProcessSettings.BufferVisualizationOverviewMaterials.Add(Material);
			}
		}

		for (UMaterialInterface* VisMaterial : View->FinalPostProcessSettings.BufferVisualizationOverviewMaterials)
		{
			// If this was just to contribute to the history buffer, no need to go any further.
			if (InSampleState.bDiscardResult)
			{
				continue;
			}
			FMoviePipelinePassIdentifier LayerPassIdentifier = FMoviePipelinePassIdentifier(PassIdentifier.Name + VisMaterial->GetName());

			auto BufferPipe = MakeShared<FImagePixelPipe, ESPMode::ThreadSafe>();
			BufferPipe->AddEndpoint(MakeForwardingEndpoint(LayerPassIdentifier, InSampleState));

			View->FinalPostProcessSettings.BufferVisualizationPipes.Add(VisMaterial->GetFName(), BufferPipe);
		}


		int32 NumValidMaterials = View->FinalPostProcessSettings.BufferVisualizationPipes.Num();
		View->FinalPostProcessSettings.bBufferVisualizationDumpRequired = NumValidMaterials > 0;

		// Submit to be rendered. Main render pass always uses target 0.
		FRenderTarget* RenderTarget = GetViewRenderTarget()->GameThread_GetRenderTargetResource();
		FCanvas Canvas = FCanvas(RenderTarget, nullptr, GetPipeline()->GetWorld(), ERHIFeatureLevel::SM5, FCanvas::CDM_DeferDrawing, 1.0f);
		GetRendererModule().BeginRenderingViewFamily(&Canvas, ViewFamily.Get());

		// Readback + Accumulate.
		PostRendererSubmission(InOutSampleState, PassIdentifier, GetOutputFileSortingOrder(), Canvas);
	}

	// Now submit stencil layers if needed
	{
		FMoviePipelineRenderPassMetrics InOutSampleState = InSampleState;

		struct FStencilValues
		{
			FStencilValues()
				: bRenderCustomDepth(false)
				, StencilMask(ERendererStencilMask::ERSM_Default)
				, CustomStencil(0)
			{
			}

			bool bRenderCustomDepth;
			ERendererStencilMask StencilMask;
			int32 CustomStencil;
		};

		// If we're going to be using stencil layers, we need to cache all of the users
		// custom stencil/depth settings since we're changing them to do the mask.
		TMap<UPrimitiveComponent*, FStencilValues> PreviousValues;
		if (StencilLayers.Num() > 0)
		{
			for (TActorIterator<AActor> ActorItr(GetWorld()); ActorItr; ++ActorItr)
			{
				AActor* Actor = *ActorItr;
				if (Actor)
				{
					for (UActorComponent* Component : Actor->GetComponents())
					{
						if (Component && Component->IsA<UPrimitiveComponent>())
						{
							UPrimitiveComponent* PrimitiveComponent = CastChecked<UPrimitiveComponent>(Component);
							FStencilValues& Values = PreviousValues.Add(PrimitiveComponent);
							Values.StencilMask = PrimitiveComponent->CustomDepthStencilWriteMask;
							Values.CustomStencil = PrimitiveComponent->CustomDepthStencilValue;
							Values.bRenderCustomDepth = PrimitiveComponent->bRenderCustomDepth;
						}
					}
				}
			}
		}

		// Now for each stencil layer we reconfigure all the actors custom depth/stencil 
		TArray<FActorLayer> AllStencilLayers = StencilLayers;
		if (bAddDefaultLayer)
		{
			FActorLayer& DefaultLayer = AllStencilLayers.AddDefaulted_GetRef();
			DefaultLayer.Name = FName("DefaultLayer");
		}

		CurrentLayerIndex = 0;
		for (const FActorLayer& Layer : AllStencilLayers)
		{
			FMoviePipelinePassIdentifier LayerPassIdentifier = FMoviePipelinePassIdentifier(PassIdentifier.Name + Layer.Name.ToString());

			for (TActorIterator<AActor> ActorItr(GetWorld()); ActorItr; ++ActorItr)
			{
				AActor* Actor = *ActorItr;
				if (Actor)
				{
					// The way stencil masking works is that we draw the actors on the given layer to the stencil buffer.
					// Then we apply a post-processing material which colors pixels outside those actors black, before
					// post processing. Then, TAA, Motion Blur, etc. is applied to all pixels. An alpha channel can preserve
					// which pixels were the geometry and which are dead space which lets you apply that as a mask later.
					bool bInLayer = true;
					if (bAddDefaultLayer && Layer.Name == FName("DefaultLayer"))
					{
						// If we're trying to render the default layer, the logic is different - we only add objects who
						// aren't in any of the stencil layers.
						for (const FActorLayer& AllLayer : StencilLayers)
						{
							bInLayer = !Actor->Layers.Contains(AllLayer.Name);
							if (!bInLayer)
							{
								break;
							}
						}
					}
					else
					{
						// If this a normal layer, we only add the actor if it exists on this layer.
						bInLayer = Actor->Layers.Contains(Layer.Name);
					}

					for (UActorComponent* Component : Actor->GetComponents())
					{
						if (Component && Component->IsA<UPrimitiveComponent>())
						{
							UPrimitiveComponent* PrimitiveComponent = CastChecked<UPrimitiveComponent>(Component);
							// We want to render all objects not on the layer to stencil too so that foreground objects mask.
							PrimitiveComponent->SetCustomDepthStencilValue(bInLayer ? 1 : 0);
							PrimitiveComponent->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_Default);
							PrimitiveComponent->SetRenderCustomDepth(true);
						}
					}
				}
			}

			if (StencilLayerMaterial)
			{
				TSharedPtr<FSceneViewFamilyContext> ViewFamily = CalculateViewFamily(InOutSampleState);
				FSceneView* View = const_cast<FSceneView*>(ViewFamily->Views[0]);

				// Now that we've modified all of the stencil values, we can submit them to be rendered.
				View->FinalPostProcessSettings.AddBlendable(StencilLayerMaterial, 1.0f);
				IBlendableInterface* BlendableInterface = Cast<IBlendableInterface>(StencilLayerMaterial);
				BlendableInterface->OverrideBlendableSettings(*View, 1.f);

				{
					FRenderTarget* RenderTarget = GetViewRenderTarget()->GameThread_GetRenderTargetResource();
					FCanvas Canvas = FCanvas(RenderTarget, nullptr, GetPipeline()->GetWorld(), ERHIFeatureLevel::SM5, FCanvas::CDM_DeferDrawing, 1.0f);
					GetRendererModule().BeginRenderingViewFamily(&Canvas, ViewFamily.Get());

					// Readback + Accumulate.
					PostRendererSubmission(InSampleState, LayerPassIdentifier, GetOutputFileSortingOrder() + 1, Canvas);
				}
			}

			CurrentLayerIndex++;
		}

		// Now we can restore the custom depth/stencil/etc. values so that the main render pass acts as the user expects next time.
		for (TPair<UPrimitiveComponent*, FStencilValues>& KVP : PreviousValues)
		{
			KVP.Key->SetCustomDepthStencilValue(KVP.Value.CustomStencil);
			KVP.Key->SetCustomDepthStencilWriteMask(KVP.Value.StencilMask);
			KVP.Key->SetRenderCustomDepth(KVP.Value.bRenderCustomDepth);
		}
	}
}

TFunction<void(TUniquePtr<FImagePixelData>&&)> UMoviePipelineDeferredPassBase::MakeForwardingEndpoint(const FMoviePipelinePassIdentifier InPassIdentifier, const FMoviePipelineRenderPassMetrics& InSampleState)
{
	// We have a pool of accumulators - we multi-thread the accumulation on the task graph, and for each frame,
	// the task has the previous samples as pre-reqs to keep the accumulation in order. However, each accumulator
	// can only work on one frame at a time, so we create a pool of them to work concurrently. This needs a limit
	// as large accumulations (16k) can take a lot of system RAM.
	TSharedPtr<FAccumulatorPool::FAccumulatorInstance, ESPMode::ThreadSafe> SampleAccumulator = nullptr;
	{
		SCOPE_CYCLE_COUNTER(STAT_MoviePipeline_WaitForAvailableAccumulator);
		SampleAccumulator = AccumulatorPool->BlockAndGetAccumulator_GameThread(InSampleState.OutputState.OutputFrameNumber, InPassIdentifier);
	}
	TSharedPtr<FMoviePipelineSurfaceQueue> LocalSurfaceQueue = SurfaceQueue;

	TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> FramePayload = MakeShared<FImagePixelDataPayload, ESPMode::ThreadSafe>();
	FramePayload->PassIdentifier = InPassIdentifier;
	FramePayload->SampleState = InSampleState;
	FramePayload->SortingOrder = GetOutputFileSortingOrder() + 1;

	MoviePipeline::FImageSampleAccumulationArgs AccumulationArgs;
	{
		AccumulationArgs.OutputMerger = GetPipeline()->OutputBuilder;
		AccumulationArgs.ImageAccumulator = StaticCastSharedPtr<FImageOverlappedAccumulator>(SampleAccumulator->Accumulator);
		AccumulationArgs.bAccumulateAlpha = bAccumulatorIncludesAlpha;
	}

	auto Callback = [this, FramePayload, AccumulationArgs, SampleAccumulator](TUniquePtr<FImagePixelData>&& InPixelData)
	{
		// Transfer the framePayload to the returned data
		TUniquePtr<FImagePixelData> PixelDataWithPayload = nullptr;
		switch (InPixelData->GetType())
		{
		case EImagePixelType::Color:
		{
			TImagePixelData<FColor>* SourceData = static_cast<TImagePixelData<FColor>*>(InPixelData.Get());
			PixelDataWithPayload = MakeUnique<TImagePixelData<FColor>>(InPixelData->GetSize(), MoveTemp(SourceData->Pixels), FramePayload);
			break;
		}
		case EImagePixelType::Float16:
		{
			TImagePixelData<FFloat16Color>* SourceData = static_cast<TImagePixelData<FFloat16Color>*>(InPixelData.Get());
			PixelDataWithPayload = MakeUnique<TImagePixelData<FFloat16Color>>(InPixelData->GetSize(), MoveTemp(SourceData->Pixels), FramePayload);
			break;
		}
		case EImagePixelType::Float32:
		{
			TImagePixelData<FLinearColor>* SourceData = static_cast<TImagePixelData<FLinearColor>*>(InPixelData.Get());
			PixelDataWithPayload = MakeUnique<TImagePixelData<FLinearColor>>(InPixelData->GetSize(), MoveTemp(SourceData->Pixels), FramePayload);
			break;
		}
		default:
			checkNoEntry();
		}

		bool bFinalSample = FramePayload->IsLastTile() && FramePayload->IsLastTemporalSample();
		bool bFirstSample = FramePayload->IsFirstTile() && FramePayload->IsFirstTemporalSample();

		FMoviePipelineBackgroundAccumulateTask Task;
		// There may be other accumulations for this accumulator which need to be processed first
		Task.LastCompletionEvent = SampleAccumulator->TaskPrereq;

		FGraphEventRef Event = Task.Execute([PixelData = MoveTemp(PixelDataWithPayload), AccumulationArgs, bFinalSample, SampleAccumulator]() mutable
		{
			// Enqueue a encode for this frame onto our worker thread.
			MoviePipeline::AccumulateSample_TaskThread(MoveTemp(PixelData), AccumulationArgs);
			if (bFinalSample)
			{
				SampleAccumulator->bIsActive = false;
				SampleAccumulator->TaskPrereq = nullptr;
			}
		});
		SampleAccumulator->TaskPrereq = Event;

		this->OutstandingTasks.Add(Event);
	};

	return Callback;
}

void UMoviePipelineDeferredPassBase::PostRendererSubmission(const FMoviePipelineRenderPassMetrics& InSampleState, const FMoviePipelinePassIdentifier InPassIdentifier, const int32 InSortingOrder, FCanvas& InCanvas)
{
	// If this was just to contribute to the history buffer, no need to go any further.
	if (InSampleState.bDiscardResult)
	{
		return;
	}
	
	// Draw letterboxing
	APlayerCameraManager* PlayerCameraManager = GetPipeline()->GetWorld()->GetFirstPlayerController()->PlayerCameraManager;
	if(PlayerCameraManager && PlayerCameraManager->GetCameraCachePOV().bConstrainAspectRatio)
	{
		const FMinimalViewInfo CameraCache = PlayerCameraManager->GetCameraCachePOV();
		UMoviePipelineOutputSetting* OutputSettings = GetPipeline()->GetPipelineMasterConfig()->FindSetting<UMoviePipelineOutputSetting>();
		check(OutputSettings);
		
		// Taking overscan into account.
		FIntPoint FullOutputSize = UMoviePipelineBlueprintLibrary::GetEffectiveOutputResolution(GetPipeline()->GetPipelineMasterConfig(), GetPipeline()->GetActiveShotList()[GetPipeline()->GetCurrentShotIndex()]);

		float OutputSizeAspectRatio = FullOutputSize.X / (float)FullOutputSize.Y;
		const FIntPoint ConstrainedFullSize = CameraCache.AspectRatio > OutputSizeAspectRatio ?
			FIntPoint(FullOutputSize.X, FMath::CeilToInt((double)FullOutputSize.X / (double)CameraCache.AspectRatio)) :
			FIntPoint(FMath::CeilToInt(CameraCache.AspectRatio * FullOutputSize.Y), FullOutputSize.Y);

		const FIntPoint TileViewMin = InSampleState.OverlappedOffset;
		const FIntPoint TileViewMax = TileViewMin + InSampleState.BackbufferSize;

		// Camera ratio constrained rect, clipped by the tile rect
		FIntPoint ConstrainedViewMin = (FullOutputSize - ConstrainedFullSize) / 2;
		FIntPoint ConstrainedViewMax = ConstrainedViewMin + ConstrainedFullSize;
		ConstrainedViewMin = FIntPoint(FMath::Clamp(ConstrainedViewMin.X, TileViewMin.X, TileViewMax.X),
			FMath::Clamp(ConstrainedViewMin.Y, TileViewMin.Y, TileViewMax.Y));
		ConstrainedViewMax = FIntPoint(FMath::Clamp(ConstrainedViewMax.X, TileViewMin.X, TileViewMax.X),
			FMath::Clamp(ConstrainedViewMax.Y, TileViewMin.Y, TileViewMax.Y));

		// Difference between the clipped constrained rect and the tile rect
		const FIntPoint OffsetMin = ConstrainedViewMin - TileViewMin;
		const FIntPoint OffsetMax = TileViewMax - ConstrainedViewMax;

		// Clear left
		if (OffsetMin.X > 0)
		{
			InCanvas.DrawTile(0, 0, OffsetMin.X, InSampleState.BackbufferSize.Y,
				0.0f, 0.0f, 1.0f, 1.0f, FLinearColor::Black, nullptr, false);
		}
		// Clear right
		if (OffsetMax.X > 0)
		{
			InCanvas.DrawTile(InSampleState.BackbufferSize.X - OffsetMax.X, 0, InSampleState.BackbufferSize.X, InSampleState.BackbufferSize.Y,
				0.0f, 0.0f, 1.0f, 1.0f, FLinearColor::Black, nullptr, false);
		}
		// Clear top
		if (OffsetMin.Y > 0)
		{
			InCanvas.DrawTile(0, 0, InSampleState.BackbufferSize.X, OffsetMin.Y,
				0.0f, 0.0f, 1.0f, 1.0f, FLinearColor::Black, nullptr, false);
		}
		// Clear bottom
		if (OffsetMax.Y > 0)
		{
			InCanvas.DrawTile(0, InSampleState.BackbufferSize.Y - OffsetMax.Y, InSampleState.BackbufferSize.X, InSampleState.BackbufferSize.Y,
				0.0f, 0.0f, 1.0f, 1.0f, FLinearColor::Black, nullptr, false);
		}

		InCanvas.Flush_GameThread(true);
	}

	// We have a pool of accumulators - we multi-thread the accumulation on the task graph, and for each frame,
	// the task has the previous samples as pre-reqs to keep the accumulation in order. However, each accumulator
	// can only work on one frame at a time, so we create a pool of them to work concurrently. This needs a limit
	// as large accumulations (16k) can take a lot of system RAM.
	TSharedPtr<FAccumulatorPool::FAccumulatorInstance, ESPMode::ThreadSafe> SampleAccumulator = nullptr;
	{
		SCOPE_CYCLE_COUNTER(STAT_MoviePipeline_WaitForAvailableAccumulator);
		SampleAccumulator = AccumulatorPool->BlockAndGetAccumulator_GameThread(InSampleState.OutputState.OutputFrameNumber, InPassIdentifier);
	}
	TSharedPtr<FMoviePipelineSurfaceQueue> LocalSurfaceQueue = SurfaceQueue;

	TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> FramePayload = MakeShared<FImagePixelDataPayload, ESPMode::ThreadSafe>();
	FramePayload->PassIdentifier = InPassIdentifier;
	FramePayload->SampleState = InSampleState;
	FramePayload->SortingOrder = InSortingOrder;

	MoviePipeline::FImageSampleAccumulationArgs AccumulationArgs;
	{
		AccumulationArgs.OutputMerger = GetPipeline()->OutputBuilder;
		AccumulationArgs.ImageAccumulator = StaticCastSharedPtr<FImageOverlappedAccumulator>(SampleAccumulator->Accumulator);
		AccumulationArgs.bAccumulateAlpha = bAccumulatorIncludesAlpha;
	}

	auto Callback = [this, FramePayload, AccumulationArgs, SampleAccumulator](TUniquePtr<FImagePixelData>&& InPixelData)
	{
		bool bFinalSample = FramePayload->IsLastTile() && FramePayload->IsLastTemporalSample();
		bool bFirstSample = FramePayload->IsFirstTile() && FramePayload->IsFirstTemporalSample();

		FMoviePipelineBackgroundAccumulateTask Task;
		// There may be other accumulations for this accumulator which need to be processed first
		Task.LastCompletionEvent = SampleAccumulator->TaskPrereq;

		FGraphEventRef Event = Task.Execute([PixelData = MoveTemp(InPixelData), AccumulationArgs, bFinalSample, SampleAccumulator]() mutable
		{
			// Enqueue a encode for this frame onto our worker thread.
			MoviePipeline::AccumulateSample_TaskThread(MoveTemp(PixelData), AccumulationArgs);
			if (bFinalSample)
			{
				// Final sample has now been executed, break the pre-req chain and free the accumulator for reuse.
				SampleAccumulator->bIsActive = false;
				SampleAccumulator->TaskPrereq = nullptr;
			}
		});
		SampleAccumulator->TaskPrereq = Event;

		this->OutstandingTasks.Add(Event);
	};

	FRenderTarget* RenderTarget = InCanvas.GetRenderTarget();

	ENQUEUE_RENDER_COMMAND(CanvasRenderTargetResolveCommand)(
		[LocalSurfaceQueue, FramePayload, Callback, RenderTarget](FRHICommandListImmediate& RHICmdList) mutable
		{
			// Enqueue a encode for this frame onto our worker thread.
			LocalSurfaceQueue->OnRenderTargetReady_RenderThread(RenderTarget->GetRenderTargetTexture(), FramePayload, MoveTemp(Callback));
		});

}

#if WITH_EDITOR
FText UMoviePipelineDeferredPass_PathTracer::GetFooterText(UMoviePipelineExecutorJob* InJob) const {
	return NSLOCTEXT(
		"MovieRenderPipeline",
		"DeferredBasePassSetting_FooterText_PathTracer",
		"Samples per Pixel for the Path Tracer are controlled by the Spatial Sample Count from the Anti-Aliasing settings.\n"
		"All other Path Tracer settings are taken from the Post Process settings as usual.");
}
#endif

namespace MoviePipeline
{
	static void AccumulateSample_TaskThread(TUniquePtr<FImagePixelData>&& InPixelData, const MoviePipeline::FImageSampleAccumulationArgs& InParams)
	{
		SCOPE_CYCLE_COUNTER(STAT_AccumulateSample_TaskThread);

		TUniquePtr<FImagePixelData> SamplePixelData = MoveTemp(InPixelData);
		const bool bIsWellFormed = SamplePixelData->IsDataWellFormed();

		if (!bIsWellFormed)
		{
			// figure out why it is not well formed, and print a warning.
			int64 RawSize = SamplePixelData->GetRawDataSizeInBytes();

			int64 SizeX = SamplePixelData->GetSize().X;
			int64 SizeY = SamplePixelData->GetSize().Y;
			int64 ByteDepth = int64(SamplePixelData->GetBitDepth() / 8);
			int64 NumChannels = int64(SamplePixelData->GetNumChannels());
			int64 ExpectedTotalSize = SizeX * SizeY * ByteDepth * NumChannels;
			int64 ActualTotalSize = SamplePixelData->GetRawDataSizeInBytes();

			UE_LOG(LogMovieRenderPipeline, Log, TEXT("AccumulateSample_RenderThread: Data is not well formed."));
			UE_LOG(LogMovieRenderPipeline, Log, TEXT("Image dimension: %lldx%lld, %lld, %lld"), SizeX, SizeY, ByteDepth, NumChannels);
			UE_LOG(LogMovieRenderPipeline, Log, TEXT("Expected size: %lld"), ExpectedTotalSize);
			UE_LOG(LogMovieRenderPipeline, Log, TEXT("Actual size:   %lld"), ActualTotalSize);
		}

		check(bIsWellFormed);

		FImagePixelDataPayload* OriginalFramePayload = SamplePixelData->GetPayload<FImagePixelDataPayload>();
		check(OriginalFramePayload);

		// We duplicate the payload for now because there are multiple cases where we need to create a new 
		// image payload and we can't transfer the existing payload over.
		TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> NewPayload = MakeShared<FImagePixelDataPayload, ESPMode::ThreadSafe>();
		NewPayload->PassIdentifier = OriginalFramePayload->PassIdentifier;
		NewPayload->SampleState = OriginalFramePayload->SampleState;
		NewPayload->SortingOrder = OriginalFramePayload->SortingOrder;

		// Writing tiles can be useful for debug reasons. These get passed onto the output every frame.
		if (NewPayload->SampleState.bWriteSampleToDisk)
		{
			// Send the data to the Output Builder. This has to be a copy of the pixel data from the GPU, since
			// it enqueues it onto the game thread and won't be read/sent to write to disk for another frame. 
			// The extra copy is unfortunate, but is only the size of a single sample (ie: 1920x1080 -> 17mb)
			TUniquePtr<FImagePixelData> SampleData = SamplePixelData->CopyImageData();
			InParams.OutputMerger->OnSingleSampleDataAvailable_AnyThread(MoveTemp(SampleData));
		}

		// Optimization! If we don't need the accumulator (no tiling, no supersampling) then we'll skip it and just send it straight to the output stage.
		// This significantly improves performance in the baseline case.
		const bool bOneTile = NewPayload->IsFirstTile() && NewPayload->IsLastTile();
		const bool bOneTS = NewPayload->IsFirstTemporalSample() && NewPayload->IsLastTemporalSample();
		const bool bOneSS = NewPayload->SampleState.SpatialSampleCount == 1;

		if (bOneTile && bOneTS && bOneSS)
		{
			// Send the data directly to the Output Builder and skip the accumulator.
			InParams.OutputMerger->OnCompleteRenderPassDataAvailable_AnyThread(MoveTemp(SamplePixelData));
			return;
		}

		// Allocate memory if the ImageAccumulator has not been initialized yet for this output
		// This usually happens on the first sample (regular case), or on the last spatial sample of the first temporal sample (path tracer)
		if (InParams.ImageAccumulator->NumChannels == 0)
		{
			int32 ChannelCount = InParams.bAccumulateAlpha ? 4 : 3;
			InParams.ImageAccumulator->InitMemory(FIntPoint(NewPayload->SampleState.TileSize.X * NewPayload->SampleState.TileCounts.X, NewPayload->SampleState.TileSize.Y * NewPayload->SampleState.TileCounts.Y), ChannelCount);
			InParams.ImageAccumulator->ZeroPlanes();
			InParams.ImageAccumulator->AccumulationGamma = NewPayload->SampleState.AccumulationGamma;
		}

		// Accumulate the new sample to our target
		{
			// Some samples can come back at a different size than expected (post process materials) which
			// creates numerous issues with the accumulators. To work around this issue for now, we will resize
			// the image to the expected resolution. 
			FIntPoint RawSize = SamplePixelData->GetSize();
			const bool bCorrectSize = (NewPayload->SampleState.TileSize.X + 2 * NewPayload->SampleState.OverlappedPad.X == RawSize.X)
								   && (NewPayload->SampleState.TileSize.Y + 2 * NewPayload->SampleState.OverlappedPad.Y == RawSize.Y);


			if (!bCorrectSize)
			{
				const double ResizeConvertBeginTime = FPlatformTime::Seconds();
				
				// Convert the incoming data to full floats (the accumulator would do this later normally anyways)
				TArray64<FLinearColor> FullSizeData;
				FullSizeData.AddUninitialized(RawSize.X * RawSize.Y);

				if (SamplePixelData->GetType() == EImagePixelType::Float32)
				{
					const void* RawDataPtr;
					int64 RawDataSize;
					SamplePixelData->GetRawData(RawDataPtr, RawDataSize);

					FMemory::Memcpy(FullSizeData.GetData(), RawDataPtr, RawDataSize);
				}
				else if (SamplePixelData->GetType() == EImagePixelType::Float16)
				{
					const void* RawDataPtr;
					int64 RawDataSize;
					SamplePixelData->GetRawData(RawDataPtr, RawDataSize);

					const FFloat16Color* DataAsColor = reinterpret_cast<const FFloat16Color*>(RawDataPtr);
					for (int64 Index = 0; Index < RawSize.X * RawSize.Y; Index++)
					{
						FullSizeData[Index] = FLinearColor(DataAsColor[Index]);
					}
				}
				else
				{
					check(0);
				}
				const double ResizeConvertEndTime = FPlatformTime::Seconds();

				// Now we can resize to our target size.
				const int32 TargetSizeX = NewPayload->SampleState.TileSize.X + 2 * NewPayload->SampleState.OverlappedPad.X;
				const int32 TargetSizeY = NewPayload->SampleState.TileSize.Y + 2 * NewPayload->SampleState.OverlappedPad.Y;

				TArray64<FLinearColor> NewPixelData;
				NewPixelData.SetNumUninitialized(TargetSizeX * TargetSizeY);

				FImageUtils::ImageResize(RawSize.X, RawSize.Y, MakeArrayView<FLinearColor>(FullSizeData.GetData(), FullSizeData.Num()), TargetSizeX, TargetSizeY, MakeArrayView<FLinearColor>(NewPixelData.GetData(), NewPixelData.Num()));

				const float ElapsedConvertMs = float((ResizeConvertEndTime - ResizeConvertBeginTime) * 1000.0f);
				const float ElapsedResizeMs = float((FPlatformTime::Seconds() - ResizeConvertEndTime) * 1000.0f);
				
				UE_LOG(LogMovieRenderPipeline, VeryVerbose, TEXT("Resize Convert Time: %8.2fms Resize Time: %8.2fms"), ElapsedConvertMs, ElapsedResizeMs);
			
				SamplePixelData = MakeUnique<TImagePixelData<FLinearColor>>(FIntPoint(TargetSizeX, TargetSizeY), MoveTemp(NewPixelData), NewPayload);

				// Update the raw size to match our new size.
				RawSize = SamplePixelData->GetSize();
			}

			const double AccumulateBeginTime = FPlatformTime::Seconds();

			check(NewPayload->SampleState.TileSize.X + 2 * NewPayload->SampleState.OverlappedPad.X == RawSize.X);
			check(NewPayload->SampleState.TileSize.Y + 2 * NewPayload->SampleState.OverlappedPad.Y == RawSize.Y);

			// bool bSkip = NewPayload->SampleState.TileIndexes.X != 0 || NewPayload->SampleState.TileIndexes.Y != 1;
			// if (!bSkip)
			{
				InParams.ImageAccumulator->AccumulatePixelData(*SamplePixelData, NewPayload->SampleState.OverlappedOffset, NewPayload->SampleState.OverlappedSubpixelShift,
					NewPayload->SampleState.WeightFunctionX, NewPayload->SampleState.WeightFunctionY);
			}

			const double AccumulateEndTime = FPlatformTime::Seconds();
			const float ElapsedMs = float((AccumulateEndTime - AccumulateBeginTime) * 1000.0f);

			UE_LOG(LogMovieRenderPipeline, VeryVerbose, TEXT("Accumulation time: %8.2fms"), ElapsedMs);

		}

		if (NewPayload->IsLastTile() && NewPayload->IsLastTemporalSample())
		{
			int32 FullSizeX = InParams.ImageAccumulator->PlaneSize.X;
			int32 FullSizeY = InParams.ImageAccumulator->PlaneSize.Y;

			// Now that a tile is fully built and accumulated we can notify the output builder that the
			// data is ready so it can pass that onto the output containers (if needed).
			if (SamplePixelData->GetType() == EImagePixelType::Float32)
			{
				// 32 bit FLinearColor
				TUniquePtr<TImagePixelData<FLinearColor> > FinalPixelData = MakeUnique<TImagePixelData<FLinearColor>>(FIntPoint(FullSizeX, FullSizeY), NewPayload);
				InParams.ImageAccumulator->FetchFinalPixelDataLinearColor(FinalPixelData->Pixels);

				// Send the data to the Output Builder
				InParams.OutputMerger->OnCompleteRenderPassDataAvailable_AnyThread(MoveTemp(FinalPixelData));
			}
			else if (SamplePixelData->GetType() == EImagePixelType::Float16)
			{
				// 32 bit FLinearColor
				TUniquePtr<TImagePixelData<FFloat16Color> > FinalPixelData = MakeUnique<TImagePixelData<FFloat16Color>>(FIntPoint(FullSizeX, FullSizeY), NewPayload);
				InParams.ImageAccumulator->FetchFinalPixelDataHalfFloat(FinalPixelData->Pixels);

				// Send the data to the Output Builder
				InParams.OutputMerger->OnCompleteRenderPassDataAvailable_AnyThread(MoveTemp(FinalPixelData));
			}
			else if (SamplePixelData->GetType() == EImagePixelType::Color)
			{
				// 8bit FColors
				TUniquePtr<TImagePixelData<FColor>> FinalPixelData = MakeUnique<TImagePixelData<FColor>>(FIntPoint(FullSizeX, FullSizeY), NewPayload);
				InParams.ImageAccumulator->FetchFinalPixelDataByte(FinalPixelData->Pixels);

				// Send the data to the Output Builder
				InParams.OutputMerger->OnCompleteRenderPassDataAvailable_AnyThread(MoveTemp(FinalPixelData));
			}
			else
			{
				check(0);
			}

			// Free the memory in the accumulator.
			InParams.ImageAccumulator->Reset();
		}
	}
}


TSharedPtr<FAccumulatorPool::FAccumulatorInstance, ESPMode::ThreadSafe> FAccumulatorPool::BlockAndGetAccumulator_GameThread(int32 InFrameNumber, const FMoviePipelinePassIdentifier& InPassIdentifier)
{
	FScopeLock ScopeLock(&CriticalSection);

	int32 AvailableIndex = INDEX_NONE;
	while (AvailableIndex == INDEX_NONE)
	{
		for (int32 Index = 0; Index < Accumulators.Num(); Index++)
		{
			if (InFrameNumber == Accumulators[Index]->ActiveFrameNumber && InPassIdentifier == Accumulators[Index]->ActivePassIdentifier)
			{
				AvailableIndex = Index;
				break;
			}
		}

		if (AvailableIndex == INDEX_NONE)
		{
			// If we don't have an accumulator already working on it let's look for a free one.
			for (int32 Index = 0; Index < Accumulators.Num(); Index++)
			{
				if (!Accumulators[Index]->IsActive())
				{
					// Found a free one, tie it to this output frame.
					Accumulators[Index]->ActiveFrameNumber = InFrameNumber;
					Accumulators[Index]->ActivePassIdentifier = InPassIdentifier;
					Accumulators[Index]->bIsActive = true;
					Accumulators[Index]->TaskPrereq = nullptr;
					AvailableIndex = Index;
					break;
				}
			}
		}
	}

	return Accumulators[AvailableIndex];
}

bool FAccumulatorPool::FAccumulatorInstance::IsActive() const
{
	return bIsActive;
}

void FAccumulatorPool::FAccumulatorInstance::SetIsActive(const bool bInIsActive)
{
	bIsActive = bInIsActive;
}

