// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Viewport/RenderFrame/DisplayClusterRenderFrameManager.h"

#include "Render/Viewport/DisplayClusterViewport.h"

#include "Render/Viewport/RenderFrame/DisplayClusterRenderFrame.h"
#include "Render/Viewport/RenderFrame/DisplayClusterRenderFrameSettings.h"


///////////////////////////////////////////////////////////////
// FDisplayClusterRenderTargetFrame
///////////////////////////////////////////////////////////////
bool FDisplayClusterRenderFrameManager::BuildRenderFrame(FViewport* InViewport, const FDisplayClusterRenderFrameSettings& InRenderFrameSettings, const TArray<FDisplayClusterViewport*>& InViewports, FDisplayClusterRenderFrame& OutRenderFrame)
{

	switch (InRenderFrameSettings.RenderMode)
	{
	case EDisplayClusterRenderFrameMode::PreviewMono:
		// Dont use render frame for preview
		break;
	default:
		if (!FindFrameTargetRect(InViewport, InViewports, InRenderFrameSettings, OutRenderFrame.FrameRect))
		{
			return false;
		}
		break;
	}

	// @todo add atlasing, merge multiple viewports in single viewfamily, etc
	// now prototype, just simple frame: use separate RTT for each viewport eye

	// Sort viewports, childs after parents
	//@todo save this order inside logic
	TArray<FDisplayClusterViewport*> SortedViewports;
	TArray<FDisplayClusterViewport*> ChildsViewports;

	// First add root viewports
	for (FDisplayClusterViewport* Viewport : InViewports)
	{
		if (Viewport->RenderSettings.GetParentViewportId().IsEmpty())
		{
			SortedViewports.Add(Viewport);
		}
		else
		{
			ChildsViewports.Add(Viewport);
		}
	}

	SortedViewports.Append(ChildsViewports);
	ChildsViewports.Empty();
	
	bool bResult = false;

	if (InRenderFrameSettings.bAllowRenderTargetAtlasing)
	{
// Not implemented yet
#if 0
		switch (InRenderFrameSettings.ViewFamilyMode)
		{
		case EDisplayClusterRenderFamilyMode::AllowMergeForGroups:
			bResult = false;
			break;

		case EDisplayClusterRenderFamilyMode::AllowMergeForGroupsAndStereo:
			//NOT_IMPLEMENTED
			bResult = false;
			break;

		case EDisplayClusterRenderFamilyMode::MergeAnyPossible:
			//NOT_IMPLEMENTED
			bResult = false;
			break;

		default:
			bResult = false;
			break;
		}
#endif
	}
	else
	{
		bResult = BuildSimpleFrame(InViewport, InRenderFrameSettings, SortedViewports, OutRenderFrame);
	}

	if (bResult)
	{
		uint32 RenderFrameViewIndex = 0;

		// Update view index for each context inside view family
		for (FDisplayClusterRenderFrame::FFrameRenderTarget& RenderTargetIt : OutRenderFrame.RenderTargets)
		{
			for (FDisplayClusterRenderFrame::FFrameViewFamily& ViewFamilieIt : RenderTargetIt.ViewFamilies)
			{
				for (FDisplayClusterRenderFrame::FFrameView& ViewIt : ViewFamilieIt.Views)
				{
					FDisplayClusterViewport* ViewportPtr = static_cast<FDisplayClusterViewport*>(ViewIt.Viewport);
					if (ViewportPtr != nullptr)
					{
						ViewportPtr->Contexts[ViewIt.ContextNum].RenderFrameViewIndex = RenderFrameViewIndex++;
					}
				}
			}
		}
	}

	return bResult;
}

bool FDisplayClusterRenderFrameManager::BuildSimpleFrame(FViewport* InViewport, const FDisplayClusterRenderFrameSettings& InRenderFrameSettings, const TArray<FDisplayClusterViewport*>& InViewports, FDisplayClusterRenderFrame& OutRenderFrame)
{
	for (FDisplayClusterViewport* ViewportIt : InViewports)
	{
		if (ViewportIt)
		{
			for (const FDisplayClusterViewport_Context& ContextIt : ViewportIt->GetContexts())
			{
				bool bShouldUseRenderTarget = true;

				FDisplayClusterRenderFrame::FFrameView FrameView;
				{
					FrameView.ContextNum = ContextIt.ContextNum;
					FrameView.Viewport = ViewportIt;
					FrameView.bDisableRender = ContextIt.bDisableRender;

					// Simple frame use unique RTT  for each viewport, so disable RTT when viewport rendering disabled
					if (ContextIt.bDisableRender)
					{
						bShouldUseRenderTarget = false;
					}
				}

					float CustomBufferRatio = ViewportIt->GetRenderSettings().BufferRatio;
					{
						// Global multiplier
						CustomBufferRatio *= InRenderFrameSettings.ClusterBufferRatioMult;

						if ((ViewportIt->RenderSettingsICVFX.RuntimeFlags & ViewportRuntime_ICVFXTarget) != 0)
						{
							// Outer viewport
							CustomBufferRatio *= InRenderFrameSettings.ClusterICVFXOuterViewportBufferRatioMult;
						}
						else
						if ((ViewportIt->RenderSettingsICVFX.RuntimeFlags & ViewportRuntime_ICVFXIncamera) != 0)
						{
							// Inner Frustum
							CustomBufferRatio *= InRenderFrameSettings.ClusterICVFXInnerFrustumBufferRatioMult;
						}
					}

				FDisplayClusterRenderFrame::FFrameViewFamily FrameViewFamily;
				{
					FrameViewFamily.Views.Add(FrameView);
					FrameViewFamily.CustomBufferRatio = CustomBufferRatio;
					FrameViewFamily.ViewExtensions = ViewportIt->GatherActiveExtensions(InViewport);
				}

				FDisplayClusterRenderFrame::FFrameRenderTarget FrameRenderTarget;
				{
					FrameRenderTarget.bShouldUseRenderTarget = bShouldUseRenderTarget;
					FrameRenderTarget.ViewFamilies.Add(FrameViewFamily);

					FrameRenderTarget.RenderTargetSize = ContextIt.RenderTargetRect.Max;
					FrameRenderTarget.CaptureMode = ViewportIt->RenderSettings.CaptureMode;
				}

				OutRenderFrame.RenderTargets.Add(FrameRenderTarget);
			}
		}
	}

	return true;
}

bool FDisplayClusterRenderFrameManager::FindFrameTargetRect(FViewport* InViewport, const TArray<FDisplayClusterViewport*>& InOutViewports, const FDisplayClusterRenderFrameSettings& InRenderFrameSettings, FIntRect& OutFrameTargetRect) const
{
	// Calculate Backbuffer frame
	bool bIsUsed = false;

	if (InRenderFrameSettings.bShouldUseFullSizeFrameTargetableResource)
	{
		// Use full-size frame RTT
		OutFrameTargetRect = FIntRect(FIntPoint(0, 0), InViewport->GetSizeXY());
		bIsUsed = true;
	}

	// Optimize frame target RTT
	for (const FDisplayClusterViewport* ViewportIt : InOutViewports)
	{
		if (ViewportIt && ViewportIt->RenderSettings.bEnable && ViewportIt->RenderSettings.bVisible)
		{
			for (const auto& ContextIt : ViewportIt->GetContexts())
			{
				if (!bIsUsed)
				{
					OutFrameTargetRect = ContextIt.FrameTargetRect;
					bIsUsed = true;
				}
				else
				{
					OutFrameTargetRect.Include(ContextIt.FrameTargetRect.Min);
					OutFrameTargetRect.Include(ContextIt.FrameTargetRect.Max);
				}
			}
		}
	}

	if (OutFrameTargetRect.Width() <= 0 || OutFrameTargetRect.Height() <= 0)
	{
		return false;
	}

	return bIsUsed;
}
