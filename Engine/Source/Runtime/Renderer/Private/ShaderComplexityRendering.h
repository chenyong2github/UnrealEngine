// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
ShaderComplexityRendering.h: Declarations used for the shader complexity viewmode.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameters.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "DebugViewModeRendering.h"
#include "DebugViewModeInterface.h"
#include "PostProcess/SceneRenderTargets.h"

class FPrimitiveSceneProxy;
struct FMeshBatchElement;
struct FMeshDrawingRenderState;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

extern int32 GCacheShaderComplexityShaders;

extern int32 GShaderComplexityBaselineForwardVS;
extern int32 GShaderComplexityBaselineForwardPS;
extern int32 GShaderComplexityBaselineForwardUnlitPS;
extern int32 GShaderComplexityBaselineDeferredVS;
extern int32 GShaderComplexityBaselineDeferredPS;
extern int32 GShaderComplexityBaselineDeferredUnlitPS;

class FComplexityAccumulatePS : public FDebugViewModePS
{
public:
	DECLARE_SHADER_TYPE(FComplexityAccumulatePS, MeshMaterial);

	enum class EQuadOverdraw
	{
		Disable,
		Enable,
		MAX
	};

	class FQuadOverdraw : SHADER_PERMUTATION_ENUM_CLASS("OUTPUT_QUAD_OVERDRAW", EQuadOverdraw);
	using FPermutationDomain = TShaderPermutationDomain<FQuadOverdraw>;

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		const bool bQuadOverdraw = PermutationVector.Get<FComplexityAccumulatePS::FQuadOverdraw>() == FComplexityAccumulatePS::EQuadOverdraw::Enable;

		return ShouldCompileDebugViewModeShader(bQuadOverdraw ? DVSM_QuadComplexity : DVSM_ShaderComplexity, Parameters);
	}

	FComplexityAccumulatePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FDebugViewModePS(Initializer)
	{
		NormalizedComplexity.Bind(Initializer.ParameterMap,TEXT("NormalizedComplexity"));
		ShowQuadOverdraw.Bind(Initializer.ParameterMap,TEXT("bShowQuadOverdraw"));
		QuadBufferUAV.Bind(Initializer.ParameterMap,TEXT("RWQuadBuffer"));
	}

	FComplexityAccumulatePS() {}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		TCHAR BufferRegister[] = { 'u', '0', 0 };
		BufferRegister[1] += GetQuadOverdrawUAVIndex(Parameters.Platform, Parameters.MaterialParameters.FeatureLevel);
		OutEnvironment.SetDefine(TEXT("QUAD_BUFFER_REGISTER"), BufferRegister);
	}

	LAYOUT_FIELD(FShaderParameter, NormalizedComplexity);
	LAYOUT_FIELD(FShaderParameter, ShowQuadOverdraw);
	LAYOUT_FIELD(FShaderResourceParameter, QuadBufferUAV);
};

class FComplexityAccumulateInterface : public FDebugViewModeInterface
{
	const bool bShowShaderComplexity;
	const bool bShowQuadComplexity;

public:

	FComplexityAccumulateInterface(bool InShowShaderComplexity, bool InShowQuadComplexity) 
		: FDebugViewModeInterface(TEXT("ComplexityAccumulate"), false, false, true)
		, bShowShaderComplexity(InShowShaderComplexity)
		, bShowQuadComplexity(InShowQuadComplexity)
	{}

	virtual void AddShaderTypes(ERHIFeatureLevel::Type InFeatureLevel,
		const FVertexFactoryType* InVertexFactoryType,
		FMaterialShaderTypes& OutShaderTypes) const override;
	virtual void SetDrawRenderState(EBlendMode BlendMode, FRenderState& DrawRenderState, bool bHasDepthPrepassForMaskedMaterial) const;

	virtual void GetDebugViewModeShaderBindings(
		const FDebugViewModePS& Shader,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
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

#endif //!(UE_BUILD_SHIPPING || UE_BUILD_TEST)
