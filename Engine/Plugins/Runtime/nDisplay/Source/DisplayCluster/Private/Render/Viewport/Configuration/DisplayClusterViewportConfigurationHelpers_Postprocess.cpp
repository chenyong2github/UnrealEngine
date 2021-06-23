// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterViewportConfigurationHelpers_Postprocess.h"
#include "DisplayClusterViewportConfiguration.h"
#include "DisplayClusterConfigurationTypes.h"
#include "Render/Viewport/DisplayClusterViewport.h"
#include "Render/Viewport/DisplayClusterViewportManager.h"

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

static bool ImplUpdateICVFXColorGrading(FDisplayClusterViewport& DstViewport, ADisplayClusterRootActor& RootActor, const FDisplayClusterConfigurationViewport_ColorGradingConfiguration& InPPConfiguration)
{
	// Check and apply the overall cluster post process settings
	const FDisplayClusterConfigurationICVFX_StageSettings& StageSettings = RootActor.GetStageSettings();
	if (StageSettings.bUseOverallClusterPostProcess)
	{
		FDisplayClusterConfigurationViewport_CustomPostprocessSettings CustomPPS;
		CustomPPS.bIsEnabled = true;
		CustomPPS.bIsOneFrame = true;
		CustomPPS.BlendWeight = StageSettings.OverallClusterPostProcessSettings.BlendWeight;

		if (InPPConfiguration.bIsEnabled)
		{
			if (InPPConfiguration.bExcludeFromOverallClusterPostProcess)
			{
				// This viewport is excluded from the overall cluster, so only use the per-viewport settings and blend weight
				const FDisplayClusterConfigurationViewport_PerViewportSettings EmptyPPSettings;
				FDisplayClusterViewportConfigurationHelpers_Postprocess::BlendPostProcessSettings(CustomPPS.PostProcessSettings, EmptyPPSettings, InPPConfiguration.PostProcessSettings);
				CustomPPS.BlendWeight = InPPConfiguration.PostProcessSettings.BlendWeight;

				ImplUpdateCustomPostprocess(DstViewport, CustomPPS.bIsEnabled, CustomPPS, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::FinalPerViewport);
				return true;
			}

			// This viewport should use a blend of the overall cluster and the per-viewport settings, so multiply the blend weights
			FDisplayClusterViewportConfigurationHelpers_Postprocess::BlendPostProcessSettings(CustomPPS.PostProcessSettings, StageSettings.OverallClusterPostProcessSettings, InPPConfiguration.PostProcessSettings);
			CustomPPS.BlendWeight *= InPPConfiguration.PostProcessSettings.BlendWeight;

			ImplUpdateCustomPostprocess(DstViewport, CustomPPS.bIsEnabled, CustomPPS, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::FinalPerViewport);
			return true;
		}

		if (!InPPConfiguration.bExcludeFromOverallClusterPostProcess)
		{
			// This viewport doesn't use per-viewport settings, so just use the overall cluster settings and blend weight
			const FDisplayClusterConfigurationViewport_PerViewportSettings EmptyPPSettings;
			FDisplayClusterViewportConfigurationHelpers_Postprocess::BlendPostProcessSettings(CustomPPS.PostProcessSettings, StageSettings.OverallClusterPostProcessSettings, EmptyPPSettings);

			ImplUpdateCustomPostprocess(DstViewport, CustomPPS.bIsEnabled, CustomPPS, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::FinalPerViewport);
			return true;
		}

		// Disable all PP
		ImplRemoveCustomPostprocess(DstViewport, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::FinalPerViewport);
		return true;
	}

	// no PP
	return false;
}

bool FDisplayClusterViewportConfigurationHelpers_Postprocess::ImplUpdateInnerFrustumColorGrading(FDisplayClusterViewport& DstViewport, ADisplayClusterRootActor& RootActor, UDisplayClusterICVFXCameraComponent& InCameraComponent)
{
	const FDisplayClusterConfigurationICVFX_CameraSettings& CameraSettings = InCameraComponent.GetCameraSettingsICVFX();
	if (CameraSettings.AllNodesColorGradingConfiguration.bIsEnabled)
	{
		const FDisplayClusterRenderFrameSettings& RenderFrameSettings = DstViewport.Owner.Configuration->GetRenderFrameSettings();

		const FString& ClusterNodeId = RenderFrameSettings.ClusterNodeId;
		if (!ClusterNodeId.IsEmpty())
		{
			for (const FDisplayClusterConfigurationViewport_ColorGradingProfile& ColorGradingProfileIt : CameraSettings.PerNodeColorGradingProfiles)
			{
				for (const FString& ClusterNodeIt : ColorGradingProfileIt.ApplyPostProcessToObjects)
				{
					if (ClusterNodeId.Compare(ClusterNodeIt, ESearchCase::IgnoreCase) == 0)
					{
						// Use cluster node PP
						if (ImplUpdateICVFXColorGrading(DstViewport, RootActor, ColorGradingProfileIt.PostProcessSettings))
						{
							return true;
						}

						break;
					}
				}
			}
		}

		// cluster node OCIO override not found, use all nodes configuration
		if (ImplUpdateICVFXColorGrading(DstViewport, RootActor, CameraSettings.AllNodesColorGradingConfiguration))
		{
			return true;
		}
	}

	return false;
}

bool FDisplayClusterViewportConfigurationHelpers_Postprocess::UpdateLightcardPostProcessSettings(FDisplayClusterViewport& DstViewport, FDisplayClusterViewport& BaseViewport, ADisplayClusterRootActor& RootActor)
{
	const FDisplayClusterConfigurationICVFX_StageSettings& StageSettings = RootActor.GetStageSettings();
	const FDisplayClusterConfigurationICVFX_LightcardSettings& LightcardSettings = StageSettings.Lightcard;

	// First try use global OCIO from stage settings
	if (LightcardSettings.bEnableOuterViewportColorGrading)
	{
		if (ImplUpdateViewportColorGrading(DstViewport, RootActor, BaseViewport.GetId()))
		{
			return true;
		}
	}

	// This viewport doesn't use PP
	ImplRemoveCustomPostprocess(DstViewport, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::FinalPerViewport);

	return false;
}

bool FDisplayClusterViewportConfigurationHelpers_Postprocess::ImplUpdateViewportColorGrading(FDisplayClusterViewport& DstViewport, ADisplayClusterRootActor& RootActor, const FString& InClusterViewportId)
{
	const FDisplayClusterConfigurationICVFX_StageSettings& StageSettings = RootActor.GetStageSettings();

	for (const FDisplayClusterConfigurationViewport_ColorGradingProfile& ColorGradingProfileIt : StageSettings.PerViewportColorGradingProfiles)
	{
		for (const FString& ViewportNameIt : ColorGradingProfileIt.ApplyPostProcessToObjects)
		{
			if (InClusterViewportId.Compare(ViewportNameIt, ESearchCase::IgnoreCase) == 0)
			{
				// Use cluster node PP
				if (ImplUpdateICVFXColorGrading(DstViewport, RootActor, ColorGradingProfileIt.PostProcessSettings))
				{
					return true;
				}

				break;
			}
		}
	}

	// cluster node PP override not found, use all viewports configuration
	FDisplayClusterConfigurationViewport_ColorGradingConfiguration EmptyColorGrading;
	if (ImplUpdateICVFXColorGrading(DstViewport, RootActor, EmptyColorGrading))
	{
		return true;
	}

	// overall PP disabled
	return false;
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

	if (!ImplUpdateInnerFrustumColorGrading(DstViewport, RootActor, InCameraComponent))
	{
		// This viewport doesn't use per-viewport PP
		ImplRemoveCustomPostprocess(DstViewport, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::FinalPerViewport);
	}
}

void FDisplayClusterViewportConfigurationHelpers_Postprocess::UpdateCustomPostProcessSettings(FDisplayClusterViewport& DstViewport, ADisplayClusterRootActor& RootActor, const FDisplayClusterConfigurationViewport_CustomPostprocess& InCustomPostprocessConfiguration)
{
	// update postprocess settings (Start, Override, Final)
	ImplUpdateCustomPostprocess(DstViewport, InCustomPostprocessConfiguration.Start.bIsEnabled, InCustomPostprocessConfiguration.Start, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::Start);
	ImplUpdateCustomPostprocess(DstViewport, InCustomPostprocessConfiguration.Override.bIsEnabled, InCustomPostprocessConfiguration.Override, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::Override);
	ImplUpdateCustomPostprocess(DstViewport, InCustomPostprocessConfiguration.Final.bIsEnabled, InCustomPostprocessConfiguration.Final, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::Final);
}

void FDisplayClusterViewportConfigurationHelpers_Postprocess::UpdatePerViewportPostProcessSettings(FDisplayClusterViewport& DstViewport, ADisplayClusterRootActor& RootActor)
{
	if (!ImplUpdateViewportColorGrading(DstViewport, RootActor, DstViewport.GetId()))
	{
		// This viewport doesn't use PP
		ImplRemoveCustomPostprocess(DstViewport, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::FinalPerViewport);
	}
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
	PP_CONDITIONAL_BLEND(+, , , , AutoExposureBias, , );
	PP_CONDITIONAL_BLEND(+, , , , ColorCorrectionHighlightsMin, , );
	PP_CONDITIONAL_BLEND(+, , , , ColorCorrectionShadowsMax, , );

	PP_CONDITIONAL_BLEND(+, , , WhiteBalance., WhiteTemp, +, -6500.0f);
	PP_CONDITIONAL_BLEND(+, , , WhiteBalance., WhiteTint, , );

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

	PP_CONDITIONAL_BLEND(+, , , Misc., BlueCorrection, , );
	PP_CONDITIONAL_BLEND(+, , , Misc., ExpandGamut, , );
	PP_CONDITIONAL_BLEND(+, , , Misc., SceneColorTint, , );
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
		PP_CONDITIONAL_COPY(, , , AutoExposureBias);
		PP_CONDITIONAL_COPY(, , , ColorCorrectionHighlightsMin);
		PP_CONDITIONAL_COPY(, , , ColorCorrectionShadowsMax);

		PP_CONDITIONAL_COPY(, WhiteBalance., , WhiteTemp);
		PP_CONDITIONAL_COPY(, WhiteBalance., , WhiteTint);

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

		PP_CONDITIONAL_COPY(, Misc., , BlueCorrection);
		PP_CONDITIONAL_COPY(, Misc., , ExpandGamut);
		PP_CONDITIONAL_COPY(, Misc., , SceneColorTint);
	}
}

#define PP_COPY(COLOR, OUTGROUP, INGROUP, NAME) \
		OutViewportPPSettings->OUTGROUP NAME = InPPS->COLOR##NAME##INGROUP; \
		OutViewportPPSettings->OUTGROUP bOverride_##NAME = true; \

void FDisplayClusterViewportConfigurationHelpers_Postprocess::CopyPPSStruct(FDisplayClusterConfigurationViewport_PerViewportSettings* OutViewportPPSettings, FPostProcessSettings* InPPS)
{
	if ((OutViewportPPSettings != nullptr) && (InPPS != nullptr))
	{
		PP_COPY(, , , AutoExposureBias);
		PP_COPY(, , , ColorCorrectionHighlightsMin);
		PP_COPY(, , , ColorCorrectionShadowsMax);

		PP_COPY(, WhiteBalance., , WhiteTemp);
		PP_COPY(, WhiteBalance., , WhiteTint);

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

		PP_COPY(, Misc., , BlueCorrection);
		PP_COPY(, Misc., , ExpandGamut);
		PP_COPY(, Misc., , SceneColorTint);
	}
}
