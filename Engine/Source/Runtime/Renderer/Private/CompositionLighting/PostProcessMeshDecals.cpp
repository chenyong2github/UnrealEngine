// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "RHI.h"
#include "HitProxies.h"
#include "Shader.h"
#include "SceneUtils.h"
#include "RHIStaticStates.h"
#include "PostProcess/SceneRenderTargets.h"
#include "MaterialShaderType.h"
#include "MeshMaterialShader.h"
#include "ShaderBaseClasses.h"
#include "DepthRendering.h"
#include "DecalRenderingCommon.h"
#include "DecalRenderingShared.h"
#include "CompositionLighting/PostProcessDeferredDecals.h"
#include "SceneRendering.h"
#include "UnrealEngine.h"
#include "DebugViewModeRendering.h"
#include "MeshPassProcessor.inl"

class FMeshDecalAccumulatePolicy
{
public:
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return Parameters.MaterialParameters.MaterialDomain == MD_DeferredDecal;
	}
};

class FMeshDecalsVS : public FMeshMaterialShader
{
public:
	DECLARE_SHADER_TYPE(FMeshDecalsVS, MeshMaterial);

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return FMeshDecalAccumulatePolicy::ShouldCompilePermutation(Parameters);
	}

	FMeshDecalsVS() = default;
	FMeshDecalsVS(const ShaderMetaType::CompiledShaderInitializerType & Initializer)
		: FMeshMaterialShader(Initializer)
	{}
};

class FMeshDecalsHS : public FBaseHS
{
public:
	DECLARE_SHADER_TYPE(FMeshDecalsHS, MeshMaterial);

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return FBaseHS::ShouldCompilePermutation(Parameters)
			&& FMeshDecalAccumulatePolicy::ShouldCompilePermutation(Parameters);
	}

	FMeshDecalsHS() = default;
	FMeshDecalsHS(const ShaderMetaType::CompiledShaderInitializerType & Initializer)
		: FBaseHS(Initializer)
	{}
};

class FMeshDecalsDS : public FBaseDS
{
public:
	DECLARE_SHADER_TYPE(FMeshDecalsDS, MeshMaterial);

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return FBaseDS::ShouldCompilePermutation(Parameters)
			&& FMeshDecalAccumulatePolicy::ShouldCompilePermutation(Parameters);
	}

	FMeshDecalsDS() = default;
	FMeshDecalsDS(const ShaderMetaType::CompiledShaderInitializerType & Initializer)
		: FBaseDS(Initializer)
	{}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(,FMeshDecalsVS,TEXT("/Engine/Private/MeshDecals.usf"),TEXT("MainVS"),SF_Vertex); 
IMPLEMENT_MATERIAL_SHADER_TYPE(,FMeshDecalsHS,TEXT("/Engine/Private/MeshDecals.usf"),TEXT("MainHull"),SF_Hull); 
IMPLEMENT_MATERIAL_SHADER_TYPE(,FMeshDecalsDS,TEXT("/Engine/Private/MeshDecals.usf"),TEXT("MainDomain"),SF_Domain);

class FMeshDecalsPS : public FMeshMaterialShader
{
public:
	DECLARE_SHADER_TYPE(FMeshDecalsPS, MeshMaterial);

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return FMeshDecalAccumulatePolicy::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FDecalRendering::SetDecalCompilationEnvironment(Parameters, OutEnvironment);
	}

	FMeshDecalsPS() = default;
	FMeshDecalsPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(,FMeshDecalsPS,TEXT("/Engine/Private/MeshDecals.usf"),TEXT("MainPS"),SF_Pixel);

class FMeshDecalsEmissivePS : public FMeshDecalsPS
{
public:
	DECLARE_SHADER_TYPE(FMeshDecalsEmissivePS, MeshMaterial);

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return FMeshDecalsPS::ShouldCompilePermutation(Parameters)
			&& Parameters.MaterialParameters.bHasEmissiveColorConnected
			&& IsDBufferDecalBlendMode(FDecalRenderingCommon::ComputeFinalDecalBlendMode(Parameters.Platform, (EDecalBlendMode)Parameters.MaterialParameters.DecalBlendMode, Parameters.MaterialParameters.bHasNormalConnected));
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMeshDecalsPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FDecalRendering::SetEmissiveDBufferDecalCompilationEnvironment(Parameters, OutEnvironment);
	}

	FMeshDecalsEmissivePS() = default;
	FMeshDecalsEmissivePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshDecalsPS(Initializer)
	{}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FMeshDecalsEmissivePS, TEXT("/Engine/Private/MeshDecals.usf"), TEXT("MainPS"), SF_Pixel);


class FMeshDecalMeshProcessor : public FMeshPassProcessor
{
public:
	FMeshDecalMeshProcessor(const FScene* Scene, 
		const FSceneView* InViewIfDynamicMeshCommand, 
		EDecalRenderStage InPassDecalStage, 
		FDecalRenderingCommon::ERenderTargetMode InRenderTargetMode,
		FMeshPassDrawListContext* InDrawListContext);

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final;

private:
	void Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		int32 StaticMeshId,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode);

	FMeshPassProcessorRenderState PassDrawRenderState;
	const EDecalRenderStage PassDecalStage;
	const FDecalRenderingCommon::ERenderTargetMode RenderTargetMode;
};


FMeshDecalMeshProcessor::FMeshDecalMeshProcessor(const FScene* Scene, 
	const FSceneView* InViewIfDynamicMeshCommand, 
	EDecalRenderStage InPassDecalStage, 
	FDecalRenderingCommon::ERenderTargetMode InRenderTargetMode,
	FMeshPassDrawListContext* InDrawListContext)
	: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
	, PassDecalStage(InPassDecalStage)
	, RenderTargetMode(InRenderTargetMode)
{
	PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI());
	PassDrawRenderState.SetViewUniformBuffer(Scene->UniformBuffers.ViewUniformBuffer);
	PassDrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);
	if (FeatureLevel == ERHIFeatureLevel::ES3_1)
	{
		PassDrawRenderState.SetPassUniformBuffer(Scene->UniformBuffers.MobileTranslucentBasePassUniformBuffer);
	}
}

void FMeshDecalMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	if (MeshBatch.bUseForMaterial && MeshBatch.IsDecal(FeatureLevel))
	{
		const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
		const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);

		if (Material && Material->IsDeferredDecal())
		{
			// We have no special engine material for decals since we don't want to eat the compilation & memory cost, so just skip if it failed to compile
			if (Material->GetRenderingThreadShaderMap())
			{
				const EShaderPlatform ShaderPlatform = ViewIfDynamicMeshCommand->GetShaderPlatform();
				const EDecalBlendMode FinalDecalBlendMode = FDecalRenderingCommon::ComputeFinalDecalBlendMode(ShaderPlatform, Material);
				const EDecalRenderStage LocalDecalRenderStage = FDecalRenderingCommon::ComputeRenderStage(ShaderPlatform, FinalDecalBlendMode);

				const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
				ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, *Material, OverrideSettings);
				ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, *Material, OverrideSettings);

				bool bShouldRender = FDecalRenderingCommon::IsCompatibleWithRenderStage(
					ShaderPlatform,
					PassDecalStage,
					LocalDecalRenderStage,
					FinalDecalBlendMode,
					Material);

				if (FinalDecalBlendMode == DBM_Normal)
				{
					bShouldRender = bShouldRender && RenderTargetMode == FDecalRenderingCommon::RTM_GBufferNormal;
				}
				else
				{
					bShouldRender = bShouldRender && RenderTargetMode != FDecalRenderingCommon::RTM_GBufferNormal;
				}

				if (PassDecalStage == DRS_Emissive)
				{
					bShouldRender = bShouldRender && Material->HasEmissiveColorConnected();
				}

				if (bShouldRender)
				{
					const bool bHasNormal = Material->HasNormalConnected();

					const EDecalBlendMode DecalBlendMode = FDecalRenderingCommon::ComputeDecalBlendModeForRenderStage(
						FDecalRenderingCommon::ComputeFinalDecalBlendMode(ShaderPlatform, (EDecalBlendMode)Material->GetDecalBlendMode(), bHasNormal),
						PassDecalStage);

					FDecalRenderingCommon::ERenderTargetMode DecalRenderTargetMode = FDecalRenderingCommon::ComputeRenderTargetMode(ShaderPlatform, DecalBlendMode, bHasNormal);

					if (DecalRenderTargetMode == RenderTargetMode)
					{
						if (ViewIfDynamicMeshCommand->Family->UseDebugViewPS())
						{
							// Deferred decals can only use translucent blend mode
							if (ViewIfDynamicMeshCommand->Family->EngineShowFlags.ShaderComplexity)
							{
								// If we are in the translucent pass then override the blend mode, otherwise maintain additive blending.
								PassDrawRenderState.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_One>::GetRHI());
							}
							else if (ViewIfDynamicMeshCommand->Family->GetDebugViewShaderMode() != DVSM_OutputMaterialTextureScales)
							{
								// Otherwise, force translucent blend mode (shaders will use an hardcoded alpha).
								PassDrawRenderState.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_InverseSourceAlpha>::GetRHI());
							}
						}
						else
						{
							PassDrawRenderState.SetBlendState(FDecalRendering::GetDecalBlendState(FeatureLevel, PassDecalStage, DecalBlendMode, bHasNormal));
						}

						Process(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, *MaterialRenderProxy, *Material, MeshFillMode, MeshCullMode);
					}
				}
			}
		}
	}
}

void FMeshDecalMeshProcessor::Process(
	const FMeshBatch& MeshBatch,
	uint64 BatchElementMask,
	int32 StaticMeshId,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;
	FVertexFactoryType* VertexFactoryType = VertexFactory->GetType();

	const EMaterialTessellationMode MaterialTessellationMode = MaterialResource.GetTessellationMode();

	const bool bNeedsHSDS = RHISupportsTessellation(GShaderPlatformForFeatureLevel[FeatureLevel])
		&& VertexFactoryType->SupportsTessellationShaders()
		&& MaterialTessellationMode != MTM_NoTessellation;

	FMaterialShaderTypes ShaderTypes;
	ShaderTypes.AddShaderType<FMeshDecalsVS>();
	//MeshDecalPassShaders.VertexShader = MaterialResource.GetShader<FMeshDecalsVS>(VertexFactoryType, 0, false);

	if (bNeedsHSDS)
	{
		ShaderTypes.AddShaderType<FMeshDecalsDS>();
		ShaderTypes.AddShaderType<FMeshDecalsHS>();
		//MeshDecalPassShaders.DomainShader = MaterialResource.GetShader<FMeshDecalsDS>(VertexFactoryType, 0, false);
		//MeshDecalPassShaders.HullShader = MaterialResource.GetShader<FMeshDecalsHS>(VertexFactoryType, 0, false);
	}

	if (PassDecalStage == DRS_Emissive)
	{
		ShaderTypes.AddShaderType<FMeshDecalsEmissivePS>();
		//MeshDecalPassShaders.PixelShader = MaterialResource.GetShader<FMeshDecalsEmissivePS>(VertexFactoryType, 0, false);
	}
	else
	{
		ShaderTypes.AddShaderType<FMeshDecalsPS>();
		//MeshDecalPassShaders.PixelShader = MaterialResource.GetShader<FMeshDecalsPS>(VertexFactoryType, 0, false);
	}

	FMaterialShaders Shaders;
	if (!MaterialResource.TryGetShaders(ShaderTypes, VertexFactoryType, Shaders))
	{
		// Skip rendering if any shaders missing
		return;
	}

	TMeshProcessorShaders<
		FMeshDecalsVS,
		FMeshDecalsHS,
		FMeshDecalsDS,
		FMeshDecalsPS> MeshDecalPassShaders;
	Shaders.TryGetVertexShader(MeshDecalPassShaders.VertexShader);
	Shaders.TryGetPixelShader(MeshDecalPassShaders.PixelShader);
	Shaders.TryGetHullShader(MeshDecalPassShaders.HullShader);
	Shaders.TryGetDomainShader(MeshDecalPassShaders.DomainShader);

	FMeshMaterialShaderElementData ShaderElementData;
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, true);

	const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(MeshDecalPassShaders.VertexShader, MeshDecalPassShaders.PixelShader);

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		PassDrawRenderState,
		MeshDecalPassShaders,
		MeshFillMode,
		MeshCullMode,
		SortKey,
		EMeshPassFeatures::Default,
		ShaderElementData);
}

