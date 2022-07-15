// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterShadersPreprocess_UVLightCards.h"

#include "Components/PrimitiveComponent.h"
#include "EngineModule.h"
#include "EngineUtils.h"
#include "InstanceCulling/InstanceCullingContext.h"
#include "MeshMaterialShader.h"
#include "MeshPassProcessor.h"
#include "PrimitiveSceneInfo.h"
#include "PrimitiveSceneProxy.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphResources.h"
#include "Renderer/Private/ScenePrivate.h"
#include "SceneRenderTargetParameters.h"
#include "MeshPassProcessor.inl"

class FUVLightCardVS : public FMeshMaterialShader
{
public:
	DECLARE_SHADER_TYPE(FUVLightCardVS, MeshMaterial);

	FUVLightCardVS() { }
	FUVLightCardVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		PassUniformBuffer.Bind(Initializer.ParameterMap, FSceneTextureUniformParameters::StaticStructMetadata.GetShaderVariableName());
	}
	              
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return EnumHasAllFlags(Parameters.Flags, EShaderPermutationFlags::HasEditorOnlyData)
			&& IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5)
			&& Parameters.VertexFactoryType == FindVertexFactoryType(TEXT("FLocalVertexFactory"));
	}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FUVLightCardVS, TEXT("/Plugin/nDisplay/Private/UVLightCardShaders.usf"), TEXT("MainVS"), SF_Vertex);

class FUVLightCardPS : public FMeshMaterialShader
{
public:
	DECLARE_SHADER_TYPE(FUVLightCardPS, MeshMaterial);

	FUVLightCardPS() { }
	FUVLightCardPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
		PassUniformBuffer.Bind(Initializer.ParameterMap, FSceneTextureUniformParameters::StaticStructMetadata.GetShaderVariableName());
	}

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return EnumHasAllFlags(Parameters.Flags, EShaderPermutationFlags::HasEditorOnlyData)
			&& IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5)
			&& Parameters.VertexFactoryType == FindVertexFactoryType(TEXT("FLocalVertexFactory"));
	}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FUVLightCardPS, TEXT("/Plugin/nDisplay/Private/UVLightCardShaders.usf"), TEXT("MainPS"), SF_Pixel);

class FUVLightCardPassProcessor : public FMeshPassProcessor
{
public:
	FUVLightCardPassProcessor(const FSceneView* InView, FMeshPassDrawListContext* InDrawListContext)
		: FMeshPassProcessor(nullptr, GMaxRHIFeatureLevel, InView, InDrawListContext)
	{
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
		DrawRenderState.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI());
	}

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final
	{
		const FMaterialRenderProxy* FallbackMaterialRenderProxy = nullptr;
		const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxy);
		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxy ? *FallbackMaterialRenderProxy : *MeshBatch.MaterialRenderProxy;

		const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

		TMeshProcessorShaders<FUVLightCardVS, FUVLightCardPS> PassShaders;

		FMaterialShaderTypes ShaderTypes;
		ShaderTypes.AddShaderType<FUVLightCardVS>();
		ShaderTypes.AddShaderType<FUVLightCardPS>();

		FMaterialShaders Shaders;
		if (!Material.TryGetShaders(ShaderTypes, VertexFactory->GetType(), Shaders))
		{
			return;
		}

		Shaders.TryGetVertexShader(PassShaders.VertexShader);
		Shaders.TryGetPixelShader(PassShaders.PixelShader);

		FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
		ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material, OverrideSettings);
		ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, Material, OverrideSettings);

		FMeshMaterialShaderElementData ShaderElementData;
		ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

		FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(PassShaders.VertexShader.GetShader(), PassShaders.PixelShader.GetShader());

		BuildMeshDrawCommands(
			MeshBatch,
			BatchElementMask,
			PrimitiveSceneProxy,
			MaterialRenderProxy,
			Material,
			DrawRenderState,
			PassShaders,
			MeshFillMode,
			MeshCullMode,
			SortKey,
			EMeshPassFeatures::Default,
			ShaderElementData);
	}

private:
	FMeshPassProcessorRenderState DrawRenderState;
};

BEGIN_SHADER_PARAMETER_STRUCT(FUVLightCardPassParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FInstanceCullingGlobalUniforms, InstanceCulling)
	SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

DECLARE_GPU_STAT_NAMED(nDisplay_UVLightCards_Render, TEXT("nDisplay UVLightCards::Render"));

bool FDisplayClusterShadersPreprocess_UVLightCards::RenderPreprocess_UVLightCards(FRHICommandListImmediate& RHICmdList, FSceneInterface* InScene, FRenderTarget* InRenderTarget, float ProjectionPlaneSize)
{
	check(IsInRenderingThread());

	if (InRenderTarget == nullptr || InScene->GetRenderScene()->PrimitiveSceneProxies.IsEmpty())
	{
		return false;
	}

	SCOPED_GPU_STAT(RHICmdList, nDisplay_UVLightCards_Render);
	SCOPED_DRAW_EVENT(RHICmdList, nDisplay_UVLightCards_Render);

	FRDGBuilder GraphBuilder(RHICmdList);

	FEngineShowFlags EngineShowFlags(ESFIM_Game);

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		InRenderTarget,
		InScene,
		EngineShowFlags)
		.SetTime(FGameTime::GetTimeSinceAppStart())
		.SetGammaCorrection(1.0f));

	FScenePrimitiveRenderingContextScopeHelper ScenePrimitiveRenderingContextScopeHelper(GetRendererModule().BeginScenePrimitiveRendering(GraphBuilder, &ViewFamily));

	FSceneViewInitOptions ViewInitOptions;

	ViewInitOptions.ViewFamily = &ViewFamily;
	ViewInitOptions.ViewLocation = FVector::ZeroVector;
	ViewInitOptions.ViewRotation = FRotator::ZeroRotator;
	ViewInitOptions.ViewOrigin = ViewInitOptions.ViewLocation;

	ViewInitOptions.SetViewRectangle(FIntRect(FIntPoint(0, 0), InRenderTarget->GetSizeXY()));

	ViewInitOptions.ViewRotationMatrix = FInverseRotationMatrix(ViewInitOptions.ViewRotation) * FMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));

	const float ZScale = 0.5f / HALF_WORLD_MAX;
	const float ZOffset = HALF_WORLD_MAX;
	const float OrthoSize = 0.5f * ProjectionPlaneSize;

	if ((bool)ERHIZBuffer::IsInverted)
	{
		ViewInitOptions.ProjectionMatrix = FReversedZOrthoMatrix(
			OrthoSize,
			OrthoSize,
			ZScale,
			ZOffset
		);
	}
	else
	{
		ViewInitOptions.ProjectionMatrix = FOrthoMatrix(
			OrthoSize,
			OrthoSize,
			ZScale,
			ZOffset
		);
	}

	ViewInitOptions.BackgroundColor = FLinearColor::Transparent;

	GetRendererModule().CreateAndInitSingleView(RHICmdList, &ViewFamily, &ViewInitOptions);
	const FSceneView* View = ViewFamily.Views[0];

	FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(InRenderTarget->GetRenderTargetTexture(), TEXT("UVLightCardRenderTarget")));
	FRenderTargetBinding OutputRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::EClear);

	FUVLightCardPassParameters* PassParameters = GraphBuilder.AllocParameters<FUVLightCardPassParameters>();
	PassParameters->View = View->ViewUniformBuffer;
	PassParameters->InstanceCulling = FInstanceCullingContext::CreateDummyInstanceCullingUniformBuffer(GraphBuilder);
	PassParameters->RenderTargets[0] = OutputRenderTargetBinding;

	GraphBuilder.AddPass(RDG_EVENT_NAME("DisplayClusterUVLightCards::Render"),
		PassParameters,
		ERDGPassFlags::Raster | ERDGPassFlags::NeverCull,
		[View, InScene](FRHICommandList& RHICmdList)
		{
			FIntRect ViewRect = View->UnscaledViewRect;
			RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

			DrawDynamicMeshPass(*View, RHICmdList, [View, InScene](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
			{
				TArray<FPrimitiveSceneProxy*> SceneProxies;
				for (FPrimitiveSceneProxy* SceneProxy : InScene->GetRenderScene()->PrimitiveSceneProxies)
				{
					SceneProxies.Add(SceneProxy);
				}

				FUVLightCardPassProcessor MeshPassProcessor(View, DynamicMeshPassContext);
				for (FPrimitiveSceneProxy* SceneProxy : SceneProxies)
				{
					if (const FMeshBatch* MeshBatch = SceneProxy->GetPrimitiveSceneInfo()->GetMeshBatch(SceneProxy->GetPrimitiveSceneInfo()->StaticMeshes.Num() - 1))
					{
						MeshBatch->MaterialRenderProxy->UpdateUniformExpressionCacheIfNeeded(View->GetFeatureLevel());

						const uint64 BatchElementMask = ~0ull;
						MeshPassProcessor.AddMeshBatch(*MeshBatch, BatchElementMask, SceneProxy);
					}
				}
			});
		});

	GraphBuilder.Execute();

	return true;
}