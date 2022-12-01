// Copyright Epic Games, Inc. All Rights Reserved.

#include "VariableRateShadingImageManager.h"
#include "FixedFoveationImageGenerator.h"
#include "StereoRenderTargetManager.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "RenderGraphUtils.h"
#include "RHIDefinitions.h"
#include "RenderTargetPool.h"
#include "SystemTextures.h"
#include "SceneView.h"
#include "IEyeTracker.h"
#include "IHeadMountedDisplay.h"
#include "IXRTrackingSystem.h"
#include "Engine/Engine.h"
#include "UnrealClient.h"
#include "PostProcess/PostProcessTonemap.h"


TGlobalResource<FVariableRateShadingImageManager> GVRSImageManager;

FVariableRateShadingImageManager::FVariableRateShadingImageManager()
	: FRenderResource()
{
	ImageGenerators.Add(MakeUnique<FFixedFoveationImageGenerator>());

	// TODO: Add more generators
}

FVariableRateShadingImageManager::~FVariableRateShadingImageManager() {}

void FVariableRateShadingImageManager::ReleaseDynamicRHI()
{
	GRenderTargetPool.FreeUnusedResources();
}

static EDisplayOutputFormat GetDisplayOutputFormat(const FViewInfo& View)
{
	FTonemapperOutputDeviceParameters Parameters = GetTonemapperOutputDeviceParameters(*View.Family);
	return (EDisplayOutputFormat)Parameters.OutputDevice;
}

static bool IsHDR10(const EDisplayOutputFormat& OutputFormat)
{
	return OutputFormat == EDisplayOutputFormat::HDR_ACES_1000nit_ST2084 ||
		OutputFormat == EDisplayOutputFormat::HDR_ACES_2000nit_ST2084;
}

bool FVariableRateShadingImageManager::IsVRSSupportedByRHI()
{
	return GRHISupportsAttachmentVariableRateShading && GRHIVariableRateShadingEnabled && GRHIAttachmentVariableRateShadingEnabled && FDataDrivenShaderPlatformInfo::GetSupportsVariableRateShading(GMaxRHIShaderPlatform);
}

bool FVariableRateShadingImageManager::IsVRSCompatibleWithOutputType(const EDisplayOutputFormat& OutputFormat)
{
	return OutputFormat == EDisplayOutputFormat::SDR_sRGB || IsHDR10(OutputFormat);
}

bool FVariableRateShadingImageManager::IsVRSCompatibleWithView(const FViewInfo& ViewInfo)
{
	// The VRS texture generation is currently only compatible with SDR and HDR10
	
	// TODO: Investigate if it's worthwhile getting scene captures working. Things that we'll need to take care of
	// is to associate shading rate texture image with main scene, and scene capture.  But what if there is
	// more than 1 scene capture?  Is there a unique identifier that connects two frames of scene capture.
	return IsVRSSupportedByRHI() && !ViewInfo.bIsSceneCapture && IsVRSCompatibleWithOutputType(GetDisplayOutputFormat(ViewInfo));
}

FRDGTextureRef FVariableRateShadingImageManager::GetVariableRateShadingImage(FRDGBuilder& GraphBuilder, const FViewInfo& ViewInfo, FVariableRateShadingImageManager::EVRSPassType PassType,
	const TArray<TRefCountPtr<IPooledRenderTarget>>* ExternalVRSSources, FVariableRateShadingImageManager::EVRSSourceType VRSTypesToExclude)
{
	// If the view doesn't support VRS, bail immediately
	if (!IsVRSCompatibleWithView(ViewInfo))
	{
		return nullptr;
	}

	// Otherwise collate all internal sources
	TArray<FRDGTextureRef> InternalVRSSources;

	for (TUniquePtr<IVariableRateShadingImageGenerator>& Generator : ImageGenerators)
	{
		FRDGTextureRef Image = nullptr;
		if (Generator->IsEnabledForView(ViewInfo) && !EnumHasAnyFlags(VRSTypesToExclude, Generator->GetType()))
		{
			Image = Generator->GetImage(GraphBuilder, ViewInfo, PassType);
		}

		if (Image)
		{
			InternalVRSSources.Add(Image);
			break; // No need to generate more than one until we support combining
		}
	}

	// TODO: Combine internal and external images with adjustable logic (min, max, priority, etc.)

	// For now, only one source can be used
	// Once CAS is added, source will be chosen by CVar, for now it defaults to the only available generator (fixed foveation)
	// We fall back on the first available external source if no internal source is available
	if (InternalVRSSources.Num() > 0)
	{
		return InternalVRSSources[0];
	}
	else if (ExternalVRSSources && ExternalVRSSources->Num() > 0)
	{
		return GraphBuilder.RegisterExternalTexture((*ExternalVRSSources)[0]);
	}
	else
	{
		return nullptr;
	}
}

void FVariableRateShadingImageManager::PrepareImageBasedVRS(FRDGBuilder& GraphBuilder, const FSceneViewFamily& ViewFamily, const FMinimalSceneTextures& SceneTextures)
{
	// If no views support VRS, bail immediately
	bool bIsAnyViewVRSCompatible = false;
	for (const FSceneView* View : ViewFamily.Views)
	{
		check(View->bIsViewInfo);
		auto ViewInfo = static_cast<const FViewInfo*>(View);
		if (IsVRSCompatibleWithView(*ViewInfo))
		{
			bIsAnyViewVRSCompatible = true;
			break;
		}
	}

	if (!bIsAnyViewVRSCompatible)
	{
		return;
	}

	// Also bail if we're given a ViewFamily with no valid RenderTarget
	if (ViewFamily.RenderTarget == nullptr)
	{
		ensureMsgf(0, TEXT("VRS Image Manager does not support ViewFamilies with no valid RenderTarget"));
		return;
	}

	// Invoke image generators
	for (TUniquePtr<IVariableRateShadingImageGenerator>& Generator : ImageGenerators)
	{
		if (Generator->IsEnabledForView(*ViewFamily.Views[0]))
		{
			Generator->PrepareImages(GraphBuilder, ViewFamily, SceneTextures);
		}
	}
}

bool FVariableRateShadingImageManager::IsTypeEnabledForView(const FSceneView& View, FVariableRateShadingImageManager::EVRSSourceType Type) const
{
	for (const TUniquePtr<IVariableRateShadingImageGenerator>& Generator : ImageGenerators)
	{
		if (EnumHasAnyFlags(Type, Generator->GetType()) && Generator->IsEnabledForView(View))
		{
			return true;
		}
	}
	return false;
}

TRefCountPtr<IPooledRenderTarget> FVariableRateShadingImageManager::GetMobileVariableRateShadingImage(const FSceneViewFamily& ViewFamily)
{
	if (!(IStereoRendering::IsStereoEyeView(*ViewFamily.Views[0]) && GEngine->XRSystem.IsValid()))
	{
		return TRefCountPtr<IPooledRenderTarget>();
	}

	FIntPoint Size(ViewFamily.RenderTarget->GetSizeXY());

	const bool bStereo = GEngine->StereoRenderingDevice.IsValid() && GEngine->StereoRenderingDevice->IsStereoEnabled();
	IStereoRenderTargetManager* const StereoRenderTargetManager = bStereo ? GEngine->StereoRenderingDevice->GetRenderTargetManager() : nullptr;

	FTexture2DRHIRef Texture;
	FIntPoint TextureSize(0, 0);

	// Allocate variable resolution texture for VR foveation if supported
	if (StereoRenderTargetManager && StereoRenderTargetManager->NeedReAllocateShadingRateTexture(MobileHMDFixedFoveationOverrideImage))
	{
		bool bAllocatedShadingRateTexture = StereoRenderTargetManager->AllocateShadingRateTexture(0, Size.X, Size.Y, GRHIVariableRateShadingImageFormat, 0, TexCreate_None, TexCreate_None, Texture, TextureSize);
		if (bAllocatedShadingRateTexture)
		{
			MobileHMDFixedFoveationOverrideImage = CreateRenderTarget(Texture, TEXT("ShadingRate"));
		}
	}

	return MobileHMDFixedFoveationOverrideImage;
}