void DrawDecalMeshCommands(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FDeferredDecalPassTextures& DecalPassTextures,
	EDecalRenderStage DecalRenderStage,
	FDecalRenderingCommon::ERenderTargetMode RenderTargetMode)
{
	auto* PassParameters = GraphBuilder.AllocParameters<FDeferredDecalPassParameters>();
	GetDeferredDecalPassParameters(View, DecalPassTextures, RenderTargetMode, *PassParameters);
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("MeshDecals"),
		PassParameters,
		ERDGPassFlags::Raster,
		[&View, DecalRenderStage, RenderTargetMode](FRHICommandList& RHICmdList)
	{
		RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

		DrawDynamicMeshPass(View, RHICmdList,
			[&View, DecalRenderStage, RenderTargetMode](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
		{
			FMeshDecalMeshProcessor PassMeshProcessor(
				View.Family->Scene->GetRenderScene(),
				&View,
				DecalRenderStage,
				RenderTargetMode,
				DynamicMeshPassContext);

			for (int32 MeshBatchIndex = 0; MeshBatchIndex < View.MeshDecalBatches.Num(); ++MeshBatchIndex)
			{
				const FMeshBatch* Mesh = View.MeshDecalBatches[MeshBatchIndex].Mesh;
				const FPrimitiveSceneProxy* PrimitiveSceneProxy = View.MeshDecalBatches[MeshBatchIndex].Proxy;
				const uint64 DefaultBatchElementMask = ~0ull;

				PassMeshProcessor.AddMeshBatch(*Mesh, DefaultBatchElementMask, PrimitiveSceneProxy);
			}
		}, true);
	});
}

void RenderMeshDecals(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FDeferredDecalPassTextures& DecalPassTextures,
	EDecalRenderStage DecalRenderStage)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FSceneRenderer_RenderMeshDecals);

	switch (DecalRenderStage)
	{
	case DRS_BeforeBasePass:
		DrawDecalMeshCommands(GraphBuilder, View, DecalPassTextures, DecalRenderStage, FDecalRenderingCommon::RTM_DBuffer);
		break;

	case DRS_AfterBasePass:
		DrawDecalMeshCommands(GraphBuilder, View, DecalPassTextures, DecalRenderStage, FDecalRenderingCommon::RTM_SceneColorAndGBufferDepthWriteWithNormal);
		break;

	case DRS_BeforeLighting:
		DrawDecalMeshCommands(GraphBuilder, View, DecalPassTextures, DecalRenderStage, FDecalRenderingCommon::RTM_GBufferNormal);
		DrawDecalMeshCommands(GraphBuilder, View, DecalPassTextures, DecalRenderStage, FDecalRenderingCommon::RTM_SceneColorAndGBufferWithNormal);
		DrawDecalMeshCommands(GraphBuilder, View, DecalPassTextures, DecalRenderStage, FDecalRenderingCommon::RTM_SceneColorAndGBufferNoNormal);
		break;

	case DRS_Mobile:
		DrawDecalMeshCommands(GraphBuilder, View, DecalPassTextures, DecalRenderStage, FDecalRenderingCommon::RTM_SceneColor);
		break;

	case DRS_AmbientOcclusion:
		DrawDecalMeshCommands(GraphBuilder, View, DecalPassTextures, DecalRenderStage, FDecalRenderingCommon::RTM_AmbientOcclusion);
		break;

	case DRS_Emissive:
		DrawDecalMeshCommands(GraphBuilder, View, DecalPassTextures, DecalRenderStage, FDecalRenderingCommon::RTM_SceneColor);
		break;
	}
}

void RenderMeshDecalsMobile(FRHICommandList& RHICmdList, const FViewInfo& View)
{
	SCOPED_DRAW_EVENT(RHICmdList, MeshDecals);

	FDecalRenderingCommon::ERenderTargetMode RenderTargetMode = IsMobileDeferredShadingEnabled(View.GetShaderPlatform()) ? 
		FDecalRenderingCommon::RTM_SceneColorAndGBufferWithNormal : 
		FDecalRenderingCommon::RTM_SceneColor;

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1);
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	DrawDynamicMeshPass(View, RHICmdList, [&View, RenderTargetMode](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
	{
		FMeshDecalMeshProcessor PassMeshProcessor(
			View.Family->Scene->GetRenderScene(),
			&View,
			DRS_Mobile,
			RenderTargetMode,
			DynamicMeshPassContext);

		for (int32 MeshBatchIndex = 0; MeshBatchIndex < View.MeshDecalBatches.Num(); ++MeshBatchIndex)
		{
			const FMeshBatch* Mesh = View.MeshDecalBatches[MeshBatchIndex].Mesh;
			const FPrimitiveSceneProxy* PrimitiveSceneProxy = View.MeshDecalBatches[MeshBatchIndex].Proxy;
			const uint64 DefaultBatchElementMask = ~0ull;

			PassMeshProcessor.AddMeshBatch(*Mesh, DefaultBatchElementMask, PrimitiveSceneProxy);
		}
	}, true);
}