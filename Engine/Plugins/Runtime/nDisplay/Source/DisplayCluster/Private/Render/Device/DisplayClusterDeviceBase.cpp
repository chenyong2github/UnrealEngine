// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Device/DisplayClusterDeviceBase.h"

#include "Cluster/IPDisplayClusterClusterManager.h"
#include "Cluster/Controller/IPDisplayClusterNodeController.h"
#include "Game/IPDisplayClusterGameManager.h"
#include "Render/IPDisplayClusterRenderManager.h"

#include "DisplayClusterCameraComponent.h"
#include "DisplayClusterScreenComponent.h"

#include "HAL/IConsoleManager.h"

#include "RHIStaticStates.h"
#include "Slate/SceneViewport.h"

#include "Misc/DisplayClusterHelpers.h"

#include "Render/Device/DisplayClusterRenderViewport.h"
#include "Render/PostProcess/IDisplayClusterPostProcess.h"
#include "Render/Presentation/DisplayClusterPresentationBase.h"
#include "Render/Projection/IDisplayClusterProjectionPolicy.h"
#include "Render/Projection/IDisplayClusterProjectionPolicyFactory.h"
#include "Render/Synchronization/IDisplayClusterRenderSyncPolicy.h"

#include "DisplayClusterGlobals.h"
#include "DisplayClusterLog.h"

#include <utility>


// Enable/disable warp&blend
static TAutoConsoleVariable<int32> CVarWarpBlendEnabled(
	TEXT("nDisplay.render.WarpBlendEnabled"),
	1,
	TEXT("Warp & Blend status\n")
	TEXT("0 : disabled\n")
	TEXT("1 : enabled\n")
	,
	ECVF_RenderThreadSafe
);

// Enable/disable nDisplay post-process
static TAutoConsoleVariable<int32> CVarCustomPPEnabled(
	TEXT("nDisplay.render.postprocess"),
	1,
	TEXT("Custom post-process (0 = disabled)\n"),
	ECVF_RenderThreadSafe
);



FDisplayClusterDeviceBase::FDisplayClusterDeviceBase(uint32 ViewsPerViewport)
	: FDisplayClusterDeviceBase_PostProcess(RenderViewports, ViewsPerViewport, EyeRegions)
	, ViewsAmountPerViewport(ViewsPerViewport)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

FDisplayClusterDeviceBase::~FDisplayClusterDeviceBase()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterStereoDevice
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterDeviceBase::Initialize()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	if (GDisplayCluster->GetOperationMode() == EDisplayClusterOperationMode::Disabled)
	{
		return false;
	}

	const IPDisplayClusterConfigManager* const ConfigMgr = GDisplayCluster->GetPrivateConfigMgr();
	if (!ConfigMgr)
	{
		return false;
	}

	IPDisplayClusterRenderManager* const RenderMgr = GDisplayCluster->GetPrivateRenderMgr();
	if (!RenderMgr)
	{
		return false;
	}

	// Get list of local viewports (assigned to this cluster node)
	TArray<FDisplayClusterConfigViewport> LocalViewports = DisplayClusterHelpers::config::GetLocalViewports();
	if (LocalViewports.Num() == 0)
	{
		UE_LOG(LogDisplayClusterRender, Error, TEXT("No viewports found for this current node"));
		return false;
	}

	// Initialize all local viewports
	for (const FDisplayClusterConfigViewport& CfgViewport : LocalViewports)
	{
		FDisplayClusterConfigProjection CfgViewportProjection;
		if (DisplayClusterHelpers::config::GetViewportProjection(CfgViewport.Id, CfgViewportProjection))
		{
			TSharedPtr<IDisplayClusterProjectionPolicyFactory> ProjPolicyFactory = RenderMgr->GetProjectionPolicyFactory(CfgViewportProjection.Type);
			if (ProjPolicyFactory.IsValid())
			{
				const FString RHIName = GDynamicRHI->GetName();
				TSharedPtr<IDisplayClusterProjectionPolicy> ProjPolicy = ProjPolicyFactory->Create(CfgViewportProjection.Type, RHIName, CfgViewport.Id);
				if (ProjPolicy.IsValid())
				{
					AddViewport(CfgViewport.Id, CfgViewport.Loc, CfgViewport.Size, ProjPolicy, CfgViewport.CameraId, CfgViewport.BufferRatio, CfgViewport.IsRTT);
				}
				else
				{
					UE_LOG(LogDisplayClusterRender, Warning, TEXT("Invalid projection policy: type '%s', RHI '%s', viewport '%s'"), *CfgViewportProjection.Type, *RHIName, *CfgViewport.Id);
				}
			}
			else
			{
				UE_LOG(LogDisplayClusterRender, Warning, TEXT("No projection factory found for projection type '%s'"), *CfgViewportProjection.Type);
			}
		}
		else
		{
			UE_LOG(LogDisplayClusterRender, Error, TEXT("No projection found for the viewport '%s'"), *CfgViewport.Id);
		}
	}

	if (RenderViewports.Num() < 1)
	{
		UE_LOG(LogDisplayClusterRender, Error, TEXT("No viewports created. At least one must present."));
		return false;
	}

	// Forward cfg line to postprocess:
	// Get list of local postprocess (assigned to this cluster node)
	TArray<FDisplayClusterConfigPostprocess> LocalPostprocess = DisplayClusterHelpers::config::GetLocalPostprocess();
	TMap<FString, IPDisplayClusterRenderManager::FDisplayClusterPPInfo> Postprocess = RenderMgr->GetRegisteredPostprocessOperations();
	// Initialize all local Postprocess
	for (const FDisplayClusterConfigPostprocess& CfgPostprocess : LocalPostprocess)
	{
		if (Postprocess.Contains(CfgPostprocess.PostprocessId))
		{
			Postprocess[CfgPostprocess.PostprocessId].Operation->InitializePostProcess(CfgPostprocess.ConfigLine);
		}
	}

	return true;
}

void FDisplayClusterDeviceBase::StartScene(UWorld* InWorld)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	bIsSceneOpen = true;

	for (FDisplayClusterRenderViewport& Viewport : RenderViewports)
	{
		Viewport.GetProjectionPolicy()->StartScene(InWorld);
	}
}

void FDisplayClusterDeviceBase::EndScene()
{
	bIsSceneOpen = false;
}

void FDisplayClusterDeviceBase::SetViewportCamera(const FString& InCameraId /* = FString() */, const FString& InViewportId /* = FString() */)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	// Assign to all viewports if camera ID is empty (default camera will be used by all viewports)
	if (InViewportId.IsEmpty())
	{
		for (FDisplayClusterRenderViewport& Viewport : RenderViewports)
		{
			Viewport.SetCameraId(InCameraId);
		}

		UE_LOG(LogDisplayClusterRender, Log, TEXT("Camera '%s' was assigned to all viewports"), *InCameraId);

		return;
	}

	// Ok, we have a request for a particular viewport. Let's find it.
	FDisplayClusterRenderViewport* const DesiredViewport = RenderViewports.FindByPredicate([InViewportId](const FDisplayClusterRenderViewport& ItemViewport)
	{
		return InViewportId.Compare(ItemViewport.GetId(), ESearchCase::IgnoreCase) == 0;
	});

	// Check if requested viewport exists
	if (!DesiredViewport)
	{
		UE_LOG(LogDisplayClusterRender, Warning, TEXT("Couldn't assign '%s' camera. Viewport '%s' not found"), *InCameraId, *InViewportId);
		return;
	}

	// Update if found
	DesiredViewport->SetCameraId(InCameraId);

	UE_LOG(LogDisplayClusterRender, Log, TEXT("Camera '%s' was assigned to '%s' viewport"), *InCameraId, *InViewportId);
}

void FDisplayClusterDeviceBase::SetStartPostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& StartPostProcessingSettings)
{
	for(int ViewportIndex = 0; ViewportIndex < RenderViewports.Num(); ViewportIndex++)
	{
		if (RenderViewports[ViewportIndex].GetId() == ViewportID)
		{
			ViewportStartPostProcessingSettings.Emplace(ViewportIndex, StartPostProcessingSettings);
			break;
		}
	}
}

void FDisplayClusterDeviceBase::SetOverridePostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& OverridePostProcessingSettings, float BlendWeight)
{
	for (int ViewportIndex = 0; ViewportIndex < RenderViewports.Num(); ViewportIndex++)
	{
		if (RenderViewports[ViewportIndex].GetId() == ViewportID)
		{
			FOverridePostProcessingSettings OverrideSettings;
			OverrideSettings.BlendWeight = BlendWeight;
			OverrideSettings.PostProcessingSettings = OverridePostProcessingSettings;
			ViewportOverridePostProcessingSettings.Emplace(ViewportIndex, OverrideSettings);
			break;
		}
	}
}


void FDisplayClusterDeviceBase::SetFinalPostProcessingSettings(const FString& ViewportID, const FPostProcessSettings& FinalPostProcessingSettings)
{
	for (int ViewportIndex = 0; ViewportIndex < RenderViewports.Num(); ViewportIndex++)
	{
		if (RenderViewports[ViewportIndex].GetId() == ViewportID)
		{
			ViewportFinalPostProcessingSettings.Emplace(ViewportIndex, FinalPostProcessingSettings);
			break;
		}
	}
}

bool FDisplayClusterDeviceBase::GetViewportRect(const FString& InViewportID, FIntRect& Rect)
{
	FDisplayClusterRenderViewport* const DesiredViewport = RenderViewports.FindByPredicate([InViewportID](const FDisplayClusterRenderViewport& ItemViewport)
	{
		return InViewportID.Equals(ItemViewport.GetId(), ESearchCase::IgnoreCase);
	});

	if (!DesiredViewport)
	{
		return false;
	}

	Rect = DesiredViewport->GetArea();

	return true;
}

bool FDisplayClusterDeviceBase::SetBufferRatio(const FString& InViewportID, float InBufferRatio)
{
	// Ok, we have a request for a particular viewport. Let's find it.
	FDisplayClusterRenderViewport* const DesiredViewport = RenderViewports.FindByPredicate([InViewportID](const FDisplayClusterRenderViewport& ItemViewport)
	{
		return InViewportID.Compare(ItemViewport.GetId(), ESearchCase::IgnoreCase) == 0;
	});

	// Update if found
	if (!DesiredViewport)
	{
		return false;
	}

	UE_LOG(LogDisplayClusterRender, Log, TEXT("Set buffer ratio %f for viewport '%s'"), InBufferRatio, *InViewportID);
	DesiredViewport->SetBufferRatio(InBufferRatio);
	return true;
}

bool FDisplayClusterDeviceBase::GetBufferRatio(const FString& InViewportID, float& OutBufferRatio) const
{
	// Ok, we have a request for a particular viewport. Let's find it.
	FDisplayClusterRenderViewport* const DesiredViewport = RenderViewports.FindByPredicate([InViewportID](const FDisplayClusterRenderViewport& ItemViewport)
	{
		return InViewportID.Compare(ItemViewport.GetId(), ESearchCase::IgnoreCase) == 0;
	});

	// Request data if found
	if (!DesiredViewport)
	{
		return false;
	}

	OutBufferRatio = DesiredViewport->GetBufferRatio();
	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Viewport '%s' has buffer ratio %f"), *InViewportID, OutBufferRatio);
	return true;
}

bool FDisplayClusterDeviceBase::GetBufferRatio(int32 ViewIdx, float& OutBufferRatio) const
{
	if (ViewIdx < 0 || ViewIdx >= RenderViewports.Num())
	{
		return false;
	}

	OutBufferRatio = RenderViewports[ViewIdx].GetBufferRatio();
	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Viewport '%s' has buffer ratio %f"), *RenderViewports[ViewIdx].GetId(), OutBufferRatio);
	return true;
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IStereoRendering
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterDeviceBase::IsStereoEnabled() const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
	return true;
}

bool FDisplayClusterDeviceBase::IsStereoEnabledOnNextFrame() const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
	return true;
}

bool FDisplayClusterDeviceBase::EnableStereo(bool stereo /*= true*/)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
	return true;
}

void FDisplayClusterDeviceBase::InitCanvasFromView(class FSceneView* InView, class UCanvas* Canvas)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

void FDisplayClusterDeviceBase::AdjustViewRect(enum EStereoscopicPass StereoPassType, int32& X, int32& Y, uint32& SizeX, uint32& SizeY) const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
	UE_LOG(LogDisplayClusterRender, Warning, TEXT("We should never be here!"));
}

void FDisplayClusterDeviceBase::CalculateStereoViewOffset(const enum EStereoscopicPass StereoPassType, FRotator& ViewRotation, const float WorldToMeters, FVector& ViewLocation)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	check(IsInGameThread());
	check(WorldToMeters > 0.f);

	UE_LOG(LogDisplayClusterRender, VeryVerbose, TEXT("OLD ViewLoc: %s, ViewRot: %s"), *ViewLocation.ToString(), *ViewRotation.ToString());
	UE_LOG(LogDisplayClusterRender, VeryVerbose, TEXT("WorldToMeters: %f"), WorldToMeters);

	if (!bIsSceneOpen)
	{
		return;
	}

	// Get current viewport
	const int CurrentViewportIndex = DecodeViewportIndex(StereoPassType);
	check(int32(CurrentViewportIndex) < RenderViewports.Num());
	FDisplayClusterRenderViewport& Viewport = RenderViewports[CurrentViewportIndex];

	// Get current view context
	const int ViewIndex = DecodeViewIndex(StereoPassType);
	FDisplayClusterRenderViewContext& ViewContext = Viewport.GetContext(ViewIndex);

	// Get camera ID assigned to the viewport
	IDisplayClusterGameManager* const GameMgr = GDisplayCluster->GetGameMgr();
	const FString& CameraId = Viewport.GetCameraId();

	// Get camera component assigned to the viewport (or default camera if nothing assigned)
	UDisplayClusterCameraComponent* ViewCamera = (CameraId.IsEmpty() ?
		ViewCamera = GDisplayCluster->GetGameMgr()->GetDefaultCamera() :
		ViewCamera = GDisplayCluster->GetGameMgr()->GetCameraById(CameraId));

	if (!ViewCamera)
	{
		UE_LOG(LogDisplayClusterRender, Warning, TEXT("No camera found for viewport '%s'"), *Viewport.GetId());
		return;
	}

	if (CameraId.Len() > 0)
	{
		UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Viewport '%s' has assigned camera '%s'"), *Viewport.GetId(), *CameraId);
	}

	// Get the actual camera settings
	const float CfgEyeDist     = ViewCamera->GetInterpupillaryDistance();
	const bool  CfgEyeSwap     = ViewCamera->GetEyesSwap();
	const int   CfgForceOffset = ViewCamera->GetForceEyeOffset();
	const float CfgNCP         = 1.f;

	// Calculate eye offset considering the world scale
	const float ScaledEyeDist     = CfgEyeDist * WorldToMeters;
	const float ScaledEyeOffset   = ScaledEyeDist / 2.f;
	const float EyeOffsetValues[] = { -ScaledEyeOffset, 0.f, ScaledEyeOffset };

	// Decode current eye type
	const EDisplayClusterEyeType EyeType = DecodeEyeType(StereoPassType);
	const int   EyeIndex = (int)EyeType;

	float PassOffset = 0.f;
	float PassOffsetSwap = 0.f;

	if (EyeType == EDisplayClusterEyeType::Mono)
	{
		// For monoscopic camera let's check if the "force offset" feature is used
		// * Force left (-1) ==> 0 left eye
		// * Force right (1) ==> 2 right eye
		// * Default (0) ==> 1 mono
		PassOffset = EyeOffsetValues[CfgForceOffset + 1];
		// Eye swap is not available for monoscopic so just save the value
		PassOffsetSwap = PassOffset;
	}
	else
	{
		// For stereo camera we can only swap eyes if required (no "force offset" allowed)
		PassOffset = EyeOffsetValues[EyeIndex];
		PassOffsetSwap = (CfgEyeSwap ? -PassOffset : PassOffset);
	}

	FVector ViewOffset = FVector::ZeroVector;
	if (ViewCamera)
	{
		// View base location
		ViewLocation = ViewCamera->GetComponentLocation();
		ViewRotation = ViewCamera->GetComponentRotation();
		// Apply computed offset to the view location
		const FQuat EyeQuat = ViewRotation.Quaternion();
		ViewOffset = EyeQuat.RotateVector(FVector(0.0f, PassOffsetSwap, 0.0f));
		ViewLocation += ViewOffset;
	}

	// Perform view calculations on a policy side
	if (!Viewport.GetProjectionPolicy()->CalculateView(ViewIndex, ViewLocation, ViewRotation, ViewOffset, WorldToMeters, CfgNCP, CfgNCP))
	{
		UE_LOG(LogDisplayClusterRender, Warning, TEXT("Couldn't compute view parameters for Viewport %s(%d), ViewIdx: %d"), *Viewport.GetId(), CurrentViewportIndex, int(ViewIndex));
	}

	// Store the view location/rotation
	ViewContext.ViewLocation  = ViewLocation;
	ViewContext.ViewRotation  = ViewRotation;
	ViewContext.WorldToMeters = WorldToMeters;

	UE_LOG(LogDisplayClusterRender, VeryVerbose, TEXT("ViewLoc: %s, ViewRot: %s"), *ViewLocation.ToString(), *ViewRotation.ToString());
}

FMatrix FDisplayClusterDeviceBase::GetStereoProjectionMatrix(const enum EStereoscopicPass StereoPassType) const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	check(IsInGameThread());
	check(StereoPassType != EStereoscopicPass::eSSP_FULL);

	const int CurrentViewportIndex = DecodeViewportIndex(StereoPassType);
	const uint32 ViewIndex = DecodeViewIndex(StereoPassType);

	FDisplayClusterRenderViewport& Viewport = RenderViewports[CurrentViewportIndex];
	FDisplayClusterRenderViewContext& ViewContext = Viewport.GetContext(ViewIndex);

	FMatrix PrjMatrix = FMatrix::Identity;
	if (bIsSceneOpen)
	{
		if (!Viewport.GetProjectionPolicy()->GetProjectionMatrix(ViewIndex, PrjMatrix))
		{
			UE_LOG(LogDisplayClusterRender, Warning, TEXT("Got invalid projection matrix: Viewport %s(%d), ViewIdx: %d"), *Viewport.GetId(), CurrentViewportIndex, int(ViewIndex));
		}
	}

	return PrjMatrix;
}

void FDisplayClusterDeviceBase::RenderTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* BackBuffer, FRHITexture2D* SrcTexture, FVector2D WindowSize) const
{
	// Get registered PP operations map
	const TMap<FString, IDisplayClusterRenderManager::FDisplayClusterPPInfo> PPOperationsMap = GDisplayCluster->GetRenderMgr()->GetRegisteredPostprocessOperations();

	// Get operations array (sorted already by the rendering manager)
	PPOperationsMap.GenerateValueArray(FDisplayClusterDeviceBase_PostProcess::PPOperations);

	// This one is to match interface function signatures only. The functions will provide the handlers with a proper data.
	const FIntRect StubRect(0, 0, 0, 0);

	// Get custom PP and warp&blend flags status
	const bool bCustomPPEnabled = (CVarCustomPPEnabled.GetValueOnRenderThread() != 0);
	const bool bWarpBlendEnabled = (CVarWarpBlendEnabled.GetValueOnRenderThread() != 0);

	// Post-process before warp&blend
	if (bCustomPPEnabled)
	{
		// PP round 1: post-process for each view region before warp&blend
		PerformPostProcessViewBeforeWarpBlend_RenderThread(RHICmdList, SrcTexture, StubRect);
		// PP round 2: post-process for each eye frame before warp&blend
		PerformPostProcessFrameBeforeWarpBlend_RenderThread(RHICmdList, SrcTexture, StubRect);
		// PP round 3: post-process for the whole render target before warp&blend
		PerformPostProcessRenderTargetBeforeWarpBlend_RenderThread(RHICmdList, SrcTexture);
	}

	// Perform warp&blend
	if (bWarpBlendEnabled)
	{
		// Iterate over viewports
		for (int i = 0; i < RenderViewports.Num(); ++i)
		{
			// Iterate over views for the current viewport
			if (RenderViewports[i].GetProjectionPolicy()->IsWarpBlendSupported())
			{
				for (uint32 j = 0; j < ViewsAmountPerViewport; ++j)
				{
					RenderViewports[i].GetProjectionPolicy()->ApplyWarpBlend_RenderThread(j, RHICmdList, SrcTexture, RenderViewports[i].GetContext(j).RenderTargetRect);
				}
			}
		}
	}

	// Post-process after warp&blend
	if (bCustomPPEnabled)
	{
		// PP round 4: post-process for each view region after warp&blend
		PerformPostProcessViewAfterWarpBlend_RenderThread(RHICmdList, SrcTexture, StubRect);
		// PP round 5: post-process for each eye frame after warp&blend
		PerformPostProcessFrameAfterWarpBlend_RenderThread(RHICmdList, SrcTexture, StubRect);
		// PP round 6: post-process for the whole render target after warp&blend
		PerformPostProcessRenderTargetAfterWarpBlend_RenderThread(RHICmdList, SrcTexture);
	}

	// Finally, copy the render target texture to the back buffer
	CopyTextureToBackBuffer_RenderThread(RHICmdList, BackBuffer, SrcTexture, WindowSize);
}

int32 FDisplayClusterDeviceBase::GetDesiredNumberOfViews(bool bStereoRequested) const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
	return RenderViewports.Num() * ViewsAmountPerViewport;
}

EStereoscopicPass FDisplayClusterDeviceBase::GetViewPassForIndex(bool bStereoRequested, uint32 ViewIndex) const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	const int CurrentPass = EncodeStereoscopicPass(ViewIndex);
	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("GetViewPassForIndex: %d"), (int)CurrentPass);

	// This is a bit tricky but it works
	return (EStereoscopicPass)CurrentPass;
}

uint32 FDisplayClusterDeviceBase::GetViewIndexForPass(EStereoscopicPass StereoPassType) const
{
	uint32 DecodedViewIndex = 0;

	switch (StereoPassType)
	{
	case EStereoscopicPass::eSSP_FULL:
	case EStereoscopicPass::eSSP_LEFT_EYE:
		DecodedViewIndex = 0;
		break;

	case EStereoscopicPass::eSSP_RIGHT_EYE:
		DecodedViewIndex = 1;
		break;

	default:
		DecodedViewIndex = (int(StereoPassType) - int(EStereoscopicPass::eSSP_RIGHT_EYE) + 1);
		break;
	}

	return DecodedViewIndex;
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IStereoRenderTargetManager
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterDeviceBase::UpdateViewport(bool bUseSeparateRenderTarget, const class FViewport& Viewport, class SViewport* ViewportWidget)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
	check(IsInGameThread());

	// Store viewport
	if (!MainViewport)
	{
		// UE viewport
		MainViewport = (FViewport*)&Viewport;
		// Current sync policy
		TSharedPtr<IDisplayClusterRenderSyncPolicy> SyncPolicy = GDisplayCluster->GetRenderMgr()->GetCurrentSynchronizationPolicy();

		// Create present handler
		FDisplayClusterPresentationBase* const CustomPresentHandler = CreatePresentationObject(MainViewport, SyncPolicy);
		check(CustomPresentHandler);

		// Set up our new present handler
		Viewport.GetViewportRHI()->SetCustomPresent(CustomPresentHandler);
	}
}

void FDisplayClusterDeviceBase::CalculateRenderTargetSize(const class FViewport& Viewport, uint32& InOutSizeX, uint32& InOutSizeY)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
	check(IsInGameThread());

	InOutSizeX = Viewport.GetSizeXY().X;
	InOutSizeY = Viewport.GetSizeXY().Y;

	for (const FDisplayClusterRenderViewport& Item : RenderViewports)
	{
		InOutSizeX = FMath::Max(InOutSizeX, (uint32)Item.GetArea().Max.X);
		InOutSizeY = FMath::Max(InOutSizeY, (uint32)Item.GetArea().Max.Y);
	}

	// Store eye region
	EyeRegions[0] = FIntRect(FIntPoint(0, 0), FIntPoint(InOutSizeX, InOutSizeY));

	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Render target size: [%d x %d]"), InOutSizeX, InOutSizeY);

	check(InOutSizeX > 0 && InOutSizeY > 0);
}

bool FDisplayClusterDeviceBase::NeedReAllocateViewportRenderTarget(const class FViewport& Viewport)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	check(IsInGameThread());

	// Get current RT size
	const FIntPoint rtSize = Viewport.GetRenderTargetTextureSizeXY();

	// Get desired RT size
	uint32 newSizeX = 0;
	uint32 newSizeY = 0;
	CalculateRenderTargetSize(Viewport, newSizeX, newSizeY);

	// Here we conclude if need to re-allocate
	const bool Result = (newSizeX != rtSize.X || newSizeY != rtSize.Y);

	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Is reallocate viewport render target needed: %d"), Result ? 1 : 0);

	if (Result)
	{
		UE_LOG(LogDisplayClusterRender, Log, TEXT("Need to re-allocate render target: cur %d:%d, new %d:%d"), rtSize.X, rtSize.Y, newSizeX, newSizeY);
	}

	return Result;
}


//////////////////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterDeviceBase
//////////////////////////////////////////////////////////////////////////////////////////////
EStereoscopicPass FDisplayClusterDeviceBase::EncodeStereoscopicPass(int ViewIndex) const
{
	EStereoscopicPass EncodedPass = EStereoscopicPass::eSSP_FULL;

	// We don't care about mono/stereo. We need to fulfill ViewState and StereoViewStates in a proper way.
	// Look at ULocalPlayer::CalcSceneViewInitOptions for view states mapping.
	if (ViewIndex < 2)
	{
		EncodedPass = (ViewIndex == 0 ? EStereoscopicPass::eSSP_LEFT_EYE : EStereoscopicPass::eSSP_RIGHT_EYE);
	}
	else
	{
		EncodedPass = EStereoscopicPass(int(EStereoscopicPass::eSSP_RIGHT_EYE) + ViewIndex - 1);
	}

	UE_LOG(LogDisplayClusterRender, VeryVerbose, TEXT("EncodeStereoscopicPass: %d -> %d"), ViewIndex, int(EncodedPass));

	return EncodedPass;
}

EStereoscopicPass FDisplayClusterDeviceBase::DecodeStereoscopicPass(const enum EStereoscopicPass StereoPassType) const
{
	EStereoscopicPass DecodedPass = EStereoscopicPass::eSSP_FULL;

	// Monoscopic rendering
	if (ViewsAmountPerViewport == 1)
	{
		DecodedPass = EStereoscopicPass::eSSP_FULL;
	}
	// Stereoscopic rendering
	else
	{
		switch (StereoPassType)
		{
		case EStereoscopicPass::eSSP_LEFT_EYE:
		case EStereoscopicPass::eSSP_RIGHT_EYE:
			DecodedPass = StereoPassType;
			break;

		default:
			DecodedPass = ((int(StereoPassType) - int(EStereoscopicPass::eSSP_RIGHT_EYE)) % 2 == 0) ? EStereoscopicPass::eSSP_RIGHT_EYE : EStereoscopicPass::eSSP_LEFT_EYE;
			break;
		}
	}

	UE_LOG(LogDisplayClusterRender, VeryVerbose, TEXT("DecodeStereoscopicPass: %d -> %d"), int(StereoPassType), int(DecodedPass));

	return DecodedPass;
}

int FDisplayClusterDeviceBase::DecodeViewportIndex(const enum EStereoscopicPass StereoPassType) const
{
	check(ViewsAmountPerViewport > 0);

	const int DecodedPassIndex = GetViewIndexForPass(StereoPassType);
	const int DecodedViewportIndex = DecodedPassIndex / ViewsAmountPerViewport;

	UE_LOG(LogDisplayClusterRender, VeryVerbose, TEXT("DecodeViewportIndex: %d -> %d"), int(StereoPassType), DecodedViewportIndex);

	check(DecodedViewportIndex >= 0);
	return DecodedViewportIndex;
}

FDisplayClusterDeviceBase::EDisplayClusterEyeType FDisplayClusterDeviceBase::DecodeEyeType(const enum EStereoscopicPass StereoPassType) const
{
	EDisplayClusterEyeType EyeType = EDisplayClusterEyeType::Mono;

	const EStereoscopicPass DecodedPass = DecodeStereoscopicPass(StereoPassType);
	switch (DecodedPass)
	{
	case EStereoscopicPass::eSSP_LEFT_EYE:
		EyeType = EDisplayClusterEyeType::StereoLeft;
		break;

	case EStereoscopicPass::eSSP_FULL:
		EyeType = EDisplayClusterEyeType::Mono;
		break;

	case EStereoscopicPass::eSSP_RIGHT_EYE:
		EyeType = EDisplayClusterEyeType::StereoRight;
		break;

	default:
		UE_LOG(LogDisplayClusterRender, Warning, TEXT("Couldn't decode eye type. Falling back to type <%d>"), int(EyeType));
		break;
	}

	UE_LOG(LogDisplayClusterRender, VeryVerbose, TEXT("DecodeEyeType: %d -> %d"), int(StereoPassType), int(EyeType));

	return EyeType;
}

uint32 FDisplayClusterDeviceBase::DecodeViewIndex(const enum EStereoscopicPass StereoPassType) const
{
	const uint32 DecodedPassIndex = GetViewIndexForPass(StereoPassType);
	const uint32 DecodedViewIndex = DecodedPassIndex % ViewsAmountPerViewport;
	UE_LOG(LogDisplayClusterRender, VeryVerbose, TEXT("DecodeViewIndex: %d -> %d"), int(StereoPassType), DecodedViewIndex);
	return DecodedViewIndex;
}

void FDisplayClusterDeviceBase::AddViewport(const FString& InViewportId, const FIntPoint& InViewportLocation, const FIntPoint& InViewportSize, TSharedPtr<IDisplayClusterProjectionPolicy> InProjPolicy, const FString& InCameraId, float InBufferRatio /* = 1.f */, bool IsRTT /*= false*/)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	FScopeLock lock(&InternalsSyncScope);

	// Check viewport ID
	if (InViewportId.IsEmpty())
	{
		UE_LOG(LogDisplayClusterRender, Warning, TEXT("Wrong viewport ID"));
		return;
	}

	// Check if a viewport with the same ID already exists
	const bool bAlreadyExists = (nullptr != RenderViewports.FindByPredicate([InViewportId](const FDisplayClusterRenderViewport& ItemViewport)
	{
		return ItemViewport.GetId().Compare(InViewportId, ESearchCase::IgnoreCase) == 0;
	}));

	// ID must be unique
	if (bAlreadyExists)
	{
		UE_LOG(LogDisplayClusterRender, Warning, TEXT("Viewport '%s' already exists"), *InViewportId);
		return;
	}

	// Initialize the viewport
	if (InProjPolicy->HandleAddViewport(InViewportSize, ViewsAmountPerViewport))
	{
		UE_LOG(LogDisplayClusterRender, Log, TEXT("A corresponded projection policy object has initialized the viewport '%s'"), *InViewportId);

		FIntRect ViewportArea = FIntRect(InViewportLocation, InViewportLocation + InViewportSize);
		FDisplayClusterRenderViewport NewViewport(InViewportId, ViewportArea, InProjPolicy, EDisplayClusterEyeType::COUNT, InCameraId, InBufferRatio, IsRTT);
		
		// Store viewport instance
		RenderViewports.Add(NewViewport);
	}
}

void FDisplayClusterDeviceBase::CopyTextureToBackBuffer_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* BackBuffer, FRHITexture2D* SrcTexture, FVector2D WindowSize) const
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);

	check(IsInRenderingThread());

	const FIntPoint BBSize = BackBuffer->GetSizeXY();

	// Copy final texture to the back buffer. This implementation is for simple cases. More complex devices override this method and provide custom copying.
	FResolveParams copyParams;
	copyParams.DestArrayIndex = 0;
	copyParams.SourceArrayIndex = 0;
	copyParams.Rect.X1 = 0;
	copyParams.Rect.Y1 = 0;
	copyParams.Rect.X2 = BBSize.X;
	copyParams.Rect.Y2 = BBSize.Y;
	copyParams.DestRect.X1 = 0;
	copyParams.DestRect.Y1 = 0;
	copyParams.DestRect.X2 = BBSize.X;
	copyParams.DestRect.Y2 = BBSize.Y;

	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("CopyToResolveTarget [L]: [%d,%d - %d,%d] -> [%d,%d - %d,%d]"),
		copyParams.Rect.X1, copyParams.Rect.Y1, copyParams.Rect.X2, copyParams.Rect.Y2,
		copyParams.DestRect.X1, copyParams.DestRect.Y1, copyParams.DestRect.X2, copyParams.DestRect.Y2);

	RHICmdList.CopyToResolveTarget(SrcTexture, BackBuffer, copyParams);
}

void FDisplayClusterDeviceBase::StartFinalPostprocessSettings(struct FPostProcessSettings* StartPostProcessingSettings, const enum EStereoscopicPass StereoPassType)
{
	int ViewportNumber = DecodeViewportIndex(StereoPassType);
	auto CustomPostProcessAvailable = ViewportStartPostProcessingSettings.Find(ViewportNumber);
	if (CustomPostProcessAvailable)
	{
		*StartPostProcessingSettings = ViewportStartPostProcessingSettings[ViewportNumber];
		ViewportStartPostProcessingSettings.Remove(ViewportNumber);
	}
}

bool FDisplayClusterDeviceBase::OverrideFinalPostprocessSettings(struct FPostProcessSettings* OverridePostProcessingSettings, const enum EStereoscopicPass StereoPassType, float& BlendWeight)
{
	int ViewportNumber = DecodeViewportIndex(StereoPassType);
	auto CustomPostProcessAvailable = ViewportOverridePostProcessingSettings.Find(ViewportNumber);
	if (CustomPostProcessAvailable)
	{
		*OverridePostProcessingSettings = ViewportOverridePostProcessingSettings[ViewportNumber].PostProcessingSettings;
		BlendWeight = ViewportOverridePostProcessingSettings[ViewportNumber].BlendWeight;
		ViewportOverridePostProcessingSettings.Remove(ViewportNumber);
		return true;
	}

	return false;
}

void FDisplayClusterDeviceBase::EndFinalPostprocessSettings(struct FPostProcessSettings* FinalPostProcessingSettings, const enum EStereoscopicPass StereoPassType)
{
	int ViewportNumber = DecodeViewportIndex(StereoPassType);
	auto CustomPostProcessAvailable = ViewportFinalPostProcessingSettings.Find(ViewportNumber);
	if (CustomPostProcessAvailable)
	{
		*FinalPostProcessingSettings = ViewportFinalPostProcessingSettings[ViewportNumber];
		ViewportFinalPostProcessingSettings.Remove(ViewportNumber);
	}		
}