// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterMeshProjectionRenderer.h"

#include "PrimitiveSceneInfo.h"
#include "PrimitiveSceneProxy.h"
#include "MeshPassProcessor.h"
#include "MeshMaterialShader.h"
#include "MeshPassProcessor.inl"
#include "Shader.h"
#include "CanvasTypes.h"
#include "EngineModule.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"
#include "Components/PrimitiveComponent.h"
#include "InstanceCulling/InstanceCullingContext.h"
#include "Materials/Material.h"


//////////////////////////////////////////////////////////////////////////
// Base Render Pass

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
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
		DrawRenderState.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI());
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


//////////////////////////////////////////////////////////////////////////
// Hit Proxy Render Pass

class FMeshProjectionHitProxyShaderElementData : public FMeshMaterialShaderElementData
{
public:
	FMeshProjectionHitProxyShaderElementData(FHitProxyId InBatchHitProxyId)
		: BatchHitProxyId(InBatchHitProxyId)
	{
	}

	FHitProxyId BatchHitProxyId;
};

class FMeshProjectionHitProxyPS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FMeshProjectionHitProxyPS, MeshMaterial);

public:
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		// Only compile the hit proxy shader on desktop editor platforms
		return IsPCPlatform(Parameters.Platform) && EnumHasAllFlags(Parameters.Flags, EShaderPermutationFlags::HasEditorOnlyData)
			// and only compile for default materials or materials that are masked.
			&& (Parameters.MaterialParameters.bIsSpecialEngineMaterial ||
				!Parameters.MaterialParameters.bWritesEveryPixel ||
				Parameters.MaterialParameters.bMaterialMayModifyMeshPosition ||
				Parameters.MaterialParameters.bIsTwoSided);
	}

	FMeshProjectionHitProxyPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FMeshMaterialShader(Initializer)
	{
		HitProxyId.Bind(Initializer.ParameterMap, TEXT("HitProxyId"), SPF_Optional);
	}

	FMeshProjectionHitProxyPS() {}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FMeshProjectionHitProxyShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);

		ShaderBindings.Add(HitProxyId, ShaderElementData.BatchHitProxyId.GetColor().ReinterpretAsLinear());
	}

private:
	LAYOUT_FIELD(FShaderParameter, HitProxyId)
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FMeshProjectionHitProxyPS, TEXT("/Plugin/nDisplay/Private/MeshProjectionHitProxy.usf"), TEXT("Main") ,SF_Pixel);

template<EDisplayClusterMeshProjectionType ProjectionType> 
class FLightCardEditorHitProxyMeshPassProcessor : public FMeshPassProcessor
{
public:
	FLightCardEditorHitProxyMeshPassProcessor(const FScene* InScene, const FSceneView* InView, FMeshPassDrawListContext* InDrawListContext)
		: FMeshPassProcessor(InScene, GMaxRHIFeatureLevel, InView, InDrawListContext)
		, DrawRenderState(*InView)
	{
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
		DrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());
	}

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final
	{
		const bool bDrawMeshBatch = MeshBatch.bUseForMaterial
			&& MeshBatch.BatchHitProxyId != FHitProxyId::InvisibleHitProxyId
			&& MeshBatch.bSelectable
			&& PrimitiveSceneProxy
			&& PrimitiveSceneProxy->IsSelectable();

		if (bDrawMeshBatch)
		{
			const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
			const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
			if (Material && Material->GetRenderingThreadShaderMap())
			{
				if (Material->WritesEveryPixel() && !Material->IsTwoSided() && !Material->MaterialModifiesMeshPosition_RenderThread())
				{
					// Default material doesn't handle masked, and doesn't have the correct bIsTwoSided setting.
					MaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
					check(MaterialRenderProxy);
					Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
					check(Material);
				}

				const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

				TMeshProcessorShaders<FMeshProjectionVS<ProjectionType>, FMeshProjectionHitProxyPS> PassShaders;

				FMaterialShaderTypes ShaderTypes;
				ShaderTypes.AddShaderType<FMeshProjectionVS<ProjectionType>>();
				ShaderTypes.AddShaderType<FMeshProjectionHitProxyPS>();

				FMaterialShaders Shaders;
				if (!Material->TryGetShaders(ShaderTypes, VertexFactory->GetType(), Shaders))
				{
					return;
				}

				Shaders.TryGetVertexShader(PassShaders.VertexShader);
				Shaders.TryGetPixelShader(PassShaders.PixelShader);

				const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
				ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, *Material, OverrideSettings);
				ERasterizerCullMode MeshCullMode = CM_None;

				FMeshProjectionHitProxyShaderElementData ShaderElementData(MeshBatch.BatchHitProxyId);
				ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

				const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(PassShaders.VertexShader, PassShaders.PixelShader);

				BuildMeshDrawCommands(
					MeshBatch,
					BatchElementMask,
					PrimitiveSceneProxy,
					*MaterialRenderProxy,
					*Material,
					DrawRenderState,
					PassShaders,
					MeshFillMode,
					MeshCullMode,
					SortKey,
					EMeshPassFeatures::Default,
					ShaderElementData);
			}
		}
	}

private:
	FMeshPassProcessorRenderState DrawRenderState;
};


//////////////////////////////////////////////////////////////////////////
// Selection Outline Render Pass

class FMeshProjectionSelectionOutlinePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMeshProjectionSelectionOutlinePS);
	SHADER_USE_PARAMETER_STRUCT(FMeshProjectionSelectionOutlinePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Color)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Depth)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ColorTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, ColorSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DepthTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, DepthSampler)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EditorPrimitivesDepth)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, EditorPrimitivesStencil)
		SHADER_PARAMETER(FVector3f, OutlineColor)
		SHADER_PARAMETER(float, SelectionHighlightIntensity)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		// Only PC platforms render editor primitives.
		return IsPCPlatform(Parameters.Platform) && EnumHasAllFlags(Parameters.Flags, EShaderPermutationFlags::HasEditorOnlyData);
	}
};

IMPLEMENT_GLOBAL_SHADER(FMeshProjectionSelectionOutlinePS, "/Plugin/nDisplay/Private/MeshProjectionSelectionOutline.usf", "Main", SF_Pixel);

template<EDisplayClusterMeshProjectionType ProjectionType> 
class FLightCardEditorSelectionPassProcessor : public FMeshPassProcessor
{
public:
	FLightCardEditorSelectionPassProcessor(const FScene* InScene, const FSceneView* InView, FMeshPassDrawListContext* InDrawListContext)
		: FMeshPassProcessor(InScene, GMaxRHIFeatureLevel, InView, InDrawListContext)
		, DrawRenderState(*InView)
		, StencilValue(1)
	{
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual, true, CF_Always, SO_Keep, SO_Keep, SO_Replace>::GetRHI());
		DrawRenderState.SetBlendState(TStaticBlendStateWriteMask<CW_NONE, CW_NONE, CW_NONE, CW_NONE>::GetRHI());
	}

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final
	{
		const bool bDrawMeshBatch = MeshBatch.bUseForMaterial
			&& MeshBatch.bUseSelectionOutline
			&& PrimitiveSceneProxy
			&& PrimitiveSceneProxy->WantsSelectionOutline()
			&& (PrimitiveSceneProxy->IsSelected() || PrimitiveSceneProxy->IsHovered());

		if (bDrawMeshBatch)
		{
			const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
			const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
			if (Material && Material->GetRenderingThreadShaderMap())
			{
				if (Material->WritesEveryPixel() && !Material->IsTwoSided() && !Material->MaterialModifiesMeshPosition_RenderThread())
				{
					// Default material doesn't handle masked, and doesn't have the correct bIsTwoSided setting.
					MaterialRenderProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
					check(MaterialRenderProxy);
					Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
					check(Material);
				}
				
				const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

				TMeshProcessorShaders<FMeshProjectionVS<ProjectionType>, FMeshProjectionPS> PassShaders;

				FMaterialShaderTypes ShaderTypes;
				ShaderTypes.AddShaderType<FMeshProjectionVS<ProjectionType>>();
				ShaderTypes.AddShaderType<FMeshProjectionPS>();

				FMaterialShaders Shaders;
				if (!Material->TryGetShaders(ShaderTypes, VertexFactory->GetType(), Shaders))
				{
					return;
				}

				Shaders.TryGetVertexShader(PassShaders.VertexShader);
				Shaders.TryGetPixelShader(PassShaders.PixelShader);

				const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
				ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, *Material, OverrideSettings);
				ERasterizerCullMode MeshCullMode = CM_None;

				DrawRenderState.SetStencilRef(StencilValue);

				FMeshMaterialShaderElementData ShaderElementData;
				ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

				FMeshDrawCommandSortKey SortKey{};

				BuildMeshDrawCommands(
					MeshBatch,
					BatchElementMask,
					PrimitiveSceneProxy,
					*MaterialRenderProxy,
					*Material,
					DrawRenderState,
					PassShaders,
					MeshFillMode,
					MeshCullMode,
					SortKey,
					EMeshPassFeatures::Default,
					ShaderElementData);
			}
		}
	}

private:
	FMeshPassProcessorRenderState DrawRenderState;
	uint32 StencilValue;
};

namespace
{
	template<typename TSetupFunction>
	void DrawScreenPass(
		FRHICommandList& RHICmdList,
		const FSceneView& View,
		const FScreenPassTextureViewport& OutputViewport,
		const FScreenPassTextureViewport& InputViewport,
		const FScreenPassPipelineState& PipelineState,
		TSetupFunction SetupFunction)
	{
		PipelineState.Validate();

		const FIntRect InputRect = InputViewport.Rect;
		const FIntPoint InputSize = InputViewport.Extent;
		const FIntRect OutputRect = OutputViewport.Rect;
		const FIntPoint OutputSize = OutputRect.Size();

		RHICmdList.SetViewport(OutputRect.Min.X, OutputRect.Min.Y, 0.0f, OutputRect.Max.X, OutputRect.Max.Y, 1.0f);

		SetScreenPassPipelineState(RHICmdList, PipelineState);

		// Setting up buffers.
		SetupFunction(RHICmdList);

		FIntPoint LocalOutputPos(FIntPoint::ZeroValue);
		FIntPoint LocalOutputSize(OutputSize);
		EDrawRectangleFlags DrawRectangleFlags = EDRF_UseTriangleOptimization;

		DrawPostProcessPass(
			RHICmdList,
			LocalOutputPos.X, LocalOutputPos.Y, LocalOutputSize.X, LocalOutputSize.Y,
			InputRect.Min.X, InputRect.Min.Y, InputRect.Width(), InputRect.Height(),
			OutputSize,
			InputSize,
			PipelineState.VertexShader,
			View.StereoViewIndex,
			false,
			DrawRectangleFlags);
	}
} //! namespace

//////////////////////////////////////////////////////////////////////////
// FDisplayClusterMeshProjectionRenderer

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

#if WITH_EDITOR
			PrimitiveComponent->SelectionOverrideDelegate = UPrimitiveComponent::FSelectionOverride::CreateRaw(this, &FDisplayClusterMeshProjectionRenderer::IsPrimitiveComponentSelected);
#endif
		}
	});
}

void FDisplayClusterMeshProjectionRenderer::RemoveActor(AActor* Actor)
{
	TArray<TWeakObjectPtr<UPrimitiveComponent>> ComponentsToRemove = PrimitiveComponents.FilterByPredicate([Actor](const TWeakObjectPtr<UPrimitiveComponent>& PrimitiveComponent)
	{
		return !PrimitiveComponent.IsValid() || PrimitiveComponent->GetOwner() == Actor;
	});

	for (const TWeakObjectPtr<UPrimitiveComponent>& PrimitiveComponent : ComponentsToRemove)
	{
#if WITH_EDITOR
		PrimitiveComponent->SelectionOverrideDelegate.Unbind();
#endif

		PrimitiveComponents.Remove(PrimitiveComponent);
	}
}

void FDisplayClusterMeshProjectionRenderer::ClearScene()
{
	for (const TWeakObjectPtr<UPrimitiveComponent>& PrimitiveComponent : PrimitiveComponents)
	{
#if WITH_EDITOR
		if (PrimitiveComponent.IsValid() && PrimitiveComponent->SelectionOverrideDelegate.IsBound())
		{
			PrimitiveComponent->SelectionOverrideDelegate.Unbind();
		}
#endif
	}

	PrimitiveComponents.Empty();
}

void FDisplayClusterMeshProjectionRenderer::Render(FCanvas* Canvas, FSceneInterface* Scene, const FSceneViewInitOptions& ViewInitOptions, const FEngineShowFlags& EngineShowFlags, EDisplayClusterMeshProjectionType ProjectionType)
{
	Canvas->Flush_GameThread();
	FRenderTarget* RenderTarget = Canvas->GetRenderTarget();
	const bool bIsHitTesting = Canvas->IsHitTesting();

	ENQUEUE_RENDER_COMMAND(FDrawProjectedMeshes)(
		[RenderTarget, Scene, ViewInitOptions, EngineShowFlags, bIsHitTesting, ProjectionType, this](FRHICommandListImmediate& RHICmdList)
		{
			FMemMark Mark(FMemStack::Get());
			FRDGBuilder GraphBuilder(RHICmdList);

			FSceneViewFamily ViewFamily(FSceneViewFamily::ConstructionValues(
				RenderTarget,
				Scene,
				EngineShowFlags)
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

			ViewFamily.EngineShowFlags.SetHitProxies(bIsHitTesting);

			FScenePrimitiveRenderingContextScopeHelper ScenePrimitiveRenderingContextScopeHelper(GetRendererModule().BeginScenePrimitiveRendering(GraphBuilder, &ViewFamily));

			FSceneViewInitOptions NewInitOptions(ViewInitOptions);
			NewInitOptions.ViewFamily = &ViewFamily;

			GetRendererModule().CreateAndInitSingleView(RHICmdList, &ViewFamily, &NewInitOptions);
			const FViewInfo* View = (FViewInfo*)ViewFamily.Views[0];

			FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(RenderTarget->GetRenderTargetTexture(), TEXT("ViewRenderTarget")));
			FRenderTargetBinding OutputRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ELoad);

			if (ViewFamily.EngineShowFlags.HitProxies)
			{
				FRDGTextureDesc Desc(FRDGTextureDesc::Create2D(OutputTexture->Desc.Extent, PF_B8G8R8A8, FClearValueBinding::Black, TexCreate_RenderTargetable | TexCreate_ShaderResource));
				FRDGTextureRef HitProxyTexture = GraphBuilder.CreateTexture(Desc, TEXT("DisplayClusterMeshProjection.HitProxyTexture"));

				const FRDGTextureDesc DepthDesc = FRDGTextureDesc::Create2D(OutputTexture->Desc.Extent, PF_DepthStencil, FClearValueBinding::DepthFar, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource);
				FRDGTextureRef HitProxyDepthTexture = GraphBuilder.CreateTexture(DepthDesc, TEXT("DisplayClusterMeshProjection.DepthTexture"));

				FRenderTargetBinding HitProxyRenderTargetBinding(HitProxyTexture, ERenderTargetLoadAction::EClear);
				FDepthStencilBinding HitProxyDepthStencilBinding(HitProxyDepthTexture, ERenderTargetLoadAction::EClear, ERenderTargetLoadAction::EClear, FExclusiveDepthStencil::DepthWrite_StencilWrite);

				AddHitProxyRenderPass(GraphBuilder, View, ProjectionType, HitProxyRenderTargetBinding, HitProxyDepthStencilBinding);

				// Copy the hit proxy buffer to the viewport family's render target
				{
					FCopyRectPS::FParameters* ScreenPassParameters = GraphBuilder.AllocParameters<FCopyRectPS::FParameters>();
					ScreenPassParameters->InputTexture = HitProxyTexture;
					ScreenPassParameters->InputSampler = TStaticSamplerState<>::GetRHI();
					ScreenPassParameters->RenderTargets[0] = OutputRenderTargetBinding;

					FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
					TShaderMapRef<FScreenPassVS> ScreenPassVS(GlobalShaderMap);
					TShaderMapRef<FCopyRectPS> CopyPixelShader(GlobalShaderMap);

					FRHIBlendState* DefaultBlendState = FScreenPassPipelineState::FDefaultBlendState::GetRHI();
					const FScreenPassTextureViewport RegionViewport(OutputRenderTargetBinding.GetTexture());

					GraphBuilder.AddPass(
						RDG_EVENT_NAME("MeshProjectionRenderer::CopyHitProxyTexture"),
						ScreenPassParameters,
						ERDGPassFlags::Raster,
						[View, ScreenPassVS, CopyPixelShader, &RegionViewport, ScreenPassParameters, DefaultBlendState](FRHICommandList& RHICmdList)
					{
						DrawScreenPass(
							RHICmdList,
							*View,
							RegionViewport,
							RegionViewport,
							FScreenPassPipelineState(ScreenPassVS, CopyPixelShader, DefaultBlendState),
							[&](FRHICommandList&)
						{
							SetShaderParameters(RHICmdList, CopyPixelShader, CopyPixelShader.GetPixelShader(), *ScreenPassParameters);
						});
					});
				}
			}
			else
			{
				const FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(OutputTexture->Desc.Extent, PF_DepthStencil, FClearValueBinding::DepthFar, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource);
				FRDGTextureRef DepthTexture = GraphBuilder.CreateTexture(Desc, TEXT("DisplayClusterMeshProjection.DepthTexture"));

				FDepthStencilBinding OutputDepthStencilBinding(DepthTexture, ERenderTargetLoadAction::EClear, ERenderTargetLoadAction::EClear, FExclusiveDepthStencil::DepthWrite_StencilNop);

				AddBaseRenderPass(GraphBuilder, View, ProjectionType, OutputRenderTargetBinding, OutputDepthStencilBinding);

#if WITH_EDITOR
				const FRDGTextureDesc SelectionDepthDesc = FRDGTextureDesc::Create2D(OutputTexture->Desc.Extent, PF_DepthStencil, FClearValueBinding::DepthFar, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource);
				FRDGTextureRef SelectionDepthTexture = GraphBuilder.CreateTexture(SelectionDepthDesc, TEXT("DisplayClusterMeshProjection.SelectionDepthTexture"));
				FDepthStencilBinding SelectionDepthStencilBinding(SelectionDepthTexture, ERenderTargetLoadAction::EClear, ERenderTargetLoadAction::EClear, FExclusiveDepthStencil::DepthWrite_StencilWrite);

				AddSelectionDepthRenderPass(GraphBuilder, View, ProjectionType, SelectionDepthStencilBinding);
				AddSelectionOutlineScreenPass(GraphBuilder, View, OutputRenderTargetBinding, OutputTexture, DepthTexture, SelectionDepthTexture);
#endif
			}

			GraphBuilder.Execute();
		});
}

// Helper macros that generate appropriate switch cases for each projection type the mesh projection renderer supports, allowing new projection types to be easily added
#define PROJECTION_TYPE_CASE(FuncName, ProjectionType, ...) case ProjectionType: \
	FuncName<ProjectionType>(__VA_ARGS__); \
	break;

#define SWITCH_ON_PROJECTION_TYPE(FuncName, ProjectionType, ...) switch (ProjectionType) \
	{ \
	PROJECTION_TYPE_CASE(FuncName, EDisplayClusterMeshProjectionType::Azimuthal, __VA_ARGS__) \
	PROJECTION_TYPE_CASE(FuncName, EDisplayClusterMeshProjectionType::Perspective, __VA_ARGS__) \
	}

void FDisplayClusterMeshProjectionRenderer::AddBaseRenderPass(FRDGBuilder& GraphBuilder,
	const FViewInfo* View, EDisplayClusterMeshProjectionType ProjectionType,
	FRenderTargetBinding& OutputRenderTargetBinding,
	FDepthStencilBinding& OutputDepthStencilBinding)
{
	FMeshProjectionPassParameters* MeshPassParameters = GraphBuilder.AllocParameters<FMeshProjectionPassParameters>();
	MeshPassParameters->View = View->ViewUniformBuffer;
	MeshPassParameters->InstanceCulling = FInstanceCullingContext::CreateDummyInstanceCullingUniformBuffer(GraphBuilder);
	MeshPassParameters->RenderTargets[0] = OutputRenderTargetBinding;
	MeshPassParameters->RenderTargets.DepthStencil = OutputDepthStencilBinding;

	GraphBuilder.AddPass(RDG_EVENT_NAME("MeshProjectionRenderer::Base"), MeshPassParameters, ERDGPassFlags::Raster | ERDGPassFlags::NeverCull, [View, ProjectionType, this](FRHICommandList& RHICmdList)
	{
		FIntRect ViewRect = View->UnscaledViewRect;
		RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

		SWITCH_ON_PROJECTION_TYPE(RenderPrimitives_RenderThread, ProjectionType, View, RHICmdList);
	});
}

void FDisplayClusterMeshProjectionRenderer::AddHitProxyRenderPass(FRDGBuilder& GraphBuilder,
	const FViewInfo* View,
	EDisplayClusterMeshProjectionType ProjectionType,
	FRenderTargetBinding& OutputRenderTargetBinding,
	FDepthStencilBinding& OutputDepthStencilBinding)
{
	FMeshProjectionPassParameters* MeshPassParameters = GraphBuilder.AllocParameters<FMeshProjectionPassParameters>();
	MeshPassParameters->View = View->ViewUniformBuffer;
	MeshPassParameters->InstanceCulling = FInstanceCullingContext::CreateDummyInstanceCullingUniformBuffer(GraphBuilder);
	MeshPassParameters->RenderTargets[0] = OutputRenderTargetBinding;
	MeshPassParameters->RenderTargets.DepthStencil = OutputDepthStencilBinding;

	GraphBuilder.AddPass(RDG_EVENT_NAME("MeshProjectionRenderer::HitProxies"), MeshPassParameters, ERDGPassFlags::Raster | ERDGPassFlags::NeverCull, [View, ProjectionType, this](FRHICommandList& RHICmdList)
	{
		FIntRect ViewRect = View->UnscaledViewRect;
		RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

		SWITCH_ON_PROJECTION_TYPE(RenderHitProxies_RenderThread, ProjectionType, View, RHICmdList);
	});
}

#if WITH_EDITOR
void FDisplayClusterMeshProjectionRenderer::AddSelectionDepthRenderPass(FRDGBuilder& GraphBuilder, 
		const FViewInfo* View,
		EDisplayClusterMeshProjectionType ProjectionType,
		FDepthStencilBinding& OutputDepthStencilBinding)
{
	FMeshProjectionPassParameters* SelectionPassParameters = GraphBuilder.AllocParameters<FMeshProjectionPassParameters>();
	SelectionPassParameters->View = View->ViewUniformBuffer;
	SelectionPassParameters->InstanceCulling = FInstanceCullingContext::CreateDummyInstanceCullingUniformBuffer(GraphBuilder);
	SelectionPassParameters->RenderTargets.DepthStencil = OutputDepthStencilBinding;

	GraphBuilder.AddPass(RDG_EVENT_NAME("MeshProjectionRenderer::SelectionDepth"), SelectionPassParameters, ERDGPassFlags::Raster | ERDGPassFlags::NeverCull, [View, ProjectionType, this](FRHICommandList& RHICmdList)
	{
		FIntRect ViewRect = View->UnscaledViewRect;
		RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

		SWITCH_ON_PROJECTION_TYPE(RenderSelection_RenderThread, ProjectionType, View, RHICmdList);
	});
}

void FDisplayClusterMeshProjectionRenderer::AddSelectionOutlineScreenPass(FRDGBuilder& GraphBuilder,
	const FViewInfo* View,
	FRenderTargetBinding& OutputRenderTargetBinding,
	FRDGTexture* SceneColor,
	FRDGTexture* SceneDepth,
	FRDGTexture* SelectionDepth)
{
	const FScreenPassTextureViewport OutputViewport(OutputRenderTargetBinding.GetTexture());
	const FScreenPassTextureViewport ColorViewport(SceneColor);
	const FScreenPassTextureViewport DepthViewport(SceneDepth);

	FRHISamplerState* PointClampSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	FMeshProjectionSelectionOutlinePS::FParameters* ScreenPassParameters = GraphBuilder.AllocParameters<FMeshProjectionSelectionOutlinePS::FParameters>();
	ScreenPassParameters->RenderTargets[0] = OutputRenderTargetBinding;
	ScreenPassParameters->View = View->ViewUniformBuffer;
	ScreenPassParameters->Color = GetScreenPassTextureViewportParameters(ColorViewport);
	ScreenPassParameters->Depth = GetScreenPassTextureViewportParameters(DepthViewport);
	ScreenPassParameters->ColorTexture = SceneColor;
	ScreenPassParameters->ColorSampler = PointClampSampler;
	ScreenPassParameters->DepthTexture = SceneDepth;
	ScreenPassParameters->DepthSampler = PointClampSampler;
	ScreenPassParameters->EditorPrimitivesDepth = SelectionDepth;
	ScreenPassParameters->EditorPrimitivesStencil = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateWithPixelFormat(SelectionDepth, PF_X24_G8));
	ScreenPassParameters->OutlineColor = FVector3f(View->SelectionOutlineColor);
	ScreenPassParameters->SelectionHighlightIntensity = GEngine->SelectionHighlightIntensity;

	TShaderMapRef<FScreenPassVS> ScreenPassVS(View->ShaderMap);
	TShaderMapRef<FMeshProjectionSelectionOutlinePS> SelectionOutlinePS(View->ShaderMap);

	FRHIBlendState* DefaultBlendState = FScreenPassPipelineState::FDefaultBlendState::GetRHI();

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("MeshProjectionRenderer::SelectionScreen"),
		ScreenPassParameters,
		ERDGPassFlags::Raster,
		[View, ScreenPassVS, SelectionOutlinePS, &OutputViewport, ScreenPassParameters, DefaultBlendState](FRHICommandList& RHICmdList)
	{
		DrawScreenPass(
			RHICmdList,
			*View,
			OutputViewport,
			OutputViewport,
			FScreenPassPipelineState(ScreenPassVS, SelectionOutlinePS, DefaultBlendState),
			[&](FRHICommandList&)
		{
			SetShaderParameters(RHICmdList, SelectionOutlinePS, SelectionOutlinePS.GetPixelShader(), *ScreenPassParameters);
		});
	});
}
#endif

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

template<EDisplayClusterMeshProjectionType ProjectionType>
void FDisplayClusterMeshProjectionRenderer::RenderHitProxies_RenderThread(const FSceneView* View, FRHICommandList& RHICmdList)
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

		FLightCardEditorHitProxyMeshPassProcessor<ProjectionType> MeshProcessor(nullptr, View, DynamicMeshPassContext);

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

#if WITH_EDITOR
template<EDisplayClusterMeshProjectionType ProjectionType>
void FDisplayClusterMeshProjectionRenderer::RenderSelection_RenderThread(const FSceneView* View, FRHICommandList& RHICmdList)
{
	DrawDynamicMeshPass(*View, RHICmdList, [View, this](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
	{
		TArray<FPrimitiveSceneProxy*> PrimitiveSceneProxies;
		for (const TWeakObjectPtr<UPrimitiveComponent>& PrimitiveComponent : PrimitiveComponents)
		{
			if (PrimitiveComponent.IsValid() && PrimitiveComponent->SceneProxy && PrimitiveComponent->SceneProxy->IsSelected())
			{
				PrimitiveSceneProxies.Add(PrimitiveComponent->SceneProxy);
			}
		}

		FLightCardEditorSelectionPassProcessor<ProjectionType> MeshProcessor(nullptr, View, DynamicMeshPassContext);

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
#endif

bool FDisplayClusterMeshProjectionRenderer::IsPrimitiveComponentSelected(const UPrimitiveComponent* InPrimitiveComponent)
{
	if (ActorSelectedDelegate.IsBound())
	{
		return ActorSelectedDelegate.Execute(InPrimitiveComponent->GetOwner());
	}

	return false;
}

// Explicit template specializations for each projection type
#define PROJECTION_TYPE_TEMPLATE_SPECIALIZATION(FuncName, ProjectionType, ...) template void FDisplayClusterMeshProjectionRenderer::FuncName<ProjectionType>(__VA_ARGS__);
#define CREATE_PROJECTION_TYPE_TEMPLATE_SPECIALIZATIONS(FuncName, ...) \
	PROJECTION_TYPE_TEMPLATE_SPECIALIZATION(FuncName, EDisplayClusterMeshProjectionType::Perspective, __VA_ARGS__) \
	PROJECTION_TYPE_TEMPLATE_SPECIALIZATION(FuncName, EDisplayClusterMeshProjectionType::Azimuthal, __VA_ARGS__)

CREATE_PROJECTION_TYPE_TEMPLATE_SPECIALIZATIONS(RenderPrimitives_RenderThread, const FSceneView* View, FRHICommandList& RHICmdList)
CREATE_PROJECTION_TYPE_TEMPLATE_SPECIALIZATIONS(RenderHitProxies_RenderThread, const FSceneView* View, FRHICommandList& RHICmdList)

#if WITH_EDITOR
CREATE_PROJECTION_TYPE_TEMPLATE_SPECIALIZATIONS(RenderSelection_RenderThread, const FSceneView* View, FRHICommandList& RHICmdList)
#endif