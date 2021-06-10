// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterViewportConfigurationHelpers_OpenColorIO.h"
#include "DisplayClusterViewportConfiguration.h"

#include "DisplayClusterConfigurationTypes_Viewport.h"
#include "DisplayClusterSceneViewExtensions.h"
#include "DisplayClusterRootActor.h"

#include "Render/Viewport/DisplayClusterViewport.h"
#include "Render/Viewport/DisplayClusterViewportManager.h"

#include "Components/DisplayClusterICVFXCameraComponent.h"
#include "OpenColorIODisplayExtension.h"

bool FDisplayClusterViewportConfigurationHelpers_OpenColorIO::UpdateBaseViewport(FDisplayClusterViewport& DstViewport, ADisplayClusterRootActor& RootActor, const UDisplayClusterConfigurationViewport& InViewportConfiguration)
{
	// First try use global OCIO from stage settings
	const FOpenColorIODisplayConfiguration* OCIO_Configuration = RootActor.GetViewportOCIO(DstViewport.GetId());
	if (OCIO_Configuration)
	{
		// OCIO defined for this viewport in stage settings, try apply for viewport
		if (ImplUpdate(DstViewport, *OCIO_Configuration))
		{
			return true;
		}

		// External OCIO configuration disabled or invalid, disable OCIO for this viewport
		ImplDisable(DstViewport);

		return false;
	}

	// Finally try use OCIO from viewport configuration
	if (ImplUpdate(DstViewport, InViewportConfiguration.OCIO_Configuration))
	{
		return true;
	}

	// No OCIO defined for this viewport, disabled
	ImplDisable(DstViewport);

	return false;
}

bool FDisplayClusterViewportConfigurationHelpers_OpenColorIO::UpdateLightcardViewport(FDisplayClusterViewport& DstViewport, FDisplayClusterViewport& BaseViewport, ADisplayClusterRootActor& RootActor)
{
	const FDisplayClusterConfigurationICVFX_StageSettings& StageSettings = RootActor.GetStageSettings();
	const FDisplayClusterConfigurationICVFX_LightcardSettings& LightcardSettings = StageSettings.Lightcard;

	// First try use global OCIO from stage settings
	if (LightcardSettings.bEnableOuterViewportOCIO)
	{
		const FOpenColorIODisplayConfiguration* OCIO_Configuration = RootActor.GetViewportOCIO(BaseViewport.GetId());
		if (OCIO_Configuration)
		{
			// OCIO defined for this viewport in stage settings, try apply for viewport
			if (ImplUpdate(DstViewport, *OCIO_Configuration))
			{
				return true;
			}

			// External OCIO configuration disabled or invalid, disable OCIO for this viewport
			ImplDisable(DstViewport);

			return false;
		}
	}

	// After try use OCIO from viewport configuration
	if (LightcardSettings.bEnableViewportOCIO)
	{
		const FDisplayClusterRenderFrameSettings& RenderFrameSettings = DstViewport.Owner.Configuration->GetRenderFrameSettings();

		const FString& ClusterNodeId = RenderFrameSettings.ClusterNodeId;
		if (!ClusterNodeId.IsEmpty())
		{
			UDisplayClusterConfigurationViewport* BaseViewportConfiguration = RootActor.GetViewportConfiguration(ClusterNodeId, BaseViewport.GetId());

			if (BaseViewportConfiguration && ImplUpdate(DstViewport, BaseViewportConfiguration->OCIO_Configuration))
			{
				return true;
			}

			// No OCIO defined for this viewport, disabled
			ImplDisable(DstViewport);

			return false;
		}
	}

	// Finally try use global OCIO from lightcard configuration
	if (ImplUpdate(DstViewport, LightcardSettings.OCIO_Configuration))
	{
		return true;
	}

	// No OCIO defined for this viewport, disabled
	ImplDisable(DstViewport);
	
	return false;
}

bool FDisplayClusterViewportConfigurationHelpers_OpenColorIO::UpdateICVFXCameraViewport(FDisplayClusterViewport& DstViewport, ADisplayClusterRootActor& RootActor, UDisplayClusterICVFXCameraComponent& InCameraComponent)
{
	const FDisplayClusterConfigurationICVFX_CameraSettings& CameraSettings = InCameraComponent.GetCameraSettingsICVFX();
	if (CameraSettings.bEnableInnerFrustumOCIO)
	{
		const FDisplayClusterRenderFrameSettings& RenderFrameSettings = DstViewport.Owner.Configuration->GetRenderFrameSettings();

		const FString& ClusterNodeId = RenderFrameSettings.ClusterNodeId;
		if (!ClusterNodeId.IsEmpty())
		{
			for (const FDisplayClusterConfigurationOCIOProfile& OCIOProfileIt : CameraSettings.InnerFrustumOCIOConfigurations)
			{
				for (const FString& ClusterNodeIt : OCIOProfileIt.ApplyOCIOToObjects)
				{
					if (ClusterNodeId.Compare(ClusterNodeIt, ESearchCase::IgnoreCase) == 0)
					{
						// Use cluster node OCIO
						FDisplayClusterViewportConfigurationHelpers_OpenColorIO::ImplUpdate(DstViewport, OCIOProfileIt.OCIOConfiguration);

						return true;
					}
				}
			}
		}

		// External OCIO configuration disabled or invalid, disable OCIO for this camera
		ImplDisable(DstViewport);

		return false;
	}

	// Finally try use OCIO from camera configuration
	if (ImplUpdate(DstViewport, CameraSettings.OCIO_Configuration))
	{
		return true;
	}

	// No OCIO defined for this camera, disabled
	ImplDisable(DstViewport);

	return false;
}

bool FDisplayClusterViewportConfigurationHelpers_OpenColorIO::ImplUpdate(FDisplayClusterViewport& DstViewport, const FOpenColorIODisplayConfiguration& InConfiguration)
{
	if (InConfiguration.bIsEnabled && InConfiguration.ColorConfiguration.IsValid())
	{
		// Create/Update OCIO:
		if (DstViewport.OpenColorIODisplayExtension.IsValid() == false)
		{
			DstViewport.OpenColorIODisplayExtension = FSceneViewExtensions::NewExtension<FOpenColorIODisplayExtension>(nullptr);
			if (DstViewport.OpenColorIODisplayExtension.IsValid())
			{
				// assign active func
				DstViewport.OpenColorIODisplayExtension->IsActiveThisFrameFunctions.Reset(1);
				DstViewport.OpenColorIODisplayExtension->IsActiveThisFrameFunctions.Add(DstViewport.GetSceneViewExtensionIsActiveFunctor());
			}
		}

		if (DstViewport.OpenColorIODisplayExtension.IsValid())
		{
			// Update configuration
			DstViewport.OpenColorIODisplayExtension->SetDisplayConfiguration(InConfiguration);
		}

		return true;
	}

	return false;
}

void FDisplayClusterViewportConfigurationHelpers_OpenColorIO::ImplDisable(FDisplayClusterViewport& DstViewport)
{
	// Remove OICO ref
	DstViewport.OpenColorIODisplayExtension.Reset();
}

