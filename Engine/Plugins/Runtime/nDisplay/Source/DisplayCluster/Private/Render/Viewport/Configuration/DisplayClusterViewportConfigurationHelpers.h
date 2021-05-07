// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Render/Viewport/DisplayClusterViewport.h"
#include "Render/Viewport/DisplayClusterViewportManager.h"

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

#include "DisplayClusterSceneViewExtensions.h"
#include "OpenColorIODisplayExtension.h"

class DisplayClusterViewportConfigurationHelpers
{
public:
	static void UpdateViewportOCIOConfiguration(FDisplayClusterViewport& DstViewport, const FOpenColorIODisplayConfiguration& InOCIO_Configuration)
	{
		if (InOCIO_Configuration.bIsEnabled && InOCIO_Configuration.ColorConfiguration.IsValid())
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
				DstViewport.OpenColorIODisplayExtension->SetDisplayConfiguration(InOCIO_Configuration);
			}
		}
		else
		{
			// Remove OICO ref
			DstViewport.OpenColorIODisplayExtension.Reset();
		}
	}

	//@todo: implement bIsOneFrame flag logic
	static void ImplUpdateViewportSetting_CustomPostprocess(FDisplayClusterViewport& DstViewport, const FDisplayClusterConfigurationViewport_CustomPostprocessSettings& InCustomPostprocess, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass RenderPass)
	{
		DstViewport.CustomPostProcessSettings.AddCustomPostProcess(RenderPass, InCustomPostprocess.PostProcessSettings, InCustomPostprocess.BlendWeight, InCustomPostprocess.bIsOneFrame);
	}

	//@todo: Implement PP disable, other PP sources, etc
	static void UpdateViewportSetting_CustomPostprocess(FDisplayClusterViewport& DstViewport, const FDisplayClusterConfigurationViewport_CustomPostprocess& InCustomPostprocessConfiguration)
	{
		if (InCustomPostprocessConfiguration.Start.bIsEnabled)
		{
			ImplUpdateViewportSetting_CustomPostprocess(DstViewport, InCustomPostprocessConfiguration.Start, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::Start);
		}
		if (InCustomPostprocessConfiguration.Override.bIsEnabled)
		{
			ImplUpdateViewportSetting_CustomPostprocess(DstViewport, InCustomPostprocessConfiguration.Override, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::Override);
		}
		if (InCustomPostprocessConfiguration.Final.bIsEnabled)
		{
			ImplUpdateViewportSetting_CustomPostprocess(DstViewport, InCustomPostprocessConfiguration.Final, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::Final);
		}
	}

	#define PP_CONDITIONAL_BLEND_WITH_OFFSET(OP, NAME, OFFSET) \
		OutputPP.bOverride_##NAME = ClusterPPSettings.bOverride_##NAME || ViewportPPSettings.bOverride_##NAME; \
		if (ClusterPPSettings.bOverride_##NAME && ViewportPPSettings.bOverride_##NAME) \
		{ \
			OutputPP.##NAME = ClusterPPSettings.##NAME ##OP ViewportPPSettings.##NAME ##OP ##OFFSET; \
		} \
		else if (ClusterPPSettings.bOverride_##NAME) \
		{ \
			OutputPP.##NAME = ClusterPPSettings.##NAME; \
		} \
		else if (ViewportPPSettings.bOverride_##NAME) \
		{ \
			OutputPP.##NAME = ViewportPPSettings.##NAME; \
		} \

	// Note that skipped parameters in macro definitions will just evaluate to nothing
	// This is intentional to get around the inconsistent naming in the color grading fields in FPostProcessSettings
	#define PP_CONDITIONAL_BLEND_COLOR(OP, OUTGROUP, INGROUP, NAME) \
		OutputPP.bOverride_Color##NAME##OUTGROUP = ClusterPPSettings.##INGROUP.bOverride_##NAME || ViewportPPSettings.##INGROUP.bOverride_##NAME; \
		if (ClusterPPSettings.##INGROUP.bOverride_##NAME && ViewportPPSettings.##INGROUP.bOverride_##NAME) \
		{ \
			OutputPP.Color##NAME##OUTGROUP = ClusterPPSettings.##INGROUP.##NAME ##OP ViewportPPSettings.##INGROUP.##NAME; \
		} \
		else if (ClusterPPSettings.##INGROUP.bOverride_##NAME) \
		{ \
			OutputPP.Color##NAME##OUTGROUP = ClusterPPSettings.##INGROUP.##NAME; \
		} \
		else if (ViewportPPSettings.##INGROUP.bOverride_##NAME) \
		{ \
			OutputPP.Color##NAME##OUTGROUP = ViewportPPSettings.##INGROUP.##NAME; \
		} \

	static void BlendPostProcessSettings(FPostProcessSettings& OutputPP, const FDisplayClusterConfigurationViewport_PerViewportSettings& ClusterPPSettings, const FDisplayClusterConfigurationViewport_PerViewportSettings& ViewportPPSettings)
	{
		PP_CONDITIONAL_BLEND_WITH_OFFSET(+, WhiteTemp, -6500.0f);
		PP_CONDITIONAL_BLEND_WITH_OFFSET(+, WhiteTint, 0.0f);
		PP_CONDITIONAL_BLEND_WITH_OFFSET(+, AutoExposureBias, 0.0f);

		PP_CONDITIONAL_BLEND_COLOR(*, , Global, Saturation);
		PP_CONDITIONAL_BLEND_COLOR(*, , Global, Contrast);
		PP_CONDITIONAL_BLEND_COLOR(*, , Global, Gamma);
		PP_CONDITIONAL_BLEND_COLOR(*, , Global, Gain);
		PP_CONDITIONAL_BLEND_COLOR(+, , Global, Offset);

		PP_CONDITIONAL_BLEND_COLOR(*, Shadows, Shadows, Saturation);
		PP_CONDITIONAL_BLEND_COLOR(*, Shadows, Shadows, Contrast);
		PP_CONDITIONAL_BLEND_COLOR(*, Shadows, Shadows, Gamma);
		PP_CONDITIONAL_BLEND_COLOR(*, Shadows, Shadows, Gain);
		PP_CONDITIONAL_BLEND_COLOR(+, Shadows, Shadows, Offset);

		PP_CONDITIONAL_BLEND_COLOR(*, Midtones, Midtones, Saturation);
		PP_CONDITIONAL_BLEND_COLOR(*, Midtones, Midtones, Contrast);
		PP_CONDITIONAL_BLEND_COLOR(*, Midtones, Midtones, Gamma);
		PP_CONDITIONAL_BLEND_COLOR(*, Midtones, Midtones, Gain);
		PP_CONDITIONAL_BLEND_COLOR(+, Midtones, Midtones, Offset);

		PP_CONDITIONAL_BLEND_COLOR(*, Highlights, Highlights, Saturation);
		PP_CONDITIONAL_BLEND_COLOR(*, Highlights, Highlights, Contrast);
		PP_CONDITIONAL_BLEND_COLOR(*, Highlights, Highlights, Gamma);
		PP_CONDITIONAL_BLEND_COLOR(*, Highlights, Highlights, Gain);
		PP_CONDITIONAL_BLEND_COLOR(+, Highlights, Highlights, Offset);
	}

	static void UpdateViewportPostProcessSettings(FDisplayClusterViewport& DstViewport, const FDisplayClusterConfigurationViewport_PostProcessSettings& InPostProcessSettings)
	{
		// Check and apply the overall cluster post process settings
		if (ADisplayClusterRootActor* RootActor = DstViewport.GetOwner().GetRootActor())
		{
			if (UDisplayClusterConfigurationData* ConfigData = RootActor->GetConfigData())
			{
				if (UDisplayClusterConfigurationCluster* ClusterConfig = ConfigData->Cluster)
				{
					if (ClusterConfig->bUseOverallClusterPostProcess)
					{
						FDisplayClusterConfigurationViewport_CustomPostprocessSettings CustomPPS;
						CustomPPS.bIsEnabled = true;
						CustomPPS.bIsOneFrame = true;
						CustomPPS.BlendWeight = ClusterConfig->OverallClusterPostProcessSettings.BlendWeight;

						const FDisplayClusterConfigurationViewport_PerViewportSettings EmptyPPSettings;

						if (InPostProcessSettings.bIsEnabled)
						{
							if (InPostProcessSettings.bExcludeFromOverallClusterPostProcess)
							{
								// This viewport is excluded from the overall cluster, so only use the per-viewport settings and blend weight
								BlendPostProcessSettings(CustomPPS.PostProcessSettings, EmptyPPSettings, InPostProcessSettings.ViewportSettings);
								CustomPPS.BlendWeight = InPostProcessSettings.ViewportSettings.BlendWeight;
							}
							else
							{
								// This viewport should use a blend of the overall cluster and the per-viewport settings, so multiply the blend weights
								BlendPostProcessSettings(CustomPPS.PostProcessSettings, ClusterConfig->OverallClusterPostProcessSettings, InPostProcessSettings.ViewportSettings);
								CustomPPS.BlendWeight *= InPostProcessSettings.ViewportSettings.BlendWeight;
							}
						}
						else
						{
							// This viewport doesn't use per-viewport settings, so just use the overall cluster settings and blend weight
							BlendPostProcessSettings(CustomPPS.PostProcessSettings, ClusterConfig->OverallClusterPostProcessSettings, EmptyPPSettings);
						}

						ImplUpdateViewportSetting_CustomPostprocess(DstViewport, CustomPPS, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::Override);

						return;
					}
				}
			}
		}

		// If there isn't a global PP set, then just apply the per-viewport settings and blend weight
		if (InPostProcessSettings.bIsEnabled)
		{
			FDisplayClusterConfigurationViewport_CustomPostprocessSettings CustomPPS;
			CustomPPS.bIsEnabled = true;
			CustomPPS.bIsOneFrame = true;
			CustomPPS.BlendWeight = InPostProcessSettings.ViewportSettings.BlendWeight;

			const FDisplayClusterConfigurationViewport_PerViewportSettings EmptyPPSettings;

			BlendPostProcessSettings(CustomPPS.PostProcessSettings, EmptyPPSettings, InPostProcessSettings.ViewportSettings);
			ImplUpdateViewportSetting_CustomPostprocess(DstViewport, CustomPPS, IDisplayClusterViewport_CustomPostProcessSettings::ERenderPass::Override);
		}
	}

	static bool IsVisibilitySettingsDefined(const FDisplayClusterConfigurationICVFX_VisibilityList& InVisibilityList)
	{
		return InVisibilityList.ActorLayers.Num() > 0 || InVisibilityList.Actors.Num() > 0 || InVisibilityList.RootActorComponentNames.Num() > 0;
	}

	static void UpdateVisibilitySetting(FDisplayClusterViewport& DstViewport, EDisplayClusterViewport_VisibilityMode VisibilityMode, const FDisplayClusterConfigurationICVFX_VisibilityList& InVisibilityList)
	{
		TSet<FPrimitiveComponentId> AdditionalComponentsList;

		// Collect Root Actor components
		//@todo: Make this GUI user friendly 
		// now test purpose
		if (InVisibilityList.RootActorComponentNames.Num() > 0)
		{
			ADisplayClusterRootActor* RootActor = DstViewport.GetOwner().GetRootActor();
			if (RootActor)
			{
				RootActor->FindPrimitivesByName(InVisibilityList.RootActorComponentNames, AdditionalComponentsList);
			}
		}

		// Collect Actor ComponentIds
		for (const AActor* ActorIt : InVisibilityList.Actors)
		{
			if (ActorIt)
			{
				for (const UActorComponent* Component : ActorIt->GetComponents())
				{
					if (const UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Component))
					{
						AdditionalComponentsList.Add(PrimComp->ComponentId);
					}
				}
			}
		}

		DstViewport.VisibilitySettings.UpdateConfiguration(VisibilityMode, InVisibilityList.ActorLayers, AdditionalComponentsList);
	}

	static void UpdateViewportStereoMode(FDisplayClusterViewport& DstViewport, const EDisplayClusterConfigurationViewport_StereoMode StereoMode)
	{
		switch (StereoMode)
		{
		case EDisplayClusterConfigurationViewport_StereoMode::ForceMono:
			DstViewport.RenderSettings.bForceMono = true;
			break;
		default:
			DstViewport.RenderSettings.bForceMono = false;
			break;
		}
	}

	static void UpdateViewportSetting_OverlayRenderSettings(FDisplayClusterViewport& DstViewport, const FDisplayClusterConfigurationICVFX_OverlayAdvancedRenderSettings& InOverlaySettings)
	{
		DstViewport.RenderSettings.BufferRatio = InOverlaySettings.BufferRatio;
		DstViewport.RenderSettings.RenderTargetRatio = InOverlaySettings.RenderTargetRatio;

		DstViewport.RenderSettings.GPUIndex = InOverlaySettings.GPUIndex;
		DstViewport.RenderSettings.StereoGPUIndex = InOverlaySettings.StereoGPUIndex;

		UpdateViewportStereoMode(DstViewport, InOverlaySettings.StereoMode);

		DstViewport.RenderSettings.RenderFamilyGroup = InOverlaySettings.RenderFamilyGroup;
	};

	static void UpdateViewportSetting_Override(FDisplayClusterViewport& DstViewport, const FDisplayClusterConfigurationPostRender_Override& InOverride)
	{
		DstViewport.PostRenderSettings.Override.TextureRHI.SafeRelease();

		if (InOverride.bAllowOverride && InOverride.SourceTexture != nullptr)
		{
			FTextureRHIRef& TextureRHI = InOverride.SourceTexture->Resource->TextureRHI;

			if (TextureRHI.IsValid())
			{
				DstViewport.PostRenderSettings.Override.TextureRHI = TextureRHI;
				FIntVector Size = TextureRHI->GetSizeXYZ();

				DstViewport.PostRenderSettings.Override.Rect = DstViewport.GetValidRect((InOverride.bShouldUseTextureRegion) ? InOverride.TextureRegion.ToRect() : FIntRect(FIntPoint(0, 0), FIntPoint(Size.X, Size.Y)), TEXT("Configuration Override"));
			}
		}
	};

	static void UpdateViewportSetting_PostprocessBlur(FDisplayClusterViewport& DstViewport, const FDisplayClusterConfigurationPostRender_BlurPostprocess& InBlurPostprocess)
	{
		switch (InBlurPostprocess.Mode)
		{
		case EDisplayClusterConfiguration_PostRenderBlur::Gaussian:
			DstViewport.PostRenderSettings.PostprocessBlur.Mode = EDisplayClusterShaderParameters_PostprocessBlur::Gaussian;
			break;
		case EDisplayClusterConfiguration_PostRenderBlur::Dilate:
			DstViewport.PostRenderSettings.PostprocessBlur.Mode = EDisplayClusterShaderParameters_PostprocessBlur::Dilate;
			break;
		default:
			DstViewport.PostRenderSettings.PostprocessBlur.Mode = EDisplayClusterShaderParameters_PostprocessBlur::None;
			break;
		}

		DstViewport.PostRenderSettings.PostprocessBlur.KernelRadius = InBlurPostprocess.KernelRadius;
		DstViewport.PostRenderSettings.PostprocessBlur.KernelScale = InBlurPostprocess.KernelScale;
	};

	static void UpdateViewportSetting_Overscan(FDisplayClusterViewport& DstViewport, const FDisplayClusterConfigurationViewport_Overscan& InOverscan)
	{
		FImplDisplayClusterViewport_OverscanSettings OverscanSettings;
		OverscanSettings.bOversize = InOverscan.bOversize;

		switch (InOverscan.Mode)
		{
		case EDisplayClusterConfigurationViewportOverscanMode::Percent:
			OverscanSettings.Mode = EDisplayClusterViewport_OverscanMode::Percent;

			// Scale 0..100% to 0..1 range
			OverscanSettings.Left   = .01f * InOverscan.Left;
			OverscanSettings.Right  = .01f * InOverscan.Right;
			OverscanSettings.Top    = .01f * InOverscan.Top;
			OverscanSettings.Bottom = .01f * InOverscan.Bottom;
			break;

		case EDisplayClusterConfigurationViewportOverscanMode::Pixels:
			OverscanSettings.Mode = EDisplayClusterViewport_OverscanMode::Pixels;

			OverscanSettings.Left   =InOverscan.Left;
			OverscanSettings.Right  =InOverscan.Right;
			OverscanSettings.Top    =InOverscan.Top;
			OverscanSettings.Bottom =InOverscan.Bottom;
			break;

		default:
			break;
		}

		DstViewport.OverscanRendering.Set(OverscanSettings);
	};

	static void UpdateViewportSetting_GenerateMips(FDisplayClusterViewport& DstViewport, const FDisplayClusterConfigurationPostRender_GenerateMips& InGenerateMips)
	{
		DstViewport.PostRenderSettings.GenerateMips.bAutoGenerateMips = InGenerateMips.bAutoGenerateMips;

		DstViewport.PostRenderSettings.GenerateMips.MipsSamplerFilter = InGenerateMips.MipsSamplerFilter;
		DstViewport.PostRenderSettings.GenerateMips.MipsAddressU = InGenerateMips.MipsAddressU;
		DstViewport.PostRenderSettings.GenerateMips.MipsAddressV = InGenerateMips.MipsAddressV;

		DstViewport.PostRenderSettings.GenerateMips.MaxNumMipsLimit = (InGenerateMips.bShouldUseMaxNumMips) ? InGenerateMips.MaxNumMips : 100;
	}

	static void ResetViewportRuntimeParameters(FDisplayClusterViewport& DstViewport)
	{
		// Reset runtim flags from prev frame:
		DstViewport.RenderSettings.BeginUpdateSettings();
		DstViewport.RenderSettingsICVFX.BeginUpdateSettings();
		DstViewport.PostRenderSettings.BeginUpdateSettings();
		DstViewport.VisibilitySettings.ResetConfiguration();
		DstViewport.CameraMotionBlur.ResetConfiguration();
		DstViewport.OverscanRendering.ResetConfiguration();
	}

	static void UpdateBaseViewportSetting(FDisplayClusterViewport& DstViewport, const UDisplayClusterConfigurationViewport& InConfigurationViewport)
	{
		// Reset runtime flags from prev frame:
		ResetViewportRuntimeParameters(DstViewport);

		// UDisplayClusterConfigurationViewport
		{
			if (InConfigurationViewport.bAllowRendering == false)
			{
				DstViewport.RenderSettings.bEnable = false;
			}

			DstViewport.RenderSettings.CameraId = InConfigurationViewport.Camera;
			DstViewport.RenderSettings.Rect = DstViewport.GetValidRect(InConfigurationViewport.Region.ToRect(), TEXT("Configuration Region"));

			DstViewport.RenderSettings.GPUIndex = InConfigurationViewport.GPUIndex;
			DstViewport.RenderSettings.OverlapOrder = InConfigurationViewport.OverlapOrder;
		}

		// OCIO
		UpdateViewportOCIOConfiguration(DstViewport, InConfigurationViewport.OCIO_Configuration);

		// Additional per-viewport PostProcess
		UpdateViewportPostProcessSettings(DstViewport, InConfigurationViewport.PostProcessSettings);

		// FDisplayClusterConfigurationViewport_RenderSettings
		const FDisplayClusterConfigurationViewport_RenderSettings& InRenderSettings = InConfigurationViewport.RenderSettings;
		{
			DstViewport.RenderSettings.BufferRatio = InRenderSettings.BufferRatio;

			UpdateViewportSetting_Overscan(DstViewport, InRenderSettings.Overscan);
			UpdateViewportSetting_CustomPostprocess(DstViewport, InRenderSettings.CustomPostprocess);

			UpdateViewportSetting_Override(DstViewport,        InRenderSettings.Override);
			UpdateViewportSetting_PostprocessBlur(DstViewport, InRenderSettings.PostprocessBlur);
			UpdateViewportSetting_GenerateMips(DstViewport,    InRenderSettings.GenerateMips);

			UpdateViewportStereoMode(DstViewport, InRenderSettings.StereoMode);

			DstViewport.RenderSettings.StereoGPUIndex = InRenderSettings.StereoGPUIndex;
			DstViewport.RenderSettings.RenderTargetRatio = InRenderSettings.RenderTargetRatio;
			DstViewport.RenderSettings.RenderFamilyGroup = InRenderSettings.RenderFamilyGroup;
		}

		// FDisplayClusterConfigurationViewport_ICVFX property:
		{
			EDisplayClusterViewportICVFXFlags& TargetFlags = DstViewport.RenderSettingsICVFX.Flags;

			if (InConfigurationViewport.ICVFX.bAllowICVFX)
			{
				TargetFlags |= ViewportICVFX_Enable;

				switch (InConfigurationViewport.ICVFX.CameraRenderMode)
				{
					// Disable camera frame render for this viewport
				case EDisplayClusterConfigurationICVFX_OverrideCameraRenderMode::Disabled:
					TargetFlags |= ViewportICVFX_DisableCamera | ViewportICVFX_DisableChromakey | ViewportICVFX_DisableChromakeyMarkers;
					break;

					// Disable chromakey render for this viewport
				case EDisplayClusterConfigurationICVFX_OverrideCameraRenderMode::DisableChromakey:
					TargetFlags |= ViewportICVFX_DisableChromakey | ViewportICVFX_DisableChromakeyMarkers;
					break;

					// Disable chromakey markers render for this viewport
				case EDisplayClusterConfigurationICVFX_OverrideCameraRenderMode::DisableChromakeyMarkers:
					TargetFlags |= ViewportICVFX_DisableChromakeyMarkers;
					break;
					// Use default rendering rules
				default:
					break;
				}

				switch (InConfigurationViewport.ICVFX.LightcardRenderMode)
				{
				case EDisplayClusterConfigurationICVFX_OverrideLightcardRenderMode::Disabled:
					TargetFlags |= ViewportICVFX_DisableLightcard;
					break;

					// Render incamera frame over lightcard for this viewport
				case EDisplayClusterConfigurationICVFX_OverrideLightcardRenderMode::Over:
					TargetFlags |= ViewportICVFX_OverrideLightcardMode;
					DstViewport.RenderSettingsICVFX.ICVFX.LightcardMode = EDisplayClusterShaderParametersICVFX_LightcardRenderMode::Over;
					break;

					// Over lightcard over incamera frame  for this viewport
				case EDisplayClusterConfigurationICVFX_OverrideLightcardRenderMode::Under:
					TargetFlags |= ViewportICVFX_OverrideLightcardMode;
					DstViewport.RenderSettingsICVFX.ICVFX.LightcardMode = EDisplayClusterShaderParametersICVFX_LightcardRenderMode::Under;
					break;
				default:
					break;
				}
			}
		}
	}

	static const FDisplayClusterConfigurationICVFX_ChromakeySettings& GetCameraChromakeySettings(const UDisplayClusterConfigurationICVFX_CameraSettings& InCameraSettings, const UDisplayClusterConfigurationICVFX_StageSettings& InStageSettings)
	{
		return (InCameraSettings.CustomChromakey.bEnable) ? InCameraSettings.CustomChromakey.Chromakey : InStageSettings.Chromakey;
	}

	static void UpdateCameraViewportSetting(FDisplayClusterViewport& DstViewport, const UDisplayClusterConfigurationICVFX_CameraSettings& InCameraSettings, const UDisplayClusterConfigurationICVFX_StageSettings& InStageSettings)
	{
		check(InCameraSettings.bEnable);

		// Reset runtime flags from prev frame:
		ResetViewportRuntimeParameters(DstViewport);

		// incamera textrure used as overlay
		DstViewport.RenderSettings.bVisible = false;

		// OCIO
		UpdateViewportOCIOConfiguration(DstViewport, InCameraSettings.OCIO_Configuration);


		// UDisplayClusterConfigurationICVFX_CameraSettings
		{
			DstViewport.RenderSettings.CameraId.Empty();
			DstViewport.RenderSettings.BufferRatio = InCameraSettings.BufferRatio;
		}

		// UDisplayClusterConfigurationICVFX_CameraRenderSettings
		const FDisplayClusterConfigurationICVFX_CameraRenderSettings& InCameraRenderSettings = InCameraSettings.RenderSettings;
		{
			FIntPoint DesiredSize(0);
			// Camera viewport frame size:
			switch (InCameraRenderSettings.FrameSize.Size)
			{
			case EDisplayClusterConfigurationICVFX_CameraFrameSizeSource::Custom:
				DesiredSize = InCameraRenderSettings.FrameSize.CustomSizeValue;
				break;

			case EDisplayClusterConfigurationICVFX_CameraFrameSizeSource::Default:
			default:
				DesiredSize = InStageSettings.DefaultFrameSize;
				break;
			}

			DesiredSize.X = FMath::Max(16, DesiredSize.X);
			DesiredSize.Y = FMath::Max(16, DesiredSize.Y);

			DstViewport.RenderSettings.Rect = DstViewport.GetValidRect(FIntRect(FIntPoint(0, 0), DesiredSize), TEXT("Configuration Camera Frame Size"));

			UpdateViewportSetting_CustomPostprocess(DstViewport, InCameraRenderSettings.CustomPostprocess);

			UpdateViewportSetting_Override(DstViewport, InCameraRenderSettings.Override);
			UpdateViewportSetting_PostprocessBlur(DstViewport, InCameraRenderSettings.PostprocessBlur);
			UpdateViewportSetting_GenerateMips(DstViewport, InCameraRenderSettings.GenerateMips);

			// UDisplayClusterConfigurationICVFX_CameraAdvancedRenderSettings
			const FDisplayClusterConfigurationICVFX_CameraAdvancedRenderSettings& InAdvancedRS = InCameraRenderSettings.AdvancedRenderSettings;
			{
				DstViewport.RenderSettings.RenderTargetRatio = InAdvancedRS.RenderTargetRatio;
				DstViewport.RenderSettings.GPUIndex          = InAdvancedRS.GPUIndex;
				DstViewport.RenderSettings.StereoGPUIndex    = InAdvancedRS.StereoGPUIndex;

				UpdateViewportStereoMode(DstViewport, InAdvancedRS.StereoMode);

				DstViewport.RenderSettings.RenderFamilyGroup = InAdvancedRS.RenderFamilyGroup;
			}
		}
	}

	static void UpdateChromakeyViewportSetting(FDisplayClusterViewport& DstViewport, const FDisplayClusterConfigurationICVFX_ChromakeySettings& InChromakeySettings, const UDisplayClusterConfigurationICVFX_StageSettings& InStageSettings)
	{
		check(InChromakeySettings.Source== EDisplayClusterConfigurationICVFX_ChromakeySource::ChromakeyRenderTexture);

		// Reset runtime flags from prev frame:
		ResetViewportRuntimeParameters(DstViewport);

		// Chromakey used as overlay
		DstViewport.RenderSettings.bVisible = false;

		// Use special capture mode (this change RTT format and render flags)
		DstViewport.RenderSettings.CaptureMode = EDisplayClusterViewportCaptureMode::Chromakey;

		// UDisplayClusterConfigurationICVFX_ChromakeyRenderSettings
		const FDisplayClusterConfigurationICVFX_ChromakeyRenderSettings& InRenderSettings = InChromakeySettings.ChromakeyRenderTexture;
		{
			UpdateViewportSetting_Override(DstViewport, InRenderSettings.Override);
			UpdateViewportSetting_PostprocessBlur(DstViewport, InRenderSettings.PostprocessBlur);
			UpdateViewportSetting_GenerateMips(DstViewport, InRenderSettings.GenerateMips);

			// Update visibility settings only for rendered viewports
			if (!DstViewport.PostRenderSettings.Override.IsEnabled())
			{
				check(IsVisibilitySettingsDefined(InRenderSettings.ShowOnlyList));
				UpdateVisibilitySetting(DstViewport, EDisplayClusterViewport_VisibilityMode::ShowOnly, InRenderSettings.ShowOnlyList);
			}
		}

		UpdateViewportSetting_OverlayRenderSettings(DstViewport, InRenderSettings.AdvancedRenderSettings);
	}

	static void UpdateChromakeyMarkerSettings(FDisplayClusterShaderParameters_ICVFX::FCameraSettings& DstCameraSettings, const FDisplayClusterConfigurationICVFX_ChromakeyMarkers& InChromakeyMarkers)
	{
		DstCameraSettings.ChromakeMarkerTextureRHI.SafeRelease();

		if (InChromakeyMarkers.bEnable && InChromakeyMarkers.MarkerTileRGBA != nullptr)
		{
			DstCameraSettings.ChromakeyMarkersScale    = InChromakeyMarkers.MarkerTileScale;
			DstCameraSettings.ChromakeyMarkersDistance = InChromakeyMarkers.MarkerTileDistance;

			// Assign texture RHI ref
			FTextureResource* MarkersResource = InChromakeyMarkers.MarkerTileRGBA->Resource;
			if (MarkersResource)
			{
				DstCameraSettings.ChromakeMarkerTextureRHI = MarkersResource->TextureRHI;
			}
		}
	}

	static bool IsShouldUseLightcard(const FDisplayClusterConfigurationICVFX_LightcardSettings& InLightcardSettings)
	{
		if (InLightcardSettings.bEnable == false)
		{
			// dont use lightcard if disabled
			return false;
		}

		if (InLightcardSettings.RenderSettings.Override.bAllowOverride)
		{
			// ! handle error
			// Override mode requre source texture
			return InLightcardSettings.RenderSettings.Override.SourceTexture != nullptr;
		}

		// Lightcard require layers for render
		return IsVisibilitySettingsDefined(InLightcardSettings.ShowOnlyList);
	}

	static void UpdateLightcardViewportSetting(FDisplayClusterViewport& DstViewport, FDisplayClusterViewport& BaseViewport, const FDisplayClusterConfigurationICVFX_LightcardSettings& InLightcardSettings, const UDisplayClusterConfigurationICVFX_StageSettings& InStageSettings, bool bIsOpenColorIO)
	{
		check(InLightcardSettings.bEnable);

		// Reset runtime flags from prev frame:
		ResetViewportRuntimeParameters(DstViewport);

		// LIghtcard texture used as overlay
		DstViewport.RenderSettings.bVisible = false;

		if (bIsOpenColorIO)
		{
			// OCIO
			UpdateViewportOCIOConfiguration(DstViewport, InLightcardSettings.OCIO_Configuration);
			DstViewport.RenderSettings.CaptureMode = EDisplayClusterViewportCaptureMode::Lightcard_OCIO;
		}
		else
		{
			DstViewport.RenderSettings.CaptureMode = EDisplayClusterViewportCaptureMode::Lightcard;
		}

		const FDisplayClusterConfigurationICVFX_LightcardRenderSettings& InRenderSettings = InLightcardSettings.RenderSettings;
		{
			UpdateViewportSetting_Override(DstViewport, InRenderSettings.Override);
			UpdateViewportSetting_PostprocessBlur(DstViewport, InRenderSettings.PostprocessBlur);
			UpdateViewportSetting_GenerateMips(DstViewport, InRenderSettings.GenerateMips);

			// Update visibility settings only for rendered viewports
			if (!DstViewport.PostRenderSettings.Override.IsEnabled())
			{
				check(IsVisibilitySettingsDefined(InLightcardSettings.ShowOnlyList));

				UpdateVisibilitySetting(DstViewport, EDisplayClusterViewport_VisibilityMode::ShowOnly, InLightcardSettings.ShowOnlyList);
			}

			UpdateViewportSetting_OverlayRenderSettings(DstViewport, InRenderSettings.AdvancedRenderSettings);
		}

		// Attach to parent viewport
		DstViewport.RenderSettings.AssignParentViewport(BaseViewport.GetId(), BaseViewport.RenderSettings);

		// Global lighcard rendering mode
		if ((BaseViewport.RenderSettingsICVFX.Flags & ViewportICVFX_OverrideLightcardMode) == 0)
		{
			// Use global lightcard blending mode
			switch (InLightcardSettings.Blendingmode)
			{
			case EDisplayClusterConfigurationICVFX_LightcardRenderMode::Over:
				BaseViewport.RenderSettingsICVFX.ICVFX.LightcardMode = EDisplayClusterShaderParametersICVFX_LightcardRenderMode::Over;
				break;
			case EDisplayClusterConfigurationICVFX_LightcardRenderMode::Under:
				BaseViewport.RenderSettingsICVFX.ICVFX.LightcardMode = EDisplayClusterShaderParametersICVFX_LightcardRenderMode::Under;
				break;
			};
		}
		// Debug: override the texture of the target viewport from this lightcard RTT
		if (InLightcardSettings.RenderSettings.bOverrideViewport)
		{
			BaseViewport.RenderSettings.OverrideViewportId = DstViewport.GetId();
		}
		else
		{
			if (bIsOpenColorIO)
			{
				BaseViewport.RenderSettingsICVFX.ICVFX.Lightcard_OCIO.ViewportId = DstViewport.GetId();
			}
			else
			{
				BaseViewport.RenderSettingsICVFX.ICVFX.Lightcard.ViewportId = DstViewport.GetId();
			}
		}
	}
};
