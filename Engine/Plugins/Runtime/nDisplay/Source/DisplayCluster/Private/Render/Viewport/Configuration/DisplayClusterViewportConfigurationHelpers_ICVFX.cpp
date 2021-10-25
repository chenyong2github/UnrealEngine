// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Viewport/Configuration/DisplayClusterViewportConfigurationHelpers_ICVFX.h"

#include "Render/Viewport/Configuration/DisplayClusterViewportConfigurationHelpers.h"
#include "Render/Viewport/Configuration/DisplayClusterViewportConfigurationHelpers_OpenColorIO.h"
#include "Render/Viewport/Configuration/DisplayClusterViewportConfigurationHelpers_Postprocess.h"
#include "Render/Viewport/Configuration/DisplayClusterViewportConfigurationHelpers_Visibility.h"

#include "Render/Viewport/DisplayClusterViewport.h"
#include "Render/Viewport/DisplayClusterViewportManager.h"
#include "Render/Viewport/DisplayClusterViewportStrings.h"

#include "DisplayClusterRootActor.h"

#include "DisplayClusterConfigurationTypes.h"
#include "DisplayClusterConfigurationTypes_Viewport.h"
#include "DisplayClusterConfigurationTypes_ICVFX.h"
#include "DisplayClusterConfigurationTypes_PostRender.h"

#include "ShaderParameters/DisplayClusterShaderParameters_PostprocessBlur.h"
#include "ShaderParameters/DisplayClusterShaderParameters_GenerateMips.h"
#include "ShaderParameters/DisplayClusterShaderParameters_Override.h"
#include "ShaderParameters/DisplayClusterShaderParameters_ICVFX.h"

#include "Render/Viewport/Containers/DisplayClusterViewport_RenderSettings.h"
#include "Render/Viewport/Containers/DisplayClusterViewport_RenderSettingsICVFX.h"
#include "Render/Viewport/Containers/DisplayClusterViewport_PostRenderSettings.h"

#include "Containers/DisplayClusterProjectionCameraPolicySettings.h"
#include "DisplayClusterProjectionStrings.h"

#include "Components/DisplayClusterICVFXCameraComponent.h"

#include "Misc/DisplayClusterLog.h"

// Initialize camera policy with camera component and settings
static bool ImplUpdateCameraProjectionSettings(TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe>& InOutCameraProjection, const FDisplayClusterConfigurationICVFX_CameraSettings& CameraSettings, UCameraComponent* const CameraComponent)
{
	FDisplayClusterProjectionCameraPolicySettings PolicyCameraSettings;
	PolicyCameraSettings.FOVMultiplier = CameraSettings.FieldOfViewMultiplier;

	// Lens correction
	PolicyCameraSettings.FrustumRotation = CameraSettings.FrustumRotation;
	PolicyCameraSettings.FrustumOffset   = CameraSettings.FrustumOffset;

	// Initialize camera policy with camera component and settings
	return IDisplayClusterProjection::Get().CameraPolicySetCamera(InOutCameraProjection, CameraComponent, PolicyCameraSettings);
}

// Return unique ICVFX name
FString ImplGetNameICVFX(const FString& InViewportId, const FString& InResourceId)
{
	return FString::Printf(TEXT("%s_%s_%s"), DisplayClusterViewportStrings::icvfx::prefix, *InViewportId, *InResourceId);
}

static FDisplayClusterViewport* ImplFindViewport(ADisplayClusterRootActor& RootActor, const FString& InViewportId, const FString& InResourceId)
{
	FDisplayClusterViewportManager* ViewportManager = FDisplayClusterViewportConfigurationHelpers_ICVFX::GetViewportManager(RootActor);
	if (ViewportManager)
	{
		return ViewportManager->ImplFindViewport(ImplGetNameICVFX(InViewportId, InResourceId));
	}

	return nullptr;
}

static bool ImplCreateProjectionPolicy(const FString& InViewportId, const FString& InResourceId, bool bIsCameraProjection, TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe>& OutProjPolicy)
{
	FDisplayClusterConfigurationProjection CameraProjectionPolicyConfig;
	CameraProjectionPolicyConfig.Type = bIsCameraProjection ? DisplayClusterProjectionStrings::projection::Camera : DisplayClusterProjectionStrings::projection::Link;

	// Create projection policy for viewport
	OutProjPolicy = FDisplayClusterViewportManager::CreateProjectionPolicy(ImplGetNameICVFX(InViewportId, InResourceId), &CameraProjectionPolicyConfig);

	if (!OutProjPolicy.IsValid())
	{
		UE_LOG(LogDisplayClusterViewport, Error, TEXT("ICVFX Viewport '%s': projection policy for resource '%s' not created."), *InViewportId, *InResourceId);
		return false;
	}

	return true;
}

//------------------------------------------------------------------------------------------
//                FDisplayClusterViewportConfigurationHelpers_ICVFX
//------------------------------------------------------------------------------------------

FDisplayClusterViewport* FDisplayClusterViewportConfigurationHelpers_ICVFX::ImplCreateViewport(ADisplayClusterRootActor& RootActor, const FString& InViewportId, const FString& InResourceId, TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe>& InProjectionPolicy)
{
	check(InProjectionPolicy.IsValid());

	FDisplayClusterViewportManager* ViewportManager = FDisplayClusterViewportConfigurationHelpers_ICVFX::GetViewportManager(RootActor);
	if (ViewportManager)
	{
		// Create viewport for new projection policy
		FDisplayClusterViewport* NewViewport = ViewportManager->ImplCreateViewport(ImplGetNameICVFX(InViewportId, InResourceId), InProjectionPolicy);
		if (NewViewport != nullptr)
		{
			// Mark as internal resource
			NewViewport->RenderSettingsICVFX.RuntimeFlags |= ViewportRuntime_InternalResource;

			// Dont show ICVFX composing viewports on frame target
			NewViewport->RenderSettings.bVisible = false;

			return NewViewport;
		}
	}

	return nullptr;
}

FDisplayClusterViewportManager* FDisplayClusterViewportConfigurationHelpers_ICVFX::GetViewportManager(ADisplayClusterRootActor& RootActor)
{
	FDisplayClusterViewportManager* ViewportManager = static_cast<FDisplayClusterViewportManager*>(RootActor.GetViewportManager());
	if (ViewportManager == nullptr)
	{
		UE_LOG(LogDisplayClusterViewport, Error, TEXT("Viewport manager not exist in root actor."));
	}

	return ViewportManager;
}

FDisplayClusterViewport* FDisplayClusterViewportConfigurationHelpers_ICVFX::FindCameraViewport(ADisplayClusterRootActor& RootActor, UDisplayClusterICVFXCameraComponent& InCameraComponent)
{
	const FString CameraId = InCameraComponent.GetCameraUniqueId();

	return ImplFindViewport(RootActor, CameraId, DisplayClusterViewportStrings::icvfx::camera);
}

FDisplayClusterViewport* FDisplayClusterViewportConfigurationHelpers_ICVFX::GetOrCreateCameraViewport(ADisplayClusterRootActor& RootActor, UDisplayClusterICVFXCameraComponent& InCameraComponent)
{
	const FString CameraId = InCameraComponent.GetCameraUniqueId();

	FDisplayClusterViewport* CameraViewport = ImplFindViewport(RootActor, CameraId, DisplayClusterViewportStrings::icvfx::camera);

	// Create new camera viewport
	if (CameraViewport == nullptr)
	{
		TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe> CameraProjectionPolicy;
		if (!ImplCreateProjectionPolicy(CameraId, DisplayClusterViewportStrings::icvfx::camera, true, CameraProjectionPolicy))
		{
			return nullptr;
		}

		CameraViewport = ImplCreateViewport(RootActor, CameraId, DisplayClusterViewportStrings::icvfx::camera, CameraProjectionPolicy);
		if (CameraViewport == nullptr)
		{
			return nullptr;
		}
	}

	// Mark viewport as used
	CameraViewport->RenderSettingsICVFX.RuntimeFlags &= ~(ViewportRuntime_Unused);

	// Add viewport ICVFX usage as Incamera
	CameraViewport->RenderSettingsICVFX.RuntimeFlags |= ViewportRuntime_ICVFXIncamera;

	return CameraViewport;
}

FDisplayClusterViewport* FDisplayClusterViewportConfigurationHelpers_ICVFX::GetOrCreateChromakeyViewport(ADisplayClusterRootActor& RootActor, UDisplayClusterICVFXCameraComponent& InCameraComponent)
{
	const FString CameraId = InCameraComponent.GetCameraUniqueId();

	FDisplayClusterViewport* ChromakeyViewport = ImplFindViewport(RootActor, CameraId, DisplayClusterViewportStrings::icvfx::chromakey);

	// Create new chromakey viewport
	if (ChromakeyViewport == nullptr)
	{
		TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe> ChromakeyProjectionPolicy;
		if (!ImplCreateProjectionPolicy(CameraId, DisplayClusterViewportStrings::icvfx::chromakey, false, ChromakeyProjectionPolicy))
		{
			return nullptr;
		}

		ChromakeyViewport = ImplCreateViewport(RootActor, CameraId, DisplayClusterViewportStrings::icvfx::chromakey, ChromakeyProjectionPolicy);
		if (ChromakeyViewport == nullptr)
		{
			return nullptr;
		}
	}

	// Mark viewport as used
	ChromakeyViewport->RenderSettingsICVFX.RuntimeFlags &= ~(ViewportRuntime_Unused);

	// Add viewport ICVFX usage as Chromakey
	ChromakeyViewport->RenderSettingsICVFX.RuntimeFlags |= ViewportRuntime_ICVFXChromakey;

	return ChromakeyViewport;
}

FDisplayClusterViewport* FDisplayClusterViewportConfigurationHelpers_ICVFX::GetOrCreateLightcardViewport(FDisplayClusterViewport& BaseViewport, ADisplayClusterRootActor& RootActor, bool bIsOpenColorIOViewportExist)
{
	// Create new lightcard viewport
	const FString ResourceId = bIsOpenColorIOViewportExist ? DisplayClusterViewportStrings::icvfx::lightcard_OCIO : DisplayClusterViewportStrings::icvfx::lightcard;

	FDisplayClusterViewport* LightcardViewport = ImplFindViewport(RootActor, BaseViewport.GetId(), ResourceId);
	if (LightcardViewport == nullptr)
	{
		TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe> LightcardProjectionPolicy;
		if (!ImplCreateProjectionPolicy(BaseViewport.GetId(), ResourceId, false, LightcardProjectionPolicy))
		{
			return nullptr;
		}

		LightcardViewport = ImplCreateViewport(RootActor, BaseViewport.GetId(), ResourceId, LightcardProjectionPolicy);
		if (LightcardViewport == nullptr)
		{
			return nullptr;
		}
	}

	// Mark viewport as used
	LightcardViewport->RenderSettingsICVFX.RuntimeFlags &= ~(ViewportRuntime_Unused);

	// Add viewport ICVFX usage as Lightcard
	LightcardViewport->RenderSettingsICVFX.RuntimeFlags |= ViewportRuntime_ICVFXLightcard;
	if (bIsOpenColorIOViewportExist)
	{
		LightcardViewport->RenderSettingsICVFX.RuntimeFlags |= ViewportRuntime_ICVFXLightcardAlpha;
	}

	return LightcardViewport;
}

bool FDisplayClusterViewportConfigurationHelpers_ICVFX::IsCameraUsed(UDisplayClusterICVFXCameraComponent& InCameraComponent)
{
	const FDisplayClusterConfigurationICVFX_CameraSettings& CameraSettings = InCameraComponent.GetCameraSettingsICVFX();

	// Check rules for camera settings:
	if (CameraSettings.bEnable == false)
	{
		// dont use camera if disabled
		return false;
	}

	if (CameraSettings.RenderSettings.Replace.bAllowReplace && CameraSettings.RenderSettings.Replace.SourceTexture == nullptr)
	{
		// RenderSettings.Override require source texture
		return false;
	}

	return true;
}

FDisplayClusterShaderParameters_ICVFX::FCameraSettings FDisplayClusterViewportConfigurationHelpers_ICVFX::GetShaderParametersCameraSettings(const FDisplayClusterViewport& InCameraViewport, ADisplayClusterRootActor& RootActor, UDisplayClusterICVFXCameraComponent& InCameraComponent)
{
	const USceneComponent* OriginComp = RootActor.GetRootComponent();
	const FDisplayClusterConfigurationICVFX_CameraSettings& CameraSettings = InCameraComponent.GetCameraSettingsICVFX();

	FDisplayClusterShaderParameters_ICVFX::FCameraSettings Result;

	Result.Resource.ViewportId = InCameraViewport.GetId();
	Result.Local2WorldTransform = OriginComp->GetComponentTransform();

	Result.SoftEdge.X = CameraSettings.SoftEdge.Horizontal;
	Result.SoftEdge.Y = CameraSettings.SoftEdge.Vertical;
	Result.SoftEdge.Z = 0;

	const FString InnerFrustumID = InCameraComponent.GetCameraUniqueId();
	const int CameraRenderOrder = RootActor.GetInnerFrustumPriority(InnerFrustumID);

	Result.RenderOrder = (CameraRenderOrder<0) ? CameraSettings.RenderSettings.RenderOrder : CameraRenderOrder;

	return Result;
}

bool FDisplayClusterViewportConfigurationHelpers_ICVFX::GetCameraContext(UDisplayClusterICVFXCameraComponent& InCameraComponent, FCameraContext_ICVFX& OutCameraContext)
{
	const FDisplayClusterConfigurationICVFX_CameraSettings& CameraSettings = InCameraComponent.GetCameraSettingsICVFX();

	// Create new camera projection policy for camera viewport
	TSharedPtr<IDisplayClusterProjectionPolicy, ESPMode::ThreadSafe> CameraProjectionPolicy;
	if (!ImplCreateProjectionPolicy(InCameraComponent.GetCameraUniqueId(), DisplayClusterViewportStrings::icvfx::camera, true, CameraProjectionPolicy))
	{
		return false;
	}

	// Initialize camera policy with camera component and settings
	if (!ImplUpdateCameraProjectionSettings(CameraProjectionPolicy, CameraSettings, InCameraComponent.GetCameraComponent()))
	{
		return false;
	}

	// Get camera pos-rot-prj from policy
	const float WorldToMeters = 100.f;
	const float CfgNCP = 1.f;
	const FVector ViewOffset = FVector::ZeroVector;

	if (CameraProjectionPolicy->CalculateView(nullptr, 0, OutCameraContext.ViewLocation, OutCameraContext.ViewRotation, ViewOffset, WorldToMeters, CfgNCP, CfgNCP) &&
		CameraProjectionPolicy->GetProjectionMatrix(nullptr, 0, OutCameraContext.PrjMatrix))
	{
		return true;
	}

	return false;
}

void FDisplayClusterViewportConfigurationHelpers_ICVFX::UpdateCameraViewportSettings(FDisplayClusterViewport& DstViewport, ADisplayClusterRootActor& RootActor, UDisplayClusterICVFXCameraComponent& InCameraComponent)
{
	const FDisplayClusterConfigurationICVFX_StageSettings&  StageSettings = RootActor.GetStageSettings();
	const FDisplayClusterConfigurationICVFX_CameraSettings& CameraSettings = InCameraComponent.GetCameraSettingsICVFX();

	// Reset runtime flags from prev frame:
	DstViewport.ResetRuntimeParameters();

	// incamera textrure used as overlay
	DstViewport.RenderSettings.bVisible = false;

	// use framecolor instead of viewport rendering
	const FDisplayClusterConfigurationICVFX_ChromakeySettings& ChromakeySettings = CameraSettings.Chromakey;
	if (ChromakeySettings.bEnable && !ChromakeySettings.ChromakeyRenderTexture.bEnable)
	{
		DstViewport.RenderSettings.bSkipRendering = true;
	}

	// Update camera viewport projection policy settings
	ImplUpdateCameraProjectionSettings(DstViewport.ProjectionPolicy, CameraSettings, InCameraComponent.GetCameraComponent());

	// OCIO
	FDisplayClusterViewportConfigurationHelpers_OpenColorIO::UpdateICVFXCameraViewport(DstViewport, RootActor, InCameraComponent);

	// Motion blur:
	const FDisplayClusterViewport_CameraMotionBlur CameraMotionBlurParameters = InCameraComponent.GetMotionBlurParameters();
	DstViewport.CameraMotionBlur.BlurSetup = CameraMotionBlurParameters;

	// FDisplayClusterConfigurationICVFX_CameraSettings
	DstViewport.RenderSettings.CameraId.Empty();
	DstViewport.Owner.SetViewportBufferRatio(DstViewport, CameraSettings.BufferRatio);

	// UDisplayClusterConfigurationICVFX_CameraRenderSettings
	FIntPoint DesiredSize(0);
	// Camera viewport frame size:
	if (CameraSettings.RenderSettings.CustomFrameSize.bUseCustomSize)
	{
		DesiredSize.X = CameraSettings.RenderSettings.CustomFrameSize.CustomWidth;
		DesiredSize.Y = CameraSettings.RenderSettings.CustomFrameSize.CustomHeight;
	}
	else
	{
		DesiredSize.X = StageSettings.DefaultFrameSize.Width;
		DesiredSize.Y = StageSettings.DefaultFrameSize.Height;
	}

	DstViewport.RenderSettings.Rect = DstViewport.GetValidRect(FIntRect(FIntPoint(0, 0), DesiredSize), TEXT("Configuration Camera Frame Size"));

	// Apply postprocess for camera
	FDisplayClusterViewportConfigurationHelpers_Postprocess::UpdateCameraPostProcessSettings(DstViewport, RootActor, InCameraComponent);

	FDisplayClusterViewportConfigurationHelpers::UpdateViewportSetting_Override(DstViewport, CameraSettings.RenderSettings.Replace);
	FDisplayClusterViewportConfigurationHelpers::UpdateViewportSetting_PostprocessBlur(DstViewport, CameraSettings.RenderSettings.PostprocessBlur);
	FDisplayClusterViewportConfigurationHelpers::UpdateViewportSetting_GenerateMips(DstViewport, CameraSettings.RenderSettings.GenerateMips);

	// UDisplayClusterConfigurationICVFX_CameraAdvancedRenderSettings
	const FDisplayClusterConfigurationICVFX_CameraAdvancedRenderSettings& InAdvancedRS = CameraSettings.RenderSettings.AdvancedRenderSettings;
	{
		DstViewport.RenderSettings.RenderTargetRatio = InAdvancedRS.RenderTargetRatio;
		DstViewport.RenderSettings.GPUIndex = InAdvancedRS.GPUIndex;
		DstViewport.RenderSettings.StereoGPUIndex = InAdvancedRS.StereoGPUIndex;
		DstViewport.RenderSettings.RenderFamilyGroup = InAdvancedRS.RenderFamilyGroup;

		FDisplayClusterViewportConfigurationHelpers::UpdateViewportStereoMode(DstViewport, InAdvancedRS.StereoMode);
	}
}

void FDisplayClusterViewportConfigurationHelpers_ICVFX::UpdateChromakeyViewportSettings(FDisplayClusterViewport& DstViewport, FDisplayClusterViewport& InCameraViewport, ADisplayClusterRootActor& RootActor, UDisplayClusterICVFXCameraComponent& InCameraComponent)
{
	const FDisplayClusterConfigurationICVFX_StageSettings& StageSettings = RootActor.GetStageSettings();
	const FDisplayClusterConfigurationICVFX_CameraSettings& CameraSettings = InCameraComponent.GetCameraSettingsICVFX();
	const FDisplayClusterConfigurationICVFX_ChromakeySettings& ChromakeySettings = CameraSettings.Chromakey;

	check(ChromakeySettings.bEnable && ChromakeySettings.ChromakeyRenderTexture.bEnable);

	// Reset runtime flags from prev frame:
	DstViewport.ResetRuntimeParameters();

	// Chromakey used as overlay
	DstViewport.RenderSettings.bVisible = false;

	// Use special capture mode (this change RTT format and render flags)
	DstViewport.RenderSettings.CaptureMode = EDisplayClusterViewportCaptureMode::Chromakey;

	// UDisplayClusterConfigurationICVFX_ChromakeyRenderSettings
	const FDisplayClusterConfigurationICVFX_ChromakeyRenderSettings& InRenderSettings = ChromakeySettings.ChromakeyRenderTexture;
	{
		FDisplayClusterViewportConfigurationHelpers::UpdateViewportSetting_Override(DstViewport, InRenderSettings.Replace);
		FDisplayClusterViewportConfigurationHelpers::UpdateViewportSetting_PostprocessBlur(DstViewport, InRenderSettings.PostprocessBlur);
		FDisplayClusterViewportConfigurationHelpers::UpdateViewportSetting_GenerateMips(DstViewport, InRenderSettings.GenerateMips);

		// Update visibility settings only for rendered viewports
		if (!DstViewport.PostRenderSettings.Replace.IsEnabled())
		{
			check(FDisplayClusterViewportConfigurationHelpers_Visibility::IsValid(InRenderSettings.ShowOnlyList));

			FDisplayClusterViewportConfigurationHelpers_Visibility::UpdateShowOnlyList(DstViewport, RootActor, InRenderSettings.ShowOnlyList);
		}
	}

	FDisplayClusterViewportConfigurationHelpers::UpdateViewportSetting_OverlayRenderSettings(DstViewport, InRenderSettings.AdvancedRenderSettings);


	// Attach to parent viewport
	DstViewport.RenderSettings.AssignParentViewport(InCameraViewport.GetId(), InCameraViewport.RenderSettings);

	// Support custom overlay size
	if (ChromakeySettings.ChromakeyRenderTexture.CustomSize.bUseCustomSize)
	{
		FIntPoint DesiredSize;
		DesiredSize.X = ChromakeySettings.ChromakeyRenderTexture.CustomSize.CustomWidth;
		DesiredSize.Y = ChromakeySettings.ChromakeyRenderTexture.CustomSize.CustomHeight;

		DstViewport.RenderSettings.Rect = DstViewport.GetValidRect(FIntRect(FIntPoint(0, 0), DesiredSize), TEXT("Configuration custom chromakey Frame Size"));
	}

	// Debug: override the texture of the target viewport from this chromakeyRTT
	if (ChromakeySettings.ChromakeyRenderTexture.bReplaceCameraViewport)
	{
		InCameraViewport.RenderSettings.OverrideViewportId = DstViewport.GetId();
		InCameraViewport.RenderSettings.bSkipRendering = true;
	}
}

void FDisplayClusterViewportConfigurationHelpers_ICVFX::UpdateCameraSettings_Chromakey(FDisplayClusterShaderParameters_ICVFX::FCameraSettings& InOutCameraSettings, const FDisplayClusterConfigurationICVFX_ChromakeySettings& InChromakeySettings, FDisplayClusterViewport* InChromakeyViewport)
{
	if (InChromakeySettings.bEnable)
	{
		if (InChromakeySettings.ChromakeyRenderTexture.bEnable)
		{
			if (InChromakeySettings.ChromakeyRenderTexture.bReplaceCameraViewport)
			{
				InOutCameraSettings.ChromakeySource = EDisplayClusterShaderParametersICVFX_ChromakeySource::Disabled;
				return;
			}
			else
			{
				check(InChromakeyViewport);

				InOutCameraSettings.ChromakeySource = EDisplayClusterShaderParametersICVFX_ChromakeySource::ChromakeyLayers;
				InOutCameraSettings.Chromakey.ViewportId = InChromakeyViewport->GetId();
				InOutCameraSettings.ChromakeyColor = InChromakeySettings.ChromakeyColor;
			}
		}
		else
		{
			InOutCameraSettings.ChromakeySource = EDisplayClusterShaderParametersICVFX_ChromakeySource::FrameColor;
			InOutCameraSettings.ChromakeyColor = InChromakeySettings.ChromakeyColor;
		}
	}
	else
	{
		InOutCameraSettings.ChromakeySource = EDisplayClusterShaderParametersICVFX_ChromakeySource::Disabled;
	}
}

void FDisplayClusterViewportConfigurationHelpers_ICVFX::UpdateCameraSettings_ChromakeyMarkers(FDisplayClusterShaderParameters_ICVFX::FCameraSettings& InOutCameraSettings, const FDisplayClusterConfigurationICVFX_ChromakeyMarkers& InChromakeyMarkers)
{
	InOutCameraSettings.ChromakeMarkerTextureRHI.SafeRelease();

	if (InChromakeyMarkers.bEnable && InChromakeyMarkers.MarkerTileRGBA != nullptr)
	{
		InOutCameraSettings.ChromakeyMarkersColor = InChromakeyMarkers.MarkerColor;
		InOutCameraSettings.ChromakeyMarkersScale = InChromakeyMarkers.MarkerSizeScale;
		InOutCameraSettings.ChromakeyMarkersDistance = InChromakeyMarkers.MarkerTileDistance;
		InOutCameraSettings.ChromakeyMarkersOffset = InChromakeyMarkers.MarkerTileOffset;

		// Assign texture RHI ref
		FTextureResource* MarkersResource = InChromakeyMarkers.MarkerTileRGBA->Resource;
		if (MarkersResource)
		{
			InOutCameraSettings.ChromakeMarkerTextureRHI = MarkersResource->TextureRHI;
		}
	}
}

bool FDisplayClusterViewportConfigurationHelpers_ICVFX::IsShouldUseLightcard(const FDisplayClusterConfigurationICVFX_LightcardSettings& InLightcardSettings)
{
	if (InLightcardSettings.bEnable == false)
	{
		// dont use lightcard if disabled
		return false;
	}

	if (InLightcardSettings.RenderSettings.Replace.bAllowReplace)
	{
		if (InLightcardSettings.RenderSettings.Replace.SourceTexture == nullptr)
		{
			// LightcardSettings.Override require source texture.
			return false;
		}

		return true;
	}

	// Lightcard require layers for render
	return FDisplayClusterViewportConfigurationHelpers_Visibility::IsValid(InLightcardSettings.ShowOnlyList);
}

void FDisplayClusterViewportConfigurationHelpers_ICVFX::UpdateLightcardViewportSetting(FDisplayClusterViewport& DstViewport, FDisplayClusterViewport& BaseViewport, ADisplayClusterRootActor& RootActor, bool bIsOpenColorIOViewportExist)
{
	const FDisplayClusterConfigurationICVFX_StageSettings& StageSettings = RootActor.GetStageSettings();
	const FDisplayClusterConfigurationICVFX_LightcardSettings& LightcardSettings = StageSettings.Lightcard;

	check(LightcardSettings.bEnable);

	// Reset runtime flags from prev frame:
	DstViewport.ResetRuntimeParameters();

	// LIghtcard texture used as overlay
	DstViewport.RenderSettings.bVisible = false;

	bool bIsLightcardUseResolvedScene = false;
	if (!bIsOpenColorIOViewportExist)
	{
		if (FDisplayClusterViewportConfigurationHelpers_Postprocess::UpdateLightcardPostProcessSettings(DstViewport, BaseViewport, RootActor))
		{
			bIsLightcardUseResolvedScene = true;
		}

		if (FDisplayClusterViewportConfigurationHelpers_OpenColorIO::UpdateLightcardViewport(DstViewport, BaseViewport, RootActor))
		{
			bIsLightcardUseResolvedScene = true;
		}
	}

	if (bIsLightcardUseResolvedScene)
	{
		// ligthcard used OCIO - capture 2 vp, [1] color with OCIO and [2] alpha
		// to support OCIO capture mode must be without alpha, so we need to render second vp with alpha
		DstViewport.RenderSettings.CaptureMode = EDisplayClusterViewportCaptureMode::Lightcard_OCIO;
		DstViewport.RenderSettingsICVFX.RuntimeFlags |= ViewportRuntime_ICVFXLightcardColor;
	}
	else
	{
		// no OCIO, capture 1 vp - color+alpha
		DstViewport.RenderSettings.CaptureMode = EDisplayClusterViewportCaptureMode::Lightcard;
	}

	const FDisplayClusterConfigurationICVFX_LightcardRenderSettings& InRenderSettings = LightcardSettings.RenderSettings;
	{
		FDisplayClusterViewportConfigurationHelpers::UpdateViewportSetting_Override(DstViewport, InRenderSettings.Replace);
		FDisplayClusterViewportConfigurationHelpers::UpdateViewportSetting_PostprocessBlur(DstViewport, InRenderSettings.PostprocessBlur);
		FDisplayClusterViewportConfigurationHelpers::UpdateViewportSetting_GenerateMips(DstViewport, InRenderSettings.GenerateMips);

		// Update visibility settings only for rendered viewports
		if (!DstViewport.PostRenderSettings.Replace.IsEnabled())
		{
			check(FDisplayClusterViewportConfigurationHelpers_Visibility::IsValid(LightcardSettings.ShowOnlyList));

			FDisplayClusterViewportConfigurationHelpers_Visibility::UpdateShowOnlyList(DstViewport, RootActor, LightcardSettings.ShowOnlyList);
		}

		FDisplayClusterViewportConfigurationHelpers::UpdateViewportSetting_OverlayRenderSettings(DstViewport, InRenderSettings.AdvancedRenderSettings);
	}

	// Attach to parent viewport
	DstViewport.RenderSettings.AssignParentViewport(BaseViewport.GetId(), BaseViewport.RenderSettings);

	// Global lighcard rendering mode
	if ((BaseViewport.RenderSettingsICVFX.Flags & ViewportICVFX_OverrideLightcardMode) == 0)
	{
		// Use global lightcard blending mode
		switch (LightcardSettings.Blendingmode)
		{
		default:
		case EDisplayClusterConfigurationICVFX_LightcardRenderMode::Over:
			BaseViewport.RenderSettingsICVFX.ICVFX.LightcardMode = EDisplayClusterShaderParametersICVFX_LightcardRenderMode::Over;
			break;
		case EDisplayClusterConfigurationICVFX_LightcardRenderMode::Under:
			BaseViewport.RenderSettingsICVFX.ICVFX.LightcardMode = EDisplayClusterShaderParametersICVFX_LightcardRenderMode::Under;
			break;
		};
	}
	// Debug: override the texture of the target viewport from this lightcard RTT
	if (InRenderSettings.bReplaceViewport)
	{
		BaseViewport.RenderSettings.OverrideViewportId = DstViewport.GetId();
		BaseViewport.RenderSettings.bSkipRendering = true;
	}
	else
	{
		if (bIsLightcardUseResolvedScene)
		{
			BaseViewport.RenderSettingsICVFX.ICVFX.Lightcard_OCIO.ViewportId = DstViewport.GetId();
		}
		else
		{
			BaseViewport.RenderSettingsICVFX.ICVFX.Lightcard.ViewportId = DstViewport.GetId();
		}
	}
}
