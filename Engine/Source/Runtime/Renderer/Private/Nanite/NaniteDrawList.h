// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NaniteShared.h"
#include "NaniteMaterials.h"
#include "PrimitiveSceneInfo.h"

struct MeshDrawCommandKeyFuncs;
class FParallelCommandListBindings;

class FNaniteDrawListContext : public FMeshPassDrawListContext
{
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

	void BeginPrimitiveSceneInfo(FPrimitiveSceneInfo& PrimitiveSceneInfo);
	void EndPrimitiveSceneInfo();

	void BeginMeshPass(ENaniteMeshPass::Type MeshPass);
	void EndMeshPass();

protected:
	void FinalizeCommandCommon(
		const FMeshBatch& MeshBatch,
		int32 BatchElementIndex,
		const FGraphicsMinimalPipelineStateInitializer& PipelineState,
		const FMeshProcessorShaders* ShadersForDebugging,
		FMeshDrawCommand& MeshDrawCommand
	);

	void AddCommandInfo(FPrimitiveSceneInfo& PrimitiveSceneInfo, FNaniteCommandInfo CommandInfo, ENaniteMeshPass::Type MeshPass, uint8 SectionIndex);

	FMeshDrawCommand MeshDrawCommandForStateBucketing;
	FPrimitiveSceneInfo* CurrPrimitiveSceneInfo = nullptr;	
	ENaniteMeshPass::Type CurrMeshPass = ENaniteMeshPass::Num;
};

class FNaniteDrawListContextImmediate : public FNaniteDrawListContext
{
public:
	FNaniteDrawListContextImmediate(FScene& InScene) : Scene(InScene) {}

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

private:
	FScene& Scene;
};

class FNaniteDrawListContextDeferred : public FNaniteDrawListContext
{
public:
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
	
	void RegisterDeferredCommands(FScene& Scene);

private:	
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

	TArray<FDeferredCommand> DeferredCommands[ENaniteMeshPass::Num];	
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
	TShaderMapRef<FNaniteIndirectMaterialVS> VertexShader,
	FRHICommandListImmediate& RHICmdListImmediate,
	FRHIBuffer* MaterialIndirectArgs,
	TArray<FNaniteMaterialPassCommand, SceneRenderingAllocator>& MaterialPassCommands
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
