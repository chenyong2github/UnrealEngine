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

BEGIN_SHADER_PARAMETER_STRUCT(FStrataOpaquePassUniformParameters, )
	SHADER_PARAMETER(uint32, MaxBytesPerPixel)
	SHADER_PARAMETER_UAV(RWByteAddressBuffer, MaterialLobesBufferUAV)
END_SHADER_PARAMETER_STRUCT()

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FStrataGlobalUniformParameters, )
	SHADER_PARAMETER(uint32, MaxBytesPerPixel)
	SHADER_PARAMETER_SRV(ByteAddressBuffer, MaterialLobesBuffer)
	SHADER_PARAMETER_TEXTURE(Texture2D<uint>, ClassificationTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D<uint2>, TopLayerTexture)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

struct FStrataSceneData
{
	uint32 MaxBytesPerPixel;
	FRWByteAddressBuffer MaterialLobesBuffer;					// This should be a RDG resource
	TRefCountPtr<IPooledRenderTarget> ClassificationTexture;	// This should be a RDG resource
	TRefCountPtr<IPooledRenderTarget> TopLayerTexture;			// This should be a RDG resource

	TRefCountPtr<FRDGPooledBuffer> ClassificationTileListBuffer;
	TRefCountPtr<FRDGPooledBuffer> ClassificationTileIndirectBuffer;

	TUniformBufferRef<FStrataGlobalUniformParameters> StrataGlobalUniformParameters;

	FStrataSceneData()
	{
	}
};

namespace Strata
{
constexpr uint32 StencilBit = 0x80; // In sync with SceneRenderTargets.h - GET_STENCIL_BIT_MASK(STENCIL_STRATA_FASTPATH)

bool IsStrataEnabled();
bool IsClassificationEnabled();

void InitialiseStrataFrameSceneData(FSceneRenderer& SceneRenderer, FRDGBuilder& GraphBuilder);

void BindStrataBasePassUniformParameters(const FViewInfo& View, FStrataOpaquePassUniformParameters& OutStrataUniformParameters);

TUniformBufferRef<FStrataGlobalUniformParameters> BindStrataGlobalUniformParameters(const FViewInfo& View);

void AddVisualizeMaterialPasses(FRDGBuilder& GraphBuilder, const TArray<FViewInfo>& Views, FRDGTextureRef SceneColorTexture);

void AddStrataMaterialClassificationPass(FRDGBuilder& GraphBuilder, const FMinimalSceneTextures& SceneTextures, const TArray<FViewInfo>& Views);

void AddStrataStencilPass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FMinimalSceneTextures& SceneTextures);
void AddStrataStencilPass(FRDGBuilder& GraphBuilder, const TArray<FViewInfo>& Views, const FMinimalSceneTextures& SceneTextures);
};


