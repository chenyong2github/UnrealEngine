// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Viewport/DisplayClusterViewportManager.h"
#include "Render/Viewport/DisplayClusterViewportManagerProxy.h"

#if WITH_EDITOR

#include "EngineModule.h"
#include "CanvasTypes.h"
#include "LegacyScreenPercentageDriver.h"

#include "SceneView.h"
#include "SceneViewExtension.h"

#include "Engine/Scene.h"
#include "GameFramework/WorldSettings.h"

#include "Render/Projection/IDisplayClusterProjectionPolicy.h"
#include "Render/Viewport/DisplayClusterViewport.h"
#include "Render/Viewport/DisplayClusterViewport_CustomPostProcessSettings.h"

#include "Render/Viewport/RenderFrame/DisplayClusterRenderFrame.h"
#include "Render/Viewport/RenderFrame/DisplayClusterRenderFrameSettings.h"

#include "Render/Viewport/Configuration/DisplayClusterViewportConfiguration.h"

#include "Misc/DisplayClusterLog.h"
#include "DisplayClusterRootActor.h" 

#include "ClearQuad.h"

///////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterViewportManager
///////////////////////////////////////////////////////////////////////////////////////

void FDisplayClusterViewportManager::ImplUpdatePreviewRTTResources()
{
	check(IsInGameThread());

	// Only for preview modes:
	switch (Configuration->GetRenderFrameSettingsConstRef().RenderMode)
	{
	case EDisplayClusterRenderFrameMode::PreviewMono:
		break;
	default:
		return;
	}

	// The preview RTT created inside root actor
	ADisplayClusterRootActor* RootActor = GetRootActor();
	if (RootActor == nullptr)
	{
		return;
	}

	TArray<FString>        PreviewViewportNames;
	TArray<FTextureRHIRef> PreviewRenderTargetableTextures;

	PreviewViewportNames.Reserve(Viewports.Num());
	PreviewRenderTargetableTextures.Reserve(Viewports.Num());

	// Collect visible and enabled viewports for preview rendering:
	for (FDisplayClusterViewport* const ViewportIt : Viewports)
	{
		if (ViewportIt->RenderSettings.bEnable && ViewportIt->RenderSettings.bVisible)
		{
			PreviewViewportNames.Add(ViewportIt->GetId());
			PreviewRenderTargetableTextures.AddDefaulted();
		}
	}

	// Get all supported preview rtt resources from root actor
	RootActor->GetPreviewRenderTargetableTextures(PreviewViewportNames, PreviewRenderTargetableTextures);

	// Configure preview RTT to viwports:
	for (int32 ViewportIndex = 0; ViewportIndex < PreviewViewportNames.Num(); ViewportIndex++)
	{
		FDisplayClusterViewport* DesiredViewport = ImplFindViewport(PreviewViewportNames[ViewportIndex]);
		if (DesiredViewport)
		{
			FTextureRHIRef& PreviewRTT = PreviewRenderTargetableTextures[ViewportIndex];
			if (PreviewRTT.IsValid())
			{
				// Use mapped preview viewport
				DesiredViewport->OutputPreviewTargetableResource = PreviewRTT;
			}
			else
			{
				// disable visible, but unused by root actor viewports
				DesiredViewport->RenderSettings.bEnable = false;
			}
		}
	}
}

bool FDisplayClusterViewportManager::UpdatePreviewConfiguration(const FDisplayClusterConfigurationViewportPreview& PreviewConfiguration, class ADisplayClusterRootActor* InRootActorPtr)
{
	Configuration->SetRootActor(InRootActorPtr);
	return Configuration->UpdatePreviewConfiguration(PreviewConfiguration);
}

bool FDisplayClusterViewportManager::RenderInEditor(class FDisplayClusterRenderFrame& InRenderFrame, FViewport* InViewport)
{
	UWorld* CurrentWorld = GetCurrentWorld();
	if (CurrentWorld == nullptr)
	{
		return false;
	}

	FSceneInterface* PreviewScene = CurrentWorld->Scene;

	bool bHasRender = false;

	FEngineShowFlags EngineShowFlags = FEngineShowFlags(EShowFlagInitMode::ESFIM_Game);

	//Experimental code from render team, now always disabled
	const bool bIsRenderedImmediatelyAfterAnotherViewFamily = false;

	for (FDisplayClusterRenderFrame::FFrameRenderTarget& RenderTargetIt : InRenderFrame.RenderTargets)
	{
		// Special flag, allow clear RTT surface only for first family
		bool AdditionalViewFamily = false;

		for (FDisplayClusterRenderFrame::FFrameViewFamily& ViewFamiliesIt : RenderTargetIt.ViewFamilies)
		{
			{
				FRenderTarget* DstResource = RenderTargetIt.RenderTargetPtr;
				// Create the view family for rendering the world scene to the viewport's render target
				FSceneViewFamilyContext ViewFamily(
					FSceneViewFamily::ConstructionValues(DstResource, PreviewScene, EngineShowFlags)
					.SetResolveScene(true)
					.SetRealtimeUpdate(true)
					.SetAdditionalViewFamily(AdditionalViewFamily));

				ConfigureViewFamily(RenderTargetIt, ViewFamiliesIt, ViewFamily);

				// Disable clean op for all next families on this render target
				AdditionalViewFamily = true;

				for (FDisplayClusterRenderFrame::FFrameView& ViewIt : ViewFamiliesIt.Views)
				{
					FDisplayClusterViewport* ViewportPtr = static_cast<FDisplayClusterViewport*>(ViewIt.Viewport);

					check(ViewportPtr != nullptr);
					check(ViewIt.ContextNum < (uint32)ViewportPtr->Contexts.Num());

					// Calculate the player's view information.
					FVector  ViewLocation;
					FRotator ViewRotation;
					FSceneView* View = ViewportPtr->ImplCalcScenePreview(ViewFamily, ViewIt.ContextNum);

					if (View && ViewIt.bDisableRender)
					{
						ViewFamily.Views.Remove(View);

						delete View;
						View = nullptr;
					}

					if (View)
					{
						// Apply viewport context settings to view (crossGPU, visibility, etc)
						ViewIt.Viewport->SetupSceneView(ViewIt.ContextNum, PreviewScene->GetWorld(), ViewFamily, *View);
					}
				}

				if (ViewFamily.Views.Num() > 0)
				{
					// Screen percentage is still not supported in scene capture.
					ViewFamily.EngineShowFlags.ScreenPercentage = false;
					ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(ViewFamily, 1.0f, false));

					ViewFamily.bIsRenderedImmediatelyAfterAnotherViewFamily = bIsRenderedImmediatelyAfterAnotherViewFamily;

					FCanvas Canvas(DstResource, nullptr, PreviewScene->GetWorld(), ERHIFeatureLevel::SM5, FCanvas::CDM_DeferDrawing /*FCanvas::CDM_ImmediateDrawing*/, 1.0f);
					Canvas.Clear(FLinearColor::Black);

					GetRendererModule().BeginRenderingViewFamily(&Canvas, &ViewFamily);
					bHasRender = true;
				}
			}
		}
	}

	if (bHasRender)
	{
		// Handle special viewports game-thread logic at frame end
		// custom postprocess single frame flag must be removed at frame end on game thread
		FinalizeNewFrame();

		// After all render target rendered call nDisplay frame rendering:
		RenderFrame(InViewport);

		return true;
	}

	return false;
}

#endif
