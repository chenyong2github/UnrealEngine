// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
MeshTexCoordSizeAccuracyRendering.h: Declarations used for the viewmode.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameters.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "DebugViewModeRendering.h"
#include "DebugViewModeInterface.h"

class FPrimitiveSceneProxy;
struct FMeshBatchElement;
struct FMeshDrawingRenderState;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

/**
* Pixel shader that renders the accuracy of the texel factor.
*/
class FMeshTexCoordSizeAccuracyPS : public FDebugViewModePS
{
	DECLARE_SHADER_TYPE(FMeshTexCoordSizeAccuracyPS,MeshMaterial);

public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return ShouldCompileDebugViewModeShader(DVSM_MeshUVDensityAccuracy, Parameters);
	}

	FMeshTexCoordSizeAccuracyPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FDebugViewModePS(Initializer)
	{
		CPUTexelFactorParameter.Bind(Initializer.ParameterMap,TEXT("CPUTexelFactor"));
		PrimitiveAlphaParameter.Bind(Initializer.ParameterMap, TEXT("PrimitiveAlpha"));
		TexCoordAnalysisIndexParameter.Bind(Initializer.ParameterMap, TEXT("TexCoordAnalysisIndex"));
	}

	FMeshTexCoordSizeAccuracyPS() {}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("UNDEFINED_ACCURACY"), UndefinedStreamingAccuracyIntensity);
	}

	LAYOUT_FIELD(FShaderParameter, CPUTexelFactorParameter);
	LAYOUT_FIELD(FShaderParameter, PrimitiveAlphaParameter);
	LAYOUT_FIELD(FShaderParameter, TexCoordAnalysisIndexParameter);
};

class FMeshTexCoordSizeAccuracyInterface : public FDebugViewModeInterface
{
public:
	FMeshTexCoordSizeAccuracyInterface() : FDebugViewModeInterface(TEXT("MeshTexCoordSizeAccuracy"), false, false, false) {}
	virtual void AddShaderTypes(ERHIFeatureLevel::Type InFeatureLevel,
		EMaterialTessellationMode InMaterialTessellationMode,
		const FVertexFactoryType* InVertexFactoryType,
		FMaterialShaderTypes& OutShaderTypes) const override
	{
		AddDebugViewModeShaderTypes(InFeatureLevel, InMaterialTessellationMode, InVertexFactoryType, OutShaderTypes);
		OutShaderTypes.AddShaderType<FMeshTexCoordSizeAccuracyPS>();
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

#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
