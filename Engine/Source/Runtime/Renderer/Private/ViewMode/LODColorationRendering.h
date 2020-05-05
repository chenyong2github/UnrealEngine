// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
LODColorationRendering.h: Declarations used for the viewmode.
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
class FLODColorationPS : public FDebugViewModePS
{
	DECLARE_SHADER_TYPE(FLODColorationPS,MeshMaterial);

public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		// See FDebugViewModeMaterialProxy::GetFriendlyName()
		return AllowDebugViewShaderMode(DVSM_LODColoration, Parameters.Platform, Parameters.MaterialParameters.FeatureLevel) && Parameters.MaterialParameters.bMaterialIsLODColoration;
	}

	FLODColorationPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FDebugViewModePS(Initializer)
	{
		LODIndexParameter.Bind(Initializer.ParameterMap, TEXT("LODIndex"));
	}

	FLODColorationPS() {}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
	}

	LAYOUT_FIELD(FShaderParameter, LODIndexParameter);
};

class FLODColorationInterface : public FDebugViewModeInterface
{
public:

	FLODColorationInterface() : FDebugViewModeInterface(TEXT("LODColoration"), false, true, false) {}
	virtual TShaderRef<FDebugViewModePS> GetPixelShader(const FMaterial* InMaterial, FVertexFactoryType* VertexFactoryType) const override { return InMaterial->GetShader<FLODColorationPS>(VertexFactoryType); }

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
