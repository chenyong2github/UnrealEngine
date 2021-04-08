// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Device/DisplayClusterDeviceBase.h"

#include "Cluster/IPDisplayClusterClusterManager.h"
#include "Cluster/Controller/IDisplayClusterNodeController.h"
#include "Config/IPDisplayClusterConfigManager.h"
#include "Game/IPDisplayClusterGameManager.h"
#include "Render/IPDisplayClusterRenderManager.h"

#include "Misc/DisplayClusterGlobals.h"
#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterStrings.h"
#include "Misc/DisplayClusterLog.h"

#include "DisplayClusterConfigurationTypes.h"

#include "DisplayClusterRootActor.h"
#include "Components/DisplayClusterCameraComponent.h"
#include "Components/DisplayClusterScreenComponent.h"

#include "HAL/IConsoleManager.h"

#include "RHIStaticStates.h"
#include "Slate/SceneViewport.h"

#include "Render/PostProcess/IDisplayClusterPostProcess.h"
#include "Render/Presentation/DisplayClusterPresentationBase.h"
#include "Render/Projection/IDisplayClusterProjectionPolicy.h"
#include "Render/Projection/IDisplayClusterProjectionPolicyFactory.h"
#include "Render/Synchronization/IDisplayClusterRenderSyncPolicy.h"

#include "ITextureShare.h"
#include "ITextureShareItem.h"

#include "Render/Viewport/DisplayClusterViewportManager.h"
#include "Render/Viewport/IDisplayClusterViewport.h"
#include "Render/Viewport/IDisplayClusterViewportProxy.h"

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

FDisplayClusterDeviceBase::FDisplayClusterDeviceBase(EDisplayClusterRenderFrameMode InRenderFrameMode)
	: RenderFrameMode(InRenderFrameMode)
{
	DCViewportManager = MakeUnique<FDisplayClusterViewportManager>();

	UE_LOG(LogDisplayClusterRender, Log, TEXT("Created DCRenderDevice"));
}

FDisplayClusterDeviceBase::~FDisplayClusterDeviceBase()
{
	//@todo: delete singleton object IDisplayClusterViewportManager
}

IDisplayClusterViewportManager& FDisplayClusterDeviceBase::GetViewportManager() const
{
	return *(DCViewportManager.Get());
}

//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterStereoDevice
//////////////////////////////////////////////////////////////////////////////////////////////

bool FDisplayClusterDeviceBase::Initialize()
{
	if (GDisplayCluster->GetOperationMode() == EDisplayClusterOperationMode::Disabled)
	{
		return false;
	}

	/*
	// Get local node configuration
	const UDisplayClusterConfigurationClusterNode* LocalNode = GDisplayCluster->GetPrivateConfigMgr()->GetLocalNode();
	if (!LocalNode)
	{
		UE_LOG(LogDisplayClusterRender, Error, TEXT("Couldn't get configuration data for current cluster node"));
		return false;
	}

	ADisplayClusterRootActor* const RootActor = IDisplayCluster::Get().GetGameMgr()->GetRootActor();
	if(RootActor == nullptr)
	{ 
		UE_LOG(LogDisplayClusterRender, Error, TEXT("Couldn't get RootActor for current cluster node"));
		return false;
	}

	FString LocalNodeId = IDisplayCluster::Get().GetConfigMgr()->GetLocalNodeId();
	if (!GetViewportManager().UpdateConfiguration(GetRenderFrameMode(), LocalNodeId, RootActor))
	{
		UE_LOG(LogDisplayClusterRender, Error, TEXT("Invalid cluster node configuration"));
		return false;
	}

	if (GetViewportManager().GetViewports().Num() < 1)
	{
		UE_LOG(LogDisplayClusterRender, Error, TEXT("No viewports created. At least one must present."));
		return false;
	}
	*/

	return true;
}

void FDisplayClusterDeviceBase::StartScene(UWorld* InWorld)
{
	GetViewportManager().StartScene(InWorld);
}

void FDisplayClusterDeviceBase::EndScene()
{
	GetViewportManager().EndScene();
}

void FDisplayClusterDeviceBase::PreTick(float DeltaSeconds)
{
	if (!bIsCustomPresentSet)
	{
		// Set up our new present handler
		if (MainViewport)
		{
			// Current sync policy
			TSharedPtr<IDisplayClusterRenderSyncPolicy> SyncPolicy = GDisplayCluster->GetRenderMgr()->GetCurrentSynchronizationPolicy();
			check(SyncPolicy.IsValid());

			// Create present handler
			CustomPresentHandler = CreatePresentationObject(MainViewport, SyncPolicy);
			check(CustomPresentHandler);

			const FViewportRHIRef& MainViewportRHI = MainViewport->GetViewportRHI();

			if (MainViewportRHI)
			{
				MainViewportRHI->SetCustomPresent(CustomPresentHandler);
				bIsCustomPresentSet = true;
				OnDisplayClusterRenderCustomPresentCreated().Broadcast();
			}
			else
			{
				UE_LOG(LogDisplayClusterRender, Error, TEXT("PreTick: MainViewport->GetViewportRHI() returned null reference"));
			}
		}
	}
}

IDisplayClusterPresentation* FDisplayClusterDeviceBase::GetPresentation() const
{
	return CustomPresentHandler;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// IStereoRendering
//////////////////////////////////////////////////////////////////////////////////////////////
bool FDisplayClusterDeviceBase::IsStereoEnabled() const
{
	return true;
}

bool FDisplayClusterDeviceBase::IsStereoEnabledOnNextFrame() const
{
	return true;
}

bool FDisplayClusterDeviceBase::EnableStereo(bool stereo /*= true*/)
{
	return true;
}

void FDisplayClusterDeviceBase::InitCanvasFromView(class FSceneView* InView, class UCanvas* Canvas)
{
	if (!bIsCustomPresentSet)
	{
		// Set up our new present handler
		if (MainViewport)
		{
			// Current sync policy
			TSharedPtr<IDisplayClusterRenderSyncPolicy> SyncPolicy = GDisplayCluster->GetRenderMgr()->GetCurrentSynchronizationPolicy();
			check(SyncPolicy.IsValid());

			// Create present handler
			CustomPresentHandler = CreatePresentationObject(MainViewport, SyncPolicy);
			check(CustomPresentHandler);

			MainViewport->GetViewportRHI()->SetCustomPresent(CustomPresentHandler);

			OnDisplayClusterRenderCustomPresentCreated().Broadcast();
		}

		bIsCustomPresentSet = true;
	}
}

void FDisplayClusterDeviceBase::AdjustViewRect(EStereoscopicPass StereoPassType, int32& X, int32& Y, uint32& SizeX, uint32& SizeY) const
{
	check(IsInGameThread());

	if (GetViewportManager().IsSceneOpened() == false)
		{ return; }

	uint32 ViewportContextNum = 0;
	IDisplayClusterViewport* pViewport = GetViewportManager().FindViewport(StereoPassType, &ViewportContextNum);
	if (pViewport == nullptr)
	{
		UE_LOG(LogDisplayClusterRender, Warning, TEXT("Viewport StereoPassType='%i' not found"), int(StereoPassType));
		return;
	}

	const FIntRect& ViewRect = pViewport->GetContexts()[ViewportContextNum].RenderTargetRect;

	X = ViewRect.Min.X;
	Y = ViewRect.Min.Y;

	SizeX = ViewRect.Width();
	SizeY = ViewRect.Height();

	UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Adjusted view rect: Viewport='%s', ViewIndex=%d, [%d,%d - %d,%d]"), *pViewport->GetId(), ViewportContextNum, ViewRect.Min.X, ViewRect.Min.Y, ViewRect.Max.X, ViewRect.Max.Y);
}

uint32 FDisplayClusterDeviceBase::GetViewIndexForPass(EStereoscopicPass StereoPassType) const
{
	uint32 DecodedViewIndex = 0;

	switch (StereoPassType)
	{
	case EStereoscopicPass::eSSP_FULL:
		DecodedViewIndex = 0;
		break;

	default:
		if (IsInRenderingThread())
		{
			uint32 ViewportContextNum = 0;
			IDisplayClusterViewportProxy* ViewportProxy = GetViewportManager().FindViewport_RenderThread(StereoPassType, &ViewportContextNum);
			if (ViewportProxy)
			{
				const FDisplayClusterViewport_Context& Context = ViewportProxy->GetContexts_RenderThread()[ViewportContextNum];
				DecodedViewIndex =  Context.RenderFrameViewIndex;
			}
		}
		else
		{
			uint32 ViewportContextNum = 0;
			IDisplayClusterViewport* pViewport = GetViewportManager().FindViewport(StereoPassType, &ViewportContextNum);
			if (pViewport)
			{
				const FDisplayClusterViewport_Context& Context = pViewport->GetContexts()[ViewportContextNum];
				DecodedViewIndex = Context.RenderFrameViewIndex;
			}
		}
		break;
	}

	return DecodedViewIndex;
}

void FDisplayClusterDeviceBase::CalculateStereoViewOffset(const enum EStereoscopicPass StereoPassType, FRotator& ViewRotation, const float WorldToMeters, FVector& ViewLocation)
{
	check(IsInGameThread());
	check(WorldToMeters > 0.f);

	if (GetViewportManager().IsSceneOpened() == false)
		{ return; }

	uint32 ViewportContextNum = 0;
	IDisplayClusterViewport* pViewport = GetViewportManager().FindViewport(StereoPassType, &ViewportContextNum);
	if (pViewport == nullptr)
	{
		UE_LOG(LogDisplayClusterRender, Warning, TEXT("Viewport StereoPassType='%i' not found"), int(StereoPassType));
		return;
	}

	if (!pViewport->GetProjectionPolicy().IsValid())
	{
		// ignore viewports with uninitialized prj policy
		return;
	}

	// Get root actor from viewport
	ADisplayClusterRootActor* const RootActor = pViewport->GetOwner().GetRootActor();
	if (!RootActor)
	{
		UE_LOG(LogDisplayClusterRender, Warning, TEXT("No root actor found in game manager"));
		return;
	}

	FDisplayClusterViewport_Context& InOutViewportContext = pViewport->GetContexts()[ViewportContextNum];

	UE_LOG(LogDisplayClusterRender, VeryVerbose, TEXT("OLD ViewLoc: %s, ViewRot: %s"), *ViewLocation.ToString(), *ViewRotation.ToString());
	UE_LOG(LogDisplayClusterRender, VeryVerbose, TEXT("WorldToMeters: %f"), WorldToMeters);


	// Get camera ID assigned to the viewport
	const FString& CameraId = pViewport->GetRenderSettings().CameraId;

	// Get camera component assigned to the viewport (or default camera if nothing assigned)
	UDisplayClusterCameraComponent* const ViewCamera = (CameraId.IsEmpty() ? RootActor->GetDefaultCamera() : RootActor->GetCameraById(CameraId));
	if (!ViewCamera)
	{
		UE_LOG(LogDisplayClusterRender, Warning, TEXT("No camera found for viewport '%s'"), *pViewport->GetId());
		return;
	}

	if (CameraId.Len() > 0)
	{
		UE_LOG(LogDisplayClusterRender, Verbose, TEXT("Viewport '%s' has assigned camera '%s'"), *pViewport->GetId(), *CameraId);
	}

	// Get the actual camera settings
	const float CfgEyeDist = ViewCamera->GetInterpupillaryDistance();
	const bool  CfgEyeSwap = ViewCamera->GetSwapEyes();
	const float CfgNCP     = 1.f;
	const EDisplayClusterEyeStereoOffset CfgEyeOffset = ViewCamera->GetStereoOffset();

	// Calculate eye offset considering the world scale
	const float ScaledEyeDist     = CfgEyeDist * WorldToMeters;
	const float ScaledEyeOffset   = ScaledEyeDist / 2.f;
	const float EyeOffsetValues[] = { -ScaledEyeOffset, 0.f, ScaledEyeOffset };

	auto DecodeEyeType = [](const EStereoscopicPass EyePass)
	{
		switch (EyePass)
		{
		case EStereoscopicPass::eSSP_LEFT_EYE:
			return EDisplayClusterEyeType::StereoLeft;
		case EStereoscopicPass::eSSP_RIGHT_EYE:
			return EDisplayClusterEyeType::StereoRight;
		default:
			break;
		}

		return EDisplayClusterEyeType::Mono;
	};

	// Decode current eye type	
	const EDisplayClusterEyeType EyeType = DecodeEyeType(InOutViewportContext.StereoscopicEye);
	const int   EyeIndex = (int)EyeType;

	float PassOffset = 0.f;
	float PassOffsetSwap = 0.f;

	if (EyeType == EDisplayClusterEyeType::Mono)
	{
		// For monoscopic camera let's check if the "force offset" feature is used
		// * Force left (-1) ==> 0 left eye
		// * Force right (1) ==> 2 right eye
		// * Default (0) ==> 1 mono
		const int EyeOffsetIdx = 
			(CfgEyeOffset == EDisplayClusterEyeStereoOffset::None ? 0 :
			(CfgEyeOffset == EDisplayClusterEyeStereoOffset::Left ? -1 : 1));

		PassOffset = EyeOffsetValues[EyeOffsetIdx + 1];
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
	if (!pViewport->GetProjectionPolicy()->CalculateView(pViewport, ViewportContextNum, ViewLocation, ViewRotation, ViewOffset, WorldToMeters, CfgNCP, CfgNCP))
	{
		UE_LOG(LogDisplayClusterRender, Warning, TEXT("Couldn't compute view parameters for Viewport %s, ViewIdx: %d"), *pViewport->GetId(), ViewportContextNum);
	}

	// Store the view location/rotation
	InOutViewportContext.ViewLocation = ViewLocation;
	InOutViewportContext.ViewRotation = ViewRotation;
	InOutViewportContext.WorldToMeters = WorldToMeters;

	UE_LOG(LogDisplayClusterRender, VeryVerbose, TEXT("ViewLoc: %s, ViewRot: %s"), *ViewLocation.ToString(), *ViewRotation.ToString());
}

FMatrix FDisplayClusterDeviceBase::GetStereoProjectionMatrix(const enum EStereoscopicPass StereoPassType) const
{
	check(IsInGameThread());

	FMatrix PrjMatrix = FMatrix::Identity;

	if (GetViewportManager().IsSceneOpened())
	{
		uint32 ViewportContextNum = 0;
		IDisplayClusterViewport* pViewport = GetViewportManager().FindViewport(StereoPassType, &ViewportContextNum);
		if (pViewport == nullptr)
		{
			UE_LOG(LogDisplayClusterRender, Warning, TEXT("Viewport StereoPassType='%i' not found"), int(StereoPassType));
		}
		else
		{
			if (!pViewport->GetProjectionPolicy().IsValid())
			{
				// ignore viewports with uninitialized prj policy
				return PrjMatrix;
			}

			FDisplayClusterViewport_Context& InOutViewportContext = pViewport->GetContexts()[ViewportContextNum];

			if (pViewport->GetProjectionPolicy()->GetProjectionMatrix(pViewport, ViewportContextNum, PrjMatrix))
			{
				InOutViewportContext.ProjectionMatrix = PrjMatrix;
			}
			else
			{
				UE_LOG(LogDisplayClusterRender, Warning, TEXT("Got invalid projection matrix: Viewport %s, ViewIdx: %d"), *pViewport->GetId(), ViewportContextNum);
			}
		}
	}
	
	return PrjMatrix;
}

DECLARE_GPU_STAT_NAMED(nDisplay_Device_RenderFrame, TEXT("nDisplay RenderDevice::RenderFrame"));

void FDisplayClusterDeviceBase::RenderFrame_RenderThread(FRHICommandListImmediate& RHICmdList)
{
	SCOPED_GPU_STAT(RHICmdList, nDisplay_Device_RenderFrame);
	SCOPED_DRAW_EVENT(RHICmdList, nDisplay_Device_RenderFrame);

	// This one is to match interface function signatures only. The functions will provide the handlers with a proper data.
	const FIntRect StubRect(0, 0, 0, 0);

	// Get custom PP and warp&blend flags status
	const bool bWarpBlendEnabled = (CVarWarpBlendEnabled.GetValueOnRenderThread() != 0);

	// Move all render target cross gpu
	GetViewportManager().DoCrossGPUTransfers_RenderThread(MainViewport, RHICmdList);
	// Now all resources on GPU#0
	
	// Update viewports resources: overlay, vp-overla, blur, nummips, etc
	GetViewportManager().UpdateDeferredResources_RenderThread(RHICmdList);

	// Update the frame resources: post-processing, warping, and finally resolving everything to the frame resource
	GetViewportManager().UpdateFrameResources_RenderThread(RHICmdList, bWarpBlendEnabled);

	// For quadbuf stereo copy only left eye, right copy from OutputFrameTarget
	//@todo Copy QuadBuf_LeftEye/(mono,sbs,tp) to separate rtt, before UI and debug rendering
	//@todo QuadBuf_LeftEye copied latter, before present
	
	FRHITexture2D* SeparateRTT = MainViewport->GetRenderTargetTexture();
	GetViewportManager().ResolveFrameTargetToBackBuffer_RenderThread(RHICmdList, 0, 0, SeparateRTT, SeparateRTT->GetSizeXY());
}

DECLARE_GPU_STAT_NAMED(nDisplay_Device_RenderTexture, TEXT("nDisplay RenderDevice::RenderTexture"));

#include "RenderGraphUtils.h"
#include "RenderGraphBuilder.h"

void FDisplayClusterDeviceBase::RenderTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* BackBuffer, FRHITexture2D* SrcTexture, FVector2D WindowSize) const
{
	SCOPED_GPU_STAT(RHICmdList, nDisplay_Device_RenderTexture);
	SCOPED_DRAW_EVENT(RHICmdList, nDisplay_Device_RenderTexture);

	// SrcTexture contain MONO/LEFT eye with debug canvas
	// copy the render target texture to the MONO/LEFT_EYE back buffer  (MONO = mono, side_by_side, top_bottom)
	RHICmdList.CopyToResolveTarget(SrcTexture, BackBuffer, FResolveParams());
	
	if (GetRenderFrameMode() == EDisplayClusterRenderFrameMode::Stereo)
	{
		// QuadBufStereo: Copy RIGHT_EYE to backbuffer
		GetViewportManager().ResolveFrameTargetToBackBuffer_RenderThread(RHICmdList, 1, 1, BackBuffer, WindowSize);
	}
	
	// Clear render target before out frame resolving
	FRHIRenderPassInfo RPInfo(SrcTexture, ERenderTargetActions::Clear_Store);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("ClearTexture"));
	RHICmdList.EndRenderPass();
}

//////////////////////////////////////////////////////////////////////////////////////////////
// IStereoRenderTargetManager
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterDeviceBase::UpdateViewport(bool bUseSeparateRenderTarget, const class FViewport& Viewport, class SViewport* ViewportWidget)
{
	check(IsInGameThread());

	// Store viewport
	if (!MainViewport)
	{
		// UE viewport
		MainViewport = (FViewport*)&Viewport;

		/*
		// Create texture share for render viewports by config line flag
		//@todo move to right place. add on\off
		{
			// Share viewports to external apps
			for (int ViewportIndex = 0; ViewportIndex < RenderViewports.Num(); ViewportIndex++)
			{
				if (RenderViewports[ViewportIndex].IsShared())
				{
					static ITextureShare& TextureShareAPI = ITextureShare::Get();

					//@todo: add custom sync setup
					FTextureShareSyncPolicy SyncPolicy;

					FString ShareName = RenderViewports[ViewportIndex].GetId();
					EStereoscopicPass PassType = GetViewPassForIndex(IsStereoEnabled(), ViewportIndex);

					// Create shared resource for external app
					if (!TextureShareAPI.CreateShare(ShareName, SyncPolicy, ETextureShareProcess::Server))
					{
						UE_LOG(LogDisplayClusterRender, Error, TEXT("Failed create viewport share '%s'"), *ShareName);
					}
					else
					{
						// Find viewport stereoscopic pass
						int ResourceViewportIndex = RenderViewports.Num() - 1;

						// Initialize render callbacks
						TSharedPtr<ITextureShareItem> ShareItem;
						if (TextureShareAPI.GetShare(ShareName, ShareItem))
						{
							if(TextureShareAPI.LinkSceneContextToShare(ShareItem, PassType, true))
							{
								// Map viewport rect to stereoscopic pass
								TextureShareAPI.SetBackbufferRect(PassType, &RenderViewports[ViewportIndex].GetRect());
								// Begin share session
								ShareItem->BeginSession();
							}
							else
							{
								TextureShareAPI.ReleaseShare(ShareName);
								UE_LOG(LogDisplayClusterRender, Error, TEXT("failed link scene conext for share '%s'"), *ShareName);
							}
						}
					}
				}
			}
		}*/
	}
}

void FDisplayClusterDeviceBase::CalculateRenderTargetSize(const class FViewport& Viewport, uint32& InOutSizeX, uint32& InOutSizeY)
{
	check(IsInGameThread());
	check(InOutSizeX > 0 && InOutSizeY > 0);
}

bool FDisplayClusterDeviceBase::NeedReAllocateViewportRenderTarget(const class FViewport& Viewport)
{
	check(IsInGameThread());

	// Get current RT size
	const FIntPoint rtSize = Viewport.GetRenderTargetTextureSizeXY();

	// Get desired RT size
	uint32 newSizeX = rtSize.X;
	uint32 newSizeY = rtSize.Y;

	//CalculateRenderTargetSize(Viewport, newSizeX, newSizeY);

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
void FDisplayClusterDeviceBase::StartFinalPostprocessSettings(struct FPostProcessSettings* StartPostProcessingSettings, const enum EStereoscopicPass StereoPassType)
{
	check(IsInGameThread());

	// eSSP_FULL pass reserved for UE4 internal render
	if (StereoPassType != EStereoscopicPass::eSSP_FULL)
	{
		IDisplayClusterViewport* pViewport = GetViewportManager().FindViewport(StereoPassType);
		if (pViewport)
		{
			pViewport->GetViewport_CustomPostProcessSettings().DoPostProcess(IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::Start, StartPostProcessingSettings);
		}
	}
}

bool FDisplayClusterDeviceBase::OverrideFinalPostprocessSettings(struct FPostProcessSettings* OverridePostProcessingSettings, const enum EStereoscopicPass StereoPassType, float& BlendWeight)
{
	check(IsInGameThread());

	// eSSP_FULL pass reserved for UE4 internal render
	if (StereoPassType != EStereoscopicPass::eSSP_FULL)
	{
		IDisplayClusterViewport* pViewport = GetViewportManager().FindViewport(StereoPassType);
		if (pViewport)
		{
			return pViewport->GetViewport_CustomPostProcessSettings().DoPostProcess(IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::Override, OverridePostProcessingSettings, &BlendWeight);
		}
	}

	return false;
}

void FDisplayClusterDeviceBase::EndFinalPostprocessSettings(struct FPostProcessSettings* FinalPostProcessingSettings, const enum EStereoscopicPass StereoPassType)
{
	check(IsInGameThread());

	// eSSP_FULL pass reserved for UE4 internal render
	if (StereoPassType != EStereoscopicPass::eSSP_FULL)
	{
		IDisplayClusterViewport* pViewport = GetViewportManager().FindViewport(StereoPassType);
		if (pViewport)
		{
			pViewport->GetViewport_CustomPostProcessSettings().DoPostProcess(IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::Final, FinalPostProcessingSettings);
		}
	}
}

