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

static bool ImplUpdateICVFXColorGrading(FDisplayClusterViewport& DstViewport, ADisplayClusterRootActor& RootActor, const FDisplayClusterConfigurationViewport_ColorGradingConfiguration* InPP, const FDisplayClusterConfigurationViewport_ColorGradingProfile* InCustomPP)
{
	// Check and apply the overall cluster post process settings
	const FDisplayClusterConfigurationICVFX_StageSettings& StageSettings = RootActor.GetStageSettings();

	// Handle blend logics stack
	bool bUseOverallClusterPostProcess = InCustomPP ? (InCustomPP->bExcludeFromOverallClusterPostProcess == false) : (InPP==nullptr || InPP->bExcludeFromOverallClusterPostProcess == false);
	bool bUseAllNodesPostProcess = InCustomPP ? (InCustomPP->bExcludeFromAllNodesPostProcess == false) : true;
	bool bUsePerNodePostProcess = InCustomPP ? (InCustomPP->bIsEnabled) : false;

	if (InPP==nullptr || InPP->bIsEnabled==false)
	{
		bUseAllNodesPostProcess = false;
	}

	if (!StageSettings.bUseOverallClusterPostProcess)
	{
		bUseOverallClusterPostProcess = false;
	}

	FDisplayClusterConfigurationViewport_CustomPostprocessSettings CustomPPS;
	CustomPPS.bIsEnabled = true;
	CustomPPS.bIsOneFrame = true;
	CustomPPS.BlendWeight = 1;

	if (bUseOverallClusterPostProcess && bUseAllNodesPostProcess)
	{
		CustomPPS.BlendWeight = StageSettings.OverallClusterPostProcessSettings.BlendWeight;

		if (bUsePerNodePostProcess)
		{
			// This viewport should use a blend of the overall cluster and the per-viewport settings, so multiply the blend weights
			FDisplayClusterViewportConfigurationHelpers_Postprocess::PerNodeBlendPostProcessSettings(CustomPPS.PostProcessSettings, StageSettings.OverallClusterPostProcessSettings, InPP->PostProcessSettings, InCustomPP->PostProcessSettings);
			CustomPPS.BlendWeight *= InPP->PostProcessSettings.BlendWeight * InCustomPP->PostProcessSettings.BlendWeight;
		}
		else
		{
			// This viewport should use a blend of the overall cluster and the per-viewport settings, so multiply the blend weights
			FDisplayClusterViewportConfigurationHelpers_Postprocess::BlendPostProcessSettings(CustomPPS.PostProcessSettings, StageSettings.OverallClusterPostProcessSettings, InPP->PostProcessSettings);
			CustomPPS.BlendWeight *= InPP->PostProcessSettings.BlendWeight;
		}

		ImplUpdateCustomPostprocess(DstViewport, CustomPPS.bIsEnabled, CustomPPS, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::FinalPerViewport);
		return true;
	}

	if (!bUseOverallClusterPostProcess && !bUseAllNodesPostProcess)
	{
		if (bUsePerNodePostProcess)
		{
			FDisplayClusterViewportConfigurationHelpers_Postprocess::CopyBlendPostProcessSettings(CustomPPS.PostProcessSettings, InCustomPP->PostProcessSettings);
			CustomPPS.BlendWeight *= InCustomPP->PostProcessSettings.BlendWeight;

			ImplUpdateCustomPostprocess(DstViewport, CustomPPS.bIsEnabled, CustomPPS, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::FinalPerViewport);
			return true;
		}
	}
	else
	{
		if (bUseOverallClusterPostProcess)
		{
			CustomPPS.BlendWeight = StageSettings.OverallClusterPostProcessSettings.BlendWeight;

			if (bUsePerNodePostProcess)
			{
				FDisplayClusterViewportConfigurationHelpers_Postprocess::BlendPostProcessSettings(CustomPPS.PostProcessSettings, StageSettings.OverallClusterPostProcessSettings, InCustomPP->PostProcessSettings);
				CustomPPS.BlendWeight *= InCustomPP->PostProcessSettings.BlendWeight;
			}
			else
			{
				// This viewport doesn't use per-viewport settings, so just use the overall cluster settings and blend weight
				FDisplayClusterViewportConfigurationHelpers_Postprocess::CopyBlendPostProcessSettings(CustomPPS.PostProcessSettings, StageSettings.OverallClusterPostProcessSettings);
			}

			ImplUpdateCustomPostprocess(DstViewport, CustomPPS.bIsEnabled, CustomPPS, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::FinalPerViewport);
			return true;
		}

		if (bUseAllNodesPostProcess)
		{
			CustomPPS.BlendWeight = InPP->PostProcessSettings.BlendWeight;

			if (bUsePerNodePostProcess)
			{
				FDisplayClusterViewportConfigurationHelpers_Postprocess::BlendPostProcessSettings(CustomPPS.PostProcessSettings, InPP->PostProcessSettings, InCustomPP->PostProcessSettings);
				CustomPPS.BlendWeight *= InCustomPP->PostProcessSettings.BlendWeight;
			}
			else
			{
				// This viewport doesn't use per-viewport settings, so just use the All nodes cluster settings and blend weight
				FDisplayClusterViewportConfigurationHelpers_Postprocess::CopyBlendPostProcessSettings(CustomPPS.PostProcessSettings, InPP->PostProcessSettings);
			}

			ImplUpdateCustomPostprocess(DstViewport, CustomPPS.bIsEnabled, CustomPPS, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::FinalPerViewport);
			return true;
		}
	}

	// Not implemented cases:
	// Disable all PP
	return false;
}

bool FDisplayClusterViewportConfigurationHelpers_Postprocess::ImplUpdateInnerFrustumColorGrading(FDisplayClusterViewport& DstViewport, ADisplayClusterRootActor& RootActor, UDisplayClusterICVFXCameraComponent& InCameraComponent)
{
	const FDisplayClusterConfigurationICVFX_CameraSettings& CameraSettings = InCameraComponent.GetCameraSettingsICVFX();
	if (CameraSettings.bUseInnerFrustumColorGrading)
	{
		const FDisplayClusterRenderFrameSettings& RenderFrameSettings = DstViewport.Owner.Configuration->GetRenderFrameSettings();

		const FString& ClusterNodeId = RenderFrameSettings.ClusterNodeId;
		if (!ClusterNodeId.IsEmpty())
		{
			for (const FDisplayClusterConfigurationViewport_ColorGradingProfile& ColorGradingProfileIt : CameraSettings.PerNodeColorGradingProfiles)
			{
				// Only allowed profiles
				if (ColorGradingProfileIt.bIsProfileEnabled)
				{
					for (const FString& ClusterNodeIt : ColorGradingProfileIt.ApplyPostProcessToObjects)
					{
						if (ClusterNodeId.Compare(ClusterNodeIt, ESearchCase::IgnoreCase) == 0)
						{
							// Use cluster node PP
							return ImplUpdateICVFXColorGrading(DstViewport, RootActor, &CameraSettings.AllNodesColorGradingConfiguration, &ColorGradingProfileIt);
						}
					}
				}
			}
		}

		return ImplUpdateICVFXColorGrading(DstViewport, RootActor, &CameraSettings.AllNodesColorGradingConfiguration, nullptr);
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
		if (ColorGradingProfileIt.bIsProfileEnabled)
		{
			for (const FString& ViewportNameIt : ColorGradingProfileIt.ApplyPostProcessToObjects)
			{
				if (InClusterViewportId.Compare(ViewportNameIt, ESearchCase::IgnoreCase) == 0)
				{
					// Use cluster node PP
					return ImplUpdateICVFXColorGrading(DstViewport, RootActor, nullptr, &ColorGradingProfileIt);
				}
			}
		}
	}

	// cluster node PP override not found, use all viewports configuration
	if (ImplUpdateICVFXColorGrading(DstViewport, RootActor, nullptr, nullptr))
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
	{ \
		bool bOverridePPSettings0 = PPSettings0.INGROUP bOverride_##NAME; \
		bool bOverridePPSettings1 = (PPSettings1 != nullptr) && PPSettings1->INGROUP bOverride_##NAME; \
		bool bOverridePPSettings2 = (PPSettings2 != nullptr) && PPSettings2->INGROUP bOverride_##NAME; \
		 \
		if (bOverridePPSettings0 && bOverridePPSettings1 && bOverridePPSettings2) \
		{ \
			OutputPP.COLOR##NAME##OUTGROUP = PPSettings0.INGROUP NAME BLENDOP PPSettings1->INGROUP NAME BLENDOP PPSettings2->INGROUP NAME OFFSETOP OFFSETVALUE OFFSETOP OFFSETVALUE; \
			OutputPP.bOverride_##COLOR##NAME##OUTGROUP = true; \
		} \
		else if (bOverridePPSettings0 && bOverridePPSettings1) \
		{ \
			OutputPP.COLOR##NAME##OUTGROUP = PPSettings0.INGROUP NAME BLENDOP PPSettings1->INGROUP NAME OFFSETOP OFFSETVALUE; \
			OutputPP.bOverride_##COLOR##NAME##OUTGROUP = true; \
		} \
		else if (bOverridePPSettings0 && bOverridePPSettings2) \
		{ \
			OutputPP.COLOR##NAME##OUTGROUP = PPSettings0.INGROUP NAME BLENDOP PPSettings2->INGROUP NAME OFFSETOP OFFSETVALUE; \
			OutputPP.bOverride_##COLOR##NAME##OUTGROUP = true; \
		} \
		else if (bOverridePPSettings1 && bOverridePPSettings2) \
		{ \
			OutputPP.COLOR##NAME##OUTGROUP = PPSettings1->INGROUP NAME BLENDOP PPSettings2->INGROUP NAME OFFSETOP OFFSETVALUE; \
			OutputPP.bOverride_##COLOR##NAME##OUTGROUP = true; \
		} \
		else if (bOverridePPSettings2) \
		{ \
			OutputPP.COLOR##NAME##OUTGROUP = PPSettings2->INGROUP NAME; \
			OutputPP.bOverride_##COLOR##NAME##OUTGROUP = true; \
		} \
		else if (bOverridePPSettings1) \
		{ \
			OutputPP.COLOR##NAME##OUTGROUP = PPSettings1->INGROUP NAME; \
			OutputPP.bOverride_##COLOR##NAME##OUTGROUP = true; \
		} \
		else if (bOverridePPSettings0) \
		{ \
			OutputPP.COLOR##NAME##OUTGROUP = PPSettings0.INGROUP NAME; \
			OutputPP.bOverride_##COLOR##NAME##OUTGROUP = true; \
		} \
	} \

#define PP_CONDITIONAL_OVERRIDE(COLOR, OUTGROUP, INGROUP, NAME) \
	{ \
		bool bOverridePPSettings0 = PPSettings0.INGROUP bOverride_##NAME; \
		bool bOverridePPSettings1 = PPSettings1 && PPSettings1->INGROUP bOverride_##NAME; \
		bool bOverridePPSettings2 = PPSettings2 && PPSettings2->INGROUP bOverride_##NAME; \
		if (bOverridePPSettings2) \
		{ \
			OutputPP.COLOR##NAME##OUTGROUP = PPSettings2->INGROUP NAME; \
			OutputPP.bOverride_##COLOR##NAME##OUTGROUP = true; \
		} \
		if (bOverridePPSettings1) \
		{ \
			OutputPP.COLOR##NAME##OUTGROUP = PPSettings1->INGROUP NAME; \
			OutputPP.bOverride_##COLOR##NAME##OUTGROUP = true; \
		} \
		if (bOverridePPSettings0) \
		{ \
			OutputPP.COLOR##NAME##OUTGROUP = PPSettings0.INGROUP NAME; \
			OutputPP.bOverride_##COLOR##NAME##OUTGROUP = true; \
		} \
	} \

static void ImplBlendPostProcessSettings(FPostProcessSettings& OutputPP, const FDisplayClusterConfigurationViewport_PerViewportSettings& PPSettings0, const FDisplayClusterConfigurationViewport_PerViewportSettings* PPSettings1, const FDisplayClusterConfigurationViewport_PerViewportSettings* PPSettings2)
{
	PP_CONDITIONAL_BLEND(+, , , , AutoExposureBias, , );
	PP_CONDITIONAL_BLEND(+, , , , ColorCorrectionHighlightsMin, , );
	PP_CONDITIONAL_BLEND(+, , , , ColorCorrectionShadowsMax, , );

	// Prioritize viewport post process settings over cluster.
	PP_CONDITIONAL_OVERRIDE(, , WhiteBalance., TemperatureType);
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
void FDisplayClusterViewportConfigurationHelpers_Postprocess::CopyBlendPostProcessSettings(FPostProcessSettings& OutputPP, const FDisplayClusterConfigurationViewport_PerViewportSettings& InPPSettings)
{
	ImplBlendPostProcessSettings(OutputPP, InPPSettings, nullptr, nullptr);
}

void FDisplayClusterViewportConfigurationHelpers_Postprocess::PerNodeBlendPostProcessSettings(FPostProcessSettings& OutputPP, const FDisplayClusterConfigurationViewport_PerViewportSettings& ClusterPPSettings, const FDisplayClusterConfigurationViewport_PerViewportSettings& ViewportPPSettings, const FDisplayClusterConfigurationViewport_PerViewportSettings& PerNodePPSettings)
{
	ImplBlendPostProcessSettings(OutputPP, ClusterPPSettings, &ViewportPPSettings, &PerNodePPSettings);
}

void FDisplayClusterViewportConfigurationHelpers_Postprocess::BlendPostProcessSettings(FPostProcessSettings& OutputPP, const FDisplayClusterConfigurationViewport_PerViewportSettings& ClusterPPSettings, const FDisplayClusterConfigurationViewport_PerViewportSettings& ViewportPPSettings)
{
	ImplBlendPostProcessSettings(OutputPP, ClusterPPSettings, &ViewportPPSettings, nullptr);
}

#define PP_CONDITIONAL_COPY(COLOR, OUTGROUP, INGROUP, NAME) \
		if (!bIsConditionalCopy || InPPS->bOverride_##COLOR##NAME##INGROUP) \
		{ \
			OutViewportPPSettings->OUTGROUP NAME = InPPS->COLOR##NAME##INGROUP; \
			OutViewportPPSettings->OUTGROUP bOverride_##NAME = true; \
		}

static void ImplCopyPPSStruct(bool bIsConditionalCopy, FDisplayClusterConfigurationViewport_PerViewportSettings* OutViewportPPSettings, FPostProcessSettings* InPPS)
{
	if ((OutViewportPPSettings != nullptr) && (InPPS != nullptr))
	{
		PP_CONDITIONAL_COPY(, , , AutoExposureBias);
		PP_CONDITIONAL_COPY(, , , ColorCorrectionHighlightsMin);
		PP_CONDITIONAL_COPY(, , , ColorCorrectionShadowsMax);

		PP_CONDITIONAL_COPY(, WhiteBalance., , TemperatureType);
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

void FDisplayClusterViewportConfigurationHelpers_Postprocess::CopyPPSStructConditional(FDisplayClusterConfigurationViewport_PerViewportSettings* OutViewportPPSettings, FPostProcessSettings* InPPS)
{
	ImplCopyPPSStruct(true, OutViewportPPSettings, InPPS);
}

void FDisplayClusterViewportConfigurationHelpers_Postprocess::CopyPPSStruct(FDisplayClusterConfigurationViewport_PerViewportSettings* OutViewportPPSettings, FPostProcessSettings* InPPS)
{
	ImplCopyPPSStruct(false, OutViewportPPSettings, InPPS);
}
