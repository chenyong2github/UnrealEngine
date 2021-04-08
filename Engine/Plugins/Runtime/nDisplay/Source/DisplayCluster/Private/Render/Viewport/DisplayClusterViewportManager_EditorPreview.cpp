// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Viewport/DisplayClusterViewportManager.h"

#include "EngineModule.h"
#include "CanvasTypes.h"
#include "LegacyScreenPercentageDriver.h"

#include "SceneView.h"
#include "SceneViewExtension.h"

#include "Engine/Scene.h"
#include "GameFramework/WorldSettings.h"

#include "ScenePrivate.h"

#include "Render/Projection/IDisplayClusterProjectionPolicy.h"
#include "Render/Viewport/DisplayClusterViewport.h"
#include "Render/Viewport/DisplayClusterViewport_CustomPostProcessSettings.h"

#include "Render/Viewport/RenderFrame/DisplayClusterRenderFrame.h"
#include "Render/Viewport/RenderFrame/DisplayClusterRenderFrameSettings.h"

#include "Render/Viewport/Configuration/DisplayClusterViewportConfiguration.h"

#include "Misc/DisplayClusterLog.h"

#include "ClearQuad.h"

///////////////////////////////////////////////////////////////////////////////////////
//          FDisplayClusterViewportManager
///////////////////////////////////////////////////////////////////////////////////////
#if WITH_EDITOR
bool FDisplayClusterViewportManager::UpdatePreviewConfiguration(class UDisplayClusterConfigurationViewportPreview* PreviewConfiguration, UWorld* PreviewWorld, class ADisplayClusterRootActor* InRootActorPtr)
{
	if (CurrentScene != PreviewWorld)
	{
		// Handle end current scene
		if (CurrentScene)
		{
			EndScene();
		}

		// Handle begin new scene
		if (PreviewWorld)
		{
			StartScene(PreviewWorld);
		}
	}

	Configuration->SetRootActor(InRootActorPtr);
	return Configuration->UpdatePreviewConfiguration(PreviewConfiguration);
}

bool FDisplayClusterViewportManager::RenderPreview(class FDisplayClusterRenderFrame& InPreviewRenderFrame)
{
	if (CurrentScene == nullptr)
	{
		return false;
	}

	FSceneInterface* PreviewScene = CurrentScene->Scene;

	bool bHasRender = false;

	FEngineShowFlags EngineShowFlags = FEngineShowFlags(EShowFlagInitMode::ESFIM_Game);

	//Experimental code from render team, now always disabled
	const bool bIsRenderedImmediatelyAfterAnotherViewFamily = false;

	for (FDisplayClusterRenderFrame::FFrameRenderTarget& RenderTargetIt : InPreviewRenderFrame.RenderTargets)
	{
		// Special flag, allow clear RTT surface only for first family
		bool AdditionalViewFamily = false;

		for (FDisplayClusterRenderFrame::FFrameViewFamily& ViewFamiliesIt : RenderTargetIt.ViewFamilies)
		{
			if (ViewFamiliesIt.NumViewsForRender > 0)
			{
				FRenderTarget* DstResource = RenderTargetIt.RenderTargetPtr;
				// Create the view family for rendering the world scene to the viewport's render target
				FSceneViewFamilyContext ViewFamily(
					FSceneViewFamily::ConstructionValues(DstResource, PreviewScene->GetRenderScene(), EngineShowFlags)
					.SetResolveScene(true)
					.SetRealtimeUpdate(true)
					.SetAdditionalViewFamily(AdditionalViewFamily));

				ConfigureViewFamily(RenderTargetIt, ViewFamiliesIt, ViewFamily);

				// Disable clean op for all next families on this render target
				AdditionalViewFamily = true;

				for (FDisplayClusterRenderFrame::FFrameView& ViewIt : ViewFamiliesIt.Views)
				{
					if (ViewIt.bDisableRender == false)
					{
						FDisplayClusterViewport* pViewport = static_cast<FDisplayClusterViewport*>(ViewIt.Viewport);

						check(pViewport != nullptr);
						check(ViewIt.ContextNum < (uint32)pViewport->Contexts.Num());

						// Calculate the player's view information.
						FVector  ViewLocation;
						FRotator ViewRotation;
						FSceneView* View = pViewport->ImplCalcScenePreview(ViewFamily, ViewIt.ContextNum);

						if (View)
						{
							// Apply viewport context settings to view (crossGPU, visibility, etc)
							ViewIt.Viewport->SetupSceneView(ViewIt.ContextNum, PreviewScene->GetWorld(), ViewFamily , *View);
						}
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
		// void FDisplayClusterDeviceBase::RenderFrame_RenderThread(FRHICommandListImmediate& RHICmdList)
		ENQUEUE_RENDER_COMMAND(UDisplayClusterViewport_PreviewRenderFrame)(
			[PreviewViewportManager = this, bWarpBlendEnabled = InPreviewRenderFrame.bWarpBlendEnabled](FRHICommandListImmediate& RHICmdList)
		{
			// Preview not used MGPU: skip cross-gpu transfers
			// PreviewViewportManager->DoCrossGPUTransfers_RenderThread(MainViewport, RHICmdList);

			// Update viewports resources: overlay, vp-overla, blur, nummips, etc
			PreviewViewportManager->UpdateDeferredResources_RenderThread(RHICmdList);

			// Update the frame resources: post-processing, warping, and finally resolving everything to the frame resource
			PreviewViewportManager->UpdateFrameResources_RenderThread(RHICmdList, bWarpBlendEnabled);

			// Preview not use frame resource, instead directly resolved to external RTT resource
			// PreviewViewportManager->ResolveFrameTargetToBackBuffer_RenderThread(RHICmdList, 0, 0, SeparateRTT, SeparateRTT->GetSizeXY());
		});


		return true;
	}

	return false;
}

#endif
