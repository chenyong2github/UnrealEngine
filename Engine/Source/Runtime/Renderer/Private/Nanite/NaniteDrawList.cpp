// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteDrawList.h"
#include "NaniteSceneProxy.h"
#include "SceneUtils.h"
#include "ScenePrivate.h"
#include "MeshPassProcessor.inl"

int32 GNaniteMaterialSortMode = 4;
static FAutoConsoleVariableRef CVarNaniteMaterialSortMode(
	TEXT("r.Nanite.MaterialSortMode"),
	GNaniteMaterialSortMode,
	TEXT("Method of sorting Nanite material draws. 0=disabled, 1=shader, 2=sortkey, 3=refcount"),
	ECVF_RenderThreadSafe
);

FMeshDrawCommand& FNaniteDrawListContext::AddCommand(FMeshDrawCommand& Initializer, uint32 NumElements)
{
	checkf(CurrentPrimitiveSceneInfo != nullptr, TEXT("BeginPrimitiveSceneInfo() must be called on the context before adding commands"));
	checkf(CurrentMeshPass < ENaniteMeshPass::Num, TEXT("BeginMeshPass() must be called on the context before adding commands"));

	{
		MeshDrawCommandForStateBucketing.~FMeshDrawCommand();
		new(&MeshDrawCommandForStateBucketing) FMeshDrawCommand();
	}

	MeshDrawCommandForStateBucketing = Initializer;
	return MeshDrawCommandForStateBucketing;
}

void FNaniteDrawListContext::BeginPrimitiveSceneInfo(FPrimitiveSceneInfo& PrimitiveSceneInfo)
{
	checkf(CurrentPrimitiveSceneInfo == nullptr, TEXT("BeginPrimitiveSceneInfo() was called without a matching EndPrimitiveSceneInfo()"));
	check(PrimitiveSceneInfo.Proxy->IsNaniteMesh());

	Nanite::FSceneProxyBase* NaniteSceneProxy = static_cast<Nanite::FSceneProxyBase*>(PrimitiveSceneInfo.Proxy);

	const TArray<Nanite::FSceneProxyBase::FMaterialSection>& MaterialSections = NaniteSceneProxy->GetMaterialSections();

	// Initialize material slots
	for (int32 NaniteMeshPassIndex = 0; NaniteMeshPassIndex < ENaniteMeshPass::Num; ++NaniteMeshPassIndex)
	{
		check(PrimitiveSceneInfo.NaniteCommandInfos[NaniteMeshPassIndex].Num() == 0);
		check(PrimitiveSceneInfo.NaniteRasterBins[NaniteMeshPassIndex].Num() == 0);

		TArray<FNaniteMaterialSlot>& MaterialSlots = PrimitiveSceneInfo.NaniteMaterialSlots[NaniteMeshPassIndex];
		check(MaterialSlots.Num() == 0);

		MaterialSlots.SetNumUninitialized(MaterialSections.Num());
		FMemory::Memset(MaterialSlots.GetData(), 0xFF, MaterialSlots.Num() * MaterialSlots.GetTypeSize());
	}

#if WITH_EDITOR
	// Initialize hit proxy IDs
	check(PrimitiveSceneInfo.NaniteHitProxyIds.Num() == 0);
	const TConstArrayView<const FHitProxyId> HitProxyIds = NaniteSceneProxy->GetHitProxyIds();
	PrimitiveSceneInfo.NaniteHitProxyIds.SetNumUninitialized(HitProxyIds.Num());
	for (int32 IdIndex = 0; IdIndex < HitProxyIds.Num(); ++IdIndex)
	{
		PrimitiveSceneInfo.NaniteHitProxyIds[IdIndex] = HitProxyIds[IdIndex].GetColor().ToPackedABGR();
	}
#endif

	CurrentPrimitiveSceneInfo = &PrimitiveSceneInfo;
}

void FNaniteDrawListContext::EndPrimitiveSceneInfo()
{
	checkf(CurrentPrimitiveSceneInfo != nullptr, TEXT("EndPrimitiveSceneInfo() was called without matching BeginPrimitiveSceneInfo()"));
	CurrentPrimitiveSceneInfo = nullptr;
}

void FNaniteDrawListContext::BeginMeshPass(ENaniteMeshPass::Type MeshPass)
{
	checkf(CurrentMeshPass == ENaniteMeshPass::Num, TEXT("BeginMeshPass() was called without a matching EndMeshPass()"));
	check(MeshPass < ENaniteMeshPass::Num);
	CurrentMeshPass = MeshPass;
}

void FNaniteDrawListContext::EndMeshPass()
{
	checkf(CurrentMeshPass < ENaniteMeshPass::Num, TEXT("EndMeshPass() was called without matching BeginMeshPass()"));
	CurrentMeshPass = ENaniteMeshPass::Num;
}

void FNaniteDrawListContext::AddShadingCommand(FPrimitiveSceneInfo& PrimitiveSceneInfo, const FNaniteCommandInfo& ShadingCommand, ENaniteMeshPass::Type MeshPass, uint8 SectionIndex)
{
	PrimitiveSceneInfo.NaniteCommandInfos[MeshPass].Add(ShadingCommand);

	TArray<FNaniteMaterialSlot>& MaterialSlots = PrimitiveSceneInfo.NaniteMaterialSlots[MeshPass];
	check(SectionIndex < uint32(MaterialSlots.Num()));

	FNaniteMaterialSlot& MaterialSlot = MaterialSlots[SectionIndex];
	check(MaterialSlot.ShadingId == 0xFFFFu);
	PrimitiveSceneInfo.NaniteMaterialSlots[MeshPass][SectionIndex].ShadingId = uint16(ShadingCommand.GetMaterialSlot());
}

void FNaniteDrawListContext::AddRasterBin(FPrimitiveSceneInfo& PrimitiveSceneInfo, const FNaniteRasterBin& RasterBin, ENaniteMeshPass::Type MeshPass, uint8 SectionIndex)
{
	PrimitiveSceneInfo.NaniteRasterBins[MeshPass].Add(RasterBin);

	TArray<FNaniteMaterialSlot>& MaterialSlots = PrimitiveSceneInfo.NaniteMaterialSlots[MeshPass];
	check(SectionIndex < uint32(MaterialSlots.Num()));

	FNaniteMaterialSlot& MaterialSlot = MaterialSlots[SectionIndex];
	check(MaterialSlot.RasterId == 0xFFFFu);
	PrimitiveSceneInfo.NaniteMaterialSlots[MeshPass][SectionIndex].RasterId = RasterBin.BinIndex;
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
	checkf(CurrentPrimitiveSceneInfo != nullptr, TEXT("BeginPrimitiveSceneInfo() must be called on the context before finalizing commands"));
	checkf(CurrentMeshPass < ENaniteMeshPass::Num, TEXT("BeginMeshPass() must be called on the context before finalizing commands"));

	FGraphicsMinimalPipelineStateId PipelineId;
	PipelineId = FGraphicsMinimalPipelineStateId::GetPersistentId(PipelineState);
	MeshDrawCommand.SetDrawParametersAndFinalize(MeshBatch, BatchElementIndex, PipelineId, ShadersForDebugging);
#if UE_BUILD_DEBUG
	FMeshDrawCommand MeshDrawCommandDebug = FMeshDrawCommand(MeshDrawCommand);
	check(MeshDrawCommandDebug.ShaderBindings.GetDynamicInstancingHash() == MeshDrawCommand.ShaderBindings.GetDynamicInstancingHash());
	check(MeshDrawCommandDebug.GetDynamicInstancingHash() == MeshDrawCommand.GetDynamicInstancingHash());
#endif

#if MESH_DRAW_COMMAND_DEBUG_DATA
	// When using state buckets, multiple PrimitiveSceneProxies can use the same 
	// MeshDrawCommand, so The PrimitiveSceneProxy pointer can't be stored.
	MeshDrawCommand.ClearDebugPrimitiveSceneProxy();
#endif

#if WITH_DEBUG_VIEW_MODES
	uint32 NumPSInstructions = 0;
	uint32 NumVSInstructions = 0;
	if (ShadersForDebugging != nullptr)
	{
		NumPSInstructions = ShadersForDebugging->PixelShader->GetNumInstructions();
		NumVSInstructions = ShadersForDebugging->VertexShader->GetNumInstructions();
	}

	const uint32 InstructionCount = static_cast<uint32>(NumPSInstructions << 16u | NumVSInstructions);
#endif

	// Defer the command
	DeferredCommands[CurrentMeshPass].Add(
		FDeferredCommand {
			CurrentPrimitiveSceneInfo,
			MeshDrawCommand,
			FNaniteMaterialEntryMap::ComputeHash(MeshDrawCommand),
		#if WITH_DEBUG_VIEW_MODES
			InstructionCount,
		#endif
			MeshBatch.SegmentIndex
		}
	);
}

void FNaniteDrawListContext::Apply(FScene& Scene)
{
	check(IsInRenderingThread());

	for (int32 MeshPass = 0; MeshPass < ENaniteMeshPass::Num; ++MeshPass)
	{
		FNaniteMaterialCommands& ShadingCommands = Scene.NaniteMaterials[MeshPass];
		FNaniteRasterPipelines& RasterPipelines  = Scene.NaniteRasterPipelines[MeshPass];

		for (auto& Command : DeferredCommands[MeshPass])
		{
			uint32 InstructionCount = 0;
		#if WITH_DEBUG_VIEW_MODES

			InstructionCount = Command.InstructionCount;
		#endif
			FPrimitiveSceneInfo* PrimitiveSceneInfo = Command.PrimitiveSceneInfo;
			FNaniteCommandInfo CommandInfo = ShadingCommands.Register(Command.MeshDrawCommand, Command.CommandHash, InstructionCount);
			AddShadingCommand(*PrimitiveSceneInfo, CommandInfo, ENaniteMeshPass::Type(MeshPass), Command.SectionIndex);
		}

		for (auto& Pipeline : DeferredPipelines[MeshPass])
		{
			FPrimitiveSceneInfo* PrimitiveSceneInfo = Pipeline.PrimitiveSceneInfo;
			const FNaniteRasterBin RasterBin = RasterPipelines.Register(Pipeline.RasterPipeline);
			AddRasterBin(*PrimitiveSceneInfo, RasterBin, ENaniteMeshPass::Type(MeshPass), Pipeline.SectionIndex);
		}
	}
}

void SubmitNaniteIndirectMaterial(
	const FNaniteMaterialPassCommand& MaterialPassCommand,
	const TShaderMapRef<FNaniteIndirectMaterialVS>& VertexShader,
	const FGraphicsMinimalPipelineStateSet& GraphicsMinimalPipelineStateSet,
	const uint32 InstanceFactor,
	FRHICommandList& RHICmdList,
	FRHIBuffer* MaterialIndirectArgs,
	FMeshDrawCommandStateCache& StateCache)
{
	const FMeshDrawCommand& MeshDrawCommand	= MaterialPassCommand.MeshDrawCommand;
	const float MaterialDepth				= MaterialPassCommand.MaterialDepth;
	const int32 MaterialSlot				= MaterialPassCommand.MaterialSlot;

#if WANTS_DRAW_MESH_EVENTS
	FMeshDrawCommand::FMeshDrawEvent MeshEvent(MeshDrawCommand, InstanceFactor, RHICmdList);
#endif
	FMeshDrawCommand::SubmitDrawIndirectBegin(MeshDrawCommand, GraphicsMinimalPipelineStateSet, nullptr, 0, InstanceFactor, RHICmdList, StateCache);

	// All Nanite mesh draw commands are using the same vertex shader, which has a material depth parameter we assign at render time.
	{
		FNaniteIndirectMaterialVS::FParameters Parameters;
		Parameters.MaterialDepth = MaterialDepth;
		Parameters.MaterialSlot = uint32(MaterialSlot);
		Parameters.TileRemapCount = FMath::DivideAndRoundUp(InstanceFactor, 32u);
		SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), Parameters);
	}

	check(MaterialIndirectArgs == nullptr || MaterialSlot != INDEX_NONE);
	const uint32 MaterialSlotIndirectOffset = MaterialIndirectArgs != nullptr ? sizeof(FRHIDrawIndexedIndirectParameters) * uint32(MaterialSlot) : 0;
	FMeshDrawCommand::SubmitDrawIndirectEnd(MeshDrawCommand, InstanceFactor, RHICmdList, MaterialIndirectArgs, MaterialSlotIndirectOffset);
}

void SubmitNaniteMultiViewMaterial(
	const FMeshDrawCommand& MeshDrawCommand,
	const float MaterialDepth,
	const TShaderMapRef<FNaniteMultiViewMaterialVS>& VertexShader,
	const FGraphicsMinimalPipelineStateSet& GraphicsMinimalPipelineStateSet,
	const uint32 InstanceFactor,
	FRHICommandList& RHICmdList,
	FMeshDrawCommandStateCache& StateCache,
	uint32 InstanceBaseOffset)
{
#if WANTS_DRAW_MESH_EVENTS
	FMeshDrawCommand::FMeshDrawEvent MeshEvent(MeshDrawCommand, InstanceFactor, RHICmdList);
#endif

	FMeshDrawCommand::SubmitDrawBegin(MeshDrawCommand, GraphicsMinimalPipelineStateSet, nullptr, 0, InstanceFactor, RHICmdList, StateCache);

	// All Nanite mesh draw commands are using the same vertex shader, which has a material depth parameter we assign at render time.
	{
		FNaniteMultiViewMaterialVS::FParameters Parameters;
		Parameters.MaterialDepth = MaterialDepth;
		Parameters.InstanceBaseOffset = InstanceBaseOffset;
		SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), Parameters);
	}

	FMeshDrawCommand::SubmitDrawEnd(MeshDrawCommand, InstanceFactor, RHICmdList);
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

	check(Nanite::IsSupportedBlendMode(BlendMode));
	check(Nanite::IsSupportedMaterialDomain(Material.GetMaterialDomain()));

	const bool bRenderSkylight = Scene && Scene->ShouldRenderSkylightInBasePass(BlendMode) && ShadingModels != MSM_Unlit;

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

	TShaderMapRef<FNaniteIndirectMaterialVS> NaniteVertexShader(GetGlobalShaderMap(FeatureLevel));
	TShaderRef<TBasePassPixelShaderPolicyParamType<FUniformLightMapPolicy>> BasePassPixelShader;

	bool bShadersValid = GetBasePassShaders<FUniformLightMapPolicy>(
		Material,
		MeshBatch.VertexFactory->GetType(),
		LightMapPolicy,
		FeatureLevel,
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
		FNaniteIndirectMaterialVS,
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

	return new FNaniteMeshProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, PassDrawRenderState, InDrawListContext);
}

class FSubmitNaniteMaterialPassCommandsAnyThreadTask : public FRenderTask
{
	FRHICommandList& RHICmdList;
	FRHIBuffer* MaterialIndirectArgs = nullptr;
	TArrayView<FNaniteMaterialPassCommand const> NaniteMaterialPassCommands;
	TShaderMapRef<FNaniteIndirectMaterialVS> NaniteVertexShader;
	FIntRect ViewRect;
	uint32 TileCount;
	int32 TaskIndex;
	int32 TaskNum;

public:

	FSubmitNaniteMaterialPassCommandsAnyThreadTask(
		FRHICommandList& InRHICmdList,
		FRHIBuffer* InMaterialIndirectArgs,
		TArrayView<FNaniteMaterialPassCommand const> InNaniteMaterialPassCommands,
		TShaderMapRef<FNaniteIndirectMaterialVS> InNaniteVertexShader,
		FIntRect InViewRect,
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

		// check for the multithreaded shader creation has been moved to FShaderCodeArchive::CreateShader() 

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
			SubmitNaniteIndirectMaterial(MaterialPassCommand, NaniteVertexShader, GraphicsMinimalPipelineStateSet, TileCount, RHICmdList, MaterialIndirectArgs, StateCache);
		}

		RHICmdList.EndRenderPass();

		// Make sure completion of this thread is extended for RT dependent tasks such as PSO creation is done
		// before kicking the next task
		RHICmdList.HandleRTThreadTaskCompletion(MyCompletionGraphEvent);
	}
};

void BuildNaniteMaterialPassCommands(
	const FGraphicsPipelineRenderTargetsInfo& RenderTargetsInfo,
	const FNaniteMaterialCommands& MaterialCommands,
	TArray<FNaniteMaterialPassCommand, SceneRenderingAllocator>& OutNaniteMaterialPassCommands)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(BuildNaniteMaterialPassCommands);

	const FNaniteMaterialEntryMap& BucketMap = MaterialCommands.GetCommands();
	checkf(OutNaniteMaterialPassCommands.Max() >= BucketMap.Num(), TEXT("Nanite mesh commands must be resized on the render thread prior to calling this method."));

	// Pull into local here so another thread can't change the sort values mid-iteration.
	const int32 MaterialSortMode = GNaniteMaterialSortMode;
	for (auto Iter = BucketMap.begin(); Iter != BucketMap.end(); ++Iter)
	{
		auto& Command = *Iter;
		const FMeshDrawCommand& MeshDrawCommand = Command.Key;
		FNaniteMaterialPassCommand PassCommand(MeshDrawCommand);
		const int32 MaterialId = Iter.GetElementId().GetIndex();

		PassCommand.MaterialDepth = FNaniteCommandInfo::GetDepthId(MaterialId);
		PassCommand.MaterialSlot  = Command.Value.MaterialSlot;

		if (MaterialSortMode == 2)
		{
			PassCommand.SortKey = MeshDrawCommand.GetPipelineStateSortingKey(RenderTargetsInfo);
		}
		else if (MaterialSortMode == 3)
		{
			// Use reference count as the sort key
			PassCommand.SortKey = uint64(Command.Value.ReferenceCount);
		}
		else if(MaterialSortMode == 4)
		{
			// TODO: Remove other sort modes and just use 4 (needs more optimization/profiling)?
			// Sort by pipeline state, but use hash of MaterialId for randomized tie-breaking.
			// This spreads out the empty draws inside the pipeline buckets and improves overall utilization.
			const uint64 PipelineSortKey = MeshDrawCommand.GetPipelineStateSortingKey(RenderTargetsInfo);
			const uint32 PipelineSortKeyHash = GetTypeHash(PipelineSortKey);
			const uint32 MaterialHash = MurmurFinalize32(MaterialId);
			PassCommand.SortKey = ((uint64)PipelineSortKeyHash << 32) | MaterialHash;
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
	FRDGParallelCommandListSet* ParallelCommandListSet,
	FRHICommandList& RHICmdList,
	const FIntRect ViewRect,
	const uint32 TileCount,
	TShaderMapRef<FNaniteIndirectMaterialVS> VertexShader,
	FRDGBuffer* MaterialIndirectArgs,
	TArrayView<FNaniteMaterialPassCommand const> MaterialPassCommands)
{
	check(!MaterialPassCommands.IsEmpty());

	MaterialIndirectArgs->MarkResourceAsUsed();

	if (ParallelCommandListSet)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ParallelSubmitNaniteMaterialPassCommands);

		// Distribute work evenly to the available task graph workers based on NumPassCommands.
		const int32 NumPassCommands = MaterialPassCommands.Num();
		const int32 NumThreads = FMath::Min<int32>(FTaskGraphInterface::Get().GetNumWorkerThreads(), ParallelCommandListSet->Width);
		const int32 NumTasks = FMath::Min<int32>(NumThreads, FMath::DivideAndRoundUp(NumPassCommands, ParallelCommandListSet->MinDrawsPerCommandList));
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

			FRHICommandList* CmdList = ParallelCommandListSet->NewParallelCommandList();

			FGraphEventRef AnyThreadCompletionEvent = TGraphTask<FSubmitNaniteMaterialPassCommandsAnyThreadTask>::CreateTask(&EmptyPrereqs, RenderThread).
				ConstructAndDispatchWhenReady(*CmdList, MaterialIndirectArgs->GetRHI(), MaterialPassCommands, VertexShader, ViewRect, TileCount, TaskIndex, NumTasks);

			ParallelCommandListSet->AddParallelCommandList(CmdList, AnyThreadCompletionEvent, NumDraws);
		}
	}
	else
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SubmitNaniteMaterialPassCommands);

		RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

		FGraphicsMinimalPipelineStateSet GraphicsMinimalPipelineStateSet;
		FMeshDrawCommandStateCache StateCache;
		for (const FNaniteMaterialPassCommand& Command : MaterialPassCommands)
		{
			SubmitNaniteIndirectMaterial(
				Command,
				VertexShader,
				GraphicsMinimalPipelineStateSet,
				TileCount,
				RHICmdList,
				MaterialIndirectArgs->GetRHI(),
				StateCache
			);
		}
	}
}