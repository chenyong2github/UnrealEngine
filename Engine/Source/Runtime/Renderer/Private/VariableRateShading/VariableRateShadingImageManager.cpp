// Copyright Epic Games, Inc. All Rights Reserved.

#include "VariableRateShadingImageManager.h"
#include "FixedFoveationImageGenerator.h"
#include "ContrastAdaptiveImageGenerator.h"
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

/**
 * Shaders
 */

constexpr int32 kCombineGroupSize = FComputeShaderUtils::kGolden2DGroupSize;

class FCombineShadingRateTexturesCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FCombineShadingRateTexturesCS);
	SHADER_USE_PARAMETER_STRUCT(FCombineShadingRateTexturesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, RWOutputTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, SourceTexture_1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, SourceTexture_2)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, SourceTexture_3)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, SourceTexture_4)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return FDataDrivenShaderPlatformInfo::GetSupportsVariableRateShading(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), kCombineGroupSize);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), kCombineGroupSize);
	}

};

IMPLEMENT_GLOBAL_SHADER(FCombineShadingRateTexturesCS, "/Engine/Private/VariableRateShading/VRSShadingRateCombine.usf", "CombineShadingRateTextures", SF_Compute);


/**
 * Public functions
 */

FVariableRateShadingImageManager::FVariableRateShadingImageManager()
	: FRenderResource()
{
	ImageGenerators.Add(MakeUnique<FFixedFoveationImageGenerator>());
	ImageGenerators.Add(MakeUnique<FContrastAdaptiveImageGenerator>());

	// TODO: Add more generators or allow registration from plugins
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

bool FVariableRateShadingImageManager::IsVRSSupportedByRHI()
{
	return GRHISupportsAttachmentVariableRateShading && GRHIVariableRateShadingEnabled && GRHIAttachmentVariableRateShadingEnabled && FDataDrivenShaderPlatformInfo::GetSupportsVariableRateShading(GMaxRHIShaderPlatform);
}

bool FVariableRateShadingImageManager::IsVRSCompatibleWithOutputType(const EDisplayOutputFormat& OutputFormat)
{
	return OutputFormat == EDisplayOutputFormat::SDR_sRGB
		|| OutputFormat == EDisplayOutputFormat::HDR_ACES_1000nit_ST2084
		|| OutputFormat == EDisplayOutputFormat::HDR_ACES_2000nit_ST2084;
}

bool FVariableRateShadingImageManager::IsVRSCompatibleWithView(const FViewInfo& ViewInfo)
{
	// The VRS texture generation is currently only compatible with SDR and HDR10
	
	// TODO: Investigate if it's worthwhile getting scene captures working. Things that we'll need to take care of
	// is to associate shading rate texture image with main scene, and scene capture.  But what if there is
	// more than 1 scene capture?  Is there a unique identifier that connects two frames of scene capture.
	return IsVRSSupportedByRHI() && !ViewInfo.bIsSceneCapture && IsVRSCompatibleWithOutputType(GetDisplayOutputFormat(ViewInfo));
}

FIntPoint FVariableRateShadingImageManager::GetSRITileSize()
{
	return FIntPoint(GRHIVariableRateShadingImageTileMinWidth, GRHIVariableRateShadingImageTileMinHeight);
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
		}
	}

	// If we have more than one internal source, combine the first available two
	// TODO: Support combining more textures
	if (InternalVRSSources.Num() > 1)
	{
		FIntPoint ViewSize = ViewInfo.UnscaledViewRect.Scale(ViewInfo.Family->SecondaryViewFraction).Size();
		const FIntPoint TileSize = GetSRITileSize();

		// Create texture to hold shading rate image
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
			ViewSize / TileSize,
			GRHIVariableRateShadingImageFormat,
			FClearValueBinding::None,
			TexCreate_Foveation | TexCreate_UAV);

		FRDGTextureRef CombinedShadingRateTexture = GraphBuilder.CreateTexture(Desc, TEXT("CombinedShadingRateTexture"));

		FCombineShadingRateTexturesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCombineShadingRateTexturesCS::FParameters>();
		PassParameters->SourceTexture_2 = InternalVRSSources[0];
		PassParameters->SourceTexture_1 = InternalVRSSources[1];
		PassParameters->RWOutputTexture = GraphBuilder.CreateUAV(CombinedShadingRateTexture);

		TShaderMapRef<FCombineShadingRateTexturesCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("CombineShadingRateImages"),
			ERDGPassFlags::AsyncCompute | ERDGPassFlags::NeverCull,
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(ViewSize, TileSize));

		return CombinedShadingRateTexture;
	}
	else if (InternalVRSSources.Num() == 1)
	{
		return InternalVRSSources[0];
	}

	// Fall back on external sources only if we have no internal ones
	// TODO: Combine external sources as well
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

// Temporary passthrough for CAS debug overlay, pending that functionality being moved to the manager
void FVariableRateShadingImageManager::CASDebugPreview(FRDGBuilder& GraphBuilder, const FSceneViewFamily& ViewFamily, FRDGTextureRef OutputSceneColor)
{
	// Get the first CAS generator present and call its debug function
	for (TUniquePtr<IVariableRateShadingImageGenerator>& Generator : ImageGenerators)
	{
		if (EnumHasAllFlags(Generator->GetType(), FVariableRateShadingImageManager::EVRSSourceType::ContrastAdaptiveShading))
		{
			((FContrastAdaptiveImageGenerator*) Generator.Get())->VRSDebugPreview(GraphBuilder, ViewFamily, OutputSceneColor);
			break;
		}
	}
}
