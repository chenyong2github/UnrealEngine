// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Viewport/DisplayClusterViewport.h"
#include "Render/Viewport/DisplayClusterViewportManager.h"
#include "Render/Viewport/DisplayClusterViewportProxy.h"

#include "Render/Viewport/Configuration/DisplayClusterViewportConfiguration.h"

#include "Render/Projection/IDisplayClusterProjectionPolicy.h"

#include "Render/Viewport/DisplayClusterViewportStereoscopicPass.h"
#include "Render/Viewport/RenderFrame/DisplayClusterRenderFrame.h"
#include "Render/Viewport/RenderFrame/DisplayClusterRenderFrameSettings.h"
#include "Render/Viewport/RenderTarget/DisplayClusterRenderTargetResource.h"

#include "Render/Viewport/Containers/DisplayClusterViewport_PostRenderSettings.h"

#include "EngineUtils.h"
#include "SceneView.h"

#include "DisplayClusterSceneViewExtensions.h"

#include "Misc/DisplayClusterLog.h"

///////////////////////////////////////////////////////////////////////////////////////
//          FDisplayClusterViewport
///////////////////////////////////////////////////////////////////////////////////////
FDisplayClusterViewport::FDisplayClusterViewport(FDisplayClusterViewportManager& InOwner, const FString& InViewportId, TSharedPtr<IDisplayClusterProjectionPolicy> InProjectionPolicy)
	: UninitializedProjectionPolicy(InProjectionPolicy)
	, ViewportId(InViewportId)
	, Owner(InOwner)
{
	check(UninitializedProjectionPolicy.IsValid());

	// Create scene proxy pair with on game thread. Outside, in ViewportManager added to proxy array on render thread
	ViewportProxy = new FDisplayClusterViewportProxy(Owner.ImplGetProxy(), *this);
}

FDisplayClusterViewport::~FDisplayClusterViewport()
{
	// ViewportProxy deleted on render thread from FDisplayClusterViewportManagerProxy::ImplDeleteViewport()
}

IDisplayClusterViewportManager& FDisplayClusterViewport::GetOwner() const
{
	return Owner;
}

const TArray<FSceneViewExtensionRef> FDisplayClusterViewport::GatherActiveExtensions(FViewport* InViewport) const
{
	if (InViewport)
	{
		FDisplayClusterSceneViewExtensionContext ViewExtensionContext(InViewport, &Owner, GetId());
		return GEngine->ViewExtensions->GatherActiveExtensions(ViewExtensionContext);
	}
	else
	{
		UWorld* CurrentWorld = Owner.GetCurrentWorld();
		if (CurrentWorld)
		{
			FDisplayClusterSceneViewExtensionContext ViewExtensionContext(CurrentWorld->Scene, &Owner, GetId());
			return GEngine->ViewExtensions->GatherActiveExtensions(ViewExtensionContext);
		}
	}

	return TArray<FSceneViewExtensionRef>();
}

FSceneViewExtensionIsActiveFunctor FDisplayClusterViewport::GetSceneViewExtensionIsActiveFunctor() const
{
	FSceneViewExtensionIsActiveFunctor IsActiveFunction;
	IsActiveFunction.IsActiveFunction = [this](const ISceneViewExtension* SceneViewExtension, const FSceneViewExtensionContext& Context)
	{
		if (Context.IsA(FDisplayClusterSceneViewExtensionContext()))
		{
			const FDisplayClusterSceneViewExtensionContext& DisplayContext = static_cast<const FDisplayClusterSceneViewExtensionContext&>(Context);

			// Find exist viewport by name
			IDisplayClusterViewport* PublicViewport = DisplayContext.ViewportManager->FindViewport(DisplayContext.ViewportId);
			if (PublicViewport)
			{
				FDisplayClusterViewport* Viewport = static_cast<FDisplayClusterViewport*>(PublicViewport);

				if (Viewport->OpenColorIODisplayExtension.IsValid() && Viewport->OpenColorIODisplayExtension.Get() == SceneViewExtension)
				{
					// This viewport use this OCIO extension
					return  TOptional<bool>(true);
				}
			}
		}

		return TOptional<bool>(false);
	};

	return IsActiveFunction;
}

bool FDisplayClusterViewport::HandleStartScene()
{
	bool bResult = true;
	if (Owner.IsSceneOpened())
	{
		if (UninitializedProjectionPolicy.IsValid())
		{
			bResult = UninitializedProjectionPolicy->HandleStartScene(this);
			if (bResult)
			{
				ProjectionPolicy = UninitializedProjectionPolicy;
				UninitializedProjectionPolicy.Reset();
			}
		}
		else 
		{
			// Already Initialized
			if (!ProjectionPolicy.IsValid())
			{
				// No projection policy for this viewport
				UE_LOG(LogDisplayClusterViewport, Error, TEXT("No projection policy assigned for Viewports '%s'."), *GetId());
			}
		}
	}

	return bResult;
}

