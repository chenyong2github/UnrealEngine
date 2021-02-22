// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenSceneRendering.h
=============================================================================*/

#pragma once

#include "RHIDefinitions.h"
#include "SceneView.h"
#include "SceneRendering.h"
#include "ScenePrivate.h"
#include "LumenSceneData.h"

class FLumenSceneData;
class FLumenCardScene;

inline bool DoesPlatformSupportLumenGI(EShaderPlatform Platform)
{
	return FDataDrivenShaderPlatformInfo::GetSupportsLumenGI(Platform);
}

class FCardRenderData
{
public:
	FPrimitiveSceneInfo* PrimitiveSceneInfo = nullptr;
	int32 PrimitiveInstanceIndexOrMergedFlag = -1;

	// CardData
	int32 CardIndex = -1.0f;
	bool bDistantScene = false;
	FIntPoint DesiredResolution;
	FIntRect AtlasAllocation;
	FVector Origin;
	FVector LocalExtent;
	FVector LocalToWorldRotationX;
	FVector LocalToWorldRotationY;
	FVector LocalToWorldRotationZ;

	FViewMatrices ViewMatrices;
	FMatrix ProjectionMatrixUnadjustedForRHI;

	int32 StartMeshDrawCommandIndex = 0;
	int32 NumMeshDrawCommands = 0;

	TArray<uint32, SceneRenderingAllocator> NaniteInstanceIds;
	TArray<FNaniteCommandInfo, SceneRenderingAllocator> NaniteCommandInfos;
	float NaniteLODScaleFactor = 1.0f;

	FCardRenderData(FLumenCard& InCardData, 
		FPrimitiveSceneInfo* InPrimitiveSceneInfo,
		int32 InPrimitiveInstanceIndex,
		ERHIFeatureLevel::Type InFeatureLevel,
		int32 InCardIndex);

	void UpdateViewMatrices(const FViewInfo& MainView);

	void PatchView(FRHICommandList& RHICmdList, const FScene* Scene, FViewInfo* View) const;
};

FMeshPassProcessor* CreateLumenCardNaniteMeshProcessor(
	const FScene* Scene,
	const FSceneView* InViewIfDynamicMeshCommand,
	FMeshPassDrawListContext* InDrawListContext);

extern void SetupLumenCardSceneParameters(FRDGBuilder& GraphBuilder, const FScene* Scene, FLumenCardScene& OutParameters);
extern void UpdateLumenMeshCards(FScene& Scene, const FDistanceFieldSceneData& DistanceFieldSceneData, FLumenSceneData& LumenSceneData, FRHICommandListImmediate& RHICmdList);

BEGIN_SHADER_PARAMETER_STRUCT(FLumenReflectionCompositeParameters, )
	SHADER_PARAMETER(float, MaxRoughnessToTrace)
	SHADER_PARAMETER(float, InvRoughnessFadeLength)
END_SHADER_PARAMETER_STRUCT()