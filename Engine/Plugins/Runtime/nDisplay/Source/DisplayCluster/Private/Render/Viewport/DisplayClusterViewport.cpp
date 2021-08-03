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
FDisplayClusterViewport::FDisplayClusterViewport(FDisplayClusterViewportManager& InOwner, const FString& InViewportId, const TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe>& InProjectionPolicy)
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
	HandleEndScene();

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

	if (RenderSettings.bSkipRendering)
	{
		//@todo: research better way to disable viewport rendering
		// now just set showOnly=[]
		InOutView.ShowOnlyPrimitives.Emplace();
	}
	else
	{
		// Apply visibility settigns to view
		VisibilitySettings.SetupSceneView(World, InOutView);
	}

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

FIntRect FDisplayClusterViewport::GetValidRect(const FIntRect& InRect, const TCHAR* DbgSourceName)
{
	// The target always needs be within GMaxTextureDimensions, larger dimensions are not supported by the engine
	static const int32 MaxTextureSize = 1 << (GMaxTextureMipCount - 1);
	static const int32 MinTextureSize = 16;

	int Width  = FMath::Max(MinTextureSize, InRect.Width());
	int Height = FMath::Max(MinTextureSize, InRect.Height());

	FIntRect OutRect(InRect.Min, InRect.Min + FIntPoint(Width, Height));

	float RectScale = 1;

	// Make sure the rect doesn't exceed the maximum resolution, and preserve its aspect ratio if it needs to be clamped
	int RectMaxSize = OutRect.Max.GetMax();
	if (RectMaxSize > MaxTextureSize)
	{
		RectScale = float(MaxTextureSize) / RectMaxSize;
		UE_LOG(LogDisplayClusterViewport, Error, TEXT("The viewport '%s' rect '%s' size %dx%d clamped: max texture dimensions is %d"), *GetId(), (DbgSourceName==nullptr) ? TEXT("none") : DbgSourceName, InRect.Max.X, InRect.Max.Y, MaxTextureSize);
	}


	OutRect.Min.X = FMath::Min(OutRect.Min.X, MaxTextureSize);
	OutRect.Min.Y = FMath::Min(OutRect.Min.Y, MaxTextureSize);

	OutRect.Max.X = FMath::Clamp(int(OutRect.Max.X * RectScale), OutRect.Min.X, MaxTextureSize);
	OutRect.Max.Y = FMath::Clamp(int(OutRect.Max.Y * RectScale), OutRect.Min.Y, MaxTextureSize);

	return OutRect;
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

	if (PostRenderSettings.GenerateMips.IsEnabled())
	{
		//Check if current projection policy supports this feature
		if (!ProjectionPolicy.IsValid() || !ProjectionPolicy->ShouldUseSourceTextureWithMips())
		{
			// Don't create unused mips texture
			PostRenderSettings.GenerateMips.Reset();
		}
	}

	uint32 FrameTargetsAmmount = 2;
	FIntRect     DesiredFrameTargetRect = RenderSettings.Rect;
	{
		switch (InFrameSettings.RenderMode)
		{
		case EDisplayClusterRenderFrameMode::Mono:
			FrameTargetsAmmount = 1;
			break;
		case EDisplayClusterRenderFrameMode::SideBySide:
			AdjustRect(DesiredFrameTargetRect, 0.5f, 1.f);
			break;
		case EDisplayClusterRenderFrameMode::TopBottom:
			AdjustRect(DesiredFrameTargetRect, 1.f, 0.5f);
			break;
		case EDisplayClusterRenderFrameMode::PreviewMono:
		{
			// Preview downscale in range 0..1
			float MultXY = FMath::Clamp(InFrameSettings.PreviewRenderTargetRatioMult, 0.f, 1.f);
			AdjustRect(DesiredFrameTargetRect, MultXY, MultXY);

			// Align each frame to zero
			DesiredFrameTargetRect = FIntRect(FIntPoint(0, 0), DesiredFrameTargetRect.Size());

			// Mono
			FrameTargetsAmmount = 1;
			break;
		}
		default:
			break;
		}
	}

	// Special case mono->stereo
	uint32 ViewportContextAmmount = RenderSettings.bForceMono ? 1 : FrameTargetsAmmount;

	// Make sure the frame target rect doesn't exceed the maximum resolution, and preserve its aspect ratio if it needs to be clamped
	FIntRect FrameTargetRect = GetValidRect(DesiredFrameTargetRect, TEXT("Context Frame"));

	// Exclude zero-size viewports from render
	if (FrameTargetRect.Size().GetMin() <= 0)
	{
		UE_LOG(LogDisplayClusterViewport, Error, TEXT("The viewport '%s' FrameTarget rect has zero size %dx%d: Disabled"), *GetId(), FrameTargetRect.Size().X, FrameTargetRect.Size().Y);
		return false;
	}


	FIntPoint DesiredContextSize = FrameTargetRect.Size();

	float ClusterRenderTargetRatioMult = InFrameSettings.ClusterRenderTargetRatioMult;

	// Support Outer viewport cluster rtt multiplier
	if ((RenderSettingsICVFX.RuntimeFlags & ViewportRuntime_ICVFXTarget) != 0)
	{
		ClusterRenderTargetRatioMult *= InFrameSettings.ClusterICVFXOuterViewportRenderTargetRatioMult;
	}
	else
	if ((RenderSettingsICVFX.RuntimeFlags & ViewportRuntime_ICVFXIncamera) != 0)
	{
		ClusterRenderTargetRatioMult *= InFrameSettings.ClusterICVFXInnerViewportRenderTargetRatioMult;
	}

	// Cluster mult downscale in range 0..1
	float ClusterRTTMult = FMath::Clamp(ClusterRenderTargetRatioMult, 0.f, 1.f);

	// Scale context for rendering
	float ViewportContextSizeMult = FMath::Max(RenderSettings.RenderTargetRatio * ClusterRTTMult, 0.f);
	DesiredContextSize.X *= ViewportContextSizeMult;
	DesiredContextSize.Y *= ViewportContextSizeMult;

	FIntRect RenderTargetRect = GetValidRect(FIntRect(FIntPoint(0, 0), DesiredContextSize), TEXT("Context RenderTarget"));

	// Exclude zero-size viewports from render
	if (RenderTargetRect.Size().GetMin()<=0)
	{
		UE_LOG(LogDisplayClusterViewport, Error, TEXT("The viewport '%s' RenderTarget rect has zero size %dx%d: Disabled"), *GetId(), RenderTargetRect.Size().X, RenderTargetRect.Size().Y);
		return false;
	}

	FIntPoint ContextSize = RenderTargetRect.Size();

	// Support overscan rendering feature
	OverscanRendering.Update(*this, RenderTargetRect);

	// Support override feature
	bool bDisableRender = PostRenderSettings.Replace.IsEnabled();
	
	
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

