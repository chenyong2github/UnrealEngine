// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
MaterialTexCoordScalesRendering.cpp: Contains definitions for rendering the viewmode.
=============================================================================*/

#include "MaterialTexCoordScalesRendering.h"
#include "PrimitiveSceneProxy.h"
#include "EngineGlobals.h"
#include "MeshBatch.h"
#include "Engine/Engine.h"

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

IMPLEMENT_MATERIAL_SHADER_TYPE(,FMaterialTexCoordScalePS,TEXT("/Engine/Private/MaterialTexCoordScalesPixelShader.usf"),TEXT("Main"),SF_Pixel);

void FMaterialTexCoordScaleBaseInterface::GetDebugViewModeShaderBindings(
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
) const 
{
	const FMaterialTexCoordScalePS& Shader = static_cast<const FMaterialTexCoordScalePS&>(ShaderBase);
	const int32 AnalysisIndex = ViewModeParam >= 0 ? FMath::Clamp<int32>(ViewModeParam, 0, TEXSTREAM_MAX_NUM_TEXTURES_PER_MATERIAL - 1) : -1;
	FVector4 OneOverCPUTexCoordScales[TEXSTREAM_MAX_NUM_TEXTURES_PER_MATERIAL / 4];
	FIntVector4 TexCoordIndices[TEXSTREAM_MAX_NUM_TEXTURES_PER_MATERIAL / 4];
	FMemory::Memzero(OneOverCPUTexCoordScales); // 0 remap to irrelevant data.
	FMemory::Memzero(TexCoordIndices);
#if WITH_EDITORONLY_DATA
	if (PrimitiveSceneProxy)
	{
		PrimitiveSceneProxy->GetMaterialTextureScales(VisualizeLODIndex, VisualizeElementIndex, nullptr, OneOverCPUTexCoordScales, TexCoordIndices);
	}
#endif
	const bool bOutputScales = DebugViewMode == DVSM_OutputMaterialTextureScales;

	ShaderBindings.Add(Shader.OneOverCPUTexCoordScalesParameter, OneOverCPUTexCoordScales);
	ShaderBindings.Add(Shader.TexCoordIndicesParameter, TexCoordIndices);
	ShaderBindings.Add(Shader.AnalysisParamsParameter, FIntPoint(bOutputScales ? -1 : AnalysisIndex, bOutputScales ? 1 : 0));
	ShaderBindings.Add(Shader.PrimitiveAlphaParameter,  (!PrimitiveSceneProxy || PrimitiveSceneProxy->IsSelected()) ? 1.f : .2f);
}

void FOutputMaterialTexCoordScaleInterface::SetDrawRenderState(EBlendMode BlendMode, FRenderState& DrawRenderState, bool bHasDepthPrepassForMaskedMaterial) const
{
	DrawRenderState.BlendState = TStaticBlendState<>::GetRHI();
	DrawRenderState.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
}

#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
