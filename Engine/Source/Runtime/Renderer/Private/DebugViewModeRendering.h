// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
static const float UndefinedStreamingAccuracyIntensity = .015f;

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FDebugViewModePassPassUniformParameters, )
	SHADER_PARAMETER_STRUCT(FSceneTexturesUniformParameters, SceneTextures)
	SHADER_PARAMETER_ARRAY(FLinearColor, AccuracyColors, [NumStreamingAccuracyColors])
END_GLOBAL_SHADER_PARAMETER_STRUCT()

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

void SetupDebugViewModePassUniformBuffer(FSceneRenderTargets& SceneContext, ERHIFeatureLevel::Type FeatureLevel, FDebugViewModePassPassUniformParameters& PassParameters);

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

	FDebugViewModeVS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer) : FMeshMaterialShader(Initializer)
	{
		PassUniformBuffer.Bind(Initializer.ParameterMap, FSceneTexturesUniformParameters::StaticStructMetadata.GetShaderVariableName());
	}

	FDebugViewModeVS() {}

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
		if (Parameters.Material->IsDefaultMaterial() || (IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && !Parameters.Material->IsUsedWithInstancedStaticMeshes()))
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

/**
 * Hull shader for quad overdraw. Required because overdraw shaders need to have SV_Position as first PS interpolant.
 */
class FDebugViewModeHS : public FBaseHS
{
	DECLARE_SHADER_TYPE(FDebugViewModeHS,MeshMaterial);
public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return FBaseHS::ShouldCompilePermutation(Parameters) && FDebugViewModeVS::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FDebugViewModeVS::SetCommonDefinitions(Parameters, OutEnvironment);
		FBaseHS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}


	FDebugViewModeHS(const ShaderMetaType::CompiledShaderInitializerType& Initializer): FBaseHS(Initializer) {}
	FDebugViewModeHS() {}
};

/**
 * Domain shader for quad overdraw. Required because overdraw shaders need to have SV_Position as first PS interpolant.
 */
class FDebugViewModeDS : public FBaseDS
{
	DECLARE_SHADER_TYPE(FDebugViewModeDS,MeshMaterial);
public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return FBaseDS::ShouldCompilePermutation(Parameters) && FDebugViewModeVS::ShouldCompilePermutation(Parameters);		
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FDebugViewModeVS::SetCommonDefinitions(Parameters, OutEnvironment);
		FBaseDS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	FDebugViewModeDS(const ShaderMetaType::CompiledShaderInitializerType& Initializer): FBaseDS(Initializer) {}
	FDebugViewModeDS() {}
};

class FDebugViewModePS : public FMeshMaterialShader
{
public:

	FDebugViewModePS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer);

	FDebugViewModePS() {}

	void GetElementShaderBindings(
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
		FVertexInputStreamArray& VertexStreams) const
	{
		FMeshMaterialShader::GetElementShaderBindings(Scene, ViewIfDynamicMeshCommand, VertexFactory, InputStreamType, FeatureLevel, PrimitiveSceneProxy, MeshBatch, BatchElement, ShaderElementData, ShaderBindings, VertexStreams);
	
		int8 VisualizeElementIndex = 0;
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		VisualizeElementIndex = BatchElement.VisualizeElementIndex;
#endif

		GetDebugViewModeShaderBindings(
			PrimitiveSceneProxy,
			ShaderElementData.MaterialRenderProxy,
			ShaderElementData.Material,
			ShaderElementData.DebugViewMode,
			ShaderElementData.ViewOrigin,
			ShaderElementData.VisualizeLODIndex,
			VisualizeElementIndex,
			ShaderElementData.NumVSInstructions,
			ShaderElementData.NumPSInstructions,
			ShaderElementData.ViewModeParam,
			ShaderElementData.ViewModeParamName,
			ShaderBindings
		);
	}

	virtual void GetDebugViewModeShaderBindings(
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT Material,
		EDebugViewShaderMode DebugViewMode,
		const FVector& ViewOrigin,
		int32 VisualizeLODIndex,
		int32 VisualizeElementIndex,
		int32 NumVSInstructions,
		int32 NumPSInstructions,
		int32 ViewModeParam,
		FName ViewModeParamName,
		FMeshDrawSingleShaderBindings& ShaderBindings
	) const = 0;
};

class FDebugViewModeMeshProcessor : public FMeshPassProcessor
{
public:
	FDebugViewModeMeshProcessor(const FScene* InScene, ERHIFeatureLevel::Type InFeatureLevel, const FSceneView* InViewIfDynamicMeshCommand, FRHIUniformBuffer* InPassUniformBuffer, bool bTranslucentBasePass, FMeshPassDrawListContext* InDrawListContext);
	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final;

private:

	void UpdateInstructionCount(FDebugViewModeShaderElementData& OutShaderElementData, const FMaterial* InBatchMaterial, FVertexFactoryType* InVertexFactoryType);

	TUniformBufferRef<FViewUniformShaderParameters> ViewUniformBuffer;
	FUniformBufferRHIRef PassUniformBuffer;
	EDebugViewShaderMode DebugViewMode;
	int32 ViewModeParam;
	FName ViewModeParamName;

	const FDebugViewModeInterface* DebugViewModeInterface;
};


#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)