// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NaniteShared.h"
#include "NaniteMaterials.h"
#include "PrimitiveSceneInfo.h"

struct MeshDrawCommandKeyFuncs;
class FParallelCommandListBindings;
class FRDGParallelCommandListSet;

class FNaniteDrawListContext : public FMeshPassDrawListContext
{
public:
	struct FDeferredCommand
	{
		FPrimitiveSceneInfo* PrimitiveSceneInfo;
		FMeshDrawCommand MeshDrawCommand;
		FNaniteMaterialCommands::FCommandHash CommandHash;
#if WITH_DEBUG_VIEW_MODES
		uint32 InstructionCount;
#endif
		uint8 SectionIndex;
	};

	struct FDeferredPipeline
	{
		FPrimitiveSceneInfo* PrimitiveSceneInfo;
		FNaniteRasterPipeline RasterPipeline;
		uint8 SectionIndex;
	};

public:
	struct FPrimitiveSceneInfoScope
	{
		FPrimitiveSceneInfoScope(const FPrimitiveSceneInfoScope&) = delete;
		FPrimitiveSceneInfoScope& operator=(const FPrimitiveSceneInfoScope&) = delete;

		inline FPrimitiveSceneInfoScope(FNaniteDrawListContext& InContext, FPrimitiveSceneInfo& PrimitiveSceneInfo)
			: Context(InContext)
		{
			Context.BeginPrimitiveSceneInfo(PrimitiveSceneInfo);
		}

		inline ~FPrimitiveSceneInfoScope()
		{
			Context.EndPrimitiveSceneInfo();
		}

	private:
		FNaniteDrawListContext& Context;
	};

	struct FMeshPassScope
	{
		FMeshPassScope(const FMeshPassScope&) = delete;
		FMeshPassScope& operator=(const FMeshPassScope&) = delete;

		inline FMeshPassScope(FNaniteDrawListContext& InContext, ENaniteMeshPass::Type MeshPass)
			: Context(InContext)
		{
			Context.BeginMeshPass(MeshPass);
		}

		inline ~FMeshPassScope()
		{
			Context.EndMeshPass();
		}
		
	private:
		FNaniteDrawListContext& Context;
	};

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

	void BeginPrimitiveSceneInfo(FPrimitiveSceneInfo& PrimitiveSceneInfo);
	void EndPrimitiveSceneInfo();

	void BeginMeshPass(ENaniteMeshPass::Type MeshPass);
	void EndMeshPass();

	void Apply(FScene& Scene);

private:
	void AddShadingCommand(FPrimitiveSceneInfo& PrimitiveSceneInfo, const FNaniteCommandInfo& CommandInfo, ENaniteMeshPass::Type MeshPass, uint8 SectionIndex);
	void AddRasterBin(FPrimitiveSceneInfo& PrimitiveSceneInfo, const FNaniteRasterBin& CommandInfo, ENaniteMeshPass::Type MeshPass, uint8 SectionIndex);

private:
	FMeshDrawCommand MeshDrawCommandForStateBucketing;
	FPrimitiveSceneInfo* CurrentPrimitiveSceneInfo = nullptr;
	ENaniteMeshPass::Type CurrentMeshPass = ENaniteMeshPass::Num;

public:
	TArray<FDeferredCommand> DeferredCommands[ENaniteMeshPass::Num];
	TArray<FDeferredPipeline> DeferredPipelines[ENaniteMeshPass::Num];
};

class FNaniteMeshProcessor : public FSceneRenderingAllocatorObject<FNaniteMeshProcessor>, public FMeshPassProcessor
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

void BuildNaniteMaterialPassCommands(
	const FGraphicsPipelineRenderTargetsInfo& RenderTargetsInfo,
	const FNaniteMaterialCommands& MaterialCommands,
	TArray<FNaniteMaterialPassCommand, SceneRenderingAllocator>& OutNaniteMaterialPassCommands);

void DrawNaniteMaterialPasses(
	FRDGParallelCommandListSet* ParallelCommandListSet,
	FRHICommandList& RHICmdList,
	const FIntRect ViewRect,
	const uint32 TileCount,
	TShaderMapRef<FNaniteIndirectMaterialVS> VertexShader,
	FRDGBuffer* MaterialIndirectArgs,
	TArrayView<FNaniteMaterialPassCommand const> MaterialPassCommands
);

void SubmitNaniteIndirectMaterial(
	const FNaniteMaterialPassCommand& MaterialPassCommand,
	const TShaderMapRef<FNaniteIndirectMaterialVS>& VertexShader,
	const FGraphicsMinimalPipelineStateSet& GraphicsMinimalPipelineStateSet,
	const uint32 InstanceFactor,
	FRHICommandList& RHICmdList,
	FRHIBuffer* MaterialIndirectArgs,
	FMeshDrawCommandStateCache& StateCache
);

void SubmitNaniteMultiViewMaterial(
	const FMeshDrawCommand& MeshDrawCommand,
	const float MaterialDepth,
	const TShaderMapRef<FNaniteMultiViewMaterialVS>& VertexShader,
	const FGraphicsMinimalPipelineStateSet& GraphicsMinimalPipelineStateSet,
	const uint32 InstanceFactor,
	FRHICommandList& RHICmdList,
	FMeshDrawCommandStateCache& StateCache,
	uint32 InstanceBaseOffset
);
