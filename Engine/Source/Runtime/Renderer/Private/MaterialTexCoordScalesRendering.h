// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
MaterialTexCoordScalesRendering.h: Declarations used for the viewmode.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameters.h"
#include "Shader.h"
#include "Engine/TextureStreamingTypes.h"
#include "MeshMaterialShader.h"
#include "DebugViewModeRendering.h"
#include "DebugViewModeInterface.h"

class FPrimitiveSceneProxy;
struct FMeshBatchElement;
struct FMeshDrawingRenderState;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

/**
* Pixel shader that renders texcoord scales.
* The shader is only compiled with the local vertex factory to prevent multiple compilation.
* Nothing from the factory is actually used, but the shader must still derive from FMeshMaterialShader.
*/
class FMaterialTexCoordScalePS : public FDebugViewModePS
{
	DECLARE_SHADER_TYPE(FMaterialTexCoordScalePS,MeshMaterial);

public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		// Both of these debug view modes use this shader
		return
			ShouldCompileDebugViewModeShader(DVSM_OutputMaterialTextureScales, Parameters) ||
			ShouldCompileDebugViewModeShader(DVSM_MaterialTextureScaleAccuracy, Parameters);
	}

	FMaterialTexCoordScalePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FDebugViewModePS(Initializer)
	{
		AnalysisParamsParameter.Bind(Initializer.ParameterMap,TEXT("AnalysisParams"));
		OneOverCPUTexCoordScalesParameter.Bind(Initializer.ParameterMap,TEXT("OneOverCPUTexCoordScales"));
		TexCoordIndicesParameter.Bind(Initializer.ParameterMap, TEXT("TexCoordIndices"));
		PrimitiveAlphaParameter.Bind(Initializer.ParameterMap, TEXT("PrimitiveAlpha"));
	}

	FMaterialTexCoordScalePS() {}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("UNDEFINED_ACCURACY"), UndefinedStreamingAccuracyIntensity);
		OutEnvironment.SetDefine(TEXT("MAX_NUM_TEX_COORD"), (uint32)TEXSTREAM_MAX_NUM_UVCHANNELS);
		OutEnvironment.SetDefine(TEXT("INITIAL_GPU_SCALE"), (uint32)TEXSTREAM_INITIAL_GPU_SCALE);
		OutEnvironment.SetDefine(TEXT("TILE_RESOLUTION"), (uint32)TEXSTREAM_TILE_RESOLUTION);
		OutEnvironment.SetDefine(TEXT("MAX_NUM_TEXTURE_REGISTER"), (uint32)TEXSTREAM_MAX_NUM_TEXTURES_PER_MATERIAL);
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	LAYOUT_FIELD(FShaderParameter, AnalysisParamsParameter);
	LAYOUT_FIELD(FShaderParameter, OneOverCPUTexCoordScalesParameter);
	LAYOUT_FIELD(FShaderParameter, TexCoordIndicesParameter);
	LAYOUT_FIELD(FShaderParameter, PrimitiveAlphaParameter);
};

class FMaterialTexCoordScaleBaseInterface : public FDebugViewModeInterface
{
public:
	FMaterialTexCoordScaleBaseInterface(
		bool InNeedsOnlyLocalVertexFactor,  // Whether this viewmode will only be used with the local vertex factory (for draw tiled mesh).
		bool InNeedsMaterialProperties,		// Whether the PS use any of the material properties (otherwise default material will be used, reducing shader compilation).
		bool InNeedsInstructionCount		// Whether FDebugViewModePS::GetDebugViewModeShaderBindings() will use the num of instructions.
	) : FDebugViewModeInterface(TEXT("MaterialTexCoordScale"), InNeedsOnlyLocalVertexFactor, InNeedsMaterialProperties, InNeedsInstructionCount) {}

	virtual void AddShaderTypes(ERHIFeatureLevel::Type InFeatureLevel,
		const FVertexFactoryType* InVertexFactoryType,
		FMaterialShaderTypes& OutShaderTypes) const override
	{
		AddDebugViewModeShaderTypes(InFeatureLevel, InVertexFactoryType, OutShaderTypes);
		OutShaderTypes.AddShaderType<FMaterialTexCoordScalePS>();
	}

	virtual void GetDebugViewModeShaderBindings(
		const FDebugViewModePS& BaseShader,
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
	) const override;
};

class FMaterialTexCoordScaleAccuracyInterface : public FMaterialTexCoordScaleBaseInterface
{
public:
	FMaterialTexCoordScaleAccuracyInterface() : FMaterialTexCoordScaleBaseInterface(false, true, false) {}
};

class FOutputMaterialTexCoordScaleInterface : public FMaterialTexCoordScaleBaseInterface
{
public:
	FOutputMaterialTexCoordScaleInterface() : FMaterialTexCoordScaleBaseInterface(true, true, false) {}
	virtual void SetDrawRenderState(EBlendMode BlendMode, FRenderState& DrawRenderState, bool bHasDepthPrepassForMaskedMaterial) const override;
};

#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
