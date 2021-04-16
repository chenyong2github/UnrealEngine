// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Viewport/DisplayClusterViewportManager.h"

#include "IDisplayCluster.h"
#include "Render/IDisplayClusterRenderManager.h"
#include "Render/Projection/IDisplayClusterProjectionPolicyFactory.h"
#include "Render/Projection/IDisplayClusterProjectionPolicy.h"

//#include "DisplayClusterConfigurationTypes.h"

#include "Render/Viewport/DisplayClusterViewport.h"
#include "Render/Viewport/DisplayClusterViewportProxy.h"
#include "Render/Viewport/DisplayClusterViewport_CustomPostProcessSettings.h"
#include "Render/Viewport/RenderFrame/DisplayClusterRenderFrameManager.h"
#include "Render/Viewport/RenderTarget/DisplayClusterRenderTargetManager.h"
#include "Render/Viewport/RenderTarget/DisplayClusterRenderTargetResource.h"
#include "Render/Viewport/DisplayClusterViewportStrings.h"
#include "Render/Viewport/DisplayClusterViewportManager_PostProcess.h"

#include "Render/Viewport/Configuration/DisplayClusterViewportConfiguration.h"
#include "Render/Viewport/Configuration/DisplayClusterViewportConfigurationBase.h"

#include "SceneViewExtension.h"

#include "DisplayClusterRootActor.h" 

#include "Misc\DisplayClusterLog.h"

///////////////////////////////////////////////////////////////////////////////////////
//          FDisplayClusterViewportManager
///////////////////////////////////////////////////////////////////////////////////////

FDisplayClusterViewportManager::FDisplayClusterViewportManager()
{
	Configuration       = MakeUnique<FDisplayClusterViewportConfiguration>(*this);
	RenderTargetManager = MakeUnique<FDisplayClusterRenderTargetManager>();
	RenderFrameManager  = MakeUnique<FDisplayClusterRenderFrameManager>();
	PostProcessManager  = MakeUnique<FDisplayClusterViewportManager_PostProcess>(*this);
}

UWorld* FDisplayClusterViewportManager::GetWorld() const
{
	return CurrentScene;
}

ADisplayClusterRootActor* FDisplayClusterViewportManager::GetRootActor() const
{
	return Configuration->GetRootActor();
}

bool FDisplayClusterViewportManager::IsSceneOpened() const
{
	check(IsInGameThread());

	return CurrentScene != nullptr;
}

void FDisplayClusterViewportManager::StartScene(UWorld* InWorld)
{
	check(IsInGameThread());

	CurrentScene = InWorld;

	for (FDisplayClusterViewport* Viewport : Viewports)
	{
		if (Viewport)
		{
			Viewport->HandleStartScene();
		}
	}
}

void FDisplayClusterViewportManager::EndScene()
{
	check(IsInGameThread());

	for (FDisplayClusterViewport* Viewport : Viewports)
	{
		if (Viewport)
		{
			Viewport->HandleEndScene();
		}
	}

	CurrentScene = nullptr;
}

void FDisplayClusterViewportManager::ResetScene()
{
	check(IsInGameThread());

	for (FDisplayClusterViewport* Viewport : Viewports)
	{
		if (Viewport)
		{
			Viewport->HandleEndScene();
			Viewport->HandleStartScene();
		}
	}
}

bool FDisplayClusterViewportManager::UpdateConfiguration(EDisplayClusterRenderFrameMode InRenderMode, const FString& InClusterNodeId, ADisplayClusterRootActor* InRootActorPtr)
{
	if (InRootActorPtr)
	{
		bool bIsRootActorChanged = Configuration->SetRootActor(InRootActorPtr);

		// When the root actor changes, we have to ResetScene() to reinitialize the internal references of the projection policy.
		if (bIsRootActorChanged)
		{
			ResetScene();
		}

		return Configuration->UpdateConfiguration(InRenderMode, InClusterNodeId);
	}

	return false;
}

bool FDisplayClusterViewportManager::BeginNewFrame(class FViewport* InViewport, FDisplayClusterRenderFrame& OutRenderFrame)
{
	check(IsInGameThread());

	// generate unique stereopass for each frame
	uint32 ViewPassNum = 0;

	// Initialize viewports from new render settings, and create new contexts, reset prev frame resources
	for (FDisplayClusterViewport* Viewport : Viewports)
	{
		if (Viewport->UpdateFrameContexts(ViewPassNum, Configuration->GetRenderFrameSettings()))
		{
			Viewport->UpdateViewExtensions(InViewport);

			ViewPassNum += Viewport->Contexts.Num();
		}
	}

	// Build new frame structure
	if (!RenderFrameManager->BuildRenderFrame(InViewport, Configuration->GetRenderFrameSettings(), Viewports, OutRenderFrame))
	{
		return false;
	}

	// Allocate resources for frame
	if (!RenderTargetManager->AllocateRenderFrameResources(InViewport, Configuration->GetRenderFrameSettings(), Viewports, OutRenderFrame))
	{
		return false;
	}

	// Send updated viewports data to render thread proxy
	for (FDisplayClusterViewport* Viewport : Viewports)
	{
		Viewport->UpdateSceneProxyData();
	}

	OutRenderFrame.UpdateDesiredNumberOfViews();

	return true;
}

void FDisplayClusterViewportManager::FinalizeNewFrame()
{
	check(IsInGameThread());

	// When all viewports processed, we remove all single frame custom postprocess
	for (FDisplayClusterViewport* Viewport : Viewports)
	{
		Viewport->CustomPostProcessSettings.RemoveAllSingleFramePosprocess();
	}
}

void FDisplayClusterViewportManager::ConfigureViewFamily(const FDisplayClusterRenderFrame::FFrameRenderTarget& InFrameTarget, const FDisplayClusterRenderFrame::FFrameViewFamily& InFrameViewFamily, FSceneViewFamilyContext& ViewFamily)
{
	// Gather Scene View Extensions
	// Scene View Extension activation with ViewportId granularity only works if you have one ViewFamily per ViewportId
	{
		ViewFamily.ViewExtensions = InFrameViewFamily.ViewExtensions;
		for (TSharedRef<ISceneViewExtension, ESPMode::ThreadSafe>& ViewExt : ViewFamily.ViewExtensions)
		{
			ViewExt->SetupViewFamily(ViewFamily);
		}
	}

	ViewFamily.SceneCaptureCompositeMode = ESceneCaptureCompositeMode::SCCM_Overwrite;

	// Setup capture mode:
	{
		switch (InFrameTarget.CaptureMode)
		{
		case EDisplayClusterViewportCaptureMode::Chromakey:
		case EDisplayClusterViewportCaptureMode::Lightcard:
			ViewFamily.SceneCaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
			ViewFamily.bResolveScene = false;

			ViewFamily.EngineShowFlags.PostProcessing = 0;

			ViewFamily.EngineShowFlags.SetAtmosphere(0);
			ViewFamily.EngineShowFlags.SetFog(0);
			ViewFamily.EngineShowFlags.SetMotionBlur(0); // motion blur doesn't work correctly with scene captures.
			ViewFamily.EngineShowFlags.SetSeparateTranslucency(0);
			ViewFamily.EngineShowFlags.SetHMDDistortion(0);
			ViewFamily.EngineShowFlags.SetOnScreenDebug(0);
			break;

		default:
			break;
		}
	}
}

void FDisplayClusterViewportManager::UpdateDeferredResources_RenderThread(FRHICommandListImmediate& RHICmdList) const
{
	check(IsInRenderingThread());

	TArray<FDisplayClusterViewportProxy*> OverridedViewports;
	OverridedViewports.Reserve(ViewportProxies.Num());

	for (FDisplayClusterViewportProxy* ViewportProxy : ViewportProxies)
	{
		if (ViewportProxy->RenderSettings.OverrideViewportId.IsEmpty())
		{
			ViewportProxy->UpdateDeferredResources(RHICmdList);
		}
		else
		{
			// Update after all
			OverridedViewports.Add(ViewportProxy);
		}
	}

	// Update deffered viewports after all
	for (FDisplayClusterViewportProxy* ViewportProxy : OverridedViewports)
	{
		ViewportProxy->UpdateDeferredResources(RHICmdList);
	}
}

void FDisplayClusterViewportManager::UpdateFrameResources_RenderThread(FRHICommandListImmediate& RHICmdList, bool bWarpBlendEnabled) const
{
	check(IsInRenderingThread());

	// Do postprocess before warp&blend
	PostProcessManager->PerformPostProcessBeforeWarpBlend_RenderThread(RHICmdList);

	// Update deffered resources and collect viewports for WarpBlend
	TArray<IDisplayClusterViewportProxy*> WarpBlendViewports;
	{
		for (IDisplayClusterViewportProxy* ViewportProxy : GetViewports_RenderThread())
		{
			if (ViewportProxy)
			{
				// Iterate over visible viewports:
				if (ViewportProxy->GetRenderSettings_RenderThread().bVisible)
				{
					const TSharedPtr<class IDisplayClusterProjectionPolicy>& PrjPolicy = ViewportProxy->GetProjectionPolicy_RenderThread();
					if (bWarpBlendEnabled && PrjPolicy.IsValid() && PrjPolicy->IsWarpBlendSupported())
					{
						WarpBlendViewports.Add(ViewportProxy);
					}
					else
					{
						// just resolve not warped viewports to frame target texture
						ViewportProxy->ResolveResources(RHICmdList, EDisplayClusterViewportResourceType::InputShaderResource, ViewportProxy->GetOutputResourceType());
					}
				}
			}
		}
	}

	// Perform warp&blend
	if (WarpBlendViewports.Num() > 0)
	{
		// Colllect warped viewports:

		// Handle warped viewport projection policy logic in 3 pass:
		for (int WarpPass = 0; WarpPass < 3; WarpPass++)
		{
			for (IDisplayClusterViewportProxy* ViewportProxy : WarpBlendViewports)
			{
				const TSharedPtr<class IDisplayClusterProjectionPolicy>& PrjPolicy = ViewportProxy->GetProjectionPolicy_RenderThread();
				switch (WarpPass)
				{
				case 0:
					PrjPolicy->BeginWarpBlend_RenderThread(RHICmdList, ViewportProxy);
					break;

				case 1:
					PrjPolicy->ApplyWarpBlend_RenderThread(RHICmdList, ViewportProxy);
					break;

				case 2:
					PrjPolicy->EndWarpBlend_RenderThread(RHICmdList, ViewportProxy);
					break;

				default:
					break;
				}
			}
		}
	}

	PostProcessManager->PerformPostProcessAfterWarpBlend_RenderThread(RHICmdList);
}

static TAutoConsoleVariable<int32> CVarCrossGPUTransfersEnabled(
	TEXT("nDisplay.render.CrossGPUTransfers"),
	1,
	TEXT("(0 = disabled)\n"),
	ECVF_RenderThreadSafe
);

#include "RHIContext.h"

void FDisplayClusterViewportManager::DoCrossGPUTransfers_RenderThread(class FViewport* InViewport, FRHICommandListImmediate& RHICmdList) const
{
	check(IsInRenderingThread());

	if ((CVarCrossGPUTransfersEnabled.GetValueOnRenderThread() == 0))
	{
		return;
	}

#if WITH_MGPU
	// Copy the view render results to all GPUs that are native to the viewport.
	TArray<FTransferTextureParams> TransferResources;

	for (FDisplayClusterViewportProxy* ViewportProxy : ViewportProxies)
	{
		for (FDisplayClusterViewport_Context& ViewportContext : ViewportProxy->Contexts)
		{
			if (ViewportContext.bAllowGPUTransferOptimization && ViewportContext.GPUIndex >= 0)
			{
				// Use optimized cross GPU transfer for this context

				FRenderTarget* RenderTarget = ViewportProxy->RenderTargets[ViewportContext.ContextNum];
				FRHITexture2D* TextureRHI = ViewportProxy->RenderTargets[ViewportContext.ContextNum]->GetRenderTargetTexture();

				FRHIGPUMask RenderTargetGPUMask = (GNumExplicitGPUsForRendering > 1 && RenderTarget) ? RenderTarget->GetGPUMask(RHICmdList) : FRHIGPUMask::GPU0();
				{
					static auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PathTracing.GPUCount"));
					if (CVar && CVar->GetInt() > 1)
					{
						RenderTargetGPUMask = FRHIGPUMask::All(); // Broadcast to all GPUs 
					}
				}

				FRHIGPUMask ContextGPUMask = FRHIGPUMask::FromIndex(ViewportContext.GPUIndex);

				if (ContextGPUMask != RenderTargetGPUMask)
				{
					// Clamp the view rect by the rendertarget rect to prevent issues when resizing the viewport.
					const FIntRect TransferRect = ViewportContext.RenderTargetRect;

					if (TransferRect.Width() > 0 && TransferRect.Height() > 0)
					{
						for (uint32 RenderTargetGPUIndex : RenderTargetGPUMask)
						{
							if (!ContextGPUMask.Contains(RenderTargetGPUIndex))
							{
								FTransferTextureParams ResourceParams(TextureRHI, TransferRect, ContextGPUMask.GetFirstIndex(), RenderTargetGPUIndex, true, ViewportContext.bEnabledGPUTransferLockSteps);
								TransferResources.Add(ResourceParams);
							}
						}
					}
				}
			}
		}
	}

	if (TransferResources.Num() > 0)
	{
		RHICmdList.TransferTextures(TransferResources);
	}

#endif // WITH_MGPU
}

bool FDisplayClusterViewportManager::GetFrameTargets_RenderThread(TArray<FRHITexture2D*>& OutFrameResources, TArray<FIntPoint>& OutTargetOffsets, TArray<FRHITexture2D*>* OutAdditionalFrameResources) const
{
	check(IsInRenderingThread());

	// Get any defined frame targets from first visible viewport
	for (FDisplayClusterViewportProxy* ViewportProxy : ViewportProxies)
	{
		if (ViewportProxy)
		{
			const TArray<FDisplayClusterTextureResource*>& Frames = ViewportProxy->OutputFrameTargetableResources;
			const TArray<FDisplayClusterTextureResource*>& AdditionalFrames = ViewportProxy->AdditionalFrameTargetableResources;

			if (Frames.Num() > 0)
			{
				OutFrameResources.Reserve(Frames.Num());
				OutTargetOffsets.Reserve(Frames.Num());

				bool bUseAdditionalFrameResources = (OutAdditionalFrameResources != nullptr) && (AdditionalFrames.Num() == Frames.Num());

				if (bUseAdditionalFrameResources)
				{
					OutAdditionalFrameResources->AddZeroed(AdditionalFrames.Num());
				}

				for (int32 FrameIt = 0; FrameIt < Frames.Num(); FrameIt++)
				{
					OutFrameResources.Add(Frames[FrameIt]->GetTextureResource());
					OutTargetOffsets.Add(Frames[FrameIt]->BackbufferFrameOffset);

					if (bUseAdditionalFrameResources)
					{
						(*OutAdditionalFrameResources)[FrameIt] = AdditionalFrames[FrameIt]->GetTextureResource();
					}
				}

				return true;
			}
		}
	}

	// no visible viewports
	return false;
}

bool FDisplayClusterViewportManager::ResolveFrameTargetToBackBuffer_RenderThread(FRHICommandListImmediate& RHICmdList, const uint32 InContextNum, const int DestArrayIndex, FRHITexture2D* DestTexture, FVector2D WindowSize) const
{
	check(IsInRenderingThread());

	TArray<FRHITexture2D*>   FrameResources;
	TArray<FIntPoint>        TargetOffsets;
	if (GetFrameTargets_RenderThread(FrameResources, TargetOffsets))
	{
		// Use internal frame textures as source
		int ContextNum = InContextNum;

		FRHITexture2D* FrameTexture = FrameResources[ContextNum];
		FIntPoint DstOffset = TargetOffsets[ContextNum];

		if (FrameTexture)
		{
			const FIntPoint SrcSize = FrameTexture->GetSizeXY();
			const FIntPoint DstSize = DestTexture->GetSizeXY();;

			FIntRect DstRect(DstOffset, DstOffset + SrcSize);

			// Fit to backbuffer size
			DstRect.Max.X = FMath::Min(DstSize.X, DstRect.Max.X);
			DstRect.Max.Y = FMath::Min(DstSize.Y, DstRect.Max.Y);

			FResolveParams CopyParams;

			CopyParams.SourceArrayIndex = 0;
			CopyParams.DestArrayIndex = DestArrayIndex;

			CopyParams.Rect.X1 = 0;
			CopyParams.Rect.Y1 = 0;
			CopyParams.Rect.X2 = DstRect.Width();
			CopyParams.Rect.Y2 = DstRect.Height();

			CopyParams.DestRect.X1 = DstRect.Min.X;
			CopyParams.DestRect.Y1 = DstRect.Min.Y;
			CopyParams.DestRect.X2 = DstRect.Max.X;
			CopyParams.DestRect.Y2 = DstRect.Max.Y;

			RHICmdList.CopyToResolveTarget(FrameTexture, DestTexture, CopyParams);

			return true;
		}
	}

	return false;
}

bool FDisplayClusterViewportManager::CreateViewport(const FString& InViewportId, const class UDisplayClusterConfigurationViewport* ConfigurationViewport)
{
	check(IsInGameThread());
	check(ConfigurationViewport != nullptr);

	// Check viewport ID
	if (InViewportId.IsEmpty())
	{
		UE_LOG(LogDisplayClusterViewport, Warning, TEXT("Wrong viewport ID"));
		return false;
	}

	// ID must be unique
	if (FindViewport(InViewportId) != nullptr)
	{
		UE_LOG(LogDisplayClusterViewport, Warning, TEXT("Viewport '%s' already exists"), *InViewportId);
		return false;
	}

	// Create projection policy for viewport
	TSharedPtr<IDisplayClusterProjectionPolicy> NewProjectionPolicy = CreateProjectionPolicy(InViewportId, &ConfigurationViewport->ProjectionPolicy);
	if (NewProjectionPolicy.IsValid())
	{
		// Create viewport for new projection policy
		FDisplayClusterViewport* NewViewport = ImplCreateViewport(InViewportId, NewProjectionPolicy);
		if (NewViewport != nullptr)
		{
			FDisplayClusterViewportConfigurationBase::UpdateViewportConfiguration(*this, NewViewport, ConfigurationViewport);
			return true;
		}
	}

	UE_LOG(LogDisplayClusterViewport, Error, TEXT("Viewports '%s' not created."), *InViewportId);
	return false;
}

FDisplayClusterViewport* FDisplayClusterViewportManager::ImplFindViewport(const FString& ViewportId) const
{
	check(IsInGameThread());

	// Ok, we have a request for a particular viewport. Let's find it.
	FDisplayClusterViewport* const* DesiredViewport = Viewports.FindByPredicate([ViewportId](const FDisplayClusterViewport* ItemViewport)
	{
		return ViewportId.Equals(ItemViewport->ViewportId, ESearchCase::IgnoreCase);
	});

	return (DesiredViewport != nullptr) ? *DesiredViewport : nullptr;
}

FDisplayClusterViewportProxy* FDisplayClusterViewportManager::ImplFindViewport_RenderThread(const FString& ViewportId) const
{
	check(IsInRenderingThread());

	// Ok, we have a request for a particular viewport. Let's find it.
	FDisplayClusterViewportProxy* const* DesiredViewport = ViewportProxies.FindByPredicate([ViewportId](const FDisplayClusterViewportProxy* ItemViewport)
	{
		return ViewportId.Equals(ItemViewport->ViewportId, ESearchCase::IgnoreCase);
	});

	return (DesiredViewport != nullptr) ? *DesiredViewport : nullptr;
}

IDisplayClusterViewport* FDisplayClusterViewportManager::CreateViewport(const FString& ViewportId, TSharedPtr<class IDisplayClusterProjectionPolicy> InProjectionPolicy)
{
	check(IsInGameThread());

	FDisplayClusterViewport* ExistViewport = ImplFindViewport(ViewportId);
	if (ExistViewport != nullptr)
	{
		//add error log: Viewport with name '%s' already exist
		return nullptr;
	}

	return ImplCreateViewport(ViewportId, InProjectionPolicy);
}

FDisplayClusterViewport* FDisplayClusterViewportManager::ImplCreateViewport(const FString& ViewportId, TSharedPtr<class IDisplayClusterProjectionPolicy> InProjectionPolicy)
{
	check(IsInGameThread());

	check(InProjectionPolicy.IsValid())

	// Create viewport
	FDisplayClusterViewport* NewViewport = new FDisplayClusterViewport(*this, ViewportId, InProjectionPolicy);

	// Add viewport on gamethread
	Viewports.Add(NewViewport);

	// Handle start scene for viewport
	NewViewport->HandleStartScene();

	// Add viewport sceneproxy on renderthread
	ENQUEUE_RENDER_COMMAND(CreateDisplayClusterViewportProxy)(
		[ViewportManager = this, ViewportProxy = NewViewport->ViewportProxy](FRHICommandListImmediate& RHICmdList)
	{
		ViewportManager->ViewportProxies.Add(ViewportProxy);
	});

	return NewViewport;
}

bool FDisplayClusterViewportManager::DeleteViewport(const FString& ViewportId)
{
	check(IsInGameThread());

	FDisplayClusterViewport* ExistViewport = ImplFindViewport(ViewportId);
	if (ExistViewport != nullptr)
	{
		ImplDeleteViewport(ExistViewport);
		return true;
	}

	return false;
}

void FDisplayClusterViewportManager::ImplDeleteViewport(FDisplayClusterViewport* ExistViewport)
{
	// Handle projection policy event
	ExistViewport->ProjectionPolicy.Reset();
	ExistViewport->UninitializedProjectionPolicy.Reset();

	// Remove viewport sceneproxy on renderthread
	ENQUEUE_RENDER_COMMAND(DeleteDisplayClusterViewportProxy)(
		[ViewportManager = this, ViewportProxy = ExistViewport->ViewportProxy](FRHICommandListImmediate& RHICmdList)
	{
		// Remove viewport obj from manager
		int ViewportProxyIndex = ViewportManager->ViewportProxies.Find(ViewportProxy);
		if (ViewportProxyIndex != INDEX_NONE)
		{
			ViewportManager->ViewportProxies[ViewportProxyIndex] = nullptr;
			ViewportManager->ViewportProxies.RemoveAt(ViewportProxyIndex);
		}

		delete ViewportProxy;
	});

	// Remove viewport obj from manager
	int ViewportIndex = Viewports.Find(ExistViewport);
	if (ViewportIndex != INDEX_NONE)
	{
		Viewports[ViewportIndex] = nullptr;
		Viewports.RemoveAt(ViewportIndex);
	}

	delete ExistViewport;
}

IDisplayClusterViewport* FDisplayClusterViewportManager::FindViewport(const enum EStereoscopicPass StereoPassType, uint32* OutContextNum) const
{
	check(IsInGameThread());

	for (FDisplayClusterViewport* Viewport : Viewports)
	{
		if (Viewport && Viewport->FindContext(StereoPassType, OutContextNum))
		{
			return Viewport;
		}
	}

	// Viewport not found
	return nullptr;
}

IDisplayClusterViewportProxy* FDisplayClusterViewportManager::FindViewport_RenderThread(const enum EStereoscopicPass StereoPassType, uint32* OutContextNum) const
{
	check(IsInRenderingThread());

	for (FDisplayClusterViewportProxy* ViewportProxy : ViewportProxies)
	{
		if (ViewportProxy && ViewportProxy->FindContext_RenderThread(StereoPassType, OutContextNum))
		{
			return ViewportProxy;
		}
	}

	// Viewport proxy not found
	return nullptr;
}


TSharedPtr<IDisplayClusterProjectionPolicy> FDisplayClusterViewportManager::CreateProjectionPolicy(const FString& InViewportId, const FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy)
{
	check(IsInGameThread());
	check(InConfigurationProjectionPolicy != nullptr);

	// Generate unique projection policy id from viewport name
	const FString ProjectionPolicyId = FString::Printf(TEXT("%s_%s"), DisplayClusterViewportStrings::prefix::projection, *InViewportId);

	IDisplayClusterRenderManager* const DCRenderManager = IDisplayCluster::Get().GetRenderMgr();
	check(DCRenderManager);

	TSharedPtr<IDisplayClusterProjectionPolicyFactory> ProjPolicyFactory = DCRenderManager->GetProjectionPolicyFactory(InConfigurationProjectionPolicy->Type);
	if (ProjPolicyFactory.IsValid())
	{
		TSharedPtr<IDisplayClusterProjectionPolicy> ProjPolicy = ProjPolicyFactory->Create(ProjectionPolicyId, InConfigurationProjectionPolicy);
		if (ProjPolicy.IsValid())
		{
			return ProjPolicy;
		}
		else
		{
			FString RHIName = GDynamicRHI->GetName();
			UE_LOG(LogDisplayClusterViewport, Warning, TEXT("Invalid projection policy: type '%s', RHI '%s', viewport '%s'"), *InConfigurationProjectionPolicy->Type, *RHIName, *ProjectionPolicyId);
		}
	}
	else
	{
		UE_LOG(LogDisplayClusterViewport, Warning, TEXT("No projection factory found for projection type '%s'"), *InConfigurationProjectionPolicy->Type);
	}

	return nullptr;
}

