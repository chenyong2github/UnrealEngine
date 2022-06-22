// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapeTextureHeightPatchPS.h"

#include "LandscapeUtils.h"
#include "PixelShaderUtils.h"

namespace UE::Landscape
{

bool FApplyLandscapeTextureHeightPatchPS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return DoesPlatformSupportEditLayers(Parameters.Platform);
}

void FApplyLandscapeTextureHeightPatchPS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	OutEnvironment.SetDefine(TEXT("APPLY_LANDSCAPE_PATCH"), 1);

	// Make our flag choices match in the shader.
	OutEnvironment.SetDefine(TEXT("RECTANGULAR_FALLOFF_FLAG"), static_cast<uint8>(FApplyLandscapeTextureHeightPatchPS::EFlags::RectangularFalloff));
	OutEnvironment.SetDefine(TEXT("APPLY_PATCH_ALPHA_FLAG"), static_cast<uint8>(FApplyLandscapeTextureHeightPatchPS::EFlags::ApplyPatchAlpha));
	OutEnvironment.SetDefine(TEXT("INPUT_IS_PACKED_HEIGHT_FLAG"), static_cast<uint8>(FApplyLandscapeTextureHeightPatchPS::EFlags::InputIsPackedHeight));

	OutEnvironment.SetDefine(TEXT("ADDITIVE_MODE"), static_cast<uint8>(FApplyLandscapeTextureHeightPatchPS::EBlendMode::Additive));
	OutEnvironment.SetDefine(TEXT("ALPHA_BLEND_MODE"), static_cast<uint8>(FApplyLandscapeTextureHeightPatchPS::EBlendMode::AlphaBlend));
	OutEnvironment.SetDefine(TEXT("MIN_MODE"), static_cast<uint8>(FApplyLandscapeTextureHeightPatchPS::EBlendMode::Min));
	OutEnvironment.SetDefine(TEXT("MAX_MODE"), static_cast<uint8>(FApplyLandscapeTextureHeightPatchPS::EBlendMode::Max));
}

void FApplyLandscapeTextureHeightPatchPS::AddToRenderGraph(FRDGBuilder& GraphBuilder, FParameters* InParameters, const FIntRect& DestinationBounds)
{
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FApplyLandscapeTextureHeightPatchPS> PixelShader(ShaderMap);

	FPixelShaderUtils::AddFullscreenPass(
		GraphBuilder,
		ShaderMap,
		RDG_EVENT_NAME("LandscapeTextureHeightPatch"),
		PixelShader,
		InParameters,
		DestinationBounds);
}

bool FOffsetHeightmapPS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return DoesPlatformSupportEditLayers(Parameters.Platform);
}

void FOffsetHeightmapPS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	OutEnvironment.SetDefine(TEXT("OFFSET_LANDSCAPE_PATCH"), 1);
}

void FOffsetHeightmapPS::AddToRenderGraph(FRDGBuilder& GraphBuilder, FParameters* InParameters)
{
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FOffsetHeightmapPS> PixelShader(ShaderMap);

	FIntVector TextureSize = InParameters->InHeightmap->Desc.Texture->Desc.GetSize();

	FPixelShaderUtils::AddFullscreenPass(
		GraphBuilder,
		ShaderMap,
		RDG_EVENT_NAME("OffsetHeightmap"),
		PixelShader,
		InParameters,
		FIntRect(0, 0, TextureSize.X, TextureSize.Y));
}

bool FSimpleTextureCopyPS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return DoesPlatformSupportEditLayers(Parameters.Platform);
}

void FSimpleTextureCopyPS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	OutEnvironment.SetDefine(TEXT("SIMPLE_TEXTURE_COPY"), 1);
}

void FSimpleTextureCopyPS::AddToRenderGraph(FRDGBuilder& GraphBuilder, FRDGTextureRef SourceTexture, FRDGTextureRef DestinationTexture)
{
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FSimpleTextureCopyPS> PixelShader(ShaderMap);

	FRDGTextureSRVRef SourceSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(SourceTexture, 0));

	FSimpleTextureCopyPS::FParameters* ShaderParams = GraphBuilder.AllocParameters<FSimpleTextureCopyPS::FParameters>();
	ShaderParams->InSource = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(SourceTexture, 0));
	ShaderParams->InSourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp>::GetRHI();
	ShaderParams->RenderTargets[0] = FRenderTargetBinding(DestinationTexture, ERenderTargetLoadAction::ENoAction, /*InMipIndex = */0);
	FIntVector DestinationSize = DestinationTexture->Desc.GetSize();
	ShaderParams->InDestinationResolution = FVector2f(DestinationSize.X, DestinationSize.Y);

	FPixelShaderUtils::AddFullscreenPass(
		GraphBuilder,
		ShaderMap,
		RDG_EVENT_NAME("SimpleTextureCopy"),
		PixelShader,
		ShaderParams,
		FIntRect(0, 0, DestinationSize.X, DestinationSize.Y));
}

bool FConvertToNativeLandscapePatchPS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return DoesPlatformSupportEditLayers(Parameters.Platform);
}

void FConvertToNativeLandscapePatchPS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	OutEnvironment.SetDefine(TEXT("CONVERT_TO_NATIVE_LANDSCAPE_PATCH"), 1);
}

void FConvertToNativeLandscapePatchPS::AddToRenderGraph(FRDGBuilder& GraphBuilder, FRDGTextureRef SourceTexture, 
	FRDGTextureRef DestinationTexture, const FConvertToNativeLandscapePatchParams& Params)
{
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FConvertToNativeLandscapePatchPS> PixelShader(ShaderMap);

	FRDGTextureSRVRef SourceSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(SourceTexture, 0));

	FConvertToNativeLandscapePatchPS::FParameters* ShaderParams = GraphBuilder.AllocParameters<FConvertToNativeLandscapePatchPS::FParameters>();
	ShaderParams->InHeightmap = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(SourceTexture, 0));
	ShaderParams->InZeroInEncoding = Params.ZeroInEncoding;
	ShaderParams->InHeightScale = Params.HeightScale;
	ShaderParams->InHeightOffset = Params.HeightOffset;
	ShaderParams->RenderTargets[0] = FRenderTargetBinding(DestinationTexture, ERenderTargetLoadAction::ENoAction, /*InMipIndex = */0);
	FIntVector DestinationSize = DestinationTexture->Desc.GetSize();

	FPixelShaderUtils::AddFullscreenPass(
		GraphBuilder,
		ShaderMap,
		RDG_EVENT_NAME("ConvertToNativeLandscapePatch"),
		PixelShader,
		ShaderParams,
		FIntRect(0, 0, DestinationSize.X, DestinationSize.Y));
}

bool FConvertBackFromNativeLandscapePatchPS::ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
{
	return DoesPlatformSupportEditLayers(Parameters.Platform);
}

void FConvertBackFromNativeLandscapePatchPS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	OutEnvironment.SetDefine(TEXT("CONVERT_BACK_FROM_NATIVE_LANDSCAPE_PATCH"), 1);
}

void FConvertBackFromNativeLandscapePatchPS::AddToRenderGraph(FRDGBuilder& GraphBuilder, FRDGTextureRef SourceTexture,
	FRDGTextureRef DestinationTexture, const FConvertToNativeLandscapePatchParams& Params)
{
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	TShaderMapRef<FConvertBackFromNativeLandscapePatchPS> PixelShader(ShaderMap);

	FRDGTextureSRVRef SourceSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(SourceTexture, 0));

	FConvertBackFromNativeLandscapePatchPS::FParameters* ShaderParams = GraphBuilder.AllocParameters<FConvertBackFromNativeLandscapePatchPS::FParameters>();
	ShaderParams->InHeightmap = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(SourceTexture, 0));
	ShaderParams->InZeroInEncoding = Params.ZeroInEncoding;
	ShaderParams->InHeightScale = Params.HeightScale;
	ShaderParams->InHeightOffset = Params.HeightOffset;
	ShaderParams->RenderTargets[0] = FRenderTargetBinding(DestinationTexture, ERenderTargetLoadAction::ENoAction, /*InMipIndex = */0);
	FIntVector DestinationSize = DestinationTexture->Desc.GetSize();

	FPixelShaderUtils::AddFullscreenPass(
		GraphBuilder,
		ShaderMap,
		RDG_EVENT_NAME("ConvertBackFromNativeLandscapePatch"),
		PixelShader,
		ShaderParams,
		FIntRect(0, 0, DestinationSize.X, DestinationSize.Y));
}

}//end UE::Landscape

IMPLEMENT_GLOBAL_SHADER(UE::Landscape::FApplyLandscapeTextureHeightPatchPS, "/Plugin/LandscapePatch/Private/LandscapeTextureHeightPatchPS.usf", "ApplyLandscapeTextureHeightPatch", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(UE::Landscape::FOffsetHeightmapPS, "/Plugin/LandscapePatch/Private/LandscapeTextureHeightPatchPS.usf", "ApplyOffsetToHeightmap", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(UE::Landscape::FSimpleTextureCopyPS, "/Plugin/LandscapePatch/Private/LandscapeTextureHeightPatchPS.usf", "SimpleTextureCopy", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(UE::Landscape::FConvertToNativeLandscapePatchPS, "/Plugin/LandscapePatch/Private/LandscapeTextureHeightPatchPS.usf", "ConvertToNativeLandscapePatch", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(UE::Landscape::FConvertBackFromNativeLandscapePatchPS, "/Plugin/LandscapePatch/Private/LandscapeTextureHeightPatchPS.usf", "ConvertBackFromNativeLandscapePatch", SF_Pixel);
