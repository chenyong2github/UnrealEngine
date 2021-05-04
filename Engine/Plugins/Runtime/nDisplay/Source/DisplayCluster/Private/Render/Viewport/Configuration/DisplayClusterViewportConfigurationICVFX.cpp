// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterViewportConfigurationICVFX.h"

#include "DisplayClusterViewportConfigurationHelpers.h"

#include "DisplayClusterRootActor.h"

#include "IDisplayClusterProjection.h"
#include "Render/Projection/IDisplayClusterProjectionPolicy.h"
#include "Containers/DisplayClusterProjectionCameraPolicySettings.h"
#include "DisplayClusterProjectionStrings.h"

#include "Render/Viewport/DisplayClusterViewport.h"
#include "Render/Viewport/DisplayClusterViewportManager.h"

#include "Components/DisplayClusterICVFX_CineCameraComponent.h"
#include "Components/DisplayClusterICVFX_RefCineCameraComponent.h"

////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterViewportConfigurationCameraViewport
////////////////////////////////////////////////////////////////////////////////
class FDisplayClusterViewportConfigurationCameraViewport
{
public:
	FDisplayClusterViewportConfigurationCameraViewport(const FTransform& InLocal2WorldTransform, FDisplayClusterViewportConfigurationICVFX& InConfigurationICVFX, UCameraComponent* const InCameraComponent, const FString InCameraId, const UDisplayClusterConfigurationICVFX_CameraSettings& InCameraSettings)
		: ConfigurationICVFX(InConfigurationICVFX)
		, CameraComponent(InCameraComponent)
		, CameraId(InCameraId)
		, CameraSettings(InCameraSettings)
		, Local2WorldTransform(InLocal2WorldTransform)

	{
		check(CameraComponent);
		check(CameraId.IsEmpty() == false);

		CameraRenderOrder = CameraSettings.RenderSettings.RenderOrder;
	}

	void SetMotionBlurParameters(const FDisplayClusterViewport_CameraMotionBlur CameraMotionBlurParameters)
	{
		if (CameraViewport)
		{
			CameraViewport->CameraMotionBlur.BlurSetup = CameraMotionBlurParameters;
		}
	}

	// Initialize camera policy with camera component and settings
	bool ImplUpdateCameraProjectionSettings(TSharedPtr<IDisplayClusterProjectionPolicy>& InOutCameraProjection)
	{
		FDisplayClusterProjectionCameraPolicySettings PolicyCameraSettings;
		PolicyCameraSettings.FOVMultiplier = CameraSettings.FieldOfViewMultiplier;

		// Lens correction
		PolicyCameraSettings.FrustumRotation = CameraSettings.FrustumRotation;
		PolicyCameraSettings.FrustumOffset = CameraSettings.FrustumOffset;

		// Initialize camera policy with camera component and settings
		return IDisplayClusterProjection::Get().CameraPolicySetCamera(InOutCameraProjection, CameraComponent, PolicyCameraSettings);
	}

	bool CreateCameraViewport()
	{
		check(CameraViewport == nullptr);

		CameraViewport = ConfigurationICVFX.FindViewport(CameraId, DisplayClusterViewportStrings::icvfx::camera);

		// Create new camera viewport
		if (CameraViewport == nullptr)
		{
			TSharedPtr<IDisplayClusterProjectionPolicy> CameraProjectionPolicy;
			if (!ConfigurationICVFX.CreateProjectionPolicy(CameraId, DisplayClusterViewportStrings::icvfx::camera, true, CameraProjectionPolicy))
			{
				//@todo handle error
				return false;
			}

			CameraViewport = ConfigurationICVFX.CreateViewport(CameraId, DisplayClusterViewportStrings::icvfx::camera, CameraProjectionPolicy);
			if (CameraViewport == nullptr)
			{
				//@todo handle error
				return false;
			}
		}

		// Mark viewport as used
		CameraViewport->RenderSettingsICVFX.RuntimeFlags &= ~(ViewportRuntime_Unused);

		// Add viewport ICVFX usage as Incamera
		CameraViewport->RenderSettingsICVFX.RuntimeFlags |= ViewportRuntime_ICVFXIncamera;

		// Update camera viewport projection policy settings
		ImplUpdateCameraProjectionSettings(CameraViewport->ProjectionPolicy);

		// Update camera viewport settings
		DisplayClusterViewportConfigurationHelpers::UpdateCameraViewportSetting(*CameraViewport, CameraSettings, ConfigurationICVFX.StageSettings);

		// Setup camera visibility
		DisplayClusterViewportConfigurationHelpers::UpdateVisibilitySetting(*CameraViewport, EDisplayClusterViewport_VisibilityMode::Hide, ConfigurationICVFX.StageSettings.HideList);

		return true;
	}

	bool CreateChromakeyViewport(const FDisplayClusterConfigurationICVFX_ChromakeySettings& InChromakeySettings)
	{
		check(CameraViewport);

		// Create new chromakey viewport
		ChromakeyViewport = ConfigurationICVFX.FindViewport(CameraId, DisplayClusterViewportStrings::icvfx::chromakey);
		if (ChromakeyViewport == nullptr)
		{
			TSharedPtr<IDisplayClusterProjectionPolicy> ChromakeyProjectionPolicy;
			if (!ConfigurationICVFX.CreateProjectionPolicy(CameraId, DisplayClusterViewportStrings::icvfx::chromakey, false, ChromakeyProjectionPolicy))
			{
				//@todo handle error
				return false;
			}

			ChromakeyViewport = ConfigurationICVFX.CreateViewport(CameraId, DisplayClusterViewportStrings::icvfx::chromakey, ChromakeyProjectionPolicy);
			if (ChromakeyViewport == nullptr)
			{
				//@todo handle error
				return false;
			}
		}

		// Mark viewport as used
		ChromakeyViewport->RenderSettingsICVFX.RuntimeFlags &= ~(ViewportRuntime_Unused);

		// Add viewport ICVFX usage as Chromakey
		ChromakeyViewport->RenderSettingsICVFX.RuntimeFlags |= ViewportRuntime_ICVFXChromakey;

		// Update chromakey viewport settings
		DisplayClusterViewportConfigurationHelpers::UpdateChromakeyViewportSetting(*ChromakeyViewport, InChromakeySettings, ConfigurationICVFX.StageSettings);

		// Attach to parent viewport
		ChromakeyViewport->RenderSettings.AssignParentViewport(CameraViewport->GetId(), CameraViewport->RenderSettings);

		return true;
	}

	bool Initialize()
	{
		// Check rules for camera settings:
		if (CameraSettings.bEnable == false)
		{
			// dont use camera if disabled
			return false;
		}

		if (CameraSettings.RenderSettings.Override.bAllowOverride && CameraSettings.RenderSettings.Override.SourceTexture == nullptr)
		{
			//@todo handle error
			// Override mode requre source texture
			return false;
		}

		// Create new camera projection policy for camera viewport
		TSharedPtr<IDisplayClusterProjectionPolicy> CameraProjectionPolicy;
		if (!ConfigurationICVFX.CreateProjectionPolicy(CameraId, DisplayClusterViewportStrings::icvfx::camera, true, CameraProjectionPolicy))
		{
			//@todo handle error
			return false;
		}

		// Initialize camera policy with camera component and settings
		if (!ImplUpdateCameraProjectionSettings(CameraProjectionPolicy))
		{
			//@todo handle error
			return false;
		}

		// Get camera pos-rot-prj from policy
		const float WorldToMeters = 100.f;
		const float CfgNCP = 1.f;
		FVector ViewOffset = FVector::ZeroVector;

		if (CameraProjectionPolicy->CalculateView(nullptr, 0, CameraViewLocation, CameraViewRotation, ViewOffset, WorldToMeters, CfgNCP, CfgNCP) &&
			CameraProjectionPolicy->GetProjectionMatrix(nullptr, 0, CameraPrjMatrix))
		{
			return true;
		}

		//@todo handle error
		return false;
	}

	void GetCameraDataICVFX(FDisplayClusterShaderParameters_ICVFX::FCameraSettings& OutCameraSettings) const
	{
		OutCameraSettings.Resource.ViewportId = CameraViewport ? CameraViewport->GetId() : FString();

		OutCameraSettings.SoftEdge = CameraSettings.SoftEdge;

		OutCameraSettings.CameraViewRotation = Local2WorldTransform.InverseTransformRotation(CameraViewRotation.Quaternion()).Rotator();
		OutCameraSettings.CameraViewLocation = Local2WorldTransform.InverseTransformPosition(CameraViewLocation);

		OutCameraSettings.CameraPrjMatrix = CameraPrjMatrix;

		OutCameraSettings.RenderOrder = CameraRenderOrder;
	}

	void AssignChromakeyToTargetViewport(FDisplayClusterViewport& DstViewport, const FDisplayClusterConfigurationICVFX_ChromakeySettings& InChromakeySettings)
	{
		if ((DstViewport.RenderSettingsICVFX.Flags & ViewportICVFX_DisableChromakey) != 0)
		{
			// chromakey disabled for this viewport
			return;
		}

		FDisplayClusterShaderParameters_ICVFX::FCameraSettings& DstCameraData = DstViewport.RenderSettingsICVFX.ICVFX.Cameras.Last();

		switch (InChromakeySettings.Source)
		{
		case EDisplayClusterConfigurationICVFX_ChromakeySource::None:
			DstCameraData.ChromakeySource = EDisplayClusterShaderParametersICVFX_ChromakeySource::Disabled;

			// skip markers
			return;

		case EDisplayClusterConfigurationICVFX_ChromakeySource::FrameColor:
			DstCameraData.ChromakeySource = EDisplayClusterShaderParametersICVFX_ChromakeySource::FrameColor;
			DstCameraData.ChromakeyColor = InChromakeySettings.ChromakeyColor;

			break;

		case EDisplayClusterConfigurationICVFX_ChromakeySource::ChromakeyRenderTexture:
			if (InChromakeySettings.ChromakeyRenderTexture.bOverrideCameraViewport)
			{
				CameraViewport->RenderSettings.OverrideViewportId = ChromakeyViewport->GetId();
				DstCameraData.ChromakeySource = EDisplayClusterShaderParametersICVFX_ChromakeySource::Disabled;
				return;
			}
			else
			{
				DstCameraData.ChromakeySource = EDisplayClusterShaderParametersICVFX_ChromakeySource::ChromakeyLayers;
				DstCameraData.Chromakey.ViewportId = ChromakeyViewport->GetId();
			}
			break;
		}

		if ((DstViewport.RenderSettingsICVFX.Flags & ViewportICVFX_DisableChromakeyMarkers) != 0)
		{
			// chromakey markers disabled for this viewport
			return;
		}

		// UDisplayClusterConfigurationICVFX_ChromakeyMarkers
		DisplayClusterViewportConfigurationHelpers::UpdateChromakeyMarkerSettings(DstCameraData, InChromakeySettings.ChromakeyMarkers);
	}

	bool IsCameraProjectionVisibleOnViewport(FDisplayClusterViewport* TargetViewport)
	{
		if (TargetViewport && TargetViewport->ProjectionPolicy.IsValid() && TargetViewport->ProjectionPolicy->IsCameraProjectionVisible(CameraViewRotation, CameraViewLocation, CameraPrjMatrix))
		{
			return true;
		}
		
		// do not use camera for this viewport
		return false;
	}

private:
	// Camera world
	FRotator CameraViewRotation;
	FVector  CameraViewLocation;
	FMatrix  CameraPrjMatrix;

	FDisplayClusterViewport* CameraViewport = nullptr;
	FDisplayClusterViewport* ChromakeyViewport = nullptr;

	int CameraRenderOrder = -1;

private:
	FDisplayClusterViewportConfigurationICVFX& ConfigurationICVFX;

	UCameraComponent* const CameraComponent;
	const FString CameraId;

	const UDisplayClusterConfigurationICVFX_CameraSettings& CameraSettings;

	const FTransform Local2WorldTransform;
};

////////////////////////////////////////////////////////////////////////////////
// FDisplayClusterViewportConfigurationCameraICVFX
////////////////////////////////////////////////////////////////////////////////

class FDisplayClusterViewportConfigurationCameraICVFX
{
public:
	FDisplayClusterViewportConfigurationCameraICVFX(const FTransform& InLocal2WorldTransform, FDisplayClusterViewportConfigurationICVFX& InConfigurationICVFX, class UDisplayClusterICVFX_CineCameraComponent* const InCameraComponent)
		: ConfigurationICVFX(InConfigurationICVFX)
		, CameraViewport(InLocal2WorldTransform, InConfigurationICVFX, InCameraComponent->GetCameraComponent(), InCameraComponent->GetCameraUniqueId(), *(InCameraComponent->GetCameraSettingsICVFX()))
		, ChromakeySettings(DisplayClusterViewportConfigurationHelpers::GetCameraChromakeySettings(*(InCameraComponent->GetCameraSettingsICVFX()), ConfigurationICVFX.StageSettings))
		, CameraMotionBlurParameters(InCameraComponent->GetMotionBlurParameters())
	{}

	FDisplayClusterViewportConfigurationCameraICVFX(const FTransform& InLocal2WorldTransform, class FDisplayClusterViewportConfigurationICVFX& InConfigurationICVFX, class UDisplayClusterICVFX_RefCineCameraComponent* const InCameraComponent)
		: ConfigurationICVFX(InConfigurationICVFX)
		, CameraViewport(InLocal2WorldTransform, InConfigurationICVFX, InCameraComponent->GetCameraComponent(), InCameraComponent->GetCameraUniqueId(), *(InCameraComponent->GetCameraSettingsICVFX()))
		, ChromakeySettings(DisplayClusterViewportConfigurationHelpers::GetCameraChromakeySettings(*(InCameraComponent->GetCameraSettingsICVFX()), ConfigurationICVFX.StageSettings))
		, CameraMotionBlurParameters(InCameraComponent->GetMotionBlurParameters())
	{}

public:
	// Create camera resources and intialize targets
	void Update();

	bool Initialize()
	{
		return CameraViewport.Initialize();
	}

	bool IsCameraProjectionVisibleOnViewport(FDisplayClusterViewport* TargetViewport)
	{
		return CameraViewport.IsCameraProjectionVisibleOnViewport(TargetViewport);
	}
protected:

	bool IsChromakeyOverrideCameraViewport() const
	{
		if (ChromakeySettings.Source == EDisplayClusterConfigurationICVFX_ChromakeySource::FrameColor)
		{
			// use framecolor instead of viewport rendering
			return true;
		}

		return false;
	}

	bool CreateChromakey();

public:
	// List of targets for this camera
	TArray<FDisplayClusterViewport*> VisibleTargets;

private:
	FDisplayClusterViewportConfigurationICVFX& ConfigurationICVFX;
	FDisplayClusterViewportConfigurationCameraViewport CameraViewport;
	const FDisplayClusterConfigurationICVFX_ChromakeySettings& ChromakeySettings;
	const FDisplayClusterViewport_CameraMotionBlur CameraMotionBlurParameters;

};

void FDisplayClusterViewportConfigurationCameraICVFX::Update()
{
	bool bCreateCameraData = true;
	if (!IsChromakeyOverrideCameraViewport())
	{
		bCreateCameraData = CameraViewport.CreateCameraViewport();
	}

	if (bCreateCameraData)
	{
		bool bAllowChromakey = false;

		CameraViewport.SetMotionBlurParameters(CameraMotionBlurParameters);

		FDisplayClusterShaderParameters_ICVFX::FCameraSettings CameraSettings;
		CameraViewport.GetCameraDataICVFX(CameraSettings);


		// Add this camera data to all visible targets:
		for (FDisplayClusterViewport* ViewportIt : VisibleTargets)
		{
			ViewportIt->RenderSettingsICVFX.ICVFX.Cameras.Add(CameraSettings);

			// At lest one target must accept chromakey
			bAllowChromakey |= (ViewportIt->RenderSettingsICVFX.Flags & ViewportICVFX_DisableChromakey) == 0;
		}

		if (bAllowChromakey)
		{
			CreateChromakey();
		}
	}
}

bool FDisplayClusterViewportConfigurationCameraICVFX::CreateChromakey()
{
	// Try create chromakey render on demand
	if(ChromakeySettings.Source == EDisplayClusterConfigurationICVFX_ChromakeySource::ChromakeyRenderTexture)
	{
		if (ChromakeySettings.ChromakeyRenderTexture.Override.bAllowOverride && ChromakeySettings.ChromakeyRenderTexture.Override.SourceTexture == nullptr)
		{
			// ! handle error
			// Override mode requre source texture
			return false;
		}

		bool bIsChromakeyHasAnyRenderComponent = DisplayClusterViewportConfigurationHelpers::IsVisibilitySettingsDefined(ChromakeySettings.ChromakeyRenderTexture.ShowOnlyList);
		if (ChromakeySettings.ChromakeyRenderTexture.Override.bAllowOverride == false && !bIsChromakeyHasAnyRenderComponent)
		{
			// ! handle error
			// Chromakey require layers for render
			return false;
		}

		if (!CameraViewport.CreateChromakeyViewport(ChromakeySettings))
		{
			return false;
		}
	}

	// Assign this chromakey to all supported targets
	for (FDisplayClusterViewport* TargetIt : VisibleTargets)
	{
		if (TargetIt)
		{
			CameraViewport.AssignChromakeyToTargetViewport(*TargetIt, ChromakeySettings);
		}
	}

	return true;
}

///////////////////////////////////////////////////////////////////
// FDisplayClusterViewportConfigurationICVFX
///////////////////////////////////////////////////////////////////
FDisplayClusterViewportConfigurationICVFX::FDisplayClusterViewportConfigurationICVFX(FDisplayClusterViewportManager& InViewportManager, ADisplayClusterRootActor& InRootActor, const UDisplayClusterConfigurationData& InConfigurationData)
	: ViewportManager(InViewportManager)
	, RootActor(InRootActor)
	, ConfigurationData(InConfigurationData)
	, StageSettings(*(InRootActor.GetStageSettings()))
	, LightcardSettings(StageSettings.Lightcard)
{ }

bool FDisplayClusterViewportConfigurationICVFX::CreateProjectionPolicy(const FString& InViewportId, const FString& InResourceId, bool bIsCameraProjection, TSharedPtr<class IDisplayClusterProjectionPolicy>& OutProjPolicy) const
{
	FDisplayClusterConfigurationProjection CameraProjectionPolicyConfig;
	CameraProjectionPolicyConfig.Type = bIsCameraProjection ? DisplayClusterProjectionStrings::projection::Camera : DisplayClusterProjectionStrings::projection::Link;

	// Create projection policy for viewport
	OutProjPolicy = ViewportManager.CreateProjectionPolicy(GetNameICVFX(InViewportId, InResourceId), &CameraProjectionPolicyConfig);

	return OutProjPolicy.IsValid();
}

FDisplayClusterViewport* FDisplayClusterViewportConfigurationICVFX::CreateViewport(const FString& InViewportId, const FString& InResourceId, TSharedPtr<IDisplayClusterProjectionPolicy>& InProjectionPolicy) const
{
	check(InProjectionPolicy.IsValid());

	// Create viewport for new projection policy
	FDisplayClusterViewport* NewViewport = ViewportManager.ImplCreateViewport(GetNameICVFX(InViewportId, InResourceId), InProjectionPolicy);
	if (NewViewport != nullptr)
	{
		// Mark as internal resource
		NewViewport->RenderSettingsICVFX.RuntimeFlags |= ViewportRuntime_InternalResource;

		// Dont show ICVFX composing viewports on frame target
		NewViewport->RenderSettings.bVisible = false;

		return NewViewport;
	}

	return nullptr;
}

FDisplayClusterViewport* FDisplayClusterViewportConfigurationICVFX::FindViewport(const FString& InViewportId, const FString& InResourceId) const
{
	return ViewportManager.ImplFindViewport(GetNameICVFX(InViewportId, InResourceId));
}

void FDisplayClusterViewportConfigurationICVFX::Update()
{
	ImplBeginReallocateViewports();

	if (StageSettings.bEnable)
	{
		TArray<FDisplayClusterViewport*> TargetViewports;
		const EDisplayClusterViewportICVFXFlags TargetViewportsFlags = ImplGetTargetViewports(TargetViewports);

		// Find ICVFX target viewports
		if (TargetViewports.Num() > 0)
		{
			// ICVFX used
			// Apply icvfx settings
			UpdateTargetViewportConfiguration(TargetViewports);

			// If not all viewports disable camera:
			// Collect all ICVFX cameras from stage
			TArray<FDisplayClusterViewportConfigurationCameraICVFX*> Cameras;
			if ((TargetViewportsFlags & ViewportICVFX_DisableCamera) == 0)
			{
				ImplGetCameras(Cameras);
			}

			// Allocate and assign camera resources
			if (Cameras.Num() > 0)
			{
				// Collect visible targets for cameras:
				for (FDisplayClusterViewport* TargetIt : TargetViewports)
				{
					// Target viewpot must support camera render:
					if ((TargetIt->RenderSettingsICVFX.Flags & ViewportICVFX_DisableCamera) == 0)
					{
						// Add this target to all cameras visible on it
						for (FDisplayClusterViewportConfigurationCameraICVFX* CameraIt : Cameras)
						{
							if(CameraIt->IsCameraProjectionVisibleOnViewport(TargetIt))
							{
								CameraIt->VisibleTargets.Add(TargetIt);
							}
						}
					}
				}

				// Create camera resources and initialize target ICVFX viewports
				for (FDisplayClusterViewportConfigurationCameraICVFX* CameraIt : Cameras)
				{
					if (CameraIt->VisibleTargets.Num() > 0)
					{
						CameraIt->Update();
					}
				}
			}

			// If not all viewports disable lightcard
			if ((TargetViewportsFlags & ViewportICVFX_DisableLightcard) == 0)
			{
				// Allocate and assign lightcard resources
				if (DisplayClusterViewportConfigurationHelpers::IsShouldUseLightcard(LightcardSettings))
				{
					for (FDisplayClusterViewport* TargetIt : TargetViewports)
					{
						// only for support targets
						if (TargetIt && (TargetIt->RenderSettingsICVFX.Flags & ViewportICVFX_DisableLightcard) == 0)
						{
							CreateLightcardViewport(*TargetIt);
						}
					}
				}
			}

			// Sort cameras by render order for each target
			for (FDisplayClusterViewport* TargetIt : TargetViewports)
			{
				if (TargetIt)
				{
					TargetIt->RenderSettingsICVFX.ICVFX.SortCamerasRenderOrder();
				}
			}
		}
	}

	ImplFinishReallocateViewports();
}

void FDisplayClusterViewportConfigurationICVFX::UpdateTargetViewportConfiguration(TArray<FDisplayClusterViewport*>& TargetViewports)
{
	for (FDisplayClusterViewport* TargetIt : TargetViewports)
	{
		DisplayClusterViewportConfigurationHelpers::UpdateVisibilitySetting(*TargetIt, EDisplayClusterViewport_VisibilityMode::Hide, StageSettings.HideList);
	}
}

bool FDisplayClusterViewportConfigurationICVFX::CreateLightcardViewport(FDisplayClusterViewport& BaseViewport)
{
	if (LightcardSettings.OCIO_Configuration.bIsEnabled)
	{
		ImplCreateLightcardViewport(BaseViewport, true);
	}

	return ImplCreateLightcardViewport(BaseViewport, false);
}

bool FDisplayClusterViewportConfigurationICVFX::ImplCreateLightcardViewport(FDisplayClusterViewport& BaseViewport, bool bIsOpenColorIO)
{
	// Create new lightcard viewport
	const FString ResourceId = bIsOpenColorIO ? DisplayClusterViewportStrings::icvfx::lightcard_OCIO : DisplayClusterViewportStrings::icvfx::lightcard;

	FDisplayClusterViewport* LightcardViewport = FindViewport(BaseViewport.GetId(), ResourceId);
	if (LightcardViewport == nullptr)
	{
		TSharedPtr<IDisplayClusterProjectionPolicy> LightcardProjectionPolicy;
		if (!CreateProjectionPolicy(BaseViewport.GetId(), ResourceId, false, LightcardProjectionPolicy))
		{
			//@todo handle error
			return false;
		}

		LightcardViewport = CreateViewport(BaseViewport.GetId(), ResourceId, LightcardProjectionPolicy);
	}

	if (LightcardViewport)
	{
		// Mark viewport as used
		LightcardViewport->RenderSettingsICVFX.RuntimeFlags &= ~(ViewportRuntime_Unused);

		// Add viewport ICVFX usage as Lightcard
		LightcardViewport->RenderSettingsICVFX.RuntimeFlags |= (bIsOpenColorIO) ? ViewportRuntime_ICVFXLightcard_OCIO : ViewportRuntime_ICVFXLightcard;

		// Update configuration
		DisplayClusterViewportConfigurationHelpers::UpdateLightcardViewportSetting(*LightcardViewport, BaseViewport, LightcardSettings, StageSettings, bIsOpenColorIO);

		return true;
	}

	return false;
}

void FDisplayClusterViewportConfigurationICVFX::ImplBeginReallocateViewports()
{
	for (FDisplayClusterViewport* ViewportIt : ViewportManager.ImplGetViewports())
	{
		// Runtime icvfx viewport support reallocate feature:
		if ((ViewportIt->RenderSettingsICVFX.RuntimeFlags & ViewportRuntime_InternalResource) != 0)
		{
			// Mark all dynamic ICVFX viewports for delete
			ViewportIt->RenderSettingsICVFX.RuntimeFlags |= ViewportRuntime_Unused;
		}
	}
}

void FDisplayClusterViewportConfigurationICVFX::ImplFinishReallocateViewports()
{
	TArray<FDisplayClusterViewport*> UnusedViewports;

	// Collect all unused viewports for remove
	for (FDisplayClusterViewport* ViewportIt : ViewportManager.ImplGetViewports())
	{
		if ((ViewportIt->RenderSettingsICVFX.RuntimeFlags & ViewportRuntime_Unused) != 0)
		{
			UnusedViewports.Add(ViewportIt);
		}
	}

	// Delete unused viewports:
	for (FDisplayClusterViewport* RemoveViewportIt : UnusedViewports)
	{
		ViewportManager.DeleteViewport(RemoveViewportIt->GetId());
	}

	UnusedViewports.Empty();
}


void FDisplayClusterViewportConfigurationICVFX::ImplGetCameras(TArray<FDisplayClusterViewportConfigurationCameraICVFX*>& OutCameras)
{
	USceneComponent* OriginComp = RootActor.GetRootComponent();
	const FTransform& Local2WorldTransform = OriginComp->GetComponentTransform();

	for (UActorComponent* ActorComponentIt : RootActor.GetComponents())
	{
		FDisplayClusterViewportConfigurationCameraICVFX* NewCamera = nullptr;

		// Try to create ICVFX camera from component:
		if (ActorComponentIt)
		{
			UDisplayClusterICVFX_CineCameraComponent* CineCameraComponent = Cast<UDisplayClusterICVFX_CineCameraComponent>(ActorComponentIt);
			if (CineCameraComponent && CineCameraComponent->IsShouldUseICVFX())
			{
				NewCamera = new FDisplayClusterViewportConfigurationCameraICVFX(Local2WorldTransform, *this, CineCameraComponent);
			}
			else
			{
				UDisplayClusterICVFX_RefCineCameraComponent* RefCineCameraComponent = Cast<UDisplayClusterICVFX_RefCineCameraComponent>(ActorComponentIt);
				if (RefCineCameraComponent && RefCineCameraComponent->IsShouldUseICVFX())
				{
					NewCamera = new FDisplayClusterViewportConfigurationCameraICVFX(Local2WorldTransform, *this, RefCineCameraComponent);
				}
			}
		}

		// If found ICVFX camera, check and add to used
		if (NewCamera)
		{
			if (NewCamera->Initialize())
			{
				OutCameras.Add(NewCamera);
			}
			else
			{
				// invalid camera, skip
				delete NewCamera;
				NewCamera = nullptr;
			}
		}
	}
}

EDisplayClusterViewportICVFXFlags FDisplayClusterViewportConfigurationICVFX::ImplGetTargetViewports(TArray<class FDisplayClusterViewport*>& OutTargets)
{
	EDisplayClusterViewportICVFXFlags InvFlags = ViewportICVFX_None;

	// Collect invertet disable flags from all target viewports
	for (FDisplayClusterViewport* ViewportIt : ViewportManager.ImplGetViewports())
	{
		// Process only external viewports:
		if ((ViewportIt->RenderSettingsICVFX.RuntimeFlags & ViewportRuntime_InternalResource) == 0)
		{
			//Raise new projection target if possible
			if (ViewportIt->RenderSettings.bEnable && (ViewportIt->RenderSettingsICVFX.Flags & ViewportICVFX_Enable) != 0)
			{
				if (ViewportIt->ProjectionPolicy.IsValid() && ViewportIt->ProjectionPolicy->ShouldSupportICVFX())
				{
					// Collect this viewport ICVFX target
					OutTargets.Add(ViewportIt);

					// proj policy support ICVFX, Use this viewport as icvfx target
					ViewportIt->RenderSettingsICVFX.RuntimeFlags |= ViewportRuntime_ICVFXTarget;

					// Update targets use flags:
					InvFlags |= ~(ViewportIt->RenderSettingsICVFX.Flags);
				}
			}
		}
	}

	// Collect all targets disable flags
	return ~(InvFlags);
}
