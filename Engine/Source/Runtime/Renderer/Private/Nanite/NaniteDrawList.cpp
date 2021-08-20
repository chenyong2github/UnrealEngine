// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteDrawList.h"
#include "SceneUtils.h"
#include "ScenePrivate.h"
#include "MeshPassProcessor.inl"

DECLARE_CYCLE_STAT(TEXT("NaniteBasePass"), STAT_CLP_NaniteBasePass, STATGROUP_ParallelCommandListMarkers);

int32 GNaniteMaterialSortMode = 2;
static FAutoConsoleVariableRef CVarNaniteMaterialSortMode(
	TEXT("r.Nanite.MaterialSortMode"),
	GNaniteMaterialSortMode,
	TEXT("Method of sorting Nanite material draws. 0=disabled, 1=shader, 2=sortkey, 3=refcount"),
	ECVF_RenderThreadSafe
);

FNaniteDrawListContext::FNaniteDrawListContext(FNaniteMaterialCommands& InMaterialCommands)
: MaterialCommands(InMaterialCommands)
{
}

FMeshDrawCommand& FNaniteDrawListContext::AddCommand(FMeshDrawCommand& Initializer, uint32 NumElements)
{
	{
		MeshDrawCommandForStateBucketing.~FMeshDrawCommand();
		new(&MeshDrawCommandForStateBucketing) FMeshDrawCommand();
	}

	MeshDrawCommandForStateBucketing = Initializer;
	return MeshDrawCommandForStateBucketing;
}

void FNaniteDrawListContext::FinalizeCommand(
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
)
{
	FGraphicsMinimalPipelineStateId PipelineId;
	PipelineId = FGraphicsMinimalPipelineStateId::GetPersistentId(PipelineState);
	MeshDrawCommand.SetDrawParametersAndFinalize(MeshBatch, BatchElementIndex, PipelineId, ShadersForDebugging);
#if UE_BUILD_DEBUG
	FMeshDrawCommand MeshDrawCommandDebug = FMeshDrawCommand(MeshDrawCommand);
	check(MeshDrawCommandDebug.ShaderBindings.GetDynamicInstancingHash() == MeshDrawCommand.ShaderBindings.GetDynamicInstancingHash());
	check(MeshDrawCommandDebug.GetDynamicInstancingHash() == MeshDrawCommand.GetDynamicInstancingHash());
#endif
	CommandInfo = MaterialCommands.Register(MeshDrawCommand);
}

void SubmitNaniteMaterialPassCommand(
	const FMeshDrawCommand& MeshDrawCommand,
	const float MaterialDepth,
	const int32 MaterialSlot,
	FRHIBuffer* MaterialIndirectArgs,
	const TShaderMapRef<FNaniteMaterialVS>& VertexShader,
	const FGraphicsMinimalPipelineStateSet& GraphicsMinimalPipelineStateSet,
	const uint32 InstanceFactor,
	FRHICommandList& RHICmdList,
	FMeshDrawCommandStateCache& StateCache,
	uint32 InstanceBaseOffset = 0)
{
#if WANTS_DRAW_MESH_EVENTS
	FMeshDrawCommand::FMeshDrawEvent MeshEvent(MeshDrawCommand, InstanceFactor, RHICmdList);
#endif
	FMeshDrawCommand::SubmitDrawIndirectBegin(MeshDrawCommand, GraphicsMinimalPipelineStateSet, nullptr, 0, InstanceFactor, RHICmdList, StateCache);

	// All Nanite mesh draw commands are using the same vertex shader, which has a material depth parameter we assign at render time.
	{
		FNaniteMaterialVS::FParameters Parameters;
		Parameters.MaterialDepth = MaterialDepth;
		Parameters.MaterialSlot = uint32(MaterialSlot);
		Parameters.TileRemapCount = FMath::DivideAndRoundUp(InstanceFactor, 32u);
		Parameters.InstanceBaseOffset = InstanceBaseOffset;
		SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), Parameters);
	}

	check(MaterialIndirectArgs == nullptr || MaterialSlot != INDEX_NONE);
	const uint32 MaterialSlotIndirectOffset = MaterialIndirectArgs != nullptr ? sizeof(FRHIDrawIndexedIndirectParameters) * uint32(MaterialSlot) : 0;
	FMeshDrawCommand::SubmitDrawIndirectEnd(MeshDrawCommand, InstanceFactor, RHICmdList, MaterialIndirectArgs, MaterialSlotIndirectOffset);
}

void SubmitNaniteMaterialPassCommand(
	const FNaniteMaterialPassCommand& MaterialPassCommand,
	const TShaderMapRef<FNaniteMaterialVS>& VertexShader,
	const FGraphicsMinimalPipelineStateSet& GraphicsMinimalPipelineStateSet,
	const uint32 InstanceFactor,
	FRHICommandList& RHICmdList,
	FRHIBuffer* MaterialIndirectArgs,
	FMeshDrawCommandStateCache& StateCache)
{
	SubmitNaniteMaterialPassCommand(
		MaterialPassCommand.MeshDrawCommand,
		MaterialPassCommand.MaterialDepth,
		MaterialPassCommand.MaterialSlot,
		MaterialIndirectArgs,
		VertexShader,
		GraphicsMinimalPipelineStateSet,
		InstanceFactor,
		RHICmdList,
		StateCache);
}

FNaniteMeshProcessor::FNaniteMeshProcessor(
	const FScene* InScene,
	ERHIFeatureLevel::Type InFeatureLevel,
	const FSceneView* InViewIfDynamicMeshCommand,
	const FMeshPassProcessorRenderState& InDrawRenderState,
	FMeshPassDrawListContext* InDrawListContext
)
	: FMeshPassProcessor(InScene, InFeatureLevel, InViewIfDynamicMeshCommand, InDrawListContext)
	, PassDrawRenderState(InDrawRenderState)
{
	check(DoesPlatformSupportNanite(GMaxRHIShaderPlatform));
}

void FNaniteMeshProcessor::AddMeshBatch(
	const FMeshBatch& RESTRICT MeshBatch,
	uint64 BatchElementMask,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	int32 StaticMeshId /*= -1 */
)
{
	LLM_SCOPE_BYTAG(Nanite);

	// this is now checking before we even attempt to add mesh batch
	checkf(MeshBatch.bUseForMaterial, TEXT("Logic in BuildNaniteDrawCommands() should not have allowed a mesh batch without bUseForMaterial to be added"));

	const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = MeshBatch.MaterialRenderProxy;
	while (FallbackMaterialRenderProxyPtr)
	{
		const FMaterial* Material = FallbackMaterialRenderProxyPtr->GetMaterialNoFallback(FeatureLevel);
		if (Material && TryAddMeshBatch(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, *FallbackMaterialRenderProxyPtr, *Material))
		{
			break;
		}
		FallbackMaterialRenderProxyPtr = FallbackMaterialRenderProxyPtr->GetFallback(FeatureLevel);
	}
}

bool FNaniteMeshProcessor::TryAddMeshBatch(
	const FMeshBatch& RESTRICT MeshBatch,
	uint64 BatchElementMask,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	int32 StaticMeshId,
	const FMaterialRenderProxy& MaterialRenderProxy,
	const FMaterial& Material
)
{
	const EBlendMode BlendMode = Material.GetBlendMode();
	const FMaterialShadingModelField ShadingModels = Material.GetShadingModels();

	check(BlendMode == BLEND_Opaque);
	check(Material.GetMaterialDomain() == MD_Surface);

	const bool bRenderSkylight = Scene && Scene->ShouldRenderSkylightInBasePass(BlendMode) && ShadingModels != MSM_Unlit;
	const bool bRenderSkyAtmosphere = IsTranslucentBlendMode(BlendMode) && (Scene && Scene->HasSkyAtmosphere() && Scene->ReadOnlyCVARCache.bSupportSkyAtmosphere);

	// Check for a cached light-map.
	const bool bIsLitMaterial = ShadingModels.IsLit();
	static const auto AllowStaticLightingVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));
	const bool bAllowStaticLighting = (!AllowStaticLightingVar || AllowStaticLightingVar->GetValueOnRenderThread() != 0);

	const FLightMapInteraction LightMapInteraction = (bAllowStaticLighting && MeshBatch.LCI && bIsLitMaterial)
		? MeshBatch.LCI->GetLightMapInteraction(FeatureLevel)
		: FLightMapInteraction();

	// force LQ light maps based on system settings
	const bool bPlatformAllowsHighQualityLightMaps = AllowHighQualityLightmaps(FeatureLevel);
	const bool bAllowHighQualityLightMaps = bPlatformAllowsHighQualityLightMaps && LightMapInteraction.AllowsHighQualityLightmaps();

	const bool bAllowIndirectLightingCache = Scene && Scene->PrecomputedLightVolumes.Num() > 0;
	const bool bUseVolumetricLightmap = Scene && Scene->VolumetricLightmapSceneData.HasData();

	static const auto CVarSupportLowQualityLightmap = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.SupportLowQualityLightmaps"));
	const bool bAllowLowQualityLightMaps = (!CVarSupportLowQualityLightmap) || (CVarSupportLowQualityLightmap->GetValueOnAnyThread() != 0);

	// Determine light map policy type
	FUniformLightMapPolicy LightMapPolicy = FUniformLightMapPolicy(LMP_NO_LIGHTMAP);
	if (LightMapInteraction.GetType() == LMIT_Texture)
	{
		if (bAllowHighQualityLightMaps)
		{
			const FShadowMapInteraction ShadowMapInteraction = (bAllowStaticLighting && MeshBatch.LCI && bIsLitMaterial)
				? MeshBatch.LCI->GetShadowMapInteraction(FeatureLevel)
				: FShadowMapInteraction();

			if (ShadowMapInteraction.GetType() == SMIT_Texture)
			{
				LightMapPolicy = FUniformLightMapPolicy(LMP_DISTANCE_FIELD_SHADOWS_AND_HQ_LIGHTMAP);
			}
			else
			{
				LightMapPolicy = FUniformLightMapPolicy(LMP_HQ_LIGHTMAP);
			}
		}
		else if (bAllowLowQualityLightMaps)
		{
			LightMapPolicy = FUniformLightMapPolicy(LMP_LQ_LIGHTMAP);
		}
	}
	else
	{
		if (bIsLitMaterial
			&& bAllowStaticLighting
			&& Scene
			&& Scene->VolumetricLightmapSceneData.HasData()
			&& PrimitiveSceneProxy
			&& (PrimitiveSceneProxy->IsMovable()
				|| PrimitiveSceneProxy->NeedsUnbuiltPreviewLighting()
				|| PrimitiveSceneProxy->GetLightmapType() == ELightmapType::ForceVolumetric))
		{
			LightMapPolicy = FUniformLightMapPolicy(LMP_PRECOMPUTED_IRRADIANCE_VOLUME_INDIRECT_LIGHTING);
		}
		else if (bIsLitMaterial
			&& IsIndirectLightingCacheAllowed(FeatureLevel)
			&& Scene
			&& Scene->PrecomputedLightVolumes.Num() > 0
			&& PrimitiveSceneProxy)
		{
			const FIndirectLightingCacheAllocation* IndirectLightingCacheAllocation = PrimitiveSceneProxy->GetPrimitiveSceneInfo()->IndirectLightingCacheAllocation;
			const bool bPrimitiveIsMovable = PrimitiveSceneProxy->IsMovable();
			const bool bPrimitiveUsesILC = PrimitiveSceneProxy->GetIndirectLightingCacheQuality() != ILCQ_Off;

			// Use the indirect lighting cache shaders if the object has a cache allocation
			// This happens for objects with unbuilt lighting
			if (bPrimitiveUsesILC &&
				((IndirectLightingCacheAllocation && IndirectLightingCacheAllocation->IsValid())
					// Use the indirect lighting cache shaders if the object is movable, it may not have a cache allocation yet because that is done in InitViews
					// And movable objects are sometimes rendered in the static draw lists
					|| bPrimitiveIsMovable))
			{
				if (CanIndirectLightingCacheUseVolumeTexture(FeatureLevel)
					&& ((IndirectLightingCacheAllocation && !IndirectLightingCacheAllocation->bPointSample)
						|| (bPrimitiveIsMovable && PrimitiveSceneProxy->GetIndirectLightingCacheQuality() == ILCQ_Volume)))
				{
					// Use a light map policy that supports reading indirect lighting from a volume texture for dynamic objects
					LightMapPolicy = FUniformLightMapPolicy(LMP_CACHED_VOLUME_INDIRECT_LIGHTING);
				}
				else
				{
					// Use a light map policy that supports reading indirect lighting from a single SH sample
					LightMapPolicy = FUniformLightMapPolicy(LMP_CACHED_POINT_INDIRECT_LIGHTING);
				}
			}
		}
	}

	TShaderMapRef<FNaniteMaterialVS> NaniteVertexShader(GetGlobalShaderMap(FeatureLevel));
	TShaderRef<TBasePassPixelShaderPolicyParamType<FUniformLightMapPolicy>> BasePassPixelShader;

	bool bShadersValid = GetBasePassShaders<FUniformLightMapPolicy>(
		Material,
		MeshBatch.VertexFactory->GetType(),
		LightMapPolicy,
		FeatureLevel,
		bRenderSkyAtmosphere,
		bRenderSkylight,
		false,
		nullptr,
		&BasePassPixelShader
		);

	if (!bShadersValid)
	{
		return false;
	}

	TMeshProcessorShaders
		<
		FNaniteMaterialVS,
		TBasePassPixelShaderPolicyParamType<FUniformLightMapPolicy>
		>
		PassShaders;

	PassShaders.VertexShader = NaniteVertexShader;
	PassShaders.PixelShader = BasePassPixelShader;

	TBasePassShaderElementData<FUniformLightMapPolicy> ShaderElementData(MeshBatch.LCI);
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, nullptr, MeshBatch, -1, false);

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		nullptr,
		MaterialRenderProxy,
		Material,
		PassDrawRenderState,
		PassShaders,
		FM_Solid,
		CM_None,
		FMeshDrawCommandSortKey::Default,
		EMeshPassFeatures::Default,
		ShaderElementData
	);

	return true;
}

FMeshPassProcessor* CreateNaniteMeshProcessor(
	const FScene* Scene,
	const FSceneView* InViewIfDynamicMeshCommand,
	FMeshPassDrawListContext* InDrawListContext
)
{
	FMeshPassProcessorRenderState PassDrawRenderState;
	PassDrawRenderState.SetNaniteUniformBuffer(Scene->UniformBuffers.NaniteUniformBuffer);

	const bool bStencilExport = (NANITE_MATERIAL_STENCIL != 0) && !UseComputeDepthExport();
	if (bStencilExport)
	{
		SetupBasePassState(FExclusiveDepthStencil::DepthWrite_StencilWrite, false, PassDrawRenderState);
		PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Equal, true, CF_Equal>::GetRHI());
		PassDrawRenderState.SetDepthStencilAccess(FExclusiveDepthStencil::DepthWrite_StencilWrite);
		PassDrawRenderState.SetStencilRef(STENCIL_SANDBOX_MASK);
	}
	else
	{
		SetupBasePassState(FExclusiveDepthStencil::DepthWrite_StencilNop, false, PassDrawRenderState);
		PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Equal>::GetRHI());
		PassDrawRenderState.SetDepthStencilAccess(FExclusiveDepthStencil::DepthWrite_StencilNop);
	}

	return new(FMemStack::Get()) FNaniteMeshProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, PassDrawRenderState, InDrawListContext);
}

class FSubmitNaniteMaterialPassCommandsAnyThreadTask : public FRenderTask
{
	FRHICommandList& RHICmdList;
	FRHIBuffer* MaterialIndirectArgs = nullptr;
	const TArray<FNaniteMaterialPassCommand, SceneRenderingAllocator>& NaniteMaterialPassCommands;
	TShaderMapRef<FNaniteMaterialVS> NaniteVertexShader;
	FIntRect ViewRect;
	uint32 TileCount;
	int32 TaskIndex;
	int32 TaskNum;

public:

	FSubmitNaniteMaterialPassCommandsAnyThreadTask(
		FRHICommandList& InRHICmdList,
		FRHIBuffer* InMaterialIndirectArgs,
		TArray<FNaniteMaterialPassCommand, SceneRenderingAllocator>& InNaniteMaterialPassCommands,
		TShaderMapRef<FNaniteMaterialVS> InNaniteVertexShader,
		const FIntRect& InViewRect,
		uint32 InTileCount,
		int32 InTaskIndex,
		int32 InTaskNum
	)
		: RHICmdList(InRHICmdList)
		, MaterialIndirectArgs(InMaterialIndirectArgs)
		, NaniteMaterialPassCommands(InNaniteMaterialPassCommands)
		, NaniteVertexShader(InNaniteVertexShader)
		, ViewRect(InViewRect)
		, TileCount(InTileCount)
		, TaskIndex(InTaskIndex)
		, TaskNum(InTaskNum)
	{}

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FSubmitNaniteMaterialPassCommandsAnyThreadTask, STATGROUP_TaskGraphTasks);
	}

	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		FOptionalTaskTagScope Scope(ETaskTag::EParallelRenderingThread);
		TRACE_CPUPROFILER_EVENT_SCOPE(SubmitNaniteMaterialPassCommandsAnyThreadTask);
		checkSlow(RHICmdList.IsInsideRenderPass());

		// FDrawVisibleMeshCommandsAnyThreadTasks must only run on RT if RHISupportsMultithreadedShaderCreation is not supported!
		check(IsInRenderingThread() || RHISupportsMultithreadedShaderCreation(GMaxRHIShaderPlatform));

		// Recompute draw range.
		const int32 DrawNum = NaniteMaterialPassCommands.Num();
		const int32 NumDrawsPerTask = TaskIndex < DrawNum ? FMath::DivideAndRoundUp(DrawNum, TaskNum) : 0;
		const int32 StartIndex = TaskIndex * NumDrawsPerTask;
		const int32 NumDraws = FMath::Min(NumDrawsPerTask, DrawNum - StartIndex);

		RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

		FMeshDrawCommandStateCache StateCache;
		FGraphicsMinimalPipelineStateSet GraphicsMinimalPipelineStateSet;
		for (int32 IterIndex = 0; IterIndex < NumDraws; ++IterIndex)
		{
			const FNaniteMaterialPassCommand& MaterialPassCommand = NaniteMaterialPassCommands[StartIndex + IterIndex];
			SubmitNaniteMaterialPassCommand(MaterialPassCommand, NaniteVertexShader, GraphicsMinimalPipelineStateSet, TileCount, RHICmdList, MaterialIndirectArgs, StateCache);
		}

		RHICmdList.EndRenderPass();

		// Make sure completion of this thread is extended for RT dependent tasks such as PSO creation is done
		// before kicking the next task
		RHICmdList.HandleRTThreadTaskCompletion(MyCompletionGraphEvent);
	}
};

static void BuildNaniteMaterialPassCommands(
	FRHICommandListImmediate& RHICmdList,
	const FNaniteMaterialCommands& MaterialCommands,
	TArray<FNaniteMaterialPassCommand, SceneRenderingAllocator>& OutNaniteMaterialPassCommands)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(BuildNaniteMaterialPassCommands);

	const FNaniteMaterialEntryMap& BucketMap = MaterialCommands.GetCommands();
	OutNaniteMaterialPassCommands.Reset(BucketMap.Num());

	FGraphicsMinimalPipelineStateSet GraphicsMinimalPipelineStateSet;

	// Pull into local here so another thread can't change the sort values mid-iteration.
	const int32 MaterialSortMode = GNaniteMaterialSortMode;

	for (auto& Command : BucketMap)
	{
		FNaniteMaterialPassCommand PassCommand(Command.Key);

		const FNaniteMaterialCommands::FCommandId CommandId = MaterialCommands.FindIdByCommand(Command.Key);

		const int32 MaterialId = CommandId.GetIndex();
		PassCommand.MaterialDepth = FNaniteCommandInfo::GetDepthId(MaterialId);
		PassCommand.MaterialSlot  = Command.Value.MaterialSlot;

		if (MaterialSortMode == 2 && GRHISupportsPipelineStateSortKey)
		{
			const FMeshDrawCommand& MeshDrawCommand = Command.Key;
			const FGraphicsMinimalPipelineStateInitializer& MeshPipelineState = MeshDrawCommand.CachedPipelineId.GetPipelineState(GraphicsMinimalPipelineStateSet);

			FGraphicsPipelineStateInitializer GraphicsPSOInit = MeshPipelineState.AsGraphicsPipelineStateInitializer();
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

			FGraphicsPipelineState* PipelineState = PipelineStateCache::GetAndOrCreateGraphicsPipelineState(RHICmdList, GraphicsPSOInit, EApplyRendertargetOption::DoNothing);
			if (PipelineState)
			{
				const uint64 StateSortKey = PipelineStateCache::RetrieveGraphicsPipelineStateSortKey(PipelineState);
				if (StateSortKey != 0) // 0 on the first occurrence (prior to caching), so these commands will fall back on shader id for sorting.
				{
					PassCommand.SortKey = StateSortKey;
				}
			}
		}
		else if (MaterialSortMode == 3)
		{
			// Use reference count as the sort key
			PassCommand.SortKey = uint64(Command.Value.ReferenceCount.load());
		}

		OutNaniteMaterialPassCommands.Emplace(PassCommand);
	}

	if (MaterialSortMode != 0)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Sort);
		OutNaniteMaterialPassCommands.Sort();
	}
}

void DrawNaniteMaterialPasses(
	const FSceneRenderer& SceneRenderer,
	const FScene& Scene,
	const FViewInfo& View,
	const uint32 TileCount,
	const bool bParallelBuild,
	const FParallelCommandListBindings& ParallelBindings,
	TShaderMapRef<FNaniteMaterialVS> VertexShader,
	FRHICommandListImmediate& RHICmdListImmediate,
	FRHIBuffer* MaterialIndirectArgs,
	TArray<FNaniteMaterialPassCommand, SceneRenderingAllocator>& MaterialPassCommands
)
{
	BuildNaniteMaterialPassCommands(RHICmdListImmediate, Scene.NaniteMaterials[ENaniteMeshPass::BasePass], MaterialPassCommands);

	if (MaterialPassCommands.Num())
	{
		if (bParallelBuild)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(BuildParallelCommandListSet);

			// Parallel set will be executed when object goes out of scope
			FRDGParallelCommandListSet ParallelCommandListSet(RHICmdListImmediate, GET_STATID(STAT_CLP_NaniteBasePass), SceneRenderer, View, ParallelBindings);

			// Force high prio so it's not preempted by another high prio task
			ParallelCommandListSet.SetHighPriority();

			// Distribute work evenly to the available task graph workers based on NumPassCommands.
			const int32 NumPassCommands = MaterialPassCommands.Num();
			const int32 NumThreads = FMath::Min<int32>(FTaskGraphInterface::Get().GetNumWorkerThreads(), ParallelCommandListSet.Width);
			const int32 NumTasks = FMath::Min<int32>(NumThreads, FMath::DivideAndRoundUp(NumPassCommands, ParallelCommandListSet.MinDrawsPerCommandList));
			const int32 NumDrawsPerTask = FMath::DivideAndRoundUp(NumPassCommands, NumTasks);

			const ENamedThreads::Type RenderThread = ENamedThreads::GetRenderThread();

			// Assume on demand shader creation is enabled for platforms supporting Nanite
			// otherwise there might be issues with PSO creation on a task which is not running on the RenderThread
			// So task prerequisites can be empty (MeshDrawCommands task has prereq on FMeshDrawCommandInitResourcesTask which calls LazilyInitShaders on all shader)
			ensure(FParallelMeshDrawCommandPass::IsOnDemandShaderCreationEnabled());
			FGraphEventArray EmptyPrereqs;

			for (int32 TaskIndex = 0; TaskIndex < NumTasks; TaskIndex++)
			{
				const int32 StartIndex = TaskIndex * NumDrawsPerTask;
				const int32 NumDraws = FMath::Min(NumDrawsPerTask, NumPassCommands - StartIndex);
				checkSlow(NumDraws > 0);

				FRHICommandList* CmdList = ParallelCommandListSet.NewParallelCommandList();

				FGraphEventRef AnyThreadCompletionEvent = TGraphTask<FSubmitNaniteMaterialPassCommandsAnyThreadTask>::CreateTask(&EmptyPrereqs, RenderThread).
					ConstructAndDispatchWhenReady(*CmdList, MaterialIndirectArgs, MaterialPassCommands, VertexShader, View.ViewRect, TileCount, TaskIndex, NumTasks);

				ParallelCommandListSet.AddParallelCommandList(CmdList, AnyThreadCompletionEvent, NumDraws);
			}
		}
		else
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(SubmitNaniteMaterialPassCommands);

			FGraphicsMinimalPipelineStateSet GraphicsMinimalPipelineStateSet;
			FMeshDrawCommandStateCache StateCache;
			for (auto CommandsIt = MaterialPassCommands.CreateConstIterator(); CommandsIt; ++CommandsIt)
			{
				SubmitNaniteMaterialPassCommand(*CommandsIt, VertexShader, GraphicsMinimalPipelineStateSet, TileCount, RHICmdListImmediate, MaterialIndirectArgs, StateCache);
			}
		}
	}
}