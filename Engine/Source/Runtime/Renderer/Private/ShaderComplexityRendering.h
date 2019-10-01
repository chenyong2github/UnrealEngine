// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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


template <bool bQuadComplexity>
class TComplexityAccumulatePS : public FDebugViewModePS
{
	DECLARE_SHADER_TYPE(TComplexityAccumulatePS,MeshMaterial);
public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		// See FDebugViewModeMaterialProxy::GetFriendlyName()
		if (AllowDebugViewShaderMode(bQuadComplexity ? DVSM_QuadComplexity : DVSM_ShaderComplexity, Parameters.Platform, Parameters.Material->GetFeatureLevel()))
		{
			// If it comes from FDebugViewModeMaterialProxy, compile it.
			if (Parameters.Material->GetFriendlyName().Contains(TEXT("ComplexityAccumulate")))
			{
				return true;
			}
			// Otherwise we only cache it if this for the shader complexity.
			else if (GCacheShaderComplexityShaders)
			{
				return !FDebugViewModeInterface::AllowFallbackToDefaultMaterial(Parameters.Material) || Parameters.Material->IsDefaultMaterial();
			}
		}
		return false;
	}

	TComplexityAccumulatePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FDebugViewModePS(Initializer)
	{
		NormalizedComplexity.Bind(Initializer.ParameterMap,TEXT("NormalizedComplexity"));
		ShowQuadOverdraw.Bind(Initializer.ParameterMap,TEXT("bShowQuadOverdraw"));
		QuadBufferUAV.Bind(Initializer.ParameterMap,TEXT("RWQuadBuffer"));
	}

	TComplexityAccumulatePS() {}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FMeshMaterialShader::Serialize(Ar);
		Ar << NormalizedComplexity;
		Ar << ShowQuadOverdraw;
		Ar << QuadBufferUAV;
		return bShaderHasOutdatedParameters;
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("OUTPUT_QUAD_OVERDRAW"), AllowDebugViewShaderMode(DVSM_QuadComplexity, Parameters.Platform, Parameters.Material->GetFeatureLevel()));
		TCHAR BufferRegister[] = { 'u', '0', 0 };
		BufferRegister[1] += FSceneRenderTargets::GetQuadOverdrawUAVIndex(Parameters.Platform, Parameters.Material->GetFeatureLevel());
		OutEnvironment.SetDefine(TEXT("QUAD_BUFFER_REGISTER"), BufferRegister);
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
	) const override;

private:

	FShaderParameter NormalizedComplexity;
	FShaderParameter ShowQuadOverdraw;
	FShaderResourceParameter QuadBufferUAV;
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

	virtual FDebugViewModePS* GetPixelShader(const FMaterial* InMaterial, FVertexFactoryType* VertexFactoryType) const override;
	virtual void SetDrawRenderState(EBlendMode BlendMode, FRenderState& DrawRenderState, bool bHasDepthPrepassForMaskedMaterial) const;
};

#endif //!(UE_BUILD_SHIPPING || UE_BUILD_TEST)
