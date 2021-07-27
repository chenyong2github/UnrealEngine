// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenTranslucencyRadianceCache.cpp
=============================================================================*/

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "VolumeLighting.h"
#include "LumenSceneUtils.h"
#include "MeshPassProcessor.inl"
#include "LumenTranslucencyVolumeLighting.h"
#include "LumenRadianceCache.h"

int32 GLumenTranslucencyRadianceCache = 1;
FAutoConsoleVariableRef CVarLumenTranslucencyRadianceCache(
	TEXT("r.Lumen.TranslucencyRadianceCache.Enable"),
	GLumenTranslucencyRadianceCache,
	TEXT("Whether to use the Radiance Cache to provide Lumen Reflections on Translucent Surfaces."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenTranslucencyRadianceCacheDownsampleFactor = 4;
FAutoConsoleVariableRef CVarLumenTranslucencyRadianceCacheDownsampleFactor(
	TEXT("r.Lumen.TranslucencyRadianceCache.MarkDownsampleFactor"),
	GLumenTranslucencyRadianceCacheDownsampleFactor,
	TEXT("Downsample factor for marking translucent surfaces in the Lumen Radiance Cache.  Too low of factors will cause incorrect Radiance Cache coverage.  Should be a power of 2."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenTranslucencyRadianceCacheReprojectionRadiusScale = 10;
FAutoConsoleVariableRef CVarLumenTranslucencyRadianceCacheReprojectionRadiusScale(
	TEXT("r.Lumen.TranslucencyRadianceCache.ReprojectionRadiusScale"),
	GLumenTranslucencyRadianceCacheReprojectionRadiusScale,
	TEXT("Larger values treat the Radiance Cache lighting as more distant."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FLumenTranslucencyRadianceCacheMarkPassUniformParameters, )
	SHADER_PARAMETER_STRUCT(FSceneTextureUniformParameters, SceneTextures)
	SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheMarkParameters, RadianceCacheMarkParameters)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, FurthestHZBTexture)
	SHADER_PARAMETER(FVector2D, ViewportUVToHZBBufferUV)
	SHADER_PARAMETER(float, HZBMipLevel)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FLumenTranslucencyRadianceCacheMarkPassUniformParameters, "LumenTranslucencyRadianceCacheMarkPass", SceneTextures);

class FLumenTranslucencyRadianceCacheMarkVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FLumenTranslucencyRadianceCacheMarkVS, MeshMaterial);

protected:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform) && IsTranslucentBlendMode(Parameters.MaterialParameters.BlendMode) && Parameters.MaterialParameters.bIsTranslucencySurface;
	}

	FLumenTranslucencyRadianceCacheMarkVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{}

	FLumenTranslucencyRadianceCacheMarkVS() = default;
};


IMPLEMENT_MATERIAL_SHADER_TYPE(, FLumenTranslucencyRadianceCacheMarkVS, TEXT("/Engine/Private/Lumen/LumenTranslucencyRadianceCacheMarkShaders.usf"), TEXT("MainVS"), SF_Vertex);

class FLumenTranslucencyRadianceCacheMarkPS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FLumenTranslucencyRadianceCacheMarkPS, MeshMaterial);

public:
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform) && IsTranslucentBlendMode(Parameters.MaterialParameters.BlendMode) && Parameters.MaterialParameters.bIsTranslucencySurface;
	}

	FLumenTranslucencyRadianceCacheMarkPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{}

	FLumenTranslucencyRadianceCacheMarkPS() = default;
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FLumenTranslucencyRadianceCacheMarkPS, TEXT("/Engine/Private/Lumen/LumenTranslucencyRadianceCacheMarkShaders.usf"), TEXT("MainPS"), SF_Pixel);

class FLumenTranslucencyRadianceCacheMarkMeshProcessor : public FMeshPassProcessor
{
public:

	FLumenTranslucencyRadianceCacheMarkMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, const FMeshPassProcessorRenderState& InPassDrawRenderState, FMeshPassDrawListContext* InDrawListContext);

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final;

	FMeshPassProcessorRenderState PassDrawRenderState;
};

bool GetLumenTranslucencyRadianceCacheMarkShaders(
	const FMaterial& Material,
	FVertexFactoryType* VertexFactoryType,
	TShaderRef<FLumenTranslucencyRadianceCacheMarkVS>& VertexShader,
	TShaderRef<FLumenTranslucencyRadianceCacheMarkPS>& PixelShader)
{
	FMaterialShaderTypes ShaderTypes;
	ShaderTypes.AddShaderType<FLumenTranslucencyRadianceCacheMarkVS>();
	ShaderTypes.AddShaderType<FLumenTranslucencyRadianceCacheMarkPS>();

	FMaterialShaders Shaders;
	if (!Material.TryGetShaders(ShaderTypes, VertexFactoryType, Shaders))
	{
		return false;
	}

	Shaders.TryGetVertexShader(VertexShader);
	Shaders.TryGetPixelShader(PixelShader);
	return true;
}

void FLumenTranslucencyRadianceCacheMarkMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	LLM_SCOPE_BYTAG(Lumen);

	if (MeshBatch.bUseForMaterial && DoesPlatformSupportLumenGI(GetFeatureLevelShaderPlatform(FeatureLevel)))
	{
		const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
		const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);

		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;

		const EBlendMode BlendMode = Material.GetBlendMode();
		const bool bIsTranslucent = IsTranslucentBlendMode(BlendMode);
		const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
		const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material, OverrideSettings);
		const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, Material, OverrideSettings);
		const ETranslucencyLightingMode TranslucencyLightingMode = Material.GetTranslucencyLightingMode();

		if (bIsTranslucent
			&& (TranslucencyLightingMode == TLM_Surface || TranslucencyLightingMode == TLM_SurfacePerPixelLighting)
			&& PrimitiveSceneProxy && PrimitiveSceneProxy->ShouldRenderInMainPass()
			&& ShouldIncludeDomainInMeshPass(Material.GetMaterialDomain()))
		{
			const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;
			FVertexFactoryType* VertexFactoryType = VertexFactory->GetType();

			TMeshProcessorShaders<
				FLumenTranslucencyRadianceCacheMarkVS,
				FLumenTranslucencyRadianceCacheMarkPS> PassShaders;

			if (!GetLumenTranslucencyRadianceCacheMarkShaders(
				Material,
				VertexFactory->GetType(),
				PassShaders.VertexShader,
				PassShaders.PixelShader))
			{
				return;
			}

			FMeshMaterialShaderElementData ShaderElementData;
			ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

			const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(PassShaders.VertexShader, PassShaders.PixelShader);

			BuildMeshDrawCommands(
				MeshBatch,
				BatchElementMask,
				PrimitiveSceneProxy,
				MaterialRenderProxy,
				Material,
				PassDrawRenderState,
				PassShaders,
				MeshFillMode,
				MeshCullMode,
				SortKey,
				EMeshPassFeatures::Default,
				ShaderElementData);
		}
	}
}

FLumenTranslucencyRadianceCacheMarkMeshProcessor::FLumenTranslucencyRadianceCacheMarkMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, const FMeshPassProcessorRenderState& InPassDrawRenderState, FMeshPassDrawListContext* InDrawListContext)
	: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
	, PassDrawRenderState(InPassDrawRenderState)
{}

FMeshPassProcessor* CreateLumenTranslucencyRadianceCacheMarkPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	LLM_SCOPE_BYTAG(Lumen);

	FMeshPassProcessorRenderState PassState;

	// We use HZB tests in the shader instead of hardware depth testing
	PassState.SetDepthStencilState(TStaticDepthStencilState<false>::GetRHI());

	PassState.SetBlendState(TStaticBlendState<>::GetRHI());

	return new(FMemStack::Get()) FLumenTranslucencyRadianceCacheMarkMeshProcessor(Scene, InViewIfDynamicMeshCommand, PassState, InDrawListContext);
}

FRegisterPassProcessorCreateFunction RegisterLumenTranslucencyRadianceCacheMarkPass(&CreateLumenTranslucencyRadianceCacheMarkPassProcessor, EShadingPath::Deferred, EMeshPass::LumenTranslucencyRadianceCacheMark, EMeshPassFlags::MainView);

BEGIN_SHADER_PARAMETER_STRUCT(FLumenTranslucencyRadianceCacheMarkParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FViewShaderParameters, View)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenTranslucencyRadianceCacheMarkPassUniformParameters, MarkPass)
	SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void LumenTranslucencyRadianceCacheMarkUsedProbes(
	FRDGBuilder& GraphBuilder,
	const FSceneRenderer& SceneRenderer,
	FViewInfo& View,
	const FSceneTextures& SceneTextures,
	const LumenRadianceCache::FRadianceCacheMarkParameters& RadianceCacheMarkParameters)
{
	check(GLumenTranslucencyRadianceCache != 0);

	const EMeshPass::Type MeshPass = EMeshPass::LumenTranslucencyRadianceCacheMark;
	const float ViewportScale = 1.0f / GLumenTranslucencyRadianceCacheDownsampleFactor;
	FIntRect DownsampledViewRect = GetScaledRect(View.ViewRect, ViewportScale);

	View.BeginRenderView();

	FLumenTranslucencyRadianceCacheMarkParameters* PassParameters = GraphBuilder.AllocParameters<FLumenTranslucencyRadianceCacheMarkParameters>();

	{
		FViewUniformShaderParameters DownsampledTranslucencyViewParameters = *View.CachedViewUniformShaderParameters;

		FViewMatrices ViewMatrices = View.ViewMatrices;
		FViewMatrices PrevViewMatrices = View.PrevViewInfo.ViewMatrices;

		// Update the parts of DownsampledTranslucencyParameters which are dependent on the buffer size and view rect
		View.SetupViewRectUniformBufferParameters(
			DownsampledTranslucencyViewParameters,
			SceneTextures.Config.Extent,
			DownsampledViewRect,
			ViewMatrices,
			PrevViewMatrices);

		PassParameters->View.View = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(DownsampledTranslucencyViewParameters, UniformBuffer_SingleFrame);
		
		if (const FViewInfo* InstancedView = View.GetInstancedView())
		{
			InstancedView->SetupViewRectUniformBufferParameters(
				DownsampledTranslucencyViewParameters,
				SceneTextures.Config.Extent,
				GetScaledRect(InstancedView->ViewRect, ViewportScale),
				ViewMatrices,
				PrevViewMatrices);

			PassParameters->View.InstancedView = TUniformBufferRef<FInstancedViewUniformShaderParameters>::CreateUniformBufferImmediate(
				reinterpret_cast<const FInstancedViewUniformShaderParameters&>(DownsampledTranslucencyViewParameters),
				UniformBuffer_SingleFrame);
		}
	}

	{
		FLumenTranslucencyRadianceCacheMarkPassUniformParameters& MarkPassParameters = *GraphBuilder.AllocParameters<FLumenTranslucencyRadianceCacheMarkPassUniformParameters>();
		SetupSceneTextureUniformParameters(GraphBuilder, View.FeatureLevel, ESceneTextureSetupMode::All, MarkPassParameters.SceneTextures);
		MarkPassParameters.RadianceCacheMarkParameters = RadianceCacheMarkParameters;

		MarkPassParameters.FurthestHZBTexture = View.HZB;
		MarkPassParameters.ViewportUVToHZBBufferUV = FVector2D(
				float(View.ViewRect.Width()) / float(2 * View.HZBMipmap0Size.X),
				float(View.ViewRect.Height()) / float(2 * View.HZBMipmap0Size.Y));
		MarkPassParameters.HZBMipLevel = FMath::Max<float>((int32)FMath::FloorLog2((float)GLumenTranslucencyRadianceCacheDownsampleFactor) - 1, 0.0f);

		PassParameters->MarkPass = GraphBuilder.CreateUniformBuffer(&MarkPassParameters);
	}

	View.ParallelMeshDrawCommandPasses[MeshPass].BuildRenderingCommands(GraphBuilder, SceneRenderer.Scene->GPUScene, PassParameters->InstanceCullingDrawParams);

	RDG_EVENT_SCOPE(GraphBuilder, "TranslucentSurfacesMarkPass");

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("LumenTranslucencyRadianceCacheMark"),
		PassParameters,
		ERDGPassFlags::Raster,
		[&View, &SceneRenderer, MeshPass, PassParameters, ViewportScale](FRHICommandListImmediate& RHICmdList)
	{
		SceneRenderer.SetStereoViewport(RHICmdList, View, ViewportScale);
		View.ParallelMeshDrawCommandPasses[MeshPass].DispatchDraw(nullptr, RHICmdList, &PassParameters->InstanceCullingDrawParams);
	});
}