void FDisplayClusterViewport::HandleEndScene()
{
	if (ProjectionPolicy.IsValid())
	{
		ProjectionPolicy->HandleEndScene(this);
		UninitializedProjectionPolicy = ProjectionPolicy;
		ProjectionPolicy.Reset();
	}
}

bool FDisplayClusterViewport::ShouldUseAdditionalTargetableResource() const
{
	check(IsInGameThread());

	// PostRender Blur require additional RTT for shader
	if (PostRenderSettings.PostprocessBlur.IsEnabled())
	{
		return true;
	}

	// Supoport projection policy additional resource
	if (ProjectionPolicy.IsValid() && ProjectionPolicy->ShouldUseAdditionalTargetableResource())
	{
		return true;
	}

	return false;
}

void FDisplayClusterViewport::SetupSceneView(uint32 ContextNum, class UWorld* World, FSceneViewFamily& InOutViewFamily, FSceneView& InOutView) const
{
	check(IsInGameThread());
	check(ContextNum < (uint32)Contexts.Num());

	// Setup MGPU features:
	if(Contexts[ContextNum].GPUIndex >= 0)
	{
		InOutView.bOverrideGPUMask = true;
		InOutView.GPUMask = FRHIGPUMask::FromIndex(Contexts[ContextNum].GPUIndex);
		InOutView.bAllowCrossGPUTransfer = (Contexts[ContextNum].bAllowGPUTransferOptimization == false);
	}

	// Apply visibility settigns to view
	VisibilitySettings.SetupSceneView(World, InOutView);

	// Handle Motion blur parameters
	CameraMotionBlur.SetupSceneView(Contexts[ContextNum], InOutView);
}

inline void AdjustRect(FIntRect& InOutRect, const float multX, const float multY)
{
	InOutRect.Min.X *= multX;
	InOutRect.Max.X *= multX;
	InOutRect.Min.Y *= multY;
	InOutRect.Max.Y *= multY;
}

bool FDisplayClusterViewport::UpdateFrameContexts(const uint32 InViewPassNum, const FDisplayClusterRenderFrameSettings& InFrameSettings)
{
	check(IsInGameThread());

	// Release old contexts
	Contexts.Empty();

	RenderTargets.Empty();
	OutputFrameTargetableResources.Empty();
	AdditionalFrameTargetableResources.Empty();

#if WITH_EDITOR
	OutputPreviewTargetableResource.SafeRelease();
#endif

	InputShaderResources.Empty();
	AdditionalTargetableResources.Empty();
	MipsShaderResources.Empty();

	if (RenderSettings.bEnable == false)
	{
		// Exclude this viewport from render and logic, but object still exist
		return false;
	}

	if (PostRenderSettings.GenerateMips.bAutoGenerateMips)
	{
		//Check if current projection policy supports this feature
		if (!ProjectionPolicy.IsValid() || !ProjectionPolicy->ShouldUseSourceTextureWithMips())
		{
			// Don't create unused mips texture
			PostRenderSettings.GenerateMips.bAutoGenerateMips = false;
		}
	}

	float RTTSizeMult = RenderSettings.RenderTargetRatio * InFrameSettings.ClusterRenderTargetRatioMult;

	uint32 FrameTargetsAmmount = 2;
	FIntRect     FrameTargetRect = RenderSettings.Rect;
	{
		switch (InFrameSettings.RenderMode)
		{
		case EDisplayClusterRenderFrameMode::Mono:
			FrameTargetsAmmount = 1;
			break;
		case EDisplayClusterRenderFrameMode::SideBySide:
			AdjustRect(FrameTargetRect, 0.5f, 1.f);
			break;
		case EDisplayClusterRenderFrameMode::TopBottom:
			AdjustRect(FrameTargetRect, 1.f, 0.5f);
			break;
		case EDisplayClusterRenderFrameMode::PreviewMono:
		{
			// Apply preview scale
			RTTSizeMult *= InFrameSettings.PreviewRenderTargetRatioMult;
			float MultXY = InFrameSettings.PreviewRenderTargetRatioMult;
			AdjustRect(FrameTargetRect, MultXY, MultXY);

			// Align each frame to zero
			FrameTargetRect = FIntRect(FIntPoint(0, 0), FrameTargetRect.Size());

			// Mono
			FrameTargetsAmmount = 1;
			break;
		}
		default:
			break;
		}
	}

	static int MaximumSupportedResolution = 1 << (GMaxTextureMipCount - 1);

	// Make sure the frame target rect doesn't exceed the maximum resolution, and preserve its aspect ratio if it needs to be clamped
	if (FrameTargetRect.Max.X > MaximumSupportedResolution)
	{
		FrameTargetRect.Max.Y = FrameTargetRect.Max.Y * MaximumSupportedResolution / (float)FrameTargetRect.Max.X;
		FrameTargetRect.Max.X = MaximumSupportedResolution;
	}

	if (FrameTargetRect.Max.Y > MaximumSupportedResolution)
	{
		FrameTargetRect.Max.X = FrameTargetRect.Max.X * MaximumSupportedResolution / (float)FrameTargetRect.Max.Y;
		FrameTargetRect.Max.Y = MaximumSupportedResolution;
	}

	// Special case mono->stereo
	uint32 ViewportContextAmmount = RenderSettings.bForceMono ? 1 : FrameTargetsAmmount;


	FIntPoint ContextSize = FrameTargetRect.Size();


	// Get valid viewport size for render:
	{
		int ViewportResolution = ContextSize.GetMax() * RTTSizeMult;

		// In UE, the maximum texture resolution is computed as:
		if (ViewportResolution > MaximumSupportedResolution)
		{
			RTTSizeMult *= float(ViewportResolution) / MaximumSupportedResolution;
	}

		ContextSize.X = FMath::Min(int(RTTSizeMult * ContextSize.X), MaximumSupportedResolution);
		ContextSize.Y = FMath::Min(int(RTTSizeMult * ContextSize.Y), MaximumSupportedResolution);
	}

	FIntRect RenderTargetRect(FIntPoint(0, 0), ContextSize);

	// Test vs zero size rects
	if (FrameTargetRect.Width() <= 0 || FrameTargetRect.Height() <= 0 || RenderTargetRect.Width() <= 0 || RenderTargetRect.Height() <= 0)
	{
		return false;
	}

	// Support overscan rendering feature
	OverscanRendering.Update(RenderTargetRect);

	check(FrameTargetRect.Max.X <= MaximumSupportedResolution);
	check(FrameTargetRect.Max.Y <= MaximumSupportedResolution);

	// Support override feature
	bool bDisableRender = PostRenderSettings.Override.IsEnabled();
	if (RenderSettings.OverrideViewportId.IsEmpty() == false)
	{
		// map to override at UpdateFrameContexts()
		bDisableRender = true;
	}
	
	//Add new contexts
	for (uint32 ContextIt = 0; ContextIt < ViewportContextAmmount; ++ContextIt)
	{
		const EStereoscopicPass StereoscopicEye  = FDisplayClusterViewportStereoscopicPass::EncodeStereoscopicEye(ContextIt, ViewportContextAmmount);
		const EStereoscopicPass StereoscopicPass = (InFrameSettings.bIsRenderingInEditor) ? EStereoscopicPass::eSSP_FULL : FDisplayClusterViewportStereoscopicPass::EncodeStereoscopicPass(InViewPassNum + ContextIt);

		FDisplayClusterViewport_Context Context(ContextIt, StereoscopicEye, StereoscopicPass);

		int ContextGPUIndex = (ContextIt > 0 && RenderSettings.StereoGPUIndex >= 0) ? RenderSettings.StereoGPUIndex : RenderSettings.GPUIndex;
		Context.GPUIndex = ContextGPUIndex;


		if (InFrameSettings.bIsRenderingInEditor)
		{
			// Disable MultiGPU feature
			Context.GPUIndex = INDEX_NONE;
		}
		else
		{
			// Control mGPU:
			switch (InFrameSettings.MultiGPUMode)
			{
			case EDisplayClusterMultiGPUMode::None:
				Context.bAllowGPUTransferOptimization = false;
				Context.GPUIndex = INDEX_NONE;
				break;

			case EDisplayClusterMultiGPUMode::Optimized_EnabledLockSteps:
				Context.bAllowGPUTransferOptimization = true;
				Context.bEnabledGPUTransferLockSteps = true;
				break;

			case EDisplayClusterMultiGPUMode::Optimized_DisabledLockSteps:
				Context.bAllowGPUTransferOptimization = true;
				Context.bEnabledGPUTransferLockSteps = false;
				break;

			default:
				Context.bAllowGPUTransferOptimization = false;
				break;
			}
		}

		Context.FrameTargetRect = FrameTargetRect;
		Context.RenderTargetRect = RenderTargetRect;
		Context.ContextSize = ContextSize;

		Context.bDisableRender = bDisableRender;

		Contexts.Add(Context);
	}

	// Reserve for resources
	RenderTargets.AddZeroed(FrameTargetsAmmount);
	InputShaderResources.AddZeroed(FrameTargetsAmmount);

	if (ShouldUseAdditionalTargetableResource())
	{
		AdditionalTargetableResources.AddZeroed(FrameTargetsAmmount);
	}

	// Setup Mips resources:
	for (FDisplayClusterViewport_Context& ContextIt: Contexts)
	{
		ContextIt.NumMips = PostRenderSettings.GenerateMips.GetRequiredNumMips(ContextIt.ContextSize);
		if (ContextIt.NumMips > 1)
		{
			MipsShaderResources.AddZeroed(1);
		}
	}

	return true;
}

