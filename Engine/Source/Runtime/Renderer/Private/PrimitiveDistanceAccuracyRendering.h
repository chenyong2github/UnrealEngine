// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
PrimitiveDistanceAccuracyRendering.h: Declarations used for the viewmode.
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
* Pixel shader that renders texture streamer wanted mips accuracy.
*/
class FPrimitiveDistanceAccuracyPS : public FDebugViewModePS
{
	DECLARE_SHADER_TYPE(FPrimitiveDistanceAccuracyPS,MeshMaterial);

public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return ShouldCompileDebugViewModeShader(DVSM_PrimitiveDistanceAccuracy, Parameters);
	}

	FPrimitiveDistanceAccuracyPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FDebugViewModePS(Initializer)
	{
		CPULogDistanceParameter.Bind(Initializer.ParameterMap, TEXT("CPULogDistance"));
		PrimitiveAlphaParameter.Bind(Initializer.ParameterMap, TEXT("PrimitiveAlpha"));
	}

	FPrimitiveDistanceAccuracyPS() {}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("UNDEFINED_ACCURACY"), UndefinedStreamingAccuracyIntensity);
	}

	LAYOUT_FIELD(FShaderParameter, CPULogDistanceParameter);
	LAYOUT_FIELD(FShaderParameter, PrimitiveAlphaParameter);
};

class FPrimitiveDistanceAccuracyInterface : public FDebugViewModeInterface
{
public:

	FPrimitiveDistanceAccuracyInterface() : FDebugViewModeInterface(TEXT("PrimitiveDistanceAccuracy"), false, false, false) {}
	virtual void AddShaderTypes(ERHIFeatureLevel::Type InFeatureLevel,
		const FVertexFactoryType* InVertexFactoryType,
		FMaterialShaderTypes& OutShaderTypes) const override
	{
		AddDebugViewModeShaderTypes(InFeatureLevel, InVertexFactoryType, OutShaderTypes);
		OutShaderTypes.AddShaderType<FPrimitiveDistanceAccuracyPS>();
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
