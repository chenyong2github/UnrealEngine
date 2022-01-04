// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameterMacros.h"
#include "RenderGraphResources.h"
#include "MeshPassProcessor.h"
#include "UnifiedBuffer.h"
#include "RHIUtilities.h"

// Forward declarations.
class FScene;
class FRDGBuilder;
struct FMinimalSceneTextures;
struct FScreenPassTexture;

BEGIN_SHADER_PARAMETER_STRUCT(FStrataBasePassUniformParameters, )
	SHADER_PARAMETER(uint32, MaxBytesPerPixel)
	SHADER_PARAMETER(uint32, bRoughDiffuse)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<uint>, MaterialTextureArrayUAVWithoutRTs)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint2>, SSSTextureUAV)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FStrataForwardPassUniformParameters, )
	SHADER_PARAMETER(uint32, bRoughDiffuse)
END_SHADER_PARAMETER_STRUCT()

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FStrataGlobalUniformParameters, )
	SHADER_PARAMETER(uint32, MaxBytesPerPixel)
	SHADER_PARAMETER(uint32, bRoughDiffuse)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2DArray<uint>, MaterialTextureArray)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, TopLayerTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint2>, SSSTexture)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

enum EStrataTileMaterialType : uint32
{
	ESimple  = 0,
	ESingle  = 1,
	EComplex = 2,
	ECount
};

const TCHAR* ToString(EStrataTileMaterialType Type);

struct FStrataSceneData
{
	uint32 MaxBytesPerPixel;
	bool bRoughDiffuse;

	// Resources allocated and updated each frame

	FRDGTextureRef MaterialTextureArray;
	FRDGTextureUAVRef MaterialTextureArrayUAVWithoutRTs;
	FRDGTextureUAVRef MaterialTextureArrayUAV;
	FRDGTextureSRVRef MaterialTextureArraySRV;

	FRDGBufferRef ClassificationTileListBuffer[EStrataTileMaterialType::ECount];
	FRDGBufferUAVRef ClassificationTileListBufferUAV[EStrataTileMaterialType::ECount];
	FRDGBufferSRVRef ClassificationTileListBufferSRV[EStrataTileMaterialType::ECount];
	FRDGBufferRef ClassificationTileIndirectBuffer[EStrataTileMaterialType::ECount];
	FRDGBufferUAVRef ClassificationTileIndirectBufferUAV[EStrataTileMaterialType::ECount];
	FRDGBufferSRVRef ClassificationTileIndirectBufferSRV[EStrataTileMaterialType::ECount];

	FRDGTextureRef TopLayerTexture;
	FRDGTextureRef SSSTexture;

	FRDGTextureUAVRef TopLayerTextureUAV;
	FRDGTextureUAVRef SSSTextureUAV;

	TRDGUniformBufferRef<FStrataGlobalUniformParameters> StrataGlobalUniformParameters{};

	FStrataSceneData()
	{
		Reset();
	}

	void Reset();
};


namespace Strata
{
constexpr uint32 StencilBit_Complex= 0x00; // No stencil bit
constexpr uint32 StencilBit_Single = 0x02; // In sync with SceneRenderTargets.h - GET_STENCIL_BIT_MASK(STENCIL_STRATA_SINGLEPATH)
constexpr uint32 StencilBit_Fast   = 0x80; // In sync with SceneRenderTargets.h - GET_STENCIL_BIT_MASK(STENCIL_STRATA_FASTPATH)

bool IsStrataEnabled();

void InitialiseStrataFrameSceneData(FSceneRenderer& SceneRenderer, FRDGBuilder& GraphBuilder);

void BindStrataBasePassUniformParameters(FRDGBuilder& GraphBuilder, FStrataSceneData* StrataSceneData, FStrataBasePassUniformParameters& OutStrataUniformParameters);
void BindStrataGlobalUniformParameters(FRDGBuilder& GraphBuilder, FStrataSceneData* StrataSceneData, FStrataGlobalUniformParameters& OutStrataUniformParameters);
void BindStrataForwardPasslUniformParameters(FRDGBuilder& GraphBuilder, FStrataSceneData* StrataSceneData, FStrataForwardPassUniformParameters& OutStrataUniformParameters);

void AppendStrataMRTs(FSceneRenderer& SceneRenderer, uint32& BasePassTextureCount, TStaticArray<FTextureRenderTargetBinding, MaxSimultaneousRenderTargets>& BasePassTextures);
void SetBasePassRenderTargetOutputFormat(const EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment);

TRDGUniformBufferRef<FStrataGlobalUniformParameters> BindStrataGlobalUniformParameters(FStrataSceneData* StrataSceneData);

void AddStrataMaterialClassificationPass(FRDGBuilder& GraphBuilder, const FMinimalSceneTextures& SceneTextures, const TArray<FViewInfo>& Views);

void AddStrataStencilPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FMinimalSceneTextures& SceneTextures);
void AddStrataStencilPass(FRDGBuilder& GraphBuilder, const TArray<FViewInfo>& Views, const FMinimalSceneTextures& SceneTextures);

bool ShouldRenderStrataDebugPasses(const FViewInfo& View);
FScreenPassTexture AddStrataDebugPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, FScreenPassTexture& ScreenPassSceneColor);


class FStrataTilePassVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FStrataTilePassVS);
	SHADER_USE_PARAMETER_STRUCT(FStrataTilePassVS, FGlobalShader);

	class FEnableDebug : SHADER_PERMUTATION_BOOL("PERMUTATION_ENABLE_DEBUG");
	class FEnableTexCoordScreenVector : SHADER_PERMUTATION_BOOL("PERMUTATION_ENABLE_TEXCOORD_SCREENVECTOR");
	using FPermutationDomain = TShaderPermutationDomain<FEnableDebug, FEnableTexCoordScreenVector>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// It would be possible to use the ViewUniformBuffer instead of copying the data here, 
		// but we would have to make sure the view UB is added to all passes using this parameter structure.
		// We should not add it here to now have duplicated input UB.
		SHADER_PARAMETER(FVector4f, OutputViewSizeAndInvSize)
		SHADER_PARAMETER(FVector4f, OutputBufferSizeAndInvSize)
		SHADER_PARAMETER(FMatrix44f, ViewScreenToTranslatedWorld)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, TileListBuffer)
		RDG_BUFFER_ACCESS(TileIndirectBuffer, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5; // We do not skip the compilation because we have some conditional when tiling a pass and the shader must be fetch once before hand.
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_TILE_VS"), 1);
	}
};

void FillUpTiledPassData(EStrataTileMaterialType Type, const FViewInfo& View, FStrataTilePassVS::FParameters& ParametersVS, EPrimitiveType& PrimitiveType);

};
