// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
RequiredTextureResolutionRendering.h: Declarations used for the viewmode.
=============================================================================*/

#pragma once

#include "MeshMaterialShader.h"
#include "DebugViewModeRendering.h"
#include "Engine/TextureStreamingTypes.h"
#include "DebugViewModeInterface.h"

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

/**
* Pixel shader that renders texcoord scales.
* The shader is only compiled with the local vertex factory to prevent multiple compilation.
* Nothing from the factory is actually used, but the shader must still derive from FMeshMaterialShader.
*/
class FRequiredTextureResolutionPS : public FDebugViewModePS
{
	DECLARE_SHADER_TYPE(FRequiredTextureResolutionPS,MeshMaterial);

public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return ShouldCompileDebugViewModeShader(DVSM_RequiredTextureResolution, Parameters);
	}

	FRequiredTextureResolutionPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FDebugViewModePS(Initializer)
	{
		AnalysisParamsParameter.Bind(Initializer.ParameterMap,TEXT("AnalysisParams"));
		PrimitiveAlphaParameter.Bind(Initializer.ParameterMap, TEXT("PrimitiveAlpha"));
	}

	FRequiredTextureResolutionPS() {}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("UNDEFINED_ACCURACY"), UndefinedStreamingAccuracyIntensity);
		OutEnvironment.SetDefine(TEXT("MAX_NUM_TEX_COORD"), (uint32)TEXSTREAM_MAX_NUM_UVCHANNELS);
		OutEnvironment.SetDefine(TEXT("MAX_NUM_TEXTURE_REGISTER"), (uint32)TEXSTREAM_MAX_NUM_TEXTURES_PER_MATERIAL);
		OutEnvironment.SetDefine(TEXT("SCENE_TEXTURES_DISABLED"), 1u);
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	LAYOUT_FIELD(FShaderParameter, AnalysisParamsParameter);
	LAYOUT_FIELD(FShaderParameter, PrimitiveAlphaParameter);
};

class FRequiredTextureResolutionInterface: public FDebugViewModeInterface
{
public:

	FRequiredTextureResolutionInterface() : FDebugViewModeInterface(TEXT("RequiredTextureResolution"), false, true, false) {}
	virtual void AddShaderTypes(ERHIFeatureLevel::Type InFeatureLevel,
		const FVertexFactoryType* InVertexFactoryType,
		FMaterialShaderTypes& OutShaderTypes) const override
	{
		AddDebugViewModeShaderTypes(InFeatureLevel, InVertexFactoryType, OutShaderTypes);
		OutShaderTypes.AddShaderType<FRequiredTextureResolutionPS>();
	}

	virtual void GetDebugViewModeShaderBindings(
		const FDebugViewModePS& ShaderBase,
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

#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
