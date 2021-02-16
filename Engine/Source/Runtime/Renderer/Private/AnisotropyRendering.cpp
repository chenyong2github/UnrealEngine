// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnisotropyRendering.h"
#include "PrimitiveSceneProxy.h"
#include "MeshPassProcessor.inl"
#include "ScenePrivate.h"
#include "DeferredShadingRenderer.h"

DECLARE_GPU_STAT_NAMED(RenderAnisotropyPass, TEXT("Render Anisotropy Pass"));

static int32 GAnisotropicMaterials = 0;
static FAutoConsoleVariableRef CVarAnisotropicMaterials(
	TEXT("r.AnisotropicMaterials"),
	GAnisotropicMaterials,
	TEXT("Whether anisotropic BRDF is used for material with anisotropy."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

bool SupportsAnisotropicMaterials(ERHIFeatureLevel::Type FeatureLevel, EShaderPlatform ShaderPlatform)
{
	return GAnisotropicMaterials
		&& FeatureLevel >= ERHIFeatureLevel::SM5
		&& FDataDrivenShaderPlatformInfo::GetSupportsAnisotropicMaterials(ShaderPlatform);
}

static bool IsAnisotropyPassCompatible(const EShaderPlatform Platform, FMaterialShaderParameters MaterialParameters)
{
	return 
		FDataDrivenShaderPlatformInfo::GetSupportsAnisotropicMaterials(Platform) &&
		MaterialParameters.bHasAnisotropyConnected &&
		!IsTranslucentBlendMode(MaterialParameters.BlendMode) && 
		MaterialParameters.ShadingModels.HasAnyShadingModel({ MSM_DefaultLit, MSM_ClearCoat });
}

class FAnisotropyVS : public FMeshMaterialShader
{
public:
	DECLARE_SHADER_TYPE(FAnisotropyVS, MeshMaterial);

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		// Compile if supported by the hardware.
		const bool bIsFeatureSupported = IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);

		return 
			bIsFeatureSupported && 
			IsAnisotropyPassCompatible(Parameters.Platform, Parameters.MaterialParameters) &&
			FMeshMaterialShader::ShouldCompilePermutation(Parameters);
	}

	FAnisotropyVS() = default;
	FAnisotropyVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{}
};

class FAnisotropyHS : public FBaseHS
{
public:
	DECLARE_SHADER_TYPE(FAnisotropyHS, MeshMaterial);

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return FBaseHS::ShouldCompilePermutation(Parameters) && FAnisotropyVS::ShouldCompilePermutation(Parameters);
	}

	FAnisotropyHS() = default;
	FAnisotropyHS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FBaseHS(Initializer)
	{}
};

class FAnisotropyDS : public FBaseDS
{
public:
	DECLARE_SHADER_TYPE(FAnisotropyDS, MeshMaterial);

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return FBaseDS::ShouldCompilePermutation(Parameters) && FAnisotropyVS::ShouldCompilePermutation(Parameters);
	}

	FAnisotropyDS() = default;
	FAnisotropyDS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FBaseDS(Initializer)
	{}
};

class FAnisotropyPS : public FMeshMaterialShader
{
public:
	DECLARE_SHADER_TYPE(FAnisotropyPS, MeshMaterial);

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return FAnisotropyVS::ShouldCompilePermutation(Parameters);
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	FAnisotropyPS() = default;
	FAnisotropyPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{}
};

IMPLEMENT_SHADER_TYPE(, FAnisotropyVS, TEXT("/Engine/Private/AnisotropyPassShader.usf"), TEXT("MainVertexShader"), SF_Vertex);
IMPLEMENT_SHADER_TYPE(, FAnisotropyHS, TEXT("/Engine/Private/AnisotropyPassShader.usf"), TEXT("MainHull"), SF_Hull);
IMPLEMENT_SHADER_TYPE(, FAnisotropyDS, TEXT("/Engine/Private/AnisotropyPassShader.usf"), TEXT("MainDomain"), SF_Domain);
IMPLEMENT_SHADER_TYPE(, FAnisotropyPS, TEXT("/Engine/Private/AnisotropyPassShader.usf"), TEXT("MainPixelShader"), SF_Pixel);
IMPLEMENT_SHADERPIPELINE_TYPE_VSPS(AnisotropyPipeline, FAnisotropyVS, FAnisotropyPS, true);

DECLARE_CYCLE_STAT(TEXT("AnisotropyPass"), STAT_CLP_AnisotropyPass, STATGROUP_ParallelCommandListMarkers);

class FAnisotropyPassParallelCommandListSet : public FParallelCommandListSet
{
public:
	FAnisotropyPassParallelCommandListSet(
		FRHICommandListImmediate& InRHICmdList,
		const FSceneRenderer& InSceneRenderer,
		const FViewInfo& InView,
		const FParallelCommandListBindings& InBindings
		)
		: FParallelCommandListSet(GET_STATID(STAT_CLP_AnisotropyPass), InView, InRHICmdList, false)
		, SceneRenderer(InSceneRenderer)
		, Bindings(InBindings)
	{}

	virtual ~FAnisotropyPassParallelCommandListSet()
	{
		Dispatch();
	}

	virtual void SetStateOnCommandList(FRHICommandList& RHICmdList) override
	{
		FParallelCommandListSet::SetStateOnCommandList(RHICmdList);
		Bindings.SetOnCommandList(RHICmdList);
		SceneRenderer.SetStereoViewport(RHICmdList, View);
	}

private:
	const FSceneRenderer& SceneRenderer;
	FParallelCommandListBindings Bindings;
};


FAnisotropyMeshProcessor::FAnisotropyMeshProcessor(
	const FScene* Scene, 
	const FSceneView* InViewIfDynamicMeshCommand, 
	const FMeshPassProcessorRenderState& InPassDrawRenderState, 
	FMeshPassDrawListContext* InDrawListContext
	)
	: FMeshPassProcessor(Scene, ERHIFeatureLevel::SM5, InViewIfDynamicMeshCommand, InDrawListContext)
	, PassDrawRenderState(InPassDrawRenderState)
{
}

FMeshPassProcessor* CreateAnisotropyPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState AnisotropyPassState(Scene->UniformBuffers.ViewUniformBuffer);
	AnisotropyPassState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);

	AnisotropyPassState.SetBlendState(TStaticBlendState<>::GetRHI());
	AnisotropyPassState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Equal>::GetRHI());

	return new(FMemStack::Get()) FAnisotropyMeshProcessor(Scene, InViewIfDynamicMeshCommand, AnisotropyPassState, InDrawListContext);
}

FRegisterPassProcessorCreateFunction RegisterAnisotropyPass(
	&CreateAnisotropyPassProcessor,
	EShadingPath::Deferred, 
	EMeshPass::AnisotropyPass, 
	EMeshPassFlags::CachedMeshCommands | EMeshPassFlags::MainView
	);

void GetAnisotropyPassShaders(
	const FMaterial& Material,
	FVertexFactoryType* VertexFactoryType,
	ERHIFeatureLevel::Type FeatureLevel,
	TShaderRef<FAnisotropyHS>& HullShader,
	TShaderRef<FAnisotropyDS>& DomainShader,
	TShaderRef<FAnisotropyVS>& VertexShader,
	TShaderRef<FAnisotropyPS>& PixelShader
	)
{
	const EMaterialTessellationMode MaterialTessellationMode = Material.GetTessellationMode();

	const bool bNeedsHSDS = RHISupportsTessellation(GShaderPlatformForFeatureLevel[FeatureLevel])
		&& VertexFactoryType->SupportsTessellationShaders()
		&& MaterialTessellationMode != MTM_NoTessellation;

	if (bNeedsHSDS)
	{
		DomainShader = Material.GetShader<FAnisotropyDS>(VertexFactoryType);
		HullShader = Material.GetShader<FAnisotropyHS>(VertexFactoryType);
	}

	static const auto* CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.ShaderPipelines"));
	const bool bUseShaderPipelines = RHISupportsShaderPipelines(GShaderPlatformForFeatureLevel[FeatureLevel]) && !bNeedsHSDS && CVar && CVar->GetValueOnAnyThread() != 0;

	FShaderPipelineRef ShaderPipeline = bUseShaderPipelines ? Material.GetShaderPipeline(&AnisotropyPipeline, VertexFactoryType, false) : FShaderPipelineRef();
	if (ShaderPipeline.IsValid())
	{
		VertexShader = ShaderPipeline.GetShader<FAnisotropyVS>();
		PixelShader = ShaderPipeline.GetShader<FAnisotropyPS>();
		check(VertexShader.IsValid() && PixelShader.IsValid());
	}
	else
	{
		VertexShader = Material.GetShader<FAnisotropyVS>(VertexFactoryType);
		PixelShader = Material.GetShader<FAnisotropyPS>(VertexFactoryType);
		check(VertexShader.IsValid() && PixelShader.IsValid());
	}
}

void FAnisotropyMeshProcessor::AddMeshBatch(
	const FMeshBatch& RESTRICT MeshBatch, 
	uint64 BatchElementMask, 
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, 
	int32 StaticMeshId /* = -1 */ 
	)
{
	if (SupportsAnisotropicMaterials(FeatureLevel, GShaderPlatformForFeatureLevel[FeatureLevel]))
	{
		const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
		const FMaterial* Material = &MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, MaterialRenderProxy);
		const EBlendMode BlendMode = Material->GetBlendMode();
		const bool bIsNotTranslucent = BlendMode == BLEND_Opaque || BlendMode == BLEND_Masked;

		if (MeshBatch.bUseForMaterial && Material->MaterialUsesAnisotropy_RenderThread() && bIsNotTranslucent && Material->GetShadingModels().HasAnyShadingModel({ MSM_DefaultLit, MSM_ClearCoat }))
		{
			const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
			const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, *Material, OverrideSettings);
			const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, *Material, OverrideSettings);

			Process(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, *MaterialRenderProxy, *Material, MeshFillMode, MeshCullMode);
		}
	}
}

void FAnisotropyMeshProcessor::Process(
	const FMeshBatch& MeshBatch, 
	uint64 BatchElementMask, 
	int32 StaticMeshId, 
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy, 
	const FMaterial& RESTRICT MaterialResource,
	ERasterizerFillMode MeshFillMode, 
	ERasterizerCullMode MeshCullMode 
	)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	TMeshProcessorShaders<
		FAnisotropyVS,
		FAnisotropyHS,
		FAnisotropyDS,
		FAnisotropyPS> AnisotropyPassShaders;

	GetAnisotropyPassShaders(
		MaterialResource,
		VertexFactory->GetType(),
		FeatureLevel,
		AnisotropyPassShaders.HullShader,
		AnisotropyPassShaders.DomainShader,
		AnisotropyPassShaders.VertexShader,
		AnisotropyPassShaders.PixelShader
		);

	FMeshMaterialShaderElementData ShaderElementData;
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

	const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(AnisotropyPassShaders.VertexShader, AnisotropyPassShaders.PixelShader);

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		PassDrawRenderState,
		AnisotropyPassShaders,
		MeshFillMode,
		MeshCullMode,
		SortKey,
		EMeshPassFeatures::Default,
		ShaderElementData
		);
}

bool FDeferredShadingSceneRenderer::ShouldRenderAnisotropyPass() const
{
	if (!SupportsAnisotropicMaterials(FeatureLevel, ShaderPlatform))
	{
		return false;
	}

	if (IsAnyForwardShadingEnabled(GetFeatureLevelShaderPlatform(FeatureLevel)))
	{
		return false;
	}

	for (auto& View : Views)
	{
		if (View.ShouldRenderView() && View.ParallelMeshDrawCommandPasses[EMeshPass::AnisotropyPass].HasAnyDraw())
		{
			return true;
		}
	}

	return false;
}

BEGIN_SHADER_PARAMETER_STRUCT(FAnisotropyPassParameters, )
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void FDeferredShadingSceneRenderer::RenderAnisotropyPass(
	FRDGBuilder& GraphBuilder, 
	FRDGTextureRef SceneDepthTexture,
	bool bDoParallelPass
	)
{
	RDG_CSV_STAT_EXCLUSIVE_SCOPE(GraphBuilder, RenderAnisotropyPass);
	SCOPED_NAMED_EVENT(FDeferredShadingSceneRenderer_RenderAnisotropyPass, FColor::Emerald);
	SCOPE_CYCLE_COUNTER(STAT_AnisotropyPassDrawTime);
	RDG_GPU_STAT_SCOPE(GraphBuilder, RenderAnisotropyPass);

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);
	if (!SceneContext.GBufferF)
	{
		SceneContext.AllocateAnisotropyTarget(GraphBuilder.RHICmdList);
		check(SceneContext.GBufferF);
	}
	FRDGTextureRef GBufferFTexture = GraphBuilder.RegisterExternalTexture(SceneContext.GBufferF);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];

		if (View.ShouldRenderView())
		{
			const FParallelMeshDrawCommandPass& ParallelMeshPass = View.ParallelMeshDrawCommandPasses[EMeshPass::AnisotropyPass];

			if (!ParallelMeshPass.HasAnyDraw())
			{
				continue;
			}

			auto* PassParameters = GraphBuilder.AllocParameters<FAnisotropyPassParameters>();
			PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(SceneDepthTexture, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthRead_StencilNop);

			if (bDoParallelPass)
			{
				AddClearRenderTargetPass(GraphBuilder, GBufferFTexture);

				PassParameters->RenderTargets[0] = FRenderTargetBinding(GBufferFTexture, ERenderTargetLoadAction::ELoad);

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("AnisotropyPassParallel"),
					PassParameters,
					ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass,
					[this, &View, &ParallelMeshPass, PassParameters](FRHICommandListImmediate& RHICmdList)
				{
					Scene->UniformBuffers.UpdateViewUniformBuffer(View);
					FAnisotropyPassParallelCommandListSet ParallelCommandListSet(RHICmdList, *this, View, FParallelCommandListBindings(PassParameters));

					ParallelMeshPass.DispatchDraw(&ParallelCommandListSet, RHICmdList);
				});
			}
			else
			{
				PassParameters->RenderTargets[0] = FRenderTargetBinding(GBufferFTexture, ERenderTargetLoadAction::EClear);

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("AnisotropyPass"),
					PassParameters,
					ERDGPassFlags::Raster,
					[this, &View, &ParallelMeshPass](FRHICommandListImmediate& RHICmdList)
				{
					Scene->UniformBuffers.UpdateViewUniformBuffer(View);
					SetStereoViewport(RHICmdList, View);

					ParallelMeshPass.DispatchDraw(nullptr, RHICmdList);
				});
			}
		}
	}
}
