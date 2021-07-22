// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NaniteShared.h"
#include "NaniteMaterials.h"

struct MeshDrawCommandKeyFuncs;
class FParallelCommandListBindings;

class FNaniteDrawListContext : public FMeshPassDrawListContext
{
public:
	FNaniteDrawListContext(FNaniteMaterialCommands& InMaterialCommands);

	virtual FMeshDrawCommand& AddCommand(FMeshDrawCommand& Initializer, uint32 NumElements) override final;

	virtual void FinalizeCommand(
		const FMeshBatch& MeshBatch,
		int32 BatchElementIndex,
		const FMeshDrawCommandPrimitiveIdInfo& IdInfo,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode,
		FMeshDrawCommandSortKey SortKey,
		EFVisibleMeshDrawCommandFlags Flags,
		const FGraphicsMinimalPipelineStateInitializer& PipelineState,
		const FMeshProcessorShaders* ShadersForDebugging,
		FMeshDrawCommand& MeshDrawCommand
	) override final;

	FNaniteCommandInfo GetCommandInfoAndReset() 
	{ 
		FNaniteCommandInfo Ret = CommandInfo;
		CommandInfo.Reset();
		return Ret; 
	}

private:
	FNaniteMaterialCommands& MaterialCommands;
	FNaniteCommandInfo CommandInfo;
	FMeshDrawCommand MeshDrawCommandForStateBucketing;
};

class FNaniteMeshProcessor : public FMeshPassProcessor
{
public:
	FNaniteMeshProcessor(
		const FScene* InScene,
		ERHIFeatureLevel::Type InFeatureLevel,
		const FSceneView* InViewIfDynamicMeshCommand,
		const FMeshPassProcessorRenderState& InDrawRenderState,
		FMeshPassDrawListContext* InDrawListContext
	);

	virtual void AddMeshBatch(
		const FMeshBatch& RESTRICT MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		int32 StaticMeshId = -1
	) override final;

private:
	bool TryAddMeshBatch(
		const FMeshBatch& RESTRICT MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		int32 StaticMeshId,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material
	);

private:
	FMeshPassProcessorRenderState PassDrawRenderState;
};

FMeshPassProcessor* CreateNaniteMeshProcessor(
	const FScene* Scene,
	const FSceneView* InViewIfDynamicMeshCommand,
	FMeshPassDrawListContext* InDrawListContext
);

void DrawNaniteMaterialPasses(
	const FSceneRenderer& SceneRenderer,
	const FScene& Scene,
	const FViewInfo& View,
	const uint32 TileCount,
	const bool bParallelBuild,
	const FParallelCommandListBindings& ParallelBindings,
	TShaderMapRef<FNaniteMaterialVS> VertexShader,
	FRHICommandListImmediate& RHICmdListImmediate,
	TArray<FNaniteMaterialPassCommand, SceneRenderingAllocator>& MaterialPassCommands
);

void SubmitNaniteMaterialPassCommand(
	const FMeshDrawCommand& MeshDrawCommand,
	const float MaterialDepth,
	const TShaderMapRef<FNaniteMaterialVS>& VertexShader,
	const FGraphicsMinimalPipelineStateSet& GraphicsMinimalPipelineStateSet,
	const uint32 InstanceFactor,
	FRHICommandList& RHICmdList,
	FMeshDrawCommandStateCache& StateCache,
	uint32 InstanceBaseOffset
);

void SubmitNaniteMaterialPassCommand(
	const FNaniteMaterialPassCommand& MaterialPassCommand,
	const TShaderMapRef<FNaniteMaterialVS>& VertexShader,
	const FGraphicsMinimalPipelineStateSet& GraphicsMinimalPipelineStateSet,
	const uint32 InstanceFactor,
	FRHICommandList& RHICmdList,
	FMeshDrawCommandStateCache& StateCache
);