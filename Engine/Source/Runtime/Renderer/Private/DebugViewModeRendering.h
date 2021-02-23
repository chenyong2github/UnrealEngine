// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
DebugViewModeRendering.h: Contains definitions for rendering debug viewmodes.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameters.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "MeshMaterialShaderType.h"
#include "MeshMaterialShader.h"
#include "ShaderBaseClasses.h"
#include "MeshPassProcessor.h"

class FPrimitiveSceneProxy;
struct FMeshBatchElement;
struct FMeshDrawingRenderState;
class FDebugViewModeInterface;

static const int32 NumStreamingAccuracyColors = 5;
static const int32 NumLODColorationColors = 8;
static const float UndefinedStreamingAccuracyIntensity = .015f;

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FDebugViewModePassUniformParameters, )
	SHADER_PARAMETER_STRUCT(FSceneTextureUniformParameters, SceneTextures)
	SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, QuadOverdraw)
	SHADER_PARAMETER_ARRAY(FLinearColor, AccuracyColors, [NumStreamingAccuracyColors])
	SHADER_PARAMETER_ARRAY(FLinearColor, LODColors, [NumLODColorationColors])
END_GLOBAL_SHADER_PARAMETER_STRUCT()

#if WITH_DEBUG_VIEW_MODES

TRDGUniformBufferRef<FDebugViewModePassUniformParameters> CreateDebugViewModePassUniformBuffer(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef QuadOverdrawTexture);

/** Returns the RT index where the QuadOverdrawUAV will be bound. */
extern int32 GetQuadOverdrawUAVIndex(EShaderPlatform Platform, ERHIFeatureLevel::Type FeatureLevel);

class FDebugViewModeShaderElementData : public FMeshMaterialShaderElementData
{
public:

	FDebugViewModeShaderElementData(
		const FMaterialRenderProxy& InMaterialRenderProxy,
		const FMaterial& InMaterial,
		EDebugViewShaderMode InDebugViewMode, 
		const FVector& InViewOrigin, 
		int32 InVisualizeLODIndex, 
		int32 InViewModeParam, 
		const FName& InViewModeParamName) 
		: MaterialRenderProxy(InMaterialRenderProxy)
		, Material(InMaterial)
		, DebugViewMode(InDebugViewMode)
		, ViewOrigin(InViewOrigin)
		, VisualizeLODIndex(InVisualizeLODIndex)
		, ViewModeParam(InViewModeParam)
		, ViewModeParamName(InViewModeParamName)
		, NumVSInstructions(0)
		, NumPSInstructions(0)
	{}

	const FMaterialRenderProxy& MaterialRenderProxy;
	const FMaterial& Material;

	EDebugViewShaderMode DebugViewMode;
	FVector ViewOrigin;
	int32 VisualizeLODIndex;
	int32 ViewModeParam;
	FName ViewModeParamName;

	int32 NumVSInstructions;
	int32 NumPSInstructions;
};

/**
 * Vertex shader for quad overdraw. Required because overdraw shaders need to have SV_Position as first PS interpolant.
 */
class FDebugViewModeVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FDebugViewModeVS,MeshMaterial);
protected:

	FDebugViewModeVS() = default;
	FDebugViewModeVS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer) 
		: FMeshMaterialShader(Initializer)
	{}

public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters);

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FDebugViewModeShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);
	}

	static void SetCommonDefinitions(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		// SM4 has less input interpolants. Also instanced meshes use more interpolants.
		if (Parameters.MaterialParameters.bIsDefaultMaterial || (IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && !Parameters.MaterialParameters.bIsUsedWithInstancedStaticMeshes))
		{	// Force the default material to pass enough texcoords to the pixel shaders (even though not using them).
			// This is required to allow material shaders to have access to the sampled coords.
			OutEnvironment.SetDefine(TEXT("MIN_MATERIAL_TEXCOORDS"), (uint32)4);
		}
		else // Otherwise still pass at minimum amount to have debug shader using a texcoord to work (material might not use any).
		{
			OutEnvironment.SetDefine(TEXT("MIN_MATERIAL_TEXCOORDS"), (uint32)2);
		}

	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		SetCommonDefinitions(Parameters, OutEnvironment);
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

class FDebugViewModePS : public FMeshMaterialShader
{
public:
	FDebugViewModePS() = default;
	FDebugViewModePS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{}

	void GetElementShaderBindings(
		const FShaderMapPointerTable& PointerTable,
		const FScene* Scene,
		const FSceneView* ViewIfDynamicMeshCommand,
		const FVertexFactory* VertexFactory,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMeshBatch& MeshBatch,
		const FMeshBatchElement& BatchElement,
		const FDebugViewModeShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const;
};

class FDebugViewModeMeshProcessor : public FMeshPassProcessor
{
public:
	FDebugViewModeMeshProcessor(const FScene* InScene, ERHIFeatureLevel::Type InFeatureLevel, const FSceneView* InViewIfDynamicMeshCommand, bool bTranslucentBasePass, FMeshPassDrawListContext* InDrawListContext);
	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final;

private:

	void UpdateInstructionCount(FDebugViewModeShaderElementData& OutShaderElementData, const FMaterial* InBatchMaterial, FVertexFactoryType* InVertexFactoryType);

	EDebugViewShaderMode DebugViewMode;
	int32 ViewModeParam;
	FName ViewModeParamName;

	const FDebugViewModeInterface* DebugViewModeInterface;
};

void AddDebugViewModeShaderTypes(ERHIFeatureLevel::Type FeatureLevel,
	const FVertexFactoryType* VertexFactoryType,
	FMaterialShaderTypes& OutShaderTypes);

#endif // WITH_DEBUG_VIEW_MODES

void RenderDebugViewMode(
	FRDGBuilder& GraphBuilder,
	TArrayView<FViewInfo> Views,
	FRDGTextureRef QuadOverdrawTexture,
	const FRenderTargetBindingSlots& RenderTargets);