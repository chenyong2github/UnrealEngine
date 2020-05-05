// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
LODColorationRendering.cpp: Contains definitions for rendering the LOD coloration viewmode.
=============================================================================*/

#include "LODColorationRendering.h"
#include "PrimitiveSceneProxy.h"
#include "EngineGlobals.h"
#include "MeshBatch.h"
#include "Engine/Engine.h"

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

IMPLEMENT_MATERIAL_SHADER_TYPE(,FLODColorationPS,TEXT("/Engine/Private/ViewMode/LODColorationPixelShader.usf"),TEXT("Main"),SF_Pixel);

void FLODColorationInterface::GetDebugViewModeShaderBindings(
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
	const FLODColorationPS& Shader = static_cast<const FLODColorationPS&>(ShaderBase);
	const int32 LODIndex = FMath::Clamp(VisualizeLODIndex, 0, NumLODColorationColors - 1);

	ShaderBindings.Add(Shader.LODIndexParameter, LODIndex);
}

#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
