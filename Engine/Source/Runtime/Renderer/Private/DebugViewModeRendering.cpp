// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
DebugViewModeRendering.cpp: Contains definitions for rendering debug viewmodes.
=============================================================================*/

#include "DebugViewModeRendering.h"
#include "Materials/Material.h"
#include "ShaderComplexityRendering.h"
#include "PrimitiveDistanceAccuracyRendering.h"
#include "MeshTexCoordSizeAccuracyRendering.h"
#include "MaterialTexCoordScalesRendering.h"
#include "RequiredTextureResolutionRendering.h"
#include "ViewMode/LODColorationRendering.h"
#include "PrimitiveSceneInfo.h"
#include "ScenePrivate.h"
#include "PostProcessing.h"
#include "PostProcess/PostProcessVisualizeComplexity.h"
#include "PostProcess/PostProcessStreamingAccuracyLegend.h"
#include "CompositionLighting/PostProcessPassThrough.h"
#include "PostProcess/PostProcessSelectionOutline.h"
#include "PostProcess/PostProcessCompositeEditorPrimitives.h"
#include "PostProcess/PostProcessUpscale.h"
#include "PostProcess/TemporalAA.h"
#include "PostProcess/PostProcessInput.h"
#include "SceneRendering.h"
#include "DeferredShadingRenderer.h"
#include "MeshPassProcessor.inl"

IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FDebugViewModePassUniformParameters, "DebugViewModePass", SceneTextures);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

void SetupDebugViewModePassUniformBufferConstants(const FViewInfo& ViewInfo, FDebugViewModePassUniformParameters& PassParameters)
{
	// Accuracy colors
	{
		const int32 NumEngineColors = FMath::Min<int32>(GEngine->StreamingAccuracyColors.Num(), NumStreamingAccuracyColors);
		int32 ColorIndex = 0;
		for (; ColorIndex < NumEngineColors; ++ColorIndex)
		{
			PassParameters.AccuracyColors[ColorIndex] = GEngine->StreamingAccuracyColors[ColorIndex];
		}
		for (; ColorIndex < NumStreamingAccuracyColors; ++ColorIndex)
		{
			PassParameters.AccuracyColors[ColorIndex] = FLinearColor::Black;
		}
	}
	// LOD / HLOD colors
	{
		const TArray<FLinearColor>* Colors = nullptr;
		if (ViewInfo.Family->EngineShowFlags.LODColoration)
		{
			Colors = &(GEngine->LODColorationColors);
		}
		else if (ViewInfo.Family->EngineShowFlags.HLODColoration)
		{
			Colors = &GEngine->HLODColorationColors;
		}

		const int32 NumColors = Colors ? FMath::Min<int32>(NumLODColorationColors, Colors->Num()) : 0;
		int32 ColorIndex = 0;
		for (; ColorIndex < NumColors; ++ColorIndex)
		{
			PassParameters.LODColors[ColorIndex] = (*Colors)[ColorIndex];
		}
		for (; ColorIndex < NumLODColorationColors; ++ColorIndex)
		{
			PassParameters.LODColors[ColorIndex] = NumColors > 0 ? Colors->Last() : FLinearColor::Black;
		}
	}
}

TUniformBufferRef<FDebugViewModePassUniformParameters> CreateDebugViewModePassUniformBuffer(FRHICommandList& RHICmdList, const FViewInfo& View)
{
	FDebugViewModePassUniformParameters Parameters;
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	SetupSceneTextureUniformParameters(SceneContext, View.FeatureLevel, ESceneTextureSetupMode::None, Parameters.SceneTextures);
	SetupDebugViewModePassUniformBufferConstants(View, Parameters);
	return TUniformBufferRef<FDebugViewModePassUniformParameters>::CreateUniformBufferImmediate(Parameters, UniformBuffer_SingleFrame);
}

TRDGUniformBufferRef<FDebugViewModePassUniformParameters> CreateDebugViewModePassUniformBuffer(FRDGBuilder& GraphBuilder, const FViewInfo& View)
{
	auto* UniformBufferParameters = GraphBuilder.AllocParameters<FDebugViewModePassUniformParameters>();
	SetupSceneTextureUniformParameters(GraphBuilder, View.FeatureLevel, ESceneTextureSetupMode::None, UniformBufferParameters->SceneTextures);
	SetupDebugViewModePassUniformBufferConstants(View, *UniformBufferParameters);
	return GraphBuilder.CreateUniformBuffer(UniformBufferParameters);
}

IMPLEMENT_MATERIAL_SHADER_TYPE(,FDebugViewModeVS,TEXT("/Engine/Private/DebugViewModeVertexShader.usf"),TEXT("Main"),SF_Vertex);	
IMPLEMENT_MATERIAL_SHADER_TYPE(,FDebugViewModeHS,TEXT("/Engine/Private/DebugViewModeVertexShader.usf"),TEXT("MainHull"),SF_Hull);	
IMPLEMENT_MATERIAL_SHADER_TYPE(,FDebugViewModeDS,TEXT("/Engine/Private/DebugViewModeVertexShader.usf"),TEXT("MainDomain"),SF_Domain);

bool FDebugViewModeVS::ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
{
	return AllowDebugViewVSDSHS(Parameters.Platform) && EnumHasAllFlags(Parameters.Flags, EShaderPermutationFlags::HasEditorOnlyData);
}

BEGIN_SHADER_PARAMETER_STRUCT(FDebugViewModePassParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FDebugViewModePassUniformParameters, Pass)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void FDeferredShadingSceneRenderer::RenderDebugViewMode(FRDGBuilder& GraphBuilder, const FRenderTargetBindingSlots& RenderTargets)
{
	RDG_EVENT_SCOPE(GraphBuilder, "DebugViewMode");

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		FViewInfo& View = Views[ViewIndex];
		RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
		RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

		auto* PassParameters = GraphBuilder.AllocParameters<FDebugViewModePassParameters>();
		PassParameters->Pass = CreateDebugViewModePassUniformBuffer(GraphBuilder, View);
		PassParameters->RenderTargets = RenderTargets;

		GraphBuilder.AddPass(
			{},
			PassParameters,
			ERDGPassFlags::Raster,
			[this, &View](FRHICommandList& RHICmdList)
		{
			Scene->UniformBuffers.UpdateViewUniformBuffer(View);
			RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1);
			View.ParallelMeshDrawCommandPasses[EMeshPass::DebugViewMode].DispatchDraw(nullptr, RHICmdList);
		});
	}
}

FDebugViewModePS::FDebugViewModePS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer) : FMeshMaterialShader(Initializer)
{
	PassUniformBuffer.Bind(Initializer.ParameterMap, FDebugViewModePassUniformParameters::StaticStructMetadata.GetShaderVariableName());
}

void FDebugViewModePS::GetElementShaderBindings(
	const FShaderMapPointerTable& PointerTable,
	const FScene* Scene,
	const FSceneView* ViewIfDynamicMeshCommand,
	const FVertexFactory* VertexFactory,
	const EVertexInputStreamType InputStreamType,
	ERHIFeatureLevel::Type FeatureLevel,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const FMeshBatch& MeshBatch,
	const FMeshBatchElement& BatchElement,
	const FDebugViewModeShaderElementData& ShaderElementData,
	FMeshDrawSingleShaderBindings& ShaderBindings,
	FVertexInputStreamArray& VertexStreams) const
{
	FMeshMaterialShader::GetElementShaderBindings(PointerTable, Scene, ViewIfDynamicMeshCommand, VertexFactory, InputStreamType, FeatureLevel, PrimitiveSceneProxy, MeshBatch, BatchElement, ShaderElementData, ShaderBindings, VertexStreams);

	int8 VisualizeElementIndex = 0;
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	VisualizeElementIndex = BatchElement.VisualizeElementIndex;
#endif

	const FDebugViewModeInterface* Interface = FDebugViewModeInterface::GetInterface(ShaderElementData.DebugViewMode);
	if (ensure(Interface))
	{
		Interface->GetDebugViewModeShaderBindings(
			*this,
			PrimitiveSceneProxy,
			ShaderElementData.MaterialRenderProxy,
			ShaderElementData.Material,
			ShaderElementData.DebugViewMode,
			ShaderElementData.ViewOrigin,
			ShaderElementData.VisualizeLODIndex,
			VisualizeElementIndex,
			ShaderElementData.NumVSInstructions,
			ShaderElementData.NumPSInstructions,
			ShaderElementData.ViewModeParam,
			ShaderElementData.ViewModeParamName,
			ShaderBindings
		);
	}
}

FDebugViewModeMeshProcessor::FDebugViewModeMeshProcessor(
	const FScene* InScene, 
	ERHIFeatureLevel::Type InFeatureLevel,
	const FSceneView* InViewIfDynamicMeshCommand, 
	FRHIUniformBuffer* InPassUniformBuffer,
	bool bTranslucentBasePass,
	FMeshPassDrawListContext* InDrawListContext
)
	: FMeshPassProcessor(InScene, InFeatureLevel, InViewIfDynamicMeshCommand, InDrawListContext)
	, PassUniformBuffer(InPassUniformBuffer)
	, DebugViewMode(DVSM_None)
	, ViewModeParam(INDEX_NONE)
	, DebugViewModeInterface(nullptr)
{
	if (InViewIfDynamicMeshCommand)
	{
		DebugViewMode = InViewIfDynamicMeshCommand->Family->GetDebugViewShaderMode();
		ViewModeParam = InViewIfDynamicMeshCommand->Family->GetViewModeParam();
		ViewModeParamName = InViewIfDynamicMeshCommand->Family->GetViewModeParamName();

		ViewUniformBuffer = InViewIfDynamicMeshCommand->ViewUniformBuffer;

		DebugViewModeInterface = FDebugViewModeInterface::GetInterface(DebugViewMode);
	}
	if (InScene)
	{
		if (!ViewUniformBuffer)
		{
			ViewUniformBuffer = InScene->UniformBuffers.ViewUniformBuffer;
		}
	}
}

void AddDebugViewModeShaderTypes(ERHIFeatureLevel::Type FeatureLevel,
	EMaterialTessellationMode MaterialTessellationMode,
	const FVertexFactoryType* VertexFactoryType,
	FMaterialShaderTypes& OutShaderTypes)
{
	const bool bNeedsHSDS = RHISupportsTessellation(GShaderPlatformForFeatureLevel[FeatureLevel])
		&& VertexFactoryType->SupportsTessellationShaders()
		&& MaterialTessellationMode != MTM_NoTessellation;

	OutShaderTypes.AddShaderType<FDebugViewModeVS>();
	if (bNeedsHSDS)
	{
		OutShaderTypes.AddShaderType<FDebugViewModeDS>();
		OutShaderTypes.AddShaderType<FDebugViewModeHS>();
	}
}

void FDebugViewModeMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	if (!DebugViewModeInterface)
	{
		return;
	}

	const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
	const FMaterial* BatchMaterial = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
	if (!BatchMaterial)
	{
		return;
	}

	const FMaterial* Material = BatchMaterial;
	if (!DebugViewModeInterface->bNeedsMaterialProperties && FDebugViewModeInterface::AllowFallbackToDefaultMaterial(Material))
	{
		MaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
		Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
		check(Material);
	}

	FVertexFactoryType* VertexFactoryType = MeshBatch.VertexFactory->GetType();
	const EMaterialTessellationMode MaterialTessellationMode = Material->GetTessellationMode();

	FMaterialShaderTypes ShaderTypes;
	DebugViewModeInterface->AddShaderTypes(FeatureLevel, MaterialTessellationMode, VertexFactoryType, ShaderTypes);
	if (!Material->ShouldCacheShaders(ShaderTypes, VertexFactoryType))
	{
		return;
	}

	FMaterialShaders Shaders;
	if (!Material->TryGetShaders(ShaderTypes, VertexFactoryType, Shaders))
	{
		return;
	}

	TMeshProcessorShaders<FDebugViewModeVS,	FDebugViewModeHS, FDebugViewModeDS,	FDebugViewModePS> DebugViewModePassShaders;
	Shaders.TryGetVertexShader(DebugViewModePassShaders.VertexShader);
	Shaders.TryGetPixelShader(DebugViewModePassShaders.PixelShader);
	Shaders.TryGetHullShader(DebugViewModePassShaders.HullShader);
	Shaders.TryGetDomainShader(DebugViewModePassShaders.DomainShader);

	const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
	const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, *BatchMaterial, OverrideSettings);
	const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, *BatchMaterial, OverrideSettings);

	FMeshPassProcessorRenderState DrawRenderState;
	DrawRenderState.SetViewUniformBuffer(ViewUniformBuffer);
	DrawRenderState.SetPassUniformBuffer(PassUniformBuffer);

	FDebugViewModeInterface::FRenderState InterfaceRenderState;
	DebugViewModeInterface->SetDrawRenderState(BatchMaterial->GetBlendMode(), InterfaceRenderState, Scene ? (Scene->GetShadingPath() == EShadingPath::Deferred && Scene->EarlyZPassMode != DDM_NonMaskedOnly) : false);
	DrawRenderState.SetBlendState(InterfaceRenderState.BlendState);
	DrawRenderState.SetDepthStencilState(InterfaceRenderState.DepthStencilState);

	FDebugViewModeShaderElementData ShaderElementData(
		*MaterialRenderProxy,
		*Material,
		DebugViewMode, 
		ViewIfDynamicMeshCommand ? ViewIfDynamicMeshCommand->ViewMatrices.GetViewOrigin() : FVector::ZeroVector, 
		(ViewIfDynamicMeshCommand && ViewIfDynamicMeshCommand->Family->EngineShowFlags.HLODColoration) ? MeshBatch.VisualizeHLODIndex : MeshBatch.VisualizeLODIndex,
		ViewModeParam, 
		ViewModeParamName);

	// Shadermap can be null while shaders are compiling.
	if (DebugViewModeInterface->bNeedsInstructionCount)
	{
		UpdateInstructionCount(ShaderElementData, BatchMaterial, VertexFactoryType);
	}

	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, true);

	const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(DebugViewModePassShaders.VertexShader, DebugViewModePassShaders.PixelShader);

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		*MaterialRenderProxy,
		*Material,
		DrawRenderState,
		DebugViewModePassShaders,
		MeshFillMode,
		MeshCullMode,
		SortKey,
		EMeshPassFeatures::Default,
		ShaderElementData);
}

void FDebugViewModeMeshProcessor::UpdateInstructionCount(FDebugViewModeShaderElementData& OutShaderElementData, const FMaterial* InBatchMaterial, FVertexFactoryType* InVertexFactoryType)
{
	check(InBatchMaterial && InVertexFactoryType);

	if (Scene)
	{
		if (Scene->GetShadingPath() == EShadingPath::Deferred)
		{
			const EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(InBatchMaterial->GetFeatureLevel());

			FMaterialShaderTypes ShaderTypes;
			if (IsSimpleForwardShadingEnabled(ShaderPlatform))
			{
				ShaderTypes.AddShaderType<TBasePassVS<TUniformLightMapPolicy<LMP_SIMPLE_NO_LIGHTMAP>, false>>();
				ShaderTypes.AddShaderType<TBasePassPS<TUniformLightMapPolicy<LMP_SIMPLE_NO_LIGHTMAP>, false>>();
			}
			else
			{
				ShaderTypes.AddShaderType<TBasePassVS<TUniformLightMapPolicy<LMP_NO_LIGHTMAP>, false>>();
				ShaderTypes.AddShaderType<TBasePassPS<TUniformLightMapPolicy<LMP_NO_LIGHTMAP>, false>>();
			}

			FMaterialShaders Shaders;
			if (InBatchMaterial->TryGetShaders(ShaderTypes, InVertexFactoryType, Shaders))
			{
				OutShaderElementData.NumVSInstructions = Shaders.Shaders[SF_Vertex]->GetNumInstructions();
				OutShaderElementData.NumPSInstructions = Shaders.Shaders[SF_Pixel]->GetNumInstructions();

				if (IsForwardShadingEnabled(ShaderPlatform) &&
					!IsSimpleForwardShadingEnabled(ShaderPlatform) &&
					!IsTranslucentBlendMode(InBatchMaterial->GetBlendMode()))
				{
					const bool bLit = InBatchMaterial->GetShadingModels().IsLit();

					// Those numbers are taken from a simple material where common inputs are bound to vector parameters (to prevent constant optimizations).
					OutShaderElementData.NumVSInstructions -= GShaderComplexityBaselineForwardVS - GShaderComplexityBaselineDeferredVS;
					OutShaderElementData.NumPSInstructions -= bLit ? (GShaderComplexityBaselineForwardPS - GShaderComplexityBaselineDeferredPS) : (GShaderComplexityBaselineForwardUnlitPS - GShaderComplexityBaselineDeferredUnlitPS);
				}

				OutShaderElementData.NumVSInstructions = FMath::Max<int32>(0, OutShaderElementData.NumVSInstructions);
				OutShaderElementData.NumPSInstructions = FMath::Max<int32>(0, OutShaderElementData.NumPSInstructions);
			}
		}
		else // EShadingPath::Mobile
		{
			TShaderRef<TMobileBasePassVSPolicyParamType<FUniformLightMapPolicy>> MobileVS;
			TShaderRef<TMobileBasePassPSPolicyParamType<FUniformLightMapPolicy>> MobilePS;
			if (MobileBasePass::GetShaders(LMP_NO_LIGHTMAP, 0, *InBatchMaterial, InVertexFactoryType, false, MobileVS, MobilePS))
			{
				OutShaderElementData.NumVSInstructions = MobileVS.IsValid() ? MobileVS->GetNumInstructions() : 0;
				OutShaderElementData.NumPSInstructions = MobilePS.IsValid() ? MobilePS->GetNumInstructions() : 0;
			}
		}
	}
}

FMeshPassProcessor* CreateDebugViewModePassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	const ERHIFeatureLevel::Type FeatureLevel = Scene ? Scene->GetFeatureLevel() : (InViewIfDynamicMeshCommand ? InViewIfDynamicMeshCommand->GetFeatureLevel() : GMaxRHIFeatureLevel);
	return new(FMemStack::Get()) FDebugViewModeMeshProcessor(Scene, FeatureLevel, InViewIfDynamicMeshCommand, nullptr, false, InDrawListContext);
}

FRegisterPassProcessorCreateFunction RegisterDebugViewModeMobilePass(&CreateDebugViewModePassProcessor, EShadingPath::Mobile, EMeshPass::DebugViewMode, EMeshPassFlags::MainView);
FRegisterPassProcessorCreateFunction RegisterDebugViewModePass(&CreateDebugViewModePassProcessor, EShadingPath::Deferred, EMeshPass::DebugViewMode, EMeshPassFlags::MainView);

void InitDebugViewModeInterfaces()
{
	FDebugViewModeInterface::SetInterface(DVSM_ShaderComplexity, new FComplexityAccumulateInterface(true, false));
	FDebugViewModeInterface::SetInterface(DVSM_ShaderComplexityContainedQuadOverhead, new FComplexityAccumulateInterface(true, false));
	FDebugViewModeInterface::SetInterface(DVSM_ShaderComplexityBleedingQuadOverhead, new FComplexityAccumulateInterface(true, true));
	FDebugViewModeInterface::SetInterface(DVSM_QuadComplexity, new FComplexityAccumulateInterface(false, false));

	FDebugViewModeInterface::SetInterface(DVSM_PrimitiveDistanceAccuracy, new FPrimitiveDistanceAccuracyInterface());
	FDebugViewModeInterface::SetInterface(DVSM_MeshUVDensityAccuracy, new FMeshTexCoordSizeAccuracyInterface());
	FDebugViewModeInterface::SetInterface(DVSM_MaterialTextureScaleAccuracy, new FMaterialTexCoordScaleAccuracyInterface());
	FDebugViewModeInterface::SetInterface(DVSM_OutputMaterialTextureScales, new FOutputMaterialTexCoordScaleInterface());
	FDebugViewModeInterface::SetInterface(DVSM_RequiredTextureResolution, new FRequiredTextureResolutionInterface());

	FDebugViewModeInterface::SetInterface(DVSM_LODColoration, new FLODColorationInterface());
}

#else // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

void FDeferredShadingSceneRenderer::RenderDebugViewMode(FRDGBuilder& GraphBuilder, const FRenderTargetBindingSlots& RenderTargets) {}

#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)


