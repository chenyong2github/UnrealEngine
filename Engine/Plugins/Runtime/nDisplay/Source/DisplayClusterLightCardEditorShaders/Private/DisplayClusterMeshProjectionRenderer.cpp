// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterMeshProjectionRenderer.h"

#include "Components/PrimitiveComponent.h"
#include "InstanceCulling/InstanceCullingContext.h"
#include "PrimitiveSceneInfo.h"
#include "PrimitiveSceneProxy.h"
#include "MeshPassProcessor.h"
#include "MeshMaterialShader.h"
#include "MeshPassProcessor.inl"
#include "Shader.h"
#include "CanvasTypes.h"
#include "EngineModule.h"

template<EDisplayClusterMeshProjectionType ProjectionType>
class FMeshProjectionVS : public FMeshMaterialShader
{
public:
	DECLARE_SHADER_TYPE(FMeshProjectionVS, MeshMaterial);

	FMeshProjectionVS() { }
	FMeshProjectionVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
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

using FMeshPerspectiveProjectionVS = FMeshProjectionVS<EDisplayClusterMeshProjectionType::Perspective>;
using FMeshAzimuthalProjectionVS = FMeshProjectionVS<EDisplayClusterMeshProjectionType::Azimuthal>;

IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FMeshPerspectiveProjectionVS, TEXT("/Plugin/nDisplay/Private/MeshProjectionShaders.usf"), TEXT("PerspectiveVS"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FMeshAzimuthalProjectionVS, TEXT("/Plugin/nDisplay/Private/MeshProjectionShaders.usf"), TEXT("AzimuthalVS"), SF_Vertex);

class FMeshProjectionPS : public FMeshMaterialShader
{
public:
	DECLARE_SHADER_TYPE(FMeshProjectionPS, MeshMaterial);

	FMeshProjectionPS() { }
	FMeshProjectionPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
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

IMPLEMENT_MATERIAL_SHADER_TYPE(, FMeshProjectionPS, TEXT("/Plugin/nDisplay/Private/MeshProjectionShaders.usf"), TEXT("MainPS"), SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FMeshProjectionPassParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FInstanceCullingGlobalUniforms, InstanceCulling)
	SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

template<EDisplayClusterMeshProjectionType ProjectionType> 
class FLightCardEditorMeshPassProcessor : public FMeshPassProcessor
{
public:
	FLightCardEditorMeshPassProcessor(const FScene* InScene, const FSceneView* InView, FMeshPassDrawListContext* InDrawListContext)
		: FMeshPassProcessor(InScene, GMaxRHIFeatureLevel, InView, InDrawListContext)
		, DrawRenderState(*InView)
	{
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
		DrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());
	}

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final
	{
		const FMaterialRenderProxy* FallbackMaterialRenderProxy = nullptr;
		const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxy);
		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxy ? *FallbackMaterialRenderProxy : *MeshBatch.MaterialRenderProxy;

		const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

		TMeshProcessorShaders<FMeshProjectionVS<ProjectionType>, FMeshProjectionPS> PassShaders;

		FMaterialShaderTypes ShaderTypes;
		ShaderTypes.AddShaderType<FMeshProjectionVS<ProjectionType>>();
		ShaderTypes.AddShaderType<FMeshProjectionPS>();

		FMaterialShaders Shaders;
		if (!Material.TryGetShaders(ShaderTypes, VertexFactory->GetType(), Shaders))
		{
			return;
		}

		Shaders.TryGetVertexShader(PassShaders.VertexShader);
		Shaders.TryGetPixelShader(PassShaders.PixelShader);

		const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
		ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material, OverrideSettings);
		ERasterizerCullMode MeshCullMode = CM_None;

		FMeshMaterialShaderElementData ShaderElementData;
		ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

		FMeshDrawCommandSortKey SortKey{};

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

void FDisplayClusterMeshProjectionRenderer::AddActor(AActor* Actor)
{
	AddActor(Actor, [](const UPrimitiveComponent* PrimitiveComponent)
	{
		return !PrimitiveComponent->bHiddenInGame;
	});
}

void FDisplayClusterMeshProjectionRenderer::AddActor(AActor* Actor, const TFunctionRef<bool(const UPrimitiveComponent*)>& PrimitiveFilter)
{
	Actor->ForEachComponent<UPrimitiveComponent>(true, [&](UPrimitiveComponent* PrimitiveComponent)
	{
		if (PrimitiveFilter(PrimitiveComponent))
		{
			PrimitiveComponents.Add(PrimitiveComponent);
		}
	});
}

void FDisplayClusterMeshProjectionRenderer::RemoveActor(AActor* Actor)
{
	PrimitiveComponents.RemoveAll([Actor](TWeakObjectPtr<UPrimitiveComponent>& PrimitiveComponent)
	{
		return !PrimitiveComponent.IsValid() || PrimitiveComponent->GetOwner() == Actor;
	});
}

void FDisplayClusterMeshProjectionRenderer::ClearScene()
{
	PrimitiveComponents.Empty();
}

void FDisplayClusterMeshProjectionRenderer::Render(FCanvas* Canvas, FSceneInterface* Scene, const FSceneViewInitOptions& ViewInitOptions, EDisplayClusterMeshProjectionType ProjectionType)
{
	Canvas->Flush_GameThread();
	FRenderTarget* RenderTarget = Canvas->GetRenderTarget();

	ENQUEUE_RENDER_COMMAND(FDrawProjectedMeshes)(
		[RenderTarget, Scene, ViewInitOptions, ProjectionType, this](FRHICommandListImmediate& RHICmdList)
		{
			FMemMark Mark(FMemStack::Get());
			FRDGBuilder GraphBuilder(RHICmdList);

			FSceneViewFamily ViewFamily(FSceneViewFamily::ConstructionValues(
				RenderTarget,
				Scene,
				FEngineShowFlags(ESFIM_Editor))
				.SetTime(FGameTime::GetTimeSinceAppStart())
				.SetGammaCorrection(1.0f));

			if (Scene)
			{
				Scene->IncrementFrameNumber();
				ViewFamily.FrameNumber = Scene->GetFrameNumber();
			}
			else
			{
				ViewFamily.FrameNumber = GFrameNumber;
			}

			FScenePrimitiveRenderingContextScopeHelper ScenePrimitiveRenderingContextScopeHelper(GetRendererModule().BeginScenePrimitiveRendering(GraphBuilder, &ViewFamily));

			FSceneViewInitOptions NewInitOptions(ViewInitOptions);
			NewInitOptions.ViewFamily = &ViewFamily;

			GetRendererModule().CreateAndInitSingleView(RHICmdList, &ViewFamily, &NewInitOptions);
			const FSceneView* View = ViewFamily.Views[0];

			FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(RenderTarget->GetRenderTargetTexture(), TEXT("ViewRenderTarget")));

			FMeshProjectionPassParameters* PassParameters = GraphBuilder.AllocParameters<FMeshProjectionPassParameters>();
			PassParameters->View = View->ViewUniformBuffer;
			PassParameters->InstanceCulling = FInstanceCullingContext::CreateDummyInstanceCullingUniformBuffer(GraphBuilder);
			PassParameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ELoad);

			GraphBuilder.AddPass(RDG_EVENT_NAME("MeshProjectionRenderer"), PassParameters, ERDGPassFlags::Raster | ERDGPassFlags::NeverCull, [View, ProjectionType, this](FRHICommandList& RHICmdList)
			{
				FIntRect ViewRect = View->UnscaledViewRect;
				RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

				switch (ProjectionType)
				{
					case EDisplayClusterMeshProjectionType::Azimuthal:
						RenderPrimitives_RenderThread<EDisplayClusterMeshProjectionType::Azimuthal>(View, RHICmdList);
						break;

					case EDisplayClusterMeshProjectionType::Perspective:
					default:
						RenderPrimitives_RenderThread<EDisplayClusterMeshProjectionType::Perspective>(View, RHICmdList);
						break;
				}
			});

			GraphBuilder.Execute();
		});
}

template<EDisplayClusterMeshProjectionType ProjectionType>
void FDisplayClusterMeshProjectionRenderer::RenderPrimitives_RenderThread(const FSceneView* View, FRHICommandList& RHICmdList)
{
	DrawDynamicMeshPass(*View, RHICmdList, [View, this](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
	{
		TArray<FPrimitiveSceneProxy*> PrimitiveSceneProxies;
		for (const TWeakObjectPtr<UPrimitiveComponent>& PrimitiveComponent : PrimitiveComponents)
		{
			if (PrimitiveComponent.IsValid() && PrimitiveComponent->SceneProxy)
			{
				PrimitiveSceneProxies.Add(PrimitiveComponent->SceneProxy);
			}
		}

		FLightCardEditorMeshPassProcessor<ProjectionType> MeshProcessor(nullptr, View, DynamicMeshPassContext);

		for (FPrimitiveSceneProxy* PrimitiveProxy : PrimitiveSceneProxies)
		{
			if (const FMeshBatch* MeshBatch = PrimitiveProxy->GetPrimitiveSceneInfo()->GetMeshBatch(PrimitiveProxy->GetPrimitiveSceneInfo()->StaticMeshes.Num() - 1))
			{
				const uint64 BatchElementMask = ~0ull;
				MeshProcessor.AddMeshBatch(*MeshBatch, BatchElementMask, PrimitiveProxy);
			}
		}
	});
}

// Explicit template specialisations for RenderPrimitives_RenderThread
template void FDisplayClusterMeshProjectionRenderer::RenderPrimitives_RenderThread<EDisplayClusterMeshProjectionType::Perspective>(const FSceneView* View, FRHICommandList& RHICmdList);
template void FDisplayClusterMeshProjectionRenderer::RenderPrimitives_RenderThread<EDisplayClusterMeshProjectionType::Azimuthal>(const FSceneView* View, FRHICommandList& RHICmdList);