// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterViewportConfigurationHelpers_Postprocess.h"

#include "DisplayClusterConfigurationTypes.h"
#include "Render/Viewport/DisplayClusterViewport.h"

#include "DisplayClusterRootActor.h"
#include "Components/DisplayClusterICVFXCameraComponent.h"

static void ImplUpdateCustomPostprocess(FDisplayClusterViewport& DstViewport, bool bEnabled, const FDisplayClusterConfigurationViewport_CustomPostprocessSettings& InCustomPostprocess, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass RenderPass)
{
	if (bEnabled)
	{
		DstViewport.CustomPostProcessSettings.AddCustomPostProcess(RenderPass, InCustomPostprocess.PostProcessSettings, InCustomPostprocess.BlendWeight, InCustomPostprocess.bIsOneFrame);
	}
	else
	{
		DstViewport.CustomPostProcessSettings.RemoveCustomPostProcess(RenderPass);
	}
}

static void ImplRemoveCustomPostprocess(FDisplayClusterViewport& DstViewport, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass RenderPass)
{
	DstViewport.CustomPostProcessSettings.RemoveCustomPostProcess(RenderPass);
}

static void ImplUpdatePerViewportPostProcessSettings(FDisplayClusterViewport& DstViewport, ADisplayClusterRootActor& RootActor, bool bExcludeFromOverallClusterPostProcess, bool bIsEnabled, const FDisplayClusterConfigurationViewport_PerViewportSettings& InPerViewportSettings)
{
	const UDisplayClusterConfigurationData* ConfigData = RootActor.GetConfigData();
	check(ConfigData);

	//---------------------------
	// FinalPerViewport (added after Final)
	//---------------------------
	// Check and apply the overall cluster post process settings
	const UDisplayClusterConfigurationCluster* ClusterConfig = ConfigData->Cluster;
	if (ClusterConfig->bUseOverallClusterPostProcess)
	{
		FDisplayClusterConfigurationViewport_CustomPostprocessSettings CustomPPS;
		CustomPPS.bIsEnabled = true;
		CustomPPS.bIsOneFrame = true;
		CustomPPS.BlendWeight = ClusterConfig->OverallClusterPostProcessSettings.BlendWeight;

		const FDisplayClusterConfigurationViewport_PerViewportSettings EmptyPPSettings;

		if (bIsEnabled)
		{
			if (bExcludeFromOverallClusterPostProcess)
			{
				// This viewport is excluded from the overall cluster, so only use the per-viewport settings and blend weight
				FDisplayClusterViewportConfigurationHelpers_Postprocess::BlendPostProcessSettings(CustomPPS.PostProcessSettings, EmptyPPSettings, InPerViewportSettings);
				CustomPPS.BlendWeight = InPerViewportSettings.BlendWeight;
			}
			else
			{
				// This viewport should use a blend of the overall cluster and the per-viewport settings, so multiply the blend weights
				FDisplayClusterViewportConfigurationHelpers_Postprocess::BlendPostProcessSettings(CustomPPS.PostProcessSettings, ClusterConfig->OverallClusterPostProcessSettings, InPerViewportSettings);
				CustomPPS.BlendWeight *= InPerViewportSettings.BlendWeight;
			}
		}
		else
		{
			if (bExcludeFromOverallClusterPostProcess)
			{
				// This viewport doesn't use per-viewport settings and is also excluded from the overall cluster, so do nothing
				ImplRemoveCustomPostprocess(DstViewport, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::FinalPerViewport);
				return;
			}
			else
			{
				// This viewport doesn't use per-viewport settings, so just use the overall cluster settings and blend weight
				FDisplayClusterViewportConfigurationHelpers_Postprocess::BlendPostProcessSettings(CustomPPS.PostProcessSettings, ClusterConfig->OverallClusterPostProcessSettings, EmptyPPSettings);
			}
		}

		// Use ERenderPass::FinalPerViewport here because we do a custom blend in FDisplayClusterDeviceBase::EndFinalPostprocessSettings
		ImplUpdateCustomPostprocess(DstViewport, CustomPPS.bIsEnabled, CustomPPS, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::FinalPerViewport);
	}
	else
	{
		// If there isn't a global PP set, then just apply the per-viewport settings and blend weight
		if (bIsEnabled)
		{
			FDisplayClusterConfigurationViewport_CustomPostprocessSettings CustomPPS;
			CustomPPS.bIsEnabled = true;
			CustomPPS.bIsOneFrame = true;
			CustomPPS.BlendWeight = InPerViewportSettings.BlendWeight;

			const FDisplayClusterConfigurationViewport_PerViewportSettings EmptyPPSettings;

			FDisplayClusterViewportConfigurationHelpers_Postprocess::BlendPostProcessSettings(CustomPPS.PostProcessSettings, EmptyPPSettings, InPerViewportSettings);

			// Use ERenderPass::FinalPerViewport here because we do a custom blend in FDisplayClusterDeviceBase::EndFinalPostprocessSettings
			ImplUpdateCustomPostprocess(DstViewport, true, CustomPPS, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::FinalPerViewport);
		}
		else
		{
			ImplRemoveCustomPostprocess(DstViewport, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::FinalPerViewport);
		}
	}
}

void FDisplayClusterViewportConfigurationHelpers_Postprocess::UpdateCameraPostProcessSettings(FDisplayClusterViewport& DstViewport, ADisplayClusterRootActor& RootActor, UDisplayClusterICVFXCameraComponent& InCameraComponent)
{
	const FDisplayClusterConfigurationICVFX_CameraSettings& CameraSettings = InCameraComponent.GetCameraSettingsICVFX();

	FDisplayClusterConfigurationViewport_CustomPostprocessSettings CameraPPS;
	CameraPPS.bIsOneFrame = true;
	CameraPPS.BlendWeight = 1.f;

	if (CameraSettings.RenderSettings.bUseCameraComponentPostprocess)
	{
		FMinimalViewInfo DesiredView;
		InCameraComponent.GetDesiredView(DesiredView);

		// Send camera postprocess to override
		CameraPPS.bIsEnabled = true;
		CameraPPS.PostProcessSettings = DesiredView.PostProcessSettings;

	}

	const FDisplayClusterConfigurationICVFX_CameraMotionBlurOverridePPS& OverrideMotionBlurPPS = CameraSettings.CameraMotionBlur.OverrideMotionBlurPPS;
	if (OverrideMotionBlurPPS.bOverrideEnable)
	{
		// Send camera postprocess to override
		CameraPPS.bIsEnabled = true;

		CameraPPS.PostProcessSettings.MotionBlurAmount = OverrideMotionBlurPPS.MotionBlurAmount;
		CameraPPS.PostProcessSettings.bOverride_MotionBlurAmount = true;

		CameraPPS.PostProcessSettings.MotionBlurMax = OverrideMotionBlurPPS.MotionBlurMax;
		CameraPPS.PostProcessSettings.bOverride_MotionBlurMax = true;
		
		CameraPPS.PostProcessSettings.MotionBlurPerObjectSize = OverrideMotionBlurPPS.MotionBlurPerObjectSize;
		CameraPPS.PostProcessSettings.bOverride_MotionBlurPerObjectSize = true;
	}

	ImplUpdateCustomPostprocess(DstViewport, CameraPPS.bIsEnabled, CameraPPS, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::Override);

	ImplUpdatePerViewportPostProcessSettings(DstViewport, RootActor, CameraSettings.PostProcessSettings.bExcludeFromOverallClusterPostProcess, CameraSettings.PostProcessSettings.bIsEnabled, CameraSettings.PostProcessSettings.ViewportSettings);
}

void FDisplayClusterViewportConfigurationHelpers_Postprocess::UpdateCustomPostProcessSettings(FDisplayClusterViewport& DstViewport, ADisplayClusterRootActor& RootActor, const FDisplayClusterConfigurationViewport_CustomPostprocess& InCustomPostprocessConfiguration)
{
	// update postprocess settings (Start, Override, Final)
	ImplUpdateCustomPostprocess(DstViewport, InCustomPostprocessConfiguration.Start.bIsEnabled, InCustomPostprocessConfiguration.Start, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::Start);
	ImplUpdateCustomPostprocess(DstViewport, InCustomPostprocessConfiguration.Override.bIsEnabled, InCustomPostprocessConfiguration.Override, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::Override);
	ImplUpdateCustomPostprocess(DstViewport, InCustomPostprocessConfiguration.Final.bIsEnabled, InCustomPostprocessConfiguration.Final, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::Final);
}

void FDisplayClusterViewportConfigurationHelpers_Postprocess::UpdatePerViewportPostProcessSettings(FDisplayClusterViewport& DstViewport, ADisplayClusterRootActor& RootActor, const FDisplayClusterConfigurationViewport_PostProcessSettings& InPostProcessSettings)
{
	ImplUpdatePerViewportPostProcessSettings(DstViewport, RootActor, InPostProcessSettings.bExcludeFromOverallClusterPostProcess, InPostProcessSettings.bIsEnabled, InPostProcessSettings.ViewportSettings);
}

// Note that skipped parameters in macro definitions will just evaluate to nothing
// This is intentional to get around the inconsistent naming in the color grading fields in FPostProcessSettings
#define PP_CONDITIONAL_BLEND(BLENDOP, COLOR, OUTGROUP, INGROUP, NAME, OFFSETOP, OFFSETVALUE) \
		OutputPP.bOverride_##COLOR##NAME##OUTGROUP = ClusterPPSettings.INGROUP bOverride_##NAME || ViewportPPSettings.INGROUP bOverride_##NAME; \
		if (ClusterPPSettings.INGROUP bOverride_##NAME && ViewportPPSettings.INGROUP bOverride_##NAME) \
		{ \
			OutputPP.COLOR##NAME##OUTGROUP = ClusterPPSettings.INGROUP NAME BLENDOP ViewportPPSettings.INGROUP NAME OFFSETOP OFFSETVALUE; \
		} \
		else if (ClusterPPSettings.INGROUP bOverride_##NAME) \
		{ \
			OutputPP.COLOR##NAME##OUTGROUP = ClusterPPSettings.INGROUP NAME; \
		} \
		else if (ViewportPPSettings.INGROUP bOverride_##NAME) \
		{ \
			OutputPP.COLOR##NAME##OUTGROUP = ViewportPPSettings.INGROUP NAME; \
		} \

void FDisplayClusterViewportConfigurationHelpers_Postprocess::BlendPostProcessSettings(FPostProcessSettings& OutputPP, const FDisplayClusterConfigurationViewport_PerViewportSettings& ClusterPPSettings, const FDisplayClusterConfigurationViewport_PerViewportSettings& ViewportPPSettings)
{
	PP_CONDITIONAL_BLEND(+, , , , WhiteTemp, +, -6500.0f);
	PP_CONDITIONAL_BLEND(+, , , , WhiteTint, , );
	PP_CONDITIONAL_BLEND(+, , , , AutoExposureBias, , );

	PP_CONDITIONAL_BLEND(*, Color, , Global., Saturation, , );
	PP_CONDITIONAL_BLEND(*, Color, , Global., Contrast, , );
	PP_CONDITIONAL_BLEND(*, Color, , Global., Gamma, , );
	PP_CONDITIONAL_BLEND(*, Color, , Global., Gain, , );
	PP_CONDITIONAL_BLEND(+, Color, , Global., Offset, , );

	PP_CONDITIONAL_BLEND(*, Color, Shadows, Shadows., Saturation, , );
	PP_CONDITIONAL_BLEND(*, Color, Shadows, Shadows., Contrast, , );
	PP_CONDITIONAL_BLEND(*, Color, Shadows, Shadows., Gamma, , );
	PP_CONDITIONAL_BLEND(*, Color, Shadows, Shadows., Gain, , );
	PP_CONDITIONAL_BLEND(+, Color, Shadows, Shadows., Offset, , );

	PP_CONDITIONAL_BLEND(*, Color, Midtones, Midtones., Saturation, , );
	PP_CONDITIONAL_BLEND(*, Color, Midtones, Midtones., Contrast, , );
	PP_CONDITIONAL_BLEND(*, Color, Midtones, Midtones., Gamma, , );
	PP_CONDITIONAL_BLEND(*, Color, Midtones, Midtones., Gain, , );
	PP_CONDITIONAL_BLEND(+, Color, Midtones, Midtones., Offset, , );

	PP_CONDITIONAL_BLEND(*, Color, Highlights, Highlights., Saturation, , );
	PP_CONDITIONAL_BLEND(*, Color, Highlights, Highlights., Contrast, , );
	PP_CONDITIONAL_BLEND(*, Color, Highlights, Highlights., Gamma, , );
	PP_CONDITIONAL_BLEND(*, Color, Highlights, Highlights., Gain, , );
	PP_CONDITIONAL_BLEND(+, Color, Highlights, Highlights., Offset, , );
}

#define PP_CONDITIONAL_COPY(COLOR, OUTGROUP, INGROUP, NAME) \
		if (InPPS->bOverride_##COLOR##NAME##INGROUP) \
		{ \
			OutViewportPPSettings->OUTGROUP NAME = InPPS->COLOR##NAME##INGROUP; \
			OutViewportPPSettings->OUTGROUP bOverride_##NAME = true; \
		} \

void FDisplayClusterViewportConfigurationHelpers_Postprocess::CopyPPSStructConditional(FDisplayClusterConfigurationViewport_PerViewportSettings* OutViewportPPSettings, FPostProcessSettings* InPPS)
{
	if ((OutViewportPPSettings != nullptr) && (InPPS != nullptr))
	{
		PP_CONDITIONAL_COPY(, , , WhiteTemp);
		PP_CONDITIONAL_COPY(, , , WhiteTint);
		PP_CONDITIONAL_COPY(, , , AutoExposureBias);

		PP_CONDITIONAL_COPY(Color, Global., , Saturation);
		PP_CONDITIONAL_COPY(Color, Global., , Contrast);
		PP_CONDITIONAL_COPY(Color, Global., , Gamma);
		PP_CONDITIONAL_COPY(Color, Global., , Gain);
		PP_CONDITIONAL_COPY(Color, Global., , Offset);

		PP_CONDITIONAL_COPY(Color, Shadows., Shadows, Saturation);
		PP_CONDITIONAL_COPY(Color, Shadows., Shadows, Contrast);
		PP_CONDITIONAL_COPY(Color, Shadows., Shadows, Gamma);
		PP_CONDITIONAL_COPY(Color, Shadows., Shadows, Gain);
		PP_CONDITIONAL_COPY(Color, Shadows., Shadows, Offset);

		PP_CONDITIONAL_COPY(Color, Midtones., Midtones, Saturation);
		PP_CONDITIONAL_COPY(Color, Midtones., Midtones, Contrast);
		PP_CONDITIONAL_COPY(Color, Midtones., Midtones, Gamma);
		PP_CONDITIONAL_COPY(Color, Midtones., Midtones, Gain);
		PP_CONDITIONAL_COPY(Color, Midtones., Midtones, Offset);

		PP_CONDITIONAL_COPY(Color, Highlights., Highlights, Saturation);
		PP_CONDITIONAL_COPY(Color, Highlights., Highlights, Contrast);
		PP_CONDITIONAL_COPY(Color, Highlights., Highlights, Gamma);
		PP_CONDITIONAL_COPY(Color, Highlights., Highlights, Gain);
		PP_CONDITIONAL_COPY(Color, Highlights., Highlights, Offset);
	}
}

#define PP_COPY(COLOR, OUTGROUP, INGROUP, NAME) \
		OutViewportPPSettings->OUTGROUP NAME = InPPS->COLOR##NAME##INGROUP; \
		OutViewportPPSettings->OUTGROUP bOverride_##NAME = true; \

void FDisplayClusterViewportConfigurationHelpers_Postprocess::CopyPPSStruct(FDisplayClusterConfigurationViewport_PerViewportSettings* OutViewportPPSettings, FPostProcessSettings* InPPS)
{
	if ((OutViewportPPSettings != nullptr) && (InPPS != nullptr))
	{
		PP_COPY(, , , WhiteTemp);
		PP_COPY(, , , WhiteTint);
		PP_COPY(, , , AutoExposureBias);

		PP_COPY(Color, Global., , Saturation);
		PP_COPY(Color, Global., , Contrast);
		PP_COPY(Color, Global., , Gamma);
		PP_COPY(Color, Global., , Gain);
		PP_COPY(Color, Global., , Offset);

		PP_COPY(Color, Shadows., Shadows, Saturation);
		PP_COPY(Color, Shadows., Shadows, Contrast);
		PP_COPY(Color, Shadows., Shadows, Gamma);
		PP_COPY(Color, Shadows., Shadows, Gain);
		PP_COPY(Color, Shadows., Shadows, Offset);

		PP_COPY(Color, Midtones., Midtones, Saturation);
		PP_COPY(Color, Midtones., Midtones, Contrast);
		PP_COPY(Color, Midtones., Midtones, Gamma);
		PP_COPY(Color, Midtones., Midtones, Gain);
		PP_COPY(Color, Midtones., Midtones, Offset);

		PP_COPY(Color, Highlights., Highlights, Saturation);
		PP_COPY(Color, Highlights., Highlights, Contrast);
		PP_COPY(Color, Highlights., Highlights, Gamma);
		PP_COPY(Color, Highlights., Highlights, Gain);
		PP_COPY(Color, Highlights., Highlights, Offset);
	}
}

