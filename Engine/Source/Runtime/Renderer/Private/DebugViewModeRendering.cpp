// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
#include "PrimitiveSceneInfo.h"
#include "ScenePrivate.h"
#include "PostProcessing.h"
#include "PostProcess/PostProcessVisualizeComplexity.h"
#include "PostProcess/PostProcessStreamingAccuracyLegend.h"
#include "CompositionLighting/PostProcessPassThrough.h"
#include "PostProcess/PostProcessSelectionOutline.h"
#include "PostProcess/PostProcessCompositeEditorPrimitives.h"
#include "PostProcess/PostProcessUpscale.h"
#include "PostProcess/PostProcessTemporalAA.h"
#include "PostProcess/PostProcessInput.h"
#include "SceneRendering.h"
#include "DeferredShadingRenderer.h"
#include "MeshPassProcessor.inl"

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FDebugViewModePassPassUniformParameters, "DebugViewModePass");

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

void SetupDebugViewModePassUniformBuffer(FSceneRenderTargets& SceneContext, ERHIFeatureLevel::Type FeatureLevel, FDebugViewModePassPassUniformParameters& PassParameters)
{
	SetupSceneTextureUniformParameters(SceneContext, FeatureLevel, ESceneTextureSetupMode::None, PassParameters.SceneTextures);

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


IMPLEMENT_MATERIAL_SHADER_TYPE(,FDebugViewModeVS,TEXT("/Engine/Private/DebugViewModeVertexShader.usf"),TEXT("Main"),SF_Vertex);	
IMPLEMENT_MATERIAL_SHADER_TYPE(,FDebugViewModeHS,TEXT("/Engine/Private/DebugViewModeVertexShader.usf"),TEXT("MainHull"),SF_Hull);	
IMPLEMENT_MATERIAL_SHADER_TYPE(,FDebugViewModeDS,TEXT("/Engine/Private/DebugViewModeVertexShader.usf"),TEXT("MainDomain"),SF_Domain);

ENGINE_API bool GetDebugViewMaterial(const UMaterialInterface* InMaterialInterface, EDebugViewShaderMode InDebugViewMode, ERHIFeatureLevel::Type InFeatureLevel,const FMaterialRenderProxy*& OutMaterialRenderProxy, const FMaterial*& OutMaterial);


bool FDebugViewModeVS::ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
{
	if (AllowDebugViewVSDSHS(Parameters.Platform))
	{
		// If it comes from FDebugViewModeMaterialProxy, compile it.
		if (Parameters.Material->GetFriendlyName().Contains(TEXT("DebugViewMode")))
		{
			return true;
		}
		// Otherwise we only cache it if this for the shader complexity.
		else if (GCacheShaderComplexityShaders)
		{
			return !FDebugViewModeInterface::AllowFallbackToDefaultMaterial(Parameters.Material) || Parameters.Material->IsDefaultMaterial();
		}
	}
	return false;
}

bool FDeferredShadingSceneRenderer::RenderDebugViewMode(FRHICommandListImmediate& RHICmdList)
{
	bool bDirty=0;
	SCOPED_DRAW_EVENT(RHICmdList, DebugViewMode);

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		FViewInfo& View = Views[ViewIndex];

		SCOPED_GPU_MASK(RHICmdList, View.GPUMask);
		SCOPED_CONDITIONAL_DRAW_EVENTF(RHICmdList, EventView, Views.Num() > 1, TEXT("View%d"), ViewIndex);

		Scene->UniformBuffers.UpdateViewUniformBuffer(View);

		// Some of the viewmodes use SCENE_TEXTURES_DISABLED to prevent issues when running in commandlet mode.
		FDebugViewModePassPassUniformParameters PassParameters;
		SetupDebugViewModePassUniformBuffer(SceneContext, View.GetFeatureLevel(), PassParameters);
		Scene->UniformBuffers.DebugViewModePassUniformBuffer.UpdateUniformBufferImmediate(PassParameters);

		RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1);
		{
			SCOPED_DRAW_EVENT(RHICmdList, Dynamic);

			View.ParallelMeshDrawCommandPasses[EMeshPass::DebugViewMode].DispatchDraw(nullptr, RHICmdList);
		}
	}

	return bDirty;
}

FDebugViewModePS::FDebugViewModePS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer) : FMeshMaterialShader(Initializer) 
{
	PassUniformBuffer.Bind(Initializer.ParameterMap, FDebugViewModePassPassUniformParameters::StaticStructMetadata.GetShaderVariableName());
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
		if (!PassUniformBuffer)
		{
			PassUniformBuffer = InScene->UniformBuffers.DebugViewModePassUniformBuffer;
		}
	}
}

void FDebugViewModeMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	const FMaterial* BatchMaterial = MeshBatch.MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);

	if (!DebugViewModeInterface || !BatchMaterial)
	{
		return;
	}

	const UMaterialInterface* ResolvedMaterial = MeshBatch.MaterialRenderProxy->GetMaterialInterface();
	if (!DebugViewModeInterface->bNeedsMaterialProperties && FDebugViewModeInterface::AllowFallbackToDefaultMaterial(BatchMaterial))
	{
		ResolvedMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	}

	const FMaterialRenderProxy* MaterialRenderProxy = nullptr;
	const FMaterial* Material = nullptr;

	if (DebugViewMode == DVSM_ShaderComplexity && GCacheShaderComplexityShaders)
	{
		Material = ResolvedMaterial->GetMaterialResource(FeatureLevel);
		MaterialRenderProxy  = ResolvedMaterial->GetRenderProxy();

		if (!Material || !MaterialRenderProxy || !Material->HasValidGameThreadShaderMap() ||  !Material->GetRenderingThreadShaderMap())
		{
			return;
		}
	}
	else if (!GetDebugViewMaterial(ResolvedMaterial, DebugViewMode, FeatureLevel, MaterialRenderProxy, Material))
	{
		return;
	}

	FVertexFactoryType* VertexFactoryType = MeshBatch.VertexFactory->GetType();

	const EMaterialTessellationMode MaterialTessellationMode = Material->GetTessellationMode();
	const bool bNeedsHSDS = RHISupportsTessellation(GShaderPlatformForFeatureLevel[FeatureLevel])
			&& VertexFactoryType->SupportsTessellationShaders()
			&& MaterialTessellationMode != MTM_NoTessellation;

	TMeshProcessorShaders<FDebugViewModeVS,	FDebugViewModeHS, FDebugViewModeDS,	FDebugViewModePS> DebugViewModePassShaders;
	DebugViewModePassShaders.VertexShader = Material->GetShader<FDebugViewModeVS>(VertexFactoryType);
	if (bNeedsHSDS)
	{
		DebugViewModePassShaders.DomainShader = Material->GetShader<FDebugViewModeDS>(VertexFactoryType);
		DebugViewModePassShaders.HullShader = Material->GetShader<FDebugViewModeHS>(VertexFactoryType);
	}
	DebugViewModePassShaders.PixelShader = DebugViewModeInterface->GetPixelShader(Material, VertexFactoryType);

	const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, *BatchMaterial);
	const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, *BatchMaterial);

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
		ViewIfDynamicMeshCommand->ViewMatrices.GetViewOrigin(), 
		MeshBatch.VisualizeLODIndex, 
		ViewModeParam, 
		ViewModeParamName);

	// Shadermap can be null while shaders are compiling.
	if (DebugViewModeInterface->bNeedsInstructionCount && BatchMaterial->GetRenderingThreadShaderMap())
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

			if (IsSimpleForwardShadingEnabled(ShaderPlatform))
			{
				OutShaderElementData.NumVSInstructions = InBatchMaterial->GetShader<TBasePassVS<TUniformLightMapPolicy<LMP_SIMPLE_NO_LIGHTMAP>, false>>(InVertexFactoryType)->GetNumInstructions();
				OutShaderElementData.NumPSInstructions = InBatchMaterial->GetShader<TBasePassPS<TUniformLightMapPolicy<LMP_SIMPLE_NO_LIGHTMAP>, false>>(InVertexFactoryType)->GetNumInstructions();
			}
			else
			{
				OutShaderElementData.NumVSInstructions = InBatchMaterial->GetShader<TBasePassVS<TUniformLightMapPolicy<LMP_NO_LIGHTMAP>, false>>(InVertexFactoryType)->GetNumInstructions();
				OutShaderElementData.NumPSInstructions = InBatchMaterial->GetShader<TBasePassPS<TUniformLightMapPolicy<LMP_NO_LIGHTMAP>, false>>(InVertexFactoryType)->GetNumInstructions();

				if (IsForwardShadingEnabled(ShaderPlatform) && !IsTranslucentBlendMode(InBatchMaterial->GetBlendMode()))
				{
					const bool bLit = InBatchMaterial->GetShadingModels().IsLit();

					// Those numbers are taken from a simple material where common inputs are bound to vector parameters (to prevent constant optimizations).
					OutShaderElementData.NumVSInstructions -= GShaderComplexityBaselineForwardVS - GShaderComplexityBaselineDeferredVS;
					OutShaderElementData.NumPSInstructions -= bLit ? (GShaderComplexityBaselineForwardPS - GShaderComplexityBaselineDeferredPS) : (GShaderComplexityBaselineForwardUnlitPS - GShaderComplexityBaselineDeferredUnlitPS);
				}
			}

			OutShaderElementData.NumVSInstructions = FMath::Max<int32>(0, OutShaderElementData.NumVSInstructions);
			OutShaderElementData.NumPSInstructions = FMath::Max<int32>(0, OutShaderElementData.NumPSInstructions);
		}
		else // EShadingPath::Mobile
		{
			TMobileBasePassVSPolicyParamType<FUniformLightMapPolicy>* MobileVS = nullptr;
			TMobileBasePassPSPolicyParamType<FUniformLightMapPolicy>* MobilePS = nullptr;

			MobileBasePass::GetShaders(LMP_NO_LIGHTMAP, 0, *InBatchMaterial, InVertexFactoryType, false, MobileVS, MobilePS);

			OutShaderElementData.NumVSInstructions = MobileVS ? MobileVS->GetNumInstructions() : 0;
			OutShaderElementData.NumPSInstructions = MobilePS ? MobilePS->GetNumInstructions() : 0;
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
}

#else // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

bool FDeferredShadingSceneRenderer::RenderDebugViewMode(FRHICommandListImmediate& RHICmdList)
{
	return false;
}

#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)


