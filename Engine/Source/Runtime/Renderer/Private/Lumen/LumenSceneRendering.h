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
	return Platform == SP_PCD3D_SM5 || FDataDrivenShaderPlatformInfo::GetSupportsLumenGI(Platform);
}

extern int32 GAllowLumenScene;

class FCardRenderData
{
public:
	FCardSourceData& CardData;

	FViewMatrices ViewMatrices;
	FMatrix ProjectionMatrixUnadjustedForRHI;

	int32 StartMeshDrawCommandIndex;
	int32 NumMeshDrawCommands;

	TArray<uint32, SceneRenderingAllocator> NaniteInstanceIds;
	TArray<FNaniteCommandInfo, SceneRenderingAllocator> NaniteCommandInfos;

	int32 CardIndex;
	float NaniteLODScaleFactor;

	FCardRenderData(FCardSourceData& InCardData, ERHIFeatureLevel::Type InFeatureLevel, int32 InCardIndex);

	void UpdateViewMatrices(const FViewInfo& MainView);

	void PatchView(FRHICommandList& RHICmdList, const FScene* Scene, FViewInfo* View) const;

	FIntRect GetAtlasAllocation() const
	{
		return CardData.AtlasAllocation;
	}

	void SetAtlasAllocation(FIntRect NewAllocation)
	{
		CardData.AtlasAllocation = NewAllocation;
	}

	FVector GetLocalToWorldRotationX() const
	{
		return CardData.LocalToWorldRotationX;
	}

	FVector GetLocalToWorldRotationY() const
	{
		return CardData.LocalToWorldRotationY;
	}

	FVector GetLocalToWorldRotationZ() const
	{
		return CardData.LocalToWorldRotationZ;
	}
};

FMeshPassProcessor* CreateLumenCardNaniteMeshProcessor(
	const FScene* Scene,
	const FSceneView* InViewIfDynamicMeshCommand,
	FMeshPassDrawListContext* InDrawListContext);

extern void SetupLumenCardSceneParameters(FScene* Scene, FLumenCardScene& OutParameters);
extern void UpdateLumenCubeMapTrees(const FDistanceFieldSceneData& DistanceFieldSceneData, FLumenSceneData& LumenSceneData, FRHICommandListImmediate& RHICmdList, int32 NumScenePrimitives);

BEGIN_SHADER_PARAMETER_STRUCT(FLumenReflectionCompositeParameters, )
	SHADER_PARAMETER(float, MaxRoughnessToTrace)
	SHADER_PARAMETER(float, InvRoughnessFadeLength)
END_SHADER_PARAMETER_STRUCT()