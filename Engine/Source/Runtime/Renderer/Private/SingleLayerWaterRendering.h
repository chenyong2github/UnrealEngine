// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SingleLayerWaterRendering.h: Water pass rendering implementation.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RendererInterface.h"
#include "MeshPassProcessor.h"
#include "Containers/Array.h"

class FViewInfo;

struct FSingleLayerWaterPassData
{
	TRefCountPtr<IPooledRenderTarget> SceneColorWithoutSingleLayerWater;
	TRefCountPtr<IPooledRenderTarget> SceneDepthWithoutSingleLayerWater;

	struct FSingleLayerWaterPassViewData
	{
		FIntRect SceneWithoutSingleLayerWaterViewRect;
		FVector4 SceneWithoutSingleLayerWaterMinMaxUV;
	};

	TArray<FSingleLayerWaterPassViewData> ViewData;
};

class FSingleLayerWaterPassMeshProcessor : public FMeshPassProcessor
{
public:

	FSingleLayerWaterPassMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, const FMeshPassProcessorRenderState& InPassDrawRenderState, FMeshPassDrawListContext* InDrawListContext);

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final;

	FMeshPassProcessorRenderState PassDrawRenderState;

private:
	void Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		int32 StaticMeshId,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode);
};



bool ShouldRenderSingleLayerWater(const TArray<FViewInfo>& Views, const FEngineShowFlags& EngineShowFlags);
bool ShouldRenderSingleLayerWaterSkippedRenderEditorNotification(const TArray<FViewInfo>& Views);


