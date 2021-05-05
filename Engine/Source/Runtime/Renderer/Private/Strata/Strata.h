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

BEGIN_SHADER_PARAMETER_STRUCT(FStrataBasePassUniformParameters, )
	SHADER_PARAMETER(uint32, MaxBytesPerPixel)
	SHADER_PARAMETER(FVector2D, GGXEnergyLUTScaleBias)
	SHADER_PARAMETER_TEXTURE(Texture3D<float2>, GGXEnergyLUT3DTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D<float4>, GGXEnergyLUT2DTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, GGXEnergyLUTSampler)
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWByteAddressBuffer, MaterialLobesBufferUAV)
END_SHADER_PARAMETER_STRUCT()

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FStrataGlobalUniformParameters, )
	SHADER_PARAMETER(uint32, MaxBytesPerPixel)
	SHADER_PARAMETER(FVector2D, GGXEnergyLUTScaleBias)
	SHADER_PARAMETER_RDG_BUFFER_SRV(ByteAddressBuffer, MaterialLobesBuffer)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, ClassificationTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, TopLayerNormalTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint2>, SSSTexture)
	SHADER_PARAMETER_TEXTURE(Texture3D<float2>, GGXEnergyLUT3DTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D<float4>, GGXEnergyLUT2DTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, GGXEnergyLUTSampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

enum EStrataTileMaterialType : uint32
{
	ESimple = 0,
	EComplex = 1,
	ECount
};

struct FStrataSceneData
{
	uint32 MaxBytesPerPixel;

	// Resources allocated and updated each frame

	FRDGBufferRef MaterialLobesBuffer;
	FRDGBufferUAVRef MaterialLobesBufferUAV;
	FRDGBufferSRVRef MaterialLobesBufferSRV;
	FRDGBufferRef ClassificationTileListBuffer[EStrataTileMaterialType::ECount];
	FRDGBufferUAVRef ClassificationTileListBufferUAV[EStrataTileMaterialType::ECount];
	FRDGBufferSRVRef ClassificationTileListBufferSRV[EStrataTileMaterialType::ECount];
	FRDGBufferRef ClassificationTileIndirectBuffer[EStrataTileMaterialType::ECount];
	FRDGBufferUAVRef ClassificationTileIndirectBufferUAV[EStrataTileMaterialType::ECount];
	FRDGBufferSRVRef ClassificationTileIndirectBufferSRV[EStrataTileMaterialType::ECount];

	FRDGTextureRef ClassificationTexture;
	FRDGTextureRef TopLayerNormalTexture;
	FRDGTextureRef SSSTexture;

	TRDGUniformBufferRef<FStrataGlobalUniformParameters> StrataGlobalUniformParameters{};

	// Resources computed once for multiple frames

	TRefCountPtr<IPooledRenderTarget> GGXEnergyLUT3DTexture;
	TRefCountPtr<IPooledRenderTarget> GGXEnergyLUT2DTexture;


	FStrataSceneData()
	{
		Reset();
	}

	void Reset();
};


namespace Strata
{
constexpr uint32 StencilBit = 0x80; // In sync with SceneRenderTargets.h - GET_STENCIL_BIT_MASK(STENCIL_STRATA_FASTPATH)

bool IsStrataEnabled();
bool IsClassificationEnabled();
bool ShouldPassesReadingStrataBeTiled(ERHIFeatureLevel::Type FeatureLevel);

void InitialiseStrataFrameSceneData(FSceneRenderer& SceneRenderer, FRDGBuilder& GraphBuilder);

void BindStrataBasePassUniformParameters(FRDGBuilder& GraphBuilder, FStrataSceneData* StrataSceneData, FStrataBasePassUniformParameters& OutStrataUniformParameters);

TRDGUniformBufferRef<FStrataGlobalUniformParameters> BindStrataGlobalUniformParameters(FStrataSceneData* StrataSceneData);

void AddStrataMaterialClassificationPass(FRDGBuilder& GraphBuilder, const FMinimalSceneTextures& SceneTextures, const TArray<FViewInfo>& Views);

void AddStrataStencilPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FMinimalSceneTextures& SceneTextures);
void AddStrataStencilPass(FRDGBuilder& GraphBuilder, const TArray<FViewInfo>& Views, const FMinimalSceneTextures& SceneTextures);

void AddStrataDebugPasses(FRDGBuilder& GraphBuilder, const TArray<FViewInfo>& Views, FRDGTextureRef SceneColorTexture, EShaderPlatform Platform);


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
		SHADER_PARAMETER(FVector4, OutputViewSizeAndInvSize)
		SHADER_PARAMETER(FVector4, OutputBufferSizeAndInvSize)
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
		OutEnvironment.SetDefine(TEXT("SHADER_STENCIL_CATEGORIZATION"), 1);
	}
};

void FillUpTiledPassData(EStrataTileMaterialType Type, const FViewInfo& View, FStrataTilePassVS::FParameters& ParametersVS, EPrimitiveType& PrimitiveType);

};




