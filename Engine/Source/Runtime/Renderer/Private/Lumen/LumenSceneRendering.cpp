// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenSceneRendering.cpp
=============================================================================*/

#include "LumenSceneRendering.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "MeshPassProcessor.inl"
#include "MeshCardRepresentation.h"
#include "GPUScene.h"
#include "Rendering/NaniteResources.h"
#include "Nanite/NaniteRender.h"
#include "PixelShaderUtils.h"
#include "Lumen.h"
#include "LumenMeshCards.h"
#include "LumenSceneUtils.h"
#include "LumenSurfaceCacheFeedback.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "HAL/LowLevelMemStats.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

int32 GLumenSceneCardCapturesPerFrame = 300;
FAutoConsoleVariableRef CVarLumenSceneCardCapturesPerFrame(
	TEXT("r.LumenScene.CardCapturesPerFrame"),
	GLumenSceneCardCapturesPerFrame,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenSceneCardCaptureMargin = 0.0f;
FAutoConsoleVariableRef CVarLumenSceneCardCaptureMargin(
	TEXT("r.LumenScene.CardCaptureMargin"),
	GLumenSceneCardCaptureMargin,
	TEXT("How far from Lumen scene range start to capture cards."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenSceneCardFixedDebugTexelDensity = -1;
FAutoConsoleVariableRef CVarLumenSceneCardFixedTexelDensity(
	TEXT("r.LumenScene.CardFixedDebugTexelDensity"),
	GLumenSceneCardFixedDebugTexelDensity,
	TEXT("Lumen card texels per world space distance"),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GLumenSceneCardCameraDistanceTexelDensityScale = 100;
FAutoConsoleVariableRef CVarLumenSceneCardCameraDistanceTexelDensityScale(
	TEXT("r.LumenScene.CardCameraDistanceTexelDensityScale"),
	GLumenSceneCardCameraDistanceTexelDensityScale,
	TEXT("Lumen card texels per world space distance"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenSceneCardMaxTexelDensity = .2f;
FAutoConsoleVariableRef CVarLumenSceneCardMaxTexelDensity(
	TEXT("r.LumenScene.CardMaxTexelDensity"),
	GLumenSceneCardMaxTexelDensity,
	TEXT("Lumen card texels per world space distance"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneMaxQuadResolution = 512;
FAutoConsoleVariableRef CVarLumenSceneMaxQuadResolution(
	TEXT("r.LumenScene.CardMaxResolution"),
	GLumenSceneMaxQuadResolution,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneReset = 0;
FAutoConsoleVariableRef CVarLumenSceneReset(
	TEXT("r.LumenScene.Reset"),
	GLumenSceneReset,
	TEXT("Reset all atlases and captured cards.\n")
	TEXT("1 - one time reset\n")
	TEXT("2 - continuos reset\n")
	TEXT("3 - continuos reset every 2 frames\n")
	TEXT("4 - continuos reset every 3 frames\n"),
	ECVF_RenderThreadSafe
);

int32 GLumenSceneForceEvict = 0;
FAutoConsoleVariableRef CVarLumenSceneForceEvict(
	TEXT("r.LumenScene.ForceEvict"),
	GLumenSceneForceEvict,
	TEXT("Evict all optional surface cache pages."),
	ECVF_RenderThreadSafe
);

int32 GLumenSceneRecaptureLumenSceneEveryFrame = 0;
FAutoConsoleVariableRef CVarLumenGIRecaptureLumenSceneEveryFrame(
	TEXT("r.LumenScene.RecaptureEveryFrame"),
	GLumenSceneRecaptureLumenSceneEveryFrame,
	TEXT(""),
	ECVF_RenderThreadSafe
);

int32 GLumenSceneNaniteMultiViewCapture = 1;
FAutoConsoleVariableRef CVarLumenSceneNaniteMultiViewCapture(
	TEXT("r.LumenScene.NaniteMultiViewCapture"),
	GLumenSceneNaniteMultiViewCapture,
	TEXT(""),
	ECVF_RenderThreadSafe
);

int32 GLumenGIMaxConeSteps = 1000;
FAutoConsoleVariableRef CVarLumenGIMaxConeSteps(
	TEXT("r.Lumen.MaxConeSteps"),
	GLumenGIMaxConeSteps,
	TEXT("Maximum steps to use for Cone Stepping of proxy cards."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenParallelBeginUpdate = 1;
FAutoConsoleVariableRef CVarLumenParallelBeginUpdate(
	TEXT("r.LumenParallelBeginUpdate"),
	GLumenParallelBeginUpdate,
	TEXT("Whether to run the Lumen begin update in parallel or not."),
	ECVF_RenderThreadSafe
);

int32 GLumenFastCameraMode = 0;
FAutoConsoleVariableRef CVarLumenFastCameraMode(
	TEXT("r.LumenScene.FastCameraMode"),
	GLumenFastCameraMode,
	TEXT("Whether to update the Lumen Scene for fast camera movement - lower quality, faster updates so lighting can keep up with the camera."),
	ECVF_RenderThreadSafe
);

int32 GLumenSceneGlobalDFResolution = 224;
FAutoConsoleVariableRef CVarLumenSceneGlobalDFResolution(
	TEXT("r.LumenScene.GlobalDFResolution"),
	GLumenSceneGlobalDFResolution,
	TEXT(""),
	ECVF_RenderThreadSafe
);

float GLumenSceneGlobalDFClipmapExtent = 2500.0f;
FAutoConsoleVariableRef CVarLumenSceneGlobalDFClipmapExtent(
	TEXT("r.LumenScene.GlobalDFClipmapExtent"),
	GLumenSceneGlobalDFClipmapExtent,
	TEXT(""),
	ECVF_RenderThreadSafe
);

int32 GLumenScenePrimitivesPerTask = 128;
FAutoConsoleVariableRef CVarLumenScenePrimitivePerTask(
	TEXT("r.LumenScene.PrimitivesPerTask"),
	GLumenScenePrimitivesPerTask,
	TEXT("How many primitives to process per single surface cache update task."),
	ECVF_RenderThreadSafe
);

int32 GLumenSceneMeshCardsPerTask = 128;
FAutoConsoleVariableRef CVarLumenSceneMeshCardsPerTask(
	TEXT("r.LumenScene.MeshCardsPerTask"),
	GLumenSceneMeshCardsPerTask,
	TEXT("How many mesh cards to process per single surface cache update task."),
	ECVF_RenderThreadSafe
);

int32 GLumenSceneCardCaptureAtlasFactor = 4;
FAutoConsoleVariableRef CVarLumenSceneCardCapturesAtlasFactor(
	TEXT("r.LumenScene.CardCaptureAtlasFactor"),
	GLumenSceneCardCaptureAtlasFactor,
	TEXT("Controls the size of a transient card capture atlas."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

#if ENABLE_LOW_LEVEL_MEM_TRACKER
DECLARE_LLM_MEMORY_STAT(TEXT("Lumen"), STAT_LumenLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Lumen"), STAT_LumenSummaryLLM, STATGROUP_LLM);
LLM_DEFINE_TAG(Lumen, NAME_None, NAME_None, GET_STATFNAME(STAT_LumenLLM), GET_STATFNAME(STAT_LumenSummaryLLM));
#endif // ENABLE_LOW_LEVEL_MEM_TRACKER

extern int32 GAllowLumenDiffuseIndirect;
extern int32 GAllowLumenReflections;


namespace Lumen
{
	bool AnyLumenHardwareRayTracingPassEnabled(const FScene* Scene, const FViewInfo& View)
	{
#if RHI_RAYTRACING
		if (GAllowLumenDiffuseIndirect != 0
			&& View.FinalPostProcessSettings.DynamicGlobalIlluminationMethod == EDynamicGlobalIlluminationMethod::Lumen
			&& (UseHardwareRayTracedScreenProbeGather() || UseHardwareRayTracedRadianceCache() || UseHardwareRayTracedShadows(View)))
		{
			return true;
		}

		if (GAllowLumenReflections != 0
			&& View.FinalPostProcessSettings.ReflectionMethod == EReflectionMethod::Lumen
			&& UseHardwareRayTracedReflections())
		{
			return true;
		}

		if (View.Family
			&& View.Family->EngineShowFlags.VisualizeLumenScene
			&& Lumen::ShouldVisualizeHardwareRayTracing())
		{
			return true;
		}
#endif
		return false;
	}
}

bool Lumen::ShouldHandleSkyLight(const FScene* Scene, const FSceneViewFamily& ViewFamily)
{
	return Scene->SkyLight
		&& (Scene->SkyLight->ProcessedTexture || Scene->SkyLight->bRealTimeCaptureEnabled)
		&& ViewFamily.EngineShowFlags.SkyLighting
		&& Scene->GetFeatureLevel() >= ERHIFeatureLevel::SM5
		&& !IsAnyForwardShadingEnabled(Scene->GetShaderPlatform())
		&& !ViewFamily.EngineShowFlags.VisualizeLightCulling;
}

bool ShouldRenderLumenForViewFamily(const FScene* Scene, const FSceneViewFamily& ViewFamily)
{
	return Scene
		&& Scene->LumenSceneData
		&& ViewFamily.Views.Num() == 1
		&& DoesPlatformSupportLumenGI(Scene->GetShaderPlatform());
}

bool Lumen::IsLumenFeatureAllowedForView(const FScene* Scene, const FViewInfo& View, bool bRequireSoftwareTracing)
{
	static const auto CMeshSDFVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.GenerateMeshDistanceFields"));

	return View.Family
		&& ShouldRenderLumenForViewFamily(Scene, *View.Family)
		// Don't update scene lighting for secondary views
		&& !View.bIsPlanarReflection
		&& !View.bIsSceneCapture
		&& !View.bIsReflectionCapture
		&& View.ViewState
		&& (!bRequireSoftwareTracing || CMeshSDFVar->GetValueOnRenderThread() != 0);
}

int32 Lumen::GetGlobalDFResolution()
{
	return GLumenSceneGlobalDFResolution;
}

float Lumen::GetGlobalDFClipmapExtent()
{
	return GLumenSceneGlobalDFClipmapExtent;
}

float GetCardCameraDistanceTexelDensityScale()
{
	return GLumenSceneCardCameraDistanceTexelDensityScale * (GLumenFastCameraMode ? .2f : 1.0f);
}

int32 GetCardMaxResolution()
{
	if (GLumenFastCameraMode)
	{
		return GLumenSceneMaxQuadResolution / 2;
	}

	return GLumenSceneMaxQuadResolution;
}

int32 GetMaxLumenSceneCardCapturesPerFrame()
{
	return GLumenSceneCardCapturesPerFrame * (GLumenFastCameraMode ? 2 : 1);
}

DECLARE_GPU_STAT(UpdateLumenScene);
DECLARE_GPU_STAT(UpdateLumenSceneBuffers);

IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FLumenCardPassUniformParameters, "LumenCardPass", SceneTextures);

class FLumenCardVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FLumenCardVS, MeshMaterial);

protected:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		//@todo DynamicGI - filter
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	FLumenCardVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{}

	FLumenCardVS() = default;
};


IMPLEMENT_MATERIAL_SHADER_TYPE(, FLumenCardVS, TEXT("/Engine/Private/Lumen/LumenCardVertexShader.usf"), TEXT("Main"), SF_Vertex);

class FLumenCardPS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FLumenCardPS, MeshMaterial);

public:
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		//@todo DynamicGI - filter
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	FLumenCardPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{}

	FLumenCardPS() = default;
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FLumenCardPS, TEXT("/Engine/Private/Lumen/LumenCardPixelShader.usf"), TEXT("Main"), SF_Pixel);

class FLumenCardMeshProcessor : public FMeshPassProcessor
{
public:

	FLumenCardMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, const FMeshPassProcessorRenderState& InPassDrawRenderState, FMeshPassDrawListContext* InDrawListContext);

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final;

	FMeshPassProcessorRenderState PassDrawRenderState;
};

void FLumenCardMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	LLM_SCOPE_BYTAG(Lumen);

	if (MeshBatch.bUseForMaterial && DoesPlatformSupportLumenGI(GetFeatureLevelShaderPlatform(FeatureLevel)))
	{
		const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
		const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);

		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;

		const EBlendMode BlendMode = Material.GetBlendMode();
		const FMaterialShadingModelField ShadingModels = Material.GetShadingModels();
		const bool bIsTranslucent = IsTranslucentBlendMode(BlendMode);
		const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
		const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material, OverrideSettings);
		const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, Material, OverrideSettings);

		if (!bIsTranslucent
			&& (PrimitiveSceneProxy && PrimitiveSceneProxy->ShouldRenderInMainPass() && PrimitiveSceneProxy->AffectsDynamicIndirectLighting())
			&& ShouldIncludeDomainInMeshPass(Material.GetMaterialDomain()))
		{
			const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;
			FVertexFactoryType* VertexFactoryType = VertexFactory->GetType();

			TMeshProcessorShaders<
				FLumenCardVS,
				FLumenCardPS> PassShaders;

			PassShaders.VertexShader = Material.GetShader<FLumenCardVS>(VertexFactoryType);
			PassShaders.PixelShader = Material.GetShader<FLumenCardPS>(VertexFactoryType);

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

FLumenCardMeshProcessor::FLumenCardMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, const FMeshPassProcessorRenderState& InPassDrawRenderState, FMeshPassDrawListContext* InDrawListContext)
	: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
	, PassDrawRenderState(InPassDrawRenderState)
{}

FMeshPassProcessor* CreateLumenCardCapturePassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	LLM_SCOPE_BYTAG(Lumen);

	FMeshPassProcessorRenderState PassState;

	// Write and test against depth
	PassState.SetDepthStencilState(TStaticDepthStencilState<true, CF_Greater>::GetRHI());

	PassState.SetBlendState(TStaticBlendState<>::GetRHI());

	return new(FMemStack::Get()) FLumenCardMeshProcessor(Scene, InViewIfDynamicMeshCommand, PassState, InDrawListContext);
}

FRegisterPassProcessorCreateFunction RegisterLumenCardCapturePass(&CreateLumenCardCapturePassProcessor, EShadingPath::Deferred, EMeshPass::LumenCardCapture, EMeshPassFlags::CachedMeshCommands);

class FLumenCardNaniteMeshProcessor : public FMeshPassProcessor
{
public:

	FLumenCardNaniteMeshProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, const FMeshPassProcessorRenderState& InPassDrawRenderState, FMeshPassDrawListContext* InDrawListContext);

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final;

	FMeshPassProcessorRenderState PassDrawRenderState;

private:
	bool TryAddMeshBatch(
		const FMeshBatch& RESTRICT MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		int32 StaticMeshId,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material);
};

FLumenCardNaniteMeshProcessor::FLumenCardNaniteMeshProcessor(
	const FScene* InScene,
	const FSceneView* InViewIfDynamicMeshCommand,
	const FMeshPassProcessorRenderState& InDrawRenderState,
	FMeshPassDrawListContext* InDrawListContext
) :
	FMeshPassProcessor(InScene, InScene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext),
	PassDrawRenderState(InDrawRenderState)
{
}

using FLumenCardNanitePassShaders = TMeshProcessorShaders<FNaniteMaterialVS, FLumenCardPS>;

void FLumenCardNaniteMeshProcessor::AddMeshBatch(
	const FMeshBatch& RESTRICT MeshBatch,
	uint64 BatchElementMask,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	int32 StaticMeshId /*= -1 */
)
{
	LLM_SCOPE_BYTAG(Lumen);

	if (PrimitiveSceneProxy && PrimitiveSceneProxy->ShouldRenderInMainPass() && PrimitiveSceneProxy->AffectsDynamicIndirectLighting() && DoesPlatformSupportLumenGI(GetFeatureLevelShaderPlatform(FeatureLevel)))
	{
		const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
		while (MaterialRenderProxy)
		{
			const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
			if (Material)
			{
				if (TryAddMeshBatch(MeshBatch, BatchElementMask, PrimitiveSceneProxy, StaticMeshId, *MaterialRenderProxy, *Material))
				{
					break;
				}
			}

			MaterialRenderProxy = MaterialRenderProxy->GetFallback(FeatureLevel);
		}
	}
}

bool FLumenCardNaniteMeshProcessor::TryAddMeshBatch(
	const FMeshBatch& RESTRICT MeshBatch,
	uint64 BatchElementMask,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	int32 StaticMeshId,
	const FMaterialRenderProxy& MaterialRenderProxy,
	const FMaterial& Material)
{
	const EBlendMode BlendMode = Material.GetBlendMode();

	check(BlendMode == BLEND_Opaque);
	check(Material.GetMaterialDomain() == MD_Surface);

	TShaderMapRef<FNaniteMaterialVS> VertexShader(GetGlobalShaderMap(FeatureLevel));

	FLumenCardNanitePassShaders PassShaders;
	PassShaders.VertexShader = VertexShader;

	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;
	FVertexFactoryType* VertexFactoryType = VertexFactory->GetType();

	FMaterialShaderTypes ShaderTypes;
	ShaderTypes.AddShaderType<FLumenCardPS>();

	FMaterialShaders Shaders;
	if (!Material.TryGetShaders(ShaderTypes, VertexFactoryType, Shaders))
	{
		return false;
	}

	Shaders.TryGetPixelShader(PassShaders.PixelShader);

	FMeshMaterialShaderElementData ShaderElementData;
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
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

FMeshPassProcessor* CreateLumenCardNaniteMeshProcessor(
	const FScene* Scene,
	const FSceneView* InViewIfDynamicMeshCommand,
	FMeshPassDrawListContext* InDrawListContext)
{
	LLM_SCOPE_BYTAG(Lumen);

	FMeshPassProcessorRenderState PassState;
	PassState.SetNaniteUniformBuffer(Scene->UniformBuffers.NaniteUniformBuffer);

	PassState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Equal, true, CF_Equal>::GetRHI());
	PassState.SetDepthStencilAccess(FExclusiveDepthStencil::DepthRead_StencilRead);
	PassState.SetStencilRef(STENCIL_SANDBOX_MASK);
	PassState.SetBlendState(TStaticBlendState<>::GetRHI());

	return new(FMemStack::Get()) FLumenCardNaniteMeshProcessor(Scene, InViewIfDynamicMeshCommand, PassState, InDrawListContext);
}

FLumenCard::FLumenCard()
{
	bVisible = false;
	WorldBounds.Init();
	Origin = FVector::ZeroVector;
	LocalExtent = FVector::ZeroVector;
	LocalToWorldRotationX = FVector::ZeroVector;
	LocalToWorldRotationY = FVector::ZeroVector;
	LocalToWorldRotationZ = FVector::ZeroVector;
	IndexInMeshCards = -1;
}

FLumenCard::~FLumenCard()
{
	for (int32 MipIndex = 0; MipIndex < UE_ARRAY_COUNT(SurfaceMipMaps); ++MipIndex)
	{
		ensure(SurfaceMipMaps[MipIndex].PageTableSpanSize == 0);
	}
}

const static FVector LumenMeshCardRotationFrame[6][3] =
{
	// X-
	{
		FVector(0.0f, 1.0f, 0.0f),
		FVector(0.0f, 0.0f, 1.0f),
		FVector(-1.0f, 0.0f, 0.0f),
	},

	// X+
	{
		FVector(0.0f, -1.0f, 0.0f),
		FVector(0.0f, 0.0f, 1.0f),
		FVector(1.0f, 0.0f, 0.0f),
	},

	// Y-
	{
		FVector(0.0f, 0.0f, 1.0f),
		FVector(1.0f, 0.0f, 0.0f),
		FVector(0.0f, -1.0f, 0.0f),
	},

	// Y+
	{
		FVector(0.0f, 0.0f, -1.0f),
		FVector(1.0f, 0.0f, 0.0f),
		FVector(0.0f, 1.0f, 0.0f),
	},

	// Z-
	{
		FVector(0.0f, -1.0f, 0.0f),
		FVector(1.0f, 0.0f, 0.0f),
		FVector(0.0f, 0.0f, -1.0f),
	},

	// Z+
	{
		FVector(0.0f, 1.0f, 0.0f),
		FVector(1.0f, 0.0f, 0.0f),
		FVector(0.0f, 0.0f, 1.0f),
	}
};

void FLumenCard::Initialize(float InResolutionScale, const FMatrix& LocalToWorld, const FLumenCardBuildData& CardBuildData, int32 InIndexInMeshCards, int32 InMeshCardsIndex)
{
	IndexInMeshCards = InIndexInMeshCards;
	MeshCardsIndex = InMeshCardsIndex;
	ResolutionScale = InResolutionScale;

	SetTransform(LocalToWorld, CardBuildData.Center, CardBuildData.Extent, CardBuildData.Orientation);
}

void FLumenCard::SetTransform(const FMatrix& LocalToWorld, FVector CardLocalCenter, FVector CardLocalExtent, int32 InOrientation)
{
	check(InOrientation < 6);

	Orientation = InOrientation;
	const FVector& CardToLocalRotationX = LumenMeshCardRotationFrame[Orientation][0];
	const FVector& CardToLocalRotationY = LumenMeshCardRotationFrame[Orientation][1];
	const FVector& CardToLocalRotationZ = LumenMeshCardRotationFrame[Orientation][2];

	SetTransform(LocalToWorld, CardLocalCenter, CardToLocalRotationX, CardToLocalRotationY, CardToLocalRotationZ, CardLocalExtent);
}

void FLumenCard::SetTransform(
	const FMatrix& LocalToWorld,
	const FVector& LocalOrigin,
	const FVector& CardToLocalRotationX,
	const FVector& CardToLocalRotationY,
	const FVector& CardToLocalRotationZ,
	const FVector& InLocalExtent)
{
	Origin = LocalToWorld.TransformPosition(LocalOrigin);

	const FVector ScaledXAxis = LocalToWorld.TransformVector(CardToLocalRotationX);
	const FVector ScaledYAxis = LocalToWorld.TransformVector(CardToLocalRotationY);
	const FVector ScaledZAxis = LocalToWorld.TransformVector(CardToLocalRotationZ);
	const float XAxisLength = ScaledXAxis.Size();
	const float YAxisLength = ScaledYAxis.Size();
	const float ZAxisLength = ScaledZAxis.Size();

	LocalToWorldRotationY = ScaledYAxis / FMath::Max(YAxisLength, DELTA);
	LocalToWorldRotationZ = ScaledZAxis / FMath::Max(ZAxisLength, DELTA);
	LocalToWorldRotationX = FVector::CrossProduct(LocalToWorldRotationZ, LocalToWorldRotationY);
	LocalToWorldRotationX.Normalize();

	LocalExtent = InLocalExtent * FVector(XAxisLength, YAxisLength, ZAxisLength);
	LocalExtent.Z = FMath::Max(LocalExtent.Z, 1.0f);

	FMatrix CardToWorld = FMatrix::Identity;
	CardToWorld.SetAxes(&LocalToWorldRotationX, &LocalToWorldRotationY, &LocalToWorldRotationZ);
	CardToWorld.SetOrigin(Origin);
	FBox LocalBounds(-LocalExtent, LocalExtent);
	WorldBounds = LocalBounds.TransformBy(CardToWorld);
}

FCardPageRenderData::FCardPageRenderData(const FViewInfo& InMainView,
	FLumenCard& InCardData,
	FVector4 InCardUVRect,
	FIntRect InCardCaptureAtlasRect,
	FIntRect InSurfaceCacheAtlasRect,
	FPrimitiveSceneInfo* InPrimitiveSceneInfo,
	int32 InPrimitiveInstanceIndexOrMergedFlag,
	int32 InCardIndex,
	int32 InPageTableIndex)
	: PrimitiveSceneInfo(InPrimitiveSceneInfo)
	, PrimitiveInstanceIndexOrMergedFlag(InPrimitiveInstanceIndexOrMergedFlag)
	, CardIndex(InCardIndex)
	, PageTableIndex(InPageTableIndex)
	, bDistantScene(InCardData.bDistantScene)
	, CardUVRect(InCardUVRect)
	, CardCaptureAtlasRect(InCardCaptureAtlasRect)
	, SurfaceCacheAtlasRect(InSurfaceCacheAtlasRect)
	, Origin(InCardData.Origin)
	, LocalExtent(InCardData.LocalExtent)
	, LocalToWorldRotationX(InCardData.LocalToWorldRotationX)
	, LocalToWorldRotationY(InCardData.LocalToWorldRotationY)
	, LocalToWorldRotationZ(InCardData.LocalToWorldRotationZ)
{
	ensure(CardIndex >= 0 && PageTableIndex >= 0);

	if (InCardData.bDistantScene)
	{
		NaniteLODScaleFactor = Lumen::GetDistanceSceneNaniteLODScaleFactor();
	}

	UpdateViewMatrices(InMainView);
}

void FCardPageRenderData::UpdateViewMatrices(const FViewInfo& MainView)
{
	ensureMsgf(FVector::DotProduct(LocalToWorldRotationX, FVector::CrossProduct(LocalToWorldRotationY, LocalToWorldRotationZ)) < 0.0f, TEXT("Card has wrong handedness"));

	FMatrix ViewRotationMatrix = FMatrix::Identity;
	ViewRotationMatrix.SetColumn(0, LocalToWorldRotationX);
	ViewRotationMatrix.SetColumn(1, LocalToWorldRotationY);
	ViewRotationMatrix.SetColumn(2, -LocalToWorldRotationZ);

	FVector ViewLocation = Origin;

	FVector FaceLocalExtent = LocalExtent;
	// Pull the view location back so the entire preview box is in front of the near plane
	ViewLocation += FaceLocalExtent.Z * LocalToWorldRotationZ;

	const float NearPlane = 0;
	const float FarPlane = FaceLocalExtent.Z * 2;

	const float ZScale = 1.0f / (FarPlane - NearPlane);
	const float ZOffset = -NearPlane;

	FVector4 ProjectionRect = FVector4(2.0f, 2.0f, 2.0f, 2.0f) * CardUVRect - FVector4(1.0f, 1.0f, 1.0f, 1.0f);

	FVector2D CardBorderOffset;
	CardBorderOffset = FVector2D(0.5f * (Lumen::PhysicalPageSize - Lumen::VirtualPageSize));
	CardBorderOffset.X *= 2.0f * (CardUVRect.Z - CardUVRect.X) / CardCaptureAtlasRect.Width();
	CardBorderOffset.Y *= 2.0f * (CardUVRect.W - CardUVRect.Y) / CardCaptureAtlasRect.Height();

	ProjectionRect.X = FMath::Clamp(ProjectionRect.X - CardBorderOffset.X, -1.0f, 1.0f);
	ProjectionRect.Y = FMath::Clamp(ProjectionRect.Y - CardBorderOffset.Y, -1.0f, 1.0f);
	ProjectionRect.Z = FMath::Clamp(ProjectionRect.Z + CardBorderOffset.X, -1.0f, 1.0f);
	ProjectionRect.W = FMath::Clamp(ProjectionRect.W + CardBorderOffset.Y, -1.0f, 1.0f);

	const float ProjectionL = ProjectionRect.X * 0.5f * FaceLocalExtent.X;
	const float ProjectionR = ProjectionRect.Z * 0.5f * FaceLocalExtent.X;

	const float ProjectionB = -ProjectionRect.W * 0.5f * FaceLocalExtent.Y;
	const float ProjectionT = -ProjectionRect.Y * 0.5f * FaceLocalExtent.Y;

	const FMatrix ProjectionMatrix = FReversedZOrthoMatrix(
		ProjectionL,
		ProjectionR,
		ProjectionB,
		ProjectionT,
		ZScale,
		ZOffset);

	ProjectionMatrixUnadjustedForRHI = ProjectionMatrix;

	FViewMatrices::FMinimalInitializer Initializer;
	Initializer.ViewRotationMatrix = ViewRotationMatrix;
	Initializer.ViewOrigin = ViewLocation;
	Initializer.ProjectionMatrix = ProjectionMatrix;
	Initializer.ConstrainedViewRect = MainView.SceneViewInitOptions.GetConstrainedViewRect();
	Initializer.StereoPass = MainView.SceneViewInitOptions.StereoPass;
#if WITH_EDITOR
	Initializer.bUseFauxOrthoViewPos = MainView.SceneViewInitOptions.bUseFauxOrthoViewPos;
#endif

	ViewMatrices = FViewMatrices(Initializer);
}

void FCardPageRenderData::PatchView(FRHICommandList& RHICmdList, const FScene* Scene, FViewInfo* View) const
{
	View->ProjectionMatrixUnadjustedForRHI = ProjectionMatrixUnadjustedForRHI;
	View->ViewMatrices = ViewMatrices;
	View->ViewRect = CardCaptureAtlasRect;

	FBox VolumeBounds[TVC_MAX];
	View->SetupUniformBufferParameters(
		VolumeBounds,
		TVC_MAX,
		*View->CachedViewUniformShaderParameters);

	View->CachedViewUniformShaderParameters->NearPlane = 0;
}

void AllocateCardAtlases(FRDGBuilder& GraphBuilder, FLumenSceneData& LumenSceneData, const FViewInfo& View)
{
	const FIntPoint PageAtlasSize = LumenSceneData.GetPhysicalAtlasSize();
	const bool bCompress = LumenSceneData.GetPhysicalAtlasCompression();

	ETextureCreateFlags TexFlags = TexCreate_ShaderResource | TexCreate_NoFastClear;

	if (!bCompress)
	{
		// Without compression we can write directly into this surface
		TexFlags |= TexCreate_RenderTargetable;
	}

	// Albedo
	FRDGTextureRef Albedo = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(
			PageAtlasSize,
			bCompress ? PF_BC7 : PF_R8G8B8A8,
			FClearValueBinding::Green,
			TexFlags),
		TEXT("Lumen.SceneAlbedo"));
	LumenSceneData.AlbedoAtlas = GraphBuilder.ConvertToExternalTexture(Albedo);

	// Opacity
	FRDGTextureRef Opacity = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(
			PageAtlasSize,
			bCompress ? PF_BC4 : PF_G8,
			FClearValueBinding::Black,	
			TexFlags),
		TEXT("Lumen.SceneOpacity"));
	LumenSceneData.OpacityAtlas = GraphBuilder.ConvertToExternalTexture(Opacity);

	// Depth
	FRDGTextureRef Depth = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(
			PageAtlasSize,
			bCompress ? PF_BC5 : PF_G16R16,
			FClearValueBinding::Black,
			TexFlags),
		TEXT("Lumen.SceneDepth"));
	LumenSceneData.DepthAtlas = GraphBuilder.ConvertToExternalTexture(Depth);

	// Normal
	FRDGTextureRef Normal = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(
			PageAtlasSize,
			bCompress ? PF_BC5 : PF_G16R16,
			FClearValueBinding::Black,
			TexFlags),
		TEXT("Lumen.SceneNormal"));
	LumenSceneData.NormalAtlas = GraphBuilder.ConvertToExternalTexture(Normal);

	// Emissive
	FRDGTextureRef Emissive = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(
			PageAtlasSize,
			bCompress ? PF_BC6H : PF_FloatR11G11B10,
			FClearValueBinding::Green,
			TexFlags),
		TEXT("Lumen.SceneEmissive"));
	LumenSceneData.EmissiveAtlas = GraphBuilder.ConvertToExternalTexture(Emissive);

	FClearValueBinding CrazyGreen(FLinearColor(0.0f, 10000.0f, 0.0f, 1.0f));
	FPooledRenderTargetDesc LightingDesc(FPooledRenderTargetDesc::Create2DDesc(PageAtlasSize, PF_FloatR11G11B10, CrazyGreen, TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_NoFastClear, false));
	LightingDesc.AutoWritable = false;
	GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, LightingDesc, LumenSceneData.FinalLightingAtlas, TEXT("Lumen.SceneFinalLighting"), ERenderTargetTransience::NonTransient);
	LumenSceneData.bFinalLightingAtlasContentsValid = false;

	FPooledRenderTargetDesc RadiosityDesc(FPooledRenderTargetDesc::Create2DDesc(LumenSceneData.GetRadiosityAtlasSize(), PF_FloatR11G11B10, FClearValueBinding::Black, TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV, false));
	RadiosityDesc.AutoWritable = false;
	GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, RadiosityDesc, LumenSceneData.RadiosityAtlas, TEXT("Lumen.SceneRadiosity"), ERenderTargetTransience::NonTransient);
}

// @todo Fold into AllocateCardAtlases after changing reallocation boolean to respect optional card atlas state settings
void AllocateOptionalCardAtlases(FRDGBuilder& GraphBuilder, FLumenSceneData& LumenSceneData, const FViewInfo& View, bool bReallocateAtlas)
{
	const FIntPoint PhysicalAtlasSize = LumenSceneData.GetPhysicalAtlasSize();

	FClearValueBinding CrazyGreen(FLinearColor(0.0f, 10000.0f, 0.0f, 1.0f));
	FPooledRenderTargetDesc LightingDesc(FPooledRenderTargetDesc::Create2DDesc(PhysicalAtlasSize, PF_FloatR11G11B10, CrazyGreen, TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_NoFastClear, false));
	LightingDesc.AutoWritable = false;

	const bool bUseIrradianceAtlas = Lumen::UseIrradianceAtlas(View);
	if (bUseIrradianceAtlas && (bReallocateAtlas || !LumenSceneData.IrradianceAtlas))
	{
		GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, LightingDesc, LumenSceneData.IrradianceAtlas, TEXT("Lumen.SceneIrradiance"), ERenderTargetTransience::NonTransient);
	}
	else if (!bUseIrradianceAtlas)
	{
		LumenSceneData.IrradianceAtlas = nullptr;
	}

	const bool bUseIndirectIrradianceAtlas = Lumen::UseIndirectIrradianceAtlas(View);
	if (bUseIndirectIrradianceAtlas && (bReallocateAtlas || !LumenSceneData.IndirectIrradianceAtlas))
	{
		GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, LightingDesc, LumenSceneData.IndirectIrradianceAtlas, TEXT("Lumen.SceneIndirectIrradiance"), ERenderTargetTransience::NonTransient);
	}
	else if (!bUseIndirectIrradianceAtlas)
	{
		LumenSceneData.IndirectIrradianceAtlas = nullptr;
	}
}

void AddCardCaptureDraws(const FScene* Scene,
	FRHICommandListImmediate& RHICmdList,
	FCardPageRenderData& CardPageRenderData,
	FMeshCommandOneFrameArray& VisibleMeshCommands,
	TArray<int32, SceneRenderingAllocator>& PrimitiveIds)
{
	LLM_SCOPE_BYTAG(Lumen);

	const EMeshPass::Type MeshPass = EMeshPass::LumenCardCapture;
	const ENaniteMeshPass::Type NaniteMeshPass = ENaniteMeshPass::LumenCardCapture;
	FPrimitiveSceneInfo* PrimitiveSceneInfo = CardPageRenderData.PrimitiveSceneInfo;

	if (PrimitiveSceneInfo && PrimitiveSceneInfo->Proxy->AffectsDynamicIndirectLighting())
	{
		if (PrimitiveSceneInfo->Proxy->IsNaniteMesh())
		{
			if (CardPageRenderData.PrimitiveInstanceIndexOrMergedFlag >= 0)
			{
				CardPageRenderData.NaniteInstanceIds.Add(PrimitiveSceneInfo->GetInstanceDataOffset() + CardPageRenderData.PrimitiveInstanceIndexOrMergedFlag);
			}
			else
			{
				// Render all instances
				const int32 NumInstances = PrimitiveSceneInfo->GetNumInstanceDataEntries();

				for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; ++InstanceIndex)
				{
					CardPageRenderData.NaniteInstanceIds.Add(PrimitiveSceneInfo->GetInstanceDataOffset() + InstanceIndex);
				}
			}

			for (const FNaniteCommandInfo& CommandInfo : PrimitiveSceneInfo->NaniteCommandInfos[NaniteMeshPass])
			{
				CardPageRenderData.NaniteCommandInfos.Add(CommandInfo);
			}
		}
		else
		{
			FLODMask LODToRender;

			int32 MaxLOD = 0;
			for (int32 MeshIndex = 0; MeshIndex < PrimitiveSceneInfo->StaticMeshRelevances.Num(); ++MeshIndex)
			{
				const FStaticMeshBatchRelevance& Mesh = PrimitiveSceneInfo->StaticMeshRelevances[MeshIndex];
				if (Mesh.ScreenSize > 0.0f)
				{
					//todo DynamicGI artist control - last LOD is sometimes billboard
					MaxLOD = FMath::Max(MaxLOD, (int32)Mesh.LODIndex);
				}
			}
			LODToRender.SetLOD(MaxLOD);

			for (int32 MeshIndex = 0; MeshIndex < PrimitiveSceneInfo->StaticMeshRelevances.Num(); MeshIndex++)
			{
				const FStaticMeshBatchRelevance& StaticMeshRelevance = PrimitiveSceneInfo->StaticMeshRelevances[MeshIndex];
				const FStaticMeshBatch& StaticMesh = PrimitiveSceneInfo->StaticMeshes[MeshIndex];

				if (StaticMeshRelevance.bUseForMaterial && LODToRender.ContainsLOD(StaticMeshRelevance.LODIndex))
				{
					const int32 StaticMeshCommandInfoIndex = StaticMeshRelevance.GetStaticMeshCommandInfoIndex(MeshPass);
					if (StaticMeshCommandInfoIndex >= 0)
					{
						const FCachedMeshDrawCommandInfo& CachedMeshDrawCommand = PrimitiveSceneInfo->StaticMeshCommandInfos[StaticMeshCommandInfoIndex];
						const FCachedPassMeshDrawList& SceneDrawList = Scene->CachedDrawLists[MeshPass];

						const FMeshDrawCommand* MeshDrawCommand = nullptr;
						if (CachedMeshDrawCommand.StateBucketId >= 0)
						{
							MeshDrawCommand = &Scene->CachedMeshDrawCommandStateBuckets[MeshPass].GetByElementId(CachedMeshDrawCommand.StateBucketId).Key;
						}
						else
						{
							MeshDrawCommand = &SceneDrawList.MeshDrawCommands[CachedMeshDrawCommand.CommandIndex];
						}

						CardPageRenderData.InstanceRuns.Reset();

						if (MeshDrawCommand->NumInstances > 1 && CardPageRenderData.PrimitiveInstanceIndexOrMergedFlag >= 0)
						{
							// Render only a single specified instance, by specifying an inclusive [x;x] range
							CardPageRenderData.InstanceRuns.Add(CardPageRenderData.PrimitiveInstanceIndexOrMergedFlag);
							CardPageRenderData.InstanceRuns.Add(CardPageRenderData.PrimitiveInstanceIndexOrMergedFlag);
						}

						FVisibleMeshDrawCommand NewVisibleMeshDrawCommand;
						
						NewVisibleMeshDrawCommand.Setup(
							MeshDrawCommand,
							PrimitiveSceneInfo->GetIndex(),
							PrimitiveSceneInfo->GetIndex(),
							CachedMeshDrawCommand.StateBucketId,
							CachedMeshDrawCommand.MeshFillMode,
							CachedMeshDrawCommand.MeshCullMode,
							CachedMeshDrawCommand.Flags,
							CachedMeshDrawCommand.SortKey,
							CardPageRenderData.InstanceRuns.Num() > 0 ? CardPageRenderData.InstanceRuns.GetData() : nullptr,
							CardPageRenderData.InstanceRuns.Num() / 2);

						VisibleMeshCommands.Add(NewVisibleMeshDrawCommand);
						PrimitiveIds.Add(PrimitiveSceneInfo->GetIndex());
					}
				}
			}
		}
	}
}

struct FMeshCardsAdd
{
	int32 LumenPrimitiveIndex;
	int32 LumenInstanceIndex;
	float DistanceSquared;
};

struct FMeshCardsRemove
{
	int32 LumenPrimitiveIndex;
	int32 LumenInstanceIndex;
};

struct FCardAllocationOutput
{
	bool bVisible = false;
	int32 ResLevel = -1;
};

// Loop over Lumen primitives and output FMeshCards adds and removes
struct FLumenSurfaceCacheUpdatePrimitivesTask
{
public:
	FLumenSurfaceCacheUpdatePrimitivesTask(
		const TArray<FLumenPrimitive>& InLumenPrimitives,
		FVector InViewOrigin,
		float InMaxDistanceFromCamera,
		int32 InFirstLumenPrimitiveIndex,
		int32 InNumPrimitivesPerPacket)
		: LumenPrimitives(InLumenPrimitives)
		, ViewOrigin(InViewOrigin)
		, FirstLumenPrimitiveIndex(InFirstLumenPrimitiveIndex)
		, NumPrimitivesPerPacket(InNumPrimitivesPerPacket)
		, MaxDistanceFromCamera(InMaxDistanceFromCamera)
		, TexelDensityScale(GetCardCameraDistanceTexelDensityScale())
	{
	}

	// Output
	TArray<FMeshCardsAdd> MeshCardsAdds;
	TArray<FMeshCardsRemove> MeshCardsRemoves;

	void AnyThreadTask()
	{
		const int32 LastLumenPrimitiveIndex = FMath::Min(FirstLumenPrimitiveIndex + NumPrimitivesPerPacket, LumenPrimitives.Num());
		const float MaxDistanceSquared = MaxDistanceFromCamera * MaxDistanceFromCamera;

		for (int32 PrimitiveIndex = FirstLumenPrimitiveIndex; PrimitiveIndex < LastLumenPrimitiveIndex; ++PrimitiveIndex)
		{
			const FLumenPrimitive& LumenPrimitive = LumenPrimitives[PrimitiveIndex];

			// Rough card min resolution test
			float DistanceSquared = ComputeSquaredDistanceFromBoxToPoint(LumenPrimitive.WorldSpaceBoundingBox.Min, LumenPrimitive.WorldSpaceBoundingBox.Max, ViewOrigin);
			float MaxCardResolution = (TexelDensityScale * LumenPrimitive.MaxCardExtent) / FMath::Sqrt(FMath::Max(DistanceSquared, 1.0f)) + 0.01f;

			if (DistanceSquared <= MaxDistanceSquared && MaxCardResolution >= 2.0f)
			{
				for (int32 InstanceIndex = 0; InstanceIndex < LumenPrimitive.Instances.Num(); ++InstanceIndex)
				{	
					const FLumenPrimitiveInstance& LumenInstance = LumenPrimitive.Instances[InstanceIndex];
					const float LumenInstanceMaxCardExtent = LumenInstance.WorldSpaceBoundingBox.GetExtent().GetMax();

					DistanceSquared = ComputeSquaredDistanceFromBoxToPoint(LumenInstance.WorldSpaceBoundingBox.Min, LumenInstance.WorldSpaceBoundingBox.Max, ViewOrigin);
					MaxCardResolution = (TexelDensityScale * LumenInstanceMaxCardExtent) / FMath::Sqrt(FMath::Max(DistanceSquared, 1.0f)) + 0.01f;

					if (DistanceSquared <= MaxDistanceSquared && MaxCardResolution >= 2.0f)
					{
						if (LumenInstance.MeshCardsIndex == -1 && LumenInstance.bValidMeshCards)
						{
							FMeshCardsAdd Add;
							Add.LumenPrimitiveIndex = PrimitiveIndex;
							Add.LumenInstanceIndex = InstanceIndex;
							Add.DistanceSquared = DistanceSquared;
							MeshCardsAdds.Add(Add);
						}
					}
					else if (LumenInstance.MeshCardsIndex >= 0)
					{
						FMeshCardsRemove Remove;
						Remove.LumenPrimitiveIndex = PrimitiveIndex;
						Remove.LumenInstanceIndex = InstanceIndex;
						MeshCardsRemoves.Add(Remove);
					}
				}
			}
			else if (LumenPrimitive.NumMeshCards > 0)
			{
				for (int32 InstanceIndex = 0; InstanceIndex < LumenPrimitive.Instances.Num(); ++InstanceIndex)
				{
					const FLumenPrimitiveInstance& LumenInstance = LumenPrimitive.Instances[InstanceIndex];

					if (LumenInstance.MeshCardsIndex >= 0)
					{
						FMeshCardsRemove Remove;
						Remove.LumenPrimitiveIndex = PrimitiveIndex;
						Remove.LumenInstanceIndex = InstanceIndex;
						MeshCardsRemoves.Add(Remove);
					}
				}
			}
		}
	}

	const TArray<FLumenPrimitive>& LumenPrimitives;
	FVector ViewOrigin;
	int32 FirstLumenPrimitiveIndex;
	int32 NumPrimitivesPerPacket;
	float MaxDistanceFromCamera;
	float TexelDensityScale;
};

struct FSurfaceCacheRemove
{
public:
	int32 LumenCardIndex;
};


// Loop over Lumen mesh cards and output card updates
struct FLumenSurfaceCacheUpdateMeshCardsTask
{
public:
	FLumenSurfaceCacheUpdateMeshCardsTask(
		const TSparseSpanArray<FLumenMeshCards>& InLumenMeshCards,
		const TSparseSpanArray<FLumenCard>& InLumenCards,
		FVector InViewOrigin,
		float InMaxDistanceFromCamera,
		int32 InFirstMeshCardsIndex,
		int32 InNumMeshCardsPerPacket)
		: LumenMeshCards(InLumenMeshCards)
		, LumenCards(InLumenCards)
		, ViewOrigin(InViewOrigin)
		, FirstMeshCardsIndex(InFirstMeshCardsIndex)
		, NumMeshCardsPerPacket(InNumMeshCardsPerPacket)
		, MaxDistanceFromCamera(InMaxDistanceFromCamera)
		, TexelDensityScale(GetCardCameraDistanceTexelDensityScale())
		, MaxTexelDensity(GLumenSceneCardMaxTexelDensity)
	{
	}

	// Output
	TArray<FSurfaceCacheRequest> SurfaceCacheRequests;
	TArray<int32> CardsToHide;

	void AnyThreadTask()
	{
		const int32 LastLumenMeshCardsIndex = FMath::Min(FirstMeshCardsIndex + NumMeshCardsPerPacket, LumenMeshCards.Num());
		const float MaxDistanceSquared = MaxDistanceFromCamera * MaxDistanceFromCamera;

		for (int32 MeshCardsIndex = FirstMeshCardsIndex; MeshCardsIndex < LastLumenMeshCardsIndex; ++MeshCardsIndex)
		{
			if (LumenMeshCards.IsAllocated(MeshCardsIndex))
			{
				const FLumenMeshCards& MeshCardsInstance = LumenMeshCards[MeshCardsIndex];

				for (uint32 CardIndex = MeshCardsInstance.FirstCardIndex; CardIndex < MeshCardsInstance.FirstCardIndex + MeshCardsInstance.NumCards; ++CardIndex)
				{
					const FLumenCard& LumenCard = LumenCards[CardIndex];

					const FVector CardSpaceViewOrigin = LumenCard.TransformWorldPositionToCardLocal(ViewOrigin);
					const FBox CardBox(-LumenCard.LocalExtent, LumenCard.LocalExtent);

					const float ViewerDistance = FMath::Max(FMath::Sqrt(CardBox.ComputeSquaredDistanceToPoint(CardSpaceViewOrigin)), 1.0f);

					// Compute resolution based on its largest extent
					float MaxExtent = FMath::Max(LumenCard.LocalExtent.X, LumenCard.LocalExtent.Y);
					float MaxProjectedSize = FMath::Min(TexelDensityScale * MaxExtent * LumenCard.ResolutionScale / ViewerDistance, GLumenSceneCardMaxTexelDensity * MaxExtent);

					if (GLumenSceneCardFixedDebugTexelDensity > 0)
					{
						MaxProjectedSize = GLumenSceneCardFixedDebugTexelDensity * MaxExtent;
					}

					const int32 MaxSnappedRes = FMath::RoundUpToPowerOfTwo(FMath::Min(FMath::TruncToInt(MaxProjectedSize), GetCardMaxResolution()));
					const int32 ResLevel = FMath::FloorLog2(MaxSnappedRes);

					const bool bVisible = ViewerDistance < MaxDistanceFromCamera&& MaxSnappedRes >= Lumen::MinCardResolution;

					if (!bVisible && LumenCard.bVisible)
					{
						CardsToHide.Add(CardIndex);
					}
					else if (bVisible && ResLevel != LumenCard.DesiredLockedResLevel)
					{
						float Distance = ViewerDistance;

						if (LumenCard.bVisible && LumenCard.DesiredLockedResLevel != ResLevel)
						{
							// Make reallocation less important than capturing new cards
							const float ResLevelDelta = FMath::Abs((int32)LumenCard.DesiredLockedResLevel - ResLevel);
							Distance += (1.0f - FMath::Clamp((ResLevelDelta + 1.0f) / 3.0f, 0.0f, 1.0f)) * 2500.0f;
						}

						FSurfaceCacheRequest Request;
						Request.ResLevel = ResLevel;
						Request.CardIndex = CardIndex;
						Request.LocalPageIndex = UINT16_MAX;
						Request.Distance = Distance;
						SurfaceCacheRequests.Add(Request);

						ensure(Request.IsLockedMip());
					}
				}
			}
		}
	}

	const TSparseSpanArray<FLumenMeshCards>& LumenMeshCards;
	const TSparseSpanArray<FLumenCard>& LumenCards;
	FVector ViewOrigin;
	int32 FirstMeshCardsIndex;
	int32 NumMeshCardsPerPacket;
	float MaxDistanceFromCamera;
	float TexelDensityScale;
	float MaxTexelDensity;
};

float ComputeMaxCardUpdateDistanceFromCamera()
{
	float MaxCardDistanceFromCamera = 0.0f;

	// Max voxel clipmap extent
	extern float GLumenSceneFirstClipmapWorldExtent;
	extern int32 GLumenSceneClipmapResolution;
	if (GetNumLumenVoxelClipmaps() > 0 && GLumenSceneClipmapResolution > 0)
	{
		const float LastClipmapExtent = GLumenSceneFirstClipmapWorldExtent * (float)(1 << (GetNumLumenVoxelClipmaps() - 1));
		MaxCardDistanceFromCamera = LastClipmapExtent;
	}

	return MaxCardDistanceFromCamera + GLumenSceneCardCaptureMargin;
}

// Process a throttled number of Lumen surface cache add requests
// It will make virtual and physical allocations, and evict old pages as required
void ProcessLumenSurfaceCacheRequests(
	const FViewInfo& MainView,
	FVector LumenSceneCameraOrigin,
	float MaxCardUpdateDistanceFromCamera,
	int32 MaxTileCapturesPerFrame,
	FLumenSceneData& LumenSceneData,
	FLumenCardRenderer& LumenCardRenderer,
	const TArray<FSurfaceCacheRequest, SceneRenderingAllocator>& SurfaceCacheRequests)
{
	QUICK_SCOPE_CYCLE_COUNTER(ProcessLumenSurfaceCacheRequests);

	TArray<FCardPageRenderData, SceneRenderingAllocator>& CardPagesToRender = LumenCardRenderer.CardPagesToRender;

	TArray<FVirtualPageIndex, SceneRenderingAllocator> HiResPagesToMap;
	TSparseUniqueList<int32, SceneRenderingAllocator> DirtyCards;

	FLumenSurfaceCacheAllocator CaptureAtlasAllocator;
	CaptureAtlasAllocator.Init(LumenSceneData.GetCardCaptureAtlasSizeInPages());

	for (int32 RequestIndex = 0; RequestIndex < SurfaceCacheRequests.Num(); ++RequestIndex)
	{
		const FSurfaceCacheRequest& Request = SurfaceCacheRequests[RequestIndex];

		if (Request.IsLockedMip())
		{
			// Update low-res locked (always resident) pages
			FLumenCard& Card = LumenSceneData.Cards[Request.CardIndex];

			if (Card.DesiredLockedResLevel != Request.ResLevel)
			{
				// Check if we can make this allocation at all
				bool bCanAlloc = true;

				uint8 NewLockedAllocationResLevel = Request.ResLevel;
				while (!LumenSceneData.IsPhysicalSpaceAvailable(Card, NewLockedAllocationResLevel, /*bSinglePage*/ false))
				{
					if (!LumenSceneData.EvictOldestAllocation(/*bForceEvict*/ true, DirtyCards))
					{
						bCanAlloc = false;
						break;
					}
				}

				// Try to decrease resolution if allocation still can't be made
				while (!bCanAlloc && NewLockedAllocationResLevel > Lumen::MinResLevel)
				{
					--NewLockedAllocationResLevel;
					bCanAlloc = LumenSceneData.IsPhysicalSpaceAvailable(Card, NewLockedAllocationResLevel, /*bSinglePage*/ false);
				}

				// Can we fit this card into the temporary card capture allocator?
				if (!CaptureAtlasAllocator.IsSpaceAvailable(Card, NewLockedAllocationResLevel, /*bSinglePage*/ false))
				{
					bCanAlloc = false;
				}

				if (bCanAlloc)
				{
					Card.bVisible = true;
					Card.DesiredLockedResLevel = Request.ResLevel;

					// Free previous MinAllocatedResLevel
					LumenSceneData.FreeVirtualSurface(Card, Card.MinAllocatedResLevel, Card.MinAllocatedResLevel);

					// Free anything lower res than the new res level
					LumenSceneData.FreeVirtualSurface(Card, Card.MinAllocatedResLevel, NewLockedAllocationResLevel - 1);


					const bool bLockPages = true;
					LumenSceneData.ReallocVirtualSurface(Card, Request.CardIndex, NewLockedAllocationResLevel, bLockPages);

					// Map and update all pages
					FLumenSurfaceMipMap& MipMap = Card.GetMipMap(Card.MinAllocatedResLevel);
					for (int32 LocalPageIndex = 0; LocalPageIndex < MipMap.SizeInPagesX * MipMap.SizeInPagesY; ++LocalPageIndex)
					{
						const int32 PageIndex = MipMap.GetPageTableIndex(LocalPageIndex);
						FLumenPageTableEntry& PageTableEntry = LumenSceneData.MapSurfaceCachePage(MipMap, PageIndex);
						ensure(PageTableEntry.IsMapped());

						// Allocate space in temporary allocation atlas
						FLumenSurfaceCacheAllocator::FAllocation CardCaptureAllocation;
						CaptureAtlasAllocator.Allocate(PageTableEntry, CardCaptureAllocation);
						ensure(CardCaptureAllocation.PhysicalPageCoord.X >= 0);

						const FLumenMeshCards& MeshCardsElement = LumenSceneData.MeshCards[Card.MeshCardsIndex];

						CardPagesToRender.Add(FCardPageRenderData(
							MainView,
							Card,
							PageTableEntry.CardUVRect,
							CardCaptureAllocation.PhysicalAtlasRect,
							PageTableEntry.PhysicalAtlasRect,
							MeshCardsElement.CapturePrimitive,
							MeshCardsElement.CaptureInstanceIndex,
							Request.CardIndex,
							PageIndex));

						LumenCardRenderer.NumCardTexelsToCapture += PageTableEntry.PhysicalAtlasRect.Area();
					}

					DirtyCards.Add(Request.CardIndex);
				}
			}
		}
		else
		{
			// Hi-Res
			if (LumenSceneData.Cards.IsAllocated(Request.CardIndex))
			{
				FLumenCard& Card = LumenSceneData.Cards[Request.CardIndex];

				if (Card.bVisible && Card.MinAllocatedResLevel >= 0 && Request.ResLevel > Card.MinAllocatedResLevel)
				{
					HiResPagesToMap.Add(FVirtualPageIndex(Request.CardIndex, Request.ResLevel, Request.LocalPageIndex));
				}
			}
		}

		if (CardPagesToRender.Num() + HiResPagesToMap.Num() >= MaxTileCapturesPerFrame)
		{
			break;
		}
	}

	// Process hi-res optional pages after locked low res ones are done
	for (const FVirtualPageIndex& VirtualPageIndex : HiResPagesToMap)
	{
		FLumenCard& Card = LumenSceneData.Cards[VirtualPageIndex.CardIndex];

		if (VirtualPageIndex.ResLevel > Card.MinAllocatedResLevel)
		{
			// Make room for new physical allocations
			bool bCanAlloc = true;
			while (!LumenSceneData.IsPhysicalSpaceAvailable(Card, VirtualPageIndex.ResLevel, /*bSinglePage*/ true))
			{
				if (!LumenSceneData.EvictOldestAllocation(/*bForceEvict*/ false, DirtyCards))
				{
					bCanAlloc = false;
					break;
				}
			}

			// Can we fit this card into the temporary card capture allocator?
			if (!CaptureAtlasAllocator.IsSpaceAvailable(Card, VirtualPageIndex.ResLevel, /*bSinglePage*/ true))
			{
				bCanAlloc = false;
			}

			if (bCanAlloc)
			{
				const bool bLockPages = false;

				LumenSceneData.ReallocVirtualSurface(Card, VirtualPageIndex.CardIndex, VirtualPageIndex.ResLevel, bLockPages);

				FLumenSurfaceMipMap& MipMap = Card.GetMipMap(VirtualPageIndex.ResLevel);
				const int32 PageIndex = MipMap.GetPageTableIndex(VirtualPageIndex.LocalPageIndex);
				FLumenPageTableEntry& PageTableEntry = LumenSceneData.MapSurfaceCachePage(MipMap, PageIndex);
				ensure(PageTableEntry.IsMapped());

				// Allocate space in temporary allocation atlas
				FLumenSurfaceCacheAllocator::FAllocation CardCaptureAllocation;
				CaptureAtlasAllocator.Allocate(PageTableEntry, CardCaptureAllocation);
				ensure(CardCaptureAllocation.PhysicalPageCoord.X >= 0);

				const FLumenMeshCards& MeshCardsElement = LumenSceneData.MeshCards[Card.MeshCardsIndex];

				CardPagesToRender.Add(FCardPageRenderData(
					MainView,
					Card,
					PageTableEntry.CardUVRect,
					CardCaptureAllocation.PhysicalAtlasRect,
					PageTableEntry.PhysicalAtlasRect,
					MeshCardsElement.CapturePrimitive,
					MeshCardsElement.CaptureInstanceIndex,
					VirtualPageIndex.CardIndex,
					PageIndex));

				LumenCardRenderer.NumCardTexelsToCapture += PageTableEntry.PhysicalAtlasRect.Area();

				DirtyCards.Add(VirtualPageIndex.CardIndex);
			}
		}
	}

	for (int32 CardIndex : DirtyCards.Array)
	{
		FLumenCard& Card = LumenSceneData.Cards[CardIndex];
		LumenSceneData.UpdateCardMipMapHierarchy(Card);
		LumenSceneData.CardIndicesToUpdateInBuffer.Add(CardIndex);
	}
}

void UpdateSurfaceCachePrimitives(
	FLumenSceneData& LumenSceneData,
	FVector LumenSceneCameraOrigin,
	float MaxCardUpdateDistanceFromCamera)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UpdateSurfaceCachePrimitives);

	const int32 NumPrimitivesPerTask = FMath::Max(GLumenScenePrimitivesPerTask, 1);
	const int32 NumTasks = FMath::DivideAndRoundUp(LumenSceneData.LumenPrimitives.Num(), GLumenScenePrimitivesPerTask);

	TArray<FLumenSurfaceCacheUpdatePrimitivesTask, SceneRenderingAllocator> Tasks;
	Tasks.Reserve(NumTasks);

	for (int32 TaskIndex = 0; TaskIndex < NumTasks; ++TaskIndex)
	{
		Tasks.Emplace(
			LumenSceneData.LumenPrimitives,
			LumenSceneCameraOrigin,
			MaxCardUpdateDistanceFromCamera,
			TaskIndex * NumPrimitivesPerTask,
			NumPrimitivesPerTask);
	}

	const bool bExecuteInParallel = FApp::ShouldUseThreadingForPerformance();

	ParallelFor(Tasks.Num(),
		[&Tasks](int32 Index)
		{
			Tasks[Index].AnyThreadTask();
		},
		!bExecuteInParallel);

	TArray<FMeshCardsAdd, SceneRenderingAllocator> MeshCardsAdds;

	for (int32 TaskIndex = 0; TaskIndex < Tasks.Num(); ++TaskIndex)
	{
		const FLumenSurfaceCacheUpdatePrimitivesTask& Task = Tasks[TaskIndex];
		LumenSceneData.NumMeshCardsToAdd += Task.MeshCardsAdds.Num();

		// Append requests to the global array
		{
			MeshCardsAdds.Reserve(MeshCardsAdds.Num() + Task.MeshCardsAdds.Num());

			for (int32 RequestIndex = 0; RequestIndex < Task.MeshCardsAdds.Num(); ++RequestIndex)
			{
				MeshCardsAdds.Add(Task.MeshCardsAdds[RequestIndex]);
			}
		}

		for (const FMeshCardsRemove& MeshCardsRemove : Task.MeshCardsRemoves)
		{
			FLumenPrimitive& LumenPrimitive = LumenSceneData.LumenPrimitives[MeshCardsRemove.LumenPrimitiveIndex];
			FLumenPrimitiveInstance& LumenPrimitiveInstance = LumenPrimitive.Instances[MeshCardsRemove.LumenInstanceIndex];

			LumenSceneData.RemoveMeshCards(LumenPrimitive, LumenPrimitiveInstance);
		}
	}

	if (MeshCardsAdds.Num() > 0)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SortAdds);

		struct FSortBySmallerDistance
		{
			FORCEINLINE bool operator()(const FMeshCardsAdd& A, const FMeshCardsAdd& B) const
			{
				return A.DistanceSquared < B.DistanceSquared;
			}
		};

		MeshCardsAdds.Sort(FSortBySmallerDistance());
	}

	const int32 MeshCardsToAddPerFrame = 2 * GetMaxLumenSceneCardCapturesPerFrame();

	for (int32 MeshCardsIndex = 0; MeshCardsIndex < FMath::Min(MeshCardsAdds.Num(), MeshCardsToAddPerFrame); ++MeshCardsIndex)
	{
		const FMeshCardsAdd& MeshCardsAdd = MeshCardsAdds[MeshCardsIndex];
		LumenSceneData.AddMeshCards(MeshCardsAdd.LumenPrimitiveIndex, MeshCardsAdd.LumenInstanceIndex);
	}
}

void UpdateSurfaceCacheMeshCards(
	FLumenSceneData& LumenSceneData,
	FVector LumenSceneCameraOrigin,
	float MaxCardUpdateDistanceFromCamera,
	TArray<FSurfaceCacheRequest, SceneRenderingAllocator>& SurfaceCacheRequests)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UpdateMeshCards);

	const int32 NumMeshCardsPerTask = FMath::Max(GLumenSceneMeshCardsPerTask, 1);
	const int32 NumTasks = FMath::DivideAndRoundUp(LumenSceneData.MeshCards.Num(), NumMeshCardsPerTask);

	TArray<FLumenSurfaceCacheUpdateMeshCardsTask, SceneRenderingAllocator> Tasks;
	Tasks.Reserve(NumTasks);

	for (int32 TaskIndex = 0; TaskIndex < NumTasks; ++TaskIndex)
	{
		Tasks.Emplace(
			LumenSceneData.MeshCards,
			LumenSceneData.Cards,
			LumenSceneCameraOrigin,
			MaxCardUpdateDistanceFromCamera,
			TaskIndex * NumMeshCardsPerTask,
			NumMeshCardsPerTask);
	}

	const bool bExecuteInParallel = FApp::ShouldUseThreadingForPerformance();

	ParallelFor(Tasks.Num(),
		[&Tasks](int32 Index)
		{
			Tasks[Index].AnyThreadTask();
		},
		!bExecuteInParallel);

	for (int32 TaskIndex = 0; TaskIndex < Tasks.Num(); ++TaskIndex)
	{
		const FLumenSurfaceCacheUpdateMeshCardsTask& Task = Tasks[TaskIndex];
		LumenSceneData.NumLockedCardsToUpdate += Task.SurfaceCacheRequests.Num();

		// Append requests to the global array
		{
			SurfaceCacheRequests.Reserve(SurfaceCacheRequests.Num() + Task.SurfaceCacheRequests.Num());

			for (int32 RequestIndex = 0; RequestIndex < Task.SurfaceCacheRequests.Num(); ++RequestIndex)
			{
				SurfaceCacheRequests.Add(Task.SurfaceCacheRequests[RequestIndex]);
			}
		}

		for (int32 CardIndex : Task.CardsToHide)
		{
			FLumenCard& Card = LumenSceneData.Cards[CardIndex];

			if (Card.bVisible)
			{
				LumenSceneData.RemoveCardFromAtlas(CardIndex);
				Card.bVisible = false;
			}
		}
	}

	LumenSceneData.UpdateSurfaceCacheFeedback(LumenSceneCameraOrigin, SurfaceCacheRequests);

	if (SurfaceCacheRequests.Num() > 0)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SortRequests);

		struct FSortBySmallerDistance
		{
			FORCEINLINE bool operator()(const FSurfaceCacheRequest& A, const FSurfaceCacheRequest& B) const
			{
				return A.Distance < B.Distance;
			}
		};

		SurfaceCacheRequests.Sort(FSortBySmallerDistance());
	}
}

extern void UpdateLumenScenePrimitives(FScene* Scene);

void FDeferredShadingSceneRenderer::BeginUpdateLumenSceneTasks(FRDGBuilder& GraphBuilder)
{
	LLM_SCOPE_BYTAG(Lumen);

	const FViewInfo& View = Views[0];
	const bool bAnyLumenActive = ShouldRenderLumenDiffuseGI(Scene, View, true)
		|| ShouldRenderLumenReflections(View, true);

	LumenCardRenderer.Reset();

	if (bAnyLumenActive
		&& !ViewFamily.EngineShowFlags.HitProxies)
	{
		SCOPED_NAMED_EVENT(FDeferredShadingSceneRenderer_BeginUpdateLumenSceneTasks, FColor::Emerald);
		QUICK_SCOPE_CYCLE_COUNTER(BeginUpdateLumenSceneTasks);
		const double StartTime = FPlatformTime::Seconds();

		FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;
		LumenSceneData.bDebugClearAllCachedState = GLumenSceneRecaptureLumenSceneEveryFrame != 0;
		const bool bReallocateAtlas = LumenSceneData.UpdateAtlasSize();

		if (GLumenSceneReset == 1 
			|| GLumenSceneReset == 2 
			|| (GLumenSceneReset == 3 && View.Family->FrameNumber % 2 == 0)
			|| (GLumenSceneReset == 4 && View.Family->FrameNumber % 3 == 0))
		{
			LumenSceneData.bDebugClearAllCachedState = true;
		}

		if (GLumenSceneReset == 1)
		{
			GLumenSceneReset = 0;
		}

		if (GLumenSceneForceEvict != 0)
		{
			LumenSceneData.ForceEvictEntireCache();
			GLumenSceneForceEvict = 0;
		}

		LumenSceneData.NumMeshCardsToAdd = 0;
		LumenSceneData.NumLockedCardsToUpdate = 0;
		LumenSceneData.NumHiResPagesToAdd = 0;

		if (LumenSceneData.bDebugClearAllCachedState || bReallocateAtlas)
		{
			LumenSceneData.RemoveAllMeshCards();
		}

		UpdateLumenScenePrimitives(Scene);
		UpdateDistantScene(Scene, Views[0]);

		const FVector LumenSceneCameraOrigin = GetLumenSceneViewOrigin(View, GetNumLumenVoxelClipmaps() - 1);
		const float MaxCardUpdateDistanceFromCamera = ComputeMaxCardUpdateDistanceFromCamera();
		const int32 MaxTileCapturesPerFrame = GLumenSceneRecaptureLumenSceneEveryFrame != 0 ? INT32_MAX : GetMaxLumenSceneCardCapturesPerFrame();

		if (MaxTileCapturesPerFrame > 0)
		{
			QUICK_SCOPE_CYCLE_COUNTER(FillCardPagesToRender);

			TArray<FSurfaceCacheRequest, SceneRenderingAllocator> SurfaceCacheRequests;

			UpdateSurfaceCachePrimitives(
				LumenSceneData,
				LumenSceneCameraOrigin,
				MaxCardUpdateDistanceFromCamera);

			UpdateSurfaceCacheMeshCards(
				LumenSceneData,
				LumenSceneCameraOrigin,
				MaxCardUpdateDistanceFromCamera,
				SurfaceCacheRequests);

			ProcessLumenSurfaceCacheRequests(
				View,
				LumenSceneCameraOrigin,
				MaxCardUpdateDistanceFromCamera,
				MaxTileCapturesPerFrame,
				LumenSceneData,
				LumenCardRenderer,
				SurfaceCacheRequests);
		}

		// Atlas reallocation
		{
			AllocateOptionalCardAtlases(GraphBuilder, LumenSceneData, View, bReallocateAtlas);

			if (bReallocateAtlas || !LumenSceneData.AlbedoAtlas)
			{
				AllocateCardAtlases(GraphBuilder, LumenSceneData, View);
			}

			if (LumenSceneData.bDebugClearAllCachedState)
			{
				ClearLumenSurfaceCacheAtlas(GraphBuilder, View);
			}

			LumenSceneData.UploadPageTable(GraphBuilder);
		}
		
		TArray<FCardPageRenderData, SceneRenderingAllocator>& CardPagesToRender = LumenCardRenderer.CardPagesToRender;
		if (CardPagesToRender.Num() > 0)
		{
			{
				QUICK_SCOPE_CYCLE_COUNTER(MeshPassSetup);

				// Make sure all mesh rendering data is prepared before we render
				{
					QUICK_SCOPE_CYCLE_COUNTER(PrepareStaticMeshData);

					// Set of unique primitives requiring static mesh update
					TSet<FPrimitiveSceneInfo*> PrimitivesToUpdateStaticMeshes;

					for (FCardPageRenderData& CardPageRenderData : CardPagesToRender)
					{
						FPrimitiveSceneInfo* PrimitiveSceneInfo = CardPageRenderData.PrimitiveSceneInfo;

						if (PrimitiveSceneInfo && PrimitiveSceneInfo->Proxy->AffectsDynamicIndirectLighting())
						{
							if (PrimitiveSceneInfo->NeedsUniformBufferUpdate())
							{
								PrimitiveSceneInfo->UpdateUniformBuffer(GraphBuilder.RHICmdList);
							}

							if (PrimitiveSceneInfo->NeedsUpdateStaticMeshes())
							{
								PrimitivesToUpdateStaticMeshes.Add(PrimitiveSceneInfo);
							}
						}
					}

					if (PrimitivesToUpdateStaticMeshes.Num() > 0)
					{
						TArray<FPrimitiveSceneInfo*> UpdatedSceneInfos;
						UpdatedSceneInfos.Reserve(PrimitivesToUpdateStaticMeshes.Num());
						for (FPrimitiveSceneInfo* PrimitiveSceneInfo : PrimitivesToUpdateStaticMeshes)
						{
							UpdatedSceneInfos.Add(PrimitiveSceneInfo);
						}

						FPrimitiveSceneInfo::UpdateStaticMeshes(GraphBuilder.RHICmdList, Scene, UpdatedSceneInfos, true);
					}
				}

				for (FCardPageRenderData& CardPageRenderData : CardPagesToRender)
				{
					CardPageRenderData.StartMeshDrawCommandIndex = LumenCardRenderer.MeshDrawCommands.Num();
					CardPageRenderData.NumMeshDrawCommands = 0;
					int32 NumNanitePrimitives = 0;

					const FLumenCard& Card = LumenSceneData.Cards[CardPageRenderData.CardIndex];
					ensure(Card.bVisible);

					AddCardCaptureDraws(Scene, 
						GraphBuilder.RHICmdList, 
						CardPageRenderData,
						LumenCardRenderer.MeshDrawCommands, 
						LumenCardRenderer.MeshDrawPrimitiveIds);

					CardPageRenderData.NumMeshDrawCommands = LumenCardRenderer.MeshDrawCommands.Num() - CardPageRenderData.StartMeshDrawCommandIndex;
				}
			}

			const float TimeElapsed = FPlatformTime::Seconds() - StartTime;

			if (TimeElapsed > .03f)
			{
				UE_LOG(LogRenderer, Log, TEXT("BeginUpdateLumenSceneTasks %u Card Renders %.1fms"), CardPagesToRender.Num(), TimeElapsed * 1000.0f);
			}
		}
	}
}

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FLumenCardScene, "LumenCardScene");

class FNullCardBuffers : public FRenderResource
{
public:

	virtual void InitRHI() override
	{
		EPixelFormat BufferFormat = PF_A32B32G32R32F;
		uint32 BytesPerElement = GPixelFormats[BufferFormat].BlockBytes;
		CardData.Initialize(TEXT("FNullCardBuffers"), BytesPerElement, 1, 0, false, false, ERHIAccess::SRVMask);
	}

	virtual void ReleaseRHI() override
	{
		CardData.Release();
	}

	FRWBufferStructured CardData;
};

TGlobalResource<FNullCardBuffers> GNullCardBuffers;


void SetupLumenCardSceneParameters(FRDGBuilder& GraphBuilder, const FScene* Scene, FLumenCardScene& OutParameters)
{
	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	OutParameters.NumCards = LumenSceneData.Cards.Num();
	OutParameters.NumCardPages = LumenSceneData.GetNumCardPages();
	OutParameters.MaxConeSteps = GLumenGIMaxConeSteps;	
	OutParameters.PhysicalAtlasSize = LumenSceneData.GetPhysicalAtlasSize();
	OutParameters.InvPhysicalAtlasSize = FVector2D(1.0f) / OutParameters.PhysicalAtlasSize;
	OutParameters.NumDistantCards = LumenSceneData.DistantCardIndices.Num();
	extern float GLumenDistantSceneMaxTraceDistance;
	OutParameters.DistantSceneMaxTraceDistance = GLumenDistantSceneMaxTraceDistance;
	OutParameters.DistantSceneDirection = FVector(0.0f, 0.0f, 0.0f);

	if (Scene->DirectionalLights.Num() > 0)
	{
		OutParameters.DistantSceneDirection = -Scene->DirectionalLights[0]->Proxy->GetDirection();
	}
	
	for (int32 i = 0; i < LumenSceneData.DistantCardIndices.Num(); i++)
	{
		OutParameters.DistantCardIndices[i] = LumenSceneData.DistantCardIndices[i];
	}

	if (LumenSceneData.CardBuffer.SRV)
	{
		OutParameters.CardData = LumenSceneData.CardBuffer.SRV;
	}
	else
	{
		OutParameters.CardData = GNullCardBuffers.CardData.SRV;
	}

	if (LumenSceneData.CardPageBuffer.SRV)
	{
		OutParameters.CardPageData = LumenSceneData.CardPageBuffer.SRV;
	}
	else
	{
		OutParameters.CardPageData = GNullCardBuffers.CardData.SRV;
	}

	if (LumenSceneData.GetPageTableBufferSRV())
	{
		OutParameters.PageTableBuffer = LumenSceneData.GetPageTableBufferSRV();
	}
	else
	{
		OutParameters.PageTableBuffer = GNullCardBuffers.CardData.SRV;
	}

	if (LumenSceneData.AlbedoAtlas.IsValid())
	{
		OutParameters.AlbedoAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.AlbedoAtlas, TEXT("Lumen.SceneAlbedo"));
		OutParameters.NormalAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.NormalAtlas, TEXT("Lumen.SceneNormal"));
		OutParameters.EmissiveAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.EmissiveAtlas, TEXT("Lumen.SceneEmissive"));
		OutParameters.DepthAtlas = GraphBuilder.RegisterExternalTexture(LumenSceneData.DepthAtlas, TEXT("Lumen.SceneDepth"));
	}
	else
	{
		FRDGTextureRef BlackDummyTextureRef = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy, TEXT("Lumen.BlackDummy"));
		OutParameters.AlbedoAtlas = BlackDummyTextureRef;
		OutParameters.NormalAtlas = BlackDummyTextureRef;
		OutParameters.EmissiveAtlas = BlackDummyTextureRef;
		OutParameters.DepthAtlas = BlackDummyTextureRef;
	}
	
	OutParameters.MeshCardsData = LumenSceneData.MeshCardsBuffer.SRV;
	OutParameters.DFObjectToMeshCardsIndexBuffer = LumenSceneData.DFObjectToMeshCardsIndexBuffer.SRV;
}

DECLARE_GPU_STAT(UpdateCardSceneBuffer);

class FClearLumenCardsPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearLumenCardsPS);
	SHADER_USE_PARAMETER_STRUCT(FClearLumenCardsPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearLumenCardsPS, "/Engine/Private/Lumen/LumenSurfaceCache.usf", "ClearLumenCardsPS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FClearLumenCardsParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FPixelShaderUtils::FRasterizeToRectsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FClearLumenCardsPS::FParameters, PS)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void ClearLumenCards(FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FCardCaptureAtlas& Atlas,
	FRDGBufferSRVRef RectCoordBufferSRV,
	uint32 NumRects)
{
	LLM_SCOPE_BYTAG(Lumen);

	FClearLumenCardsParameters* PassParameters = GraphBuilder.AllocParameters<FClearLumenCardsParameters>();

	PassParameters->RenderTargets[0] = FRenderTargetBinding(Atlas.Albedo, ERenderTargetLoadAction::ELoad);
	PassParameters->RenderTargets[1] = FRenderTargetBinding(Atlas.Normal, ERenderTargetLoadAction::ELoad);
	PassParameters->RenderTargets[2] = FRenderTargetBinding(Atlas.Emissive, ERenderTargetLoadAction::ELoad);
	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(Atlas.DepthStencil, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilWrite);

	auto PixelShader = View.ShaderMap->GetShader<FClearLumenCardsPS>();

	FPixelShaderUtils::AddRasterizeToRectsPass<FClearLumenCardsPS>(
		GraphBuilder,
		View.ShaderMap,
		RDG_EVENT_NAME("ClearLumenCards"),
		PixelShader,
		PassParameters,
		Atlas.Size,
		RectCoordBufferSRV,
		NumRects,
		TStaticBlendState<>::GetRHI(),
		TStaticRasterizerState<>::GetRHI(),
		TStaticDepthStencilState<true, CF_Always,
		true, CF_Always, SO_Replace, SO_Replace, SO_Replace,
		false, CF_Always, SO_Replace, SO_Replace, SO_Replace,
		0xff, 0xff>::GetRHI());
}

BEGIN_SHADER_PARAMETER_STRUCT(FLumenBufferUpload, )
	RDG_BUFFER_ACCESS(DestBuffer, ERHIAccess::CopyDest)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardPassParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardPassUniformParameters, CardPass)
	SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void Lumen::SetupViewUniformBufferParameters(FScene* Scene, FViewUniformShaderParameters& ViewUniformShaderParameters)
{
	// #lumen_todo: Rename ViewUniformShaderParameters LumenInstance to LumenDFInstance in a separate CL
	if (Scene && Scene->LumenSceneData && Scene->LumenSceneData->PrimitiveToLumenDFInstanceOffsetBufferSize > 0)
	{
		FLumenSceneData* LumenSceneData = Scene->LumenSceneData;
		ViewUniformShaderParameters.PrimitiveToLumenInstanceOffsetBuffer = LumenSceneData->PrimitiveToDFLumenInstanceOffsetBuffer.SRV;
		ViewUniformShaderParameters.PrimitiveToLumenInstanceOffsetBufferSize = LumenSceneData->PrimitiveToLumenDFInstanceOffsetBufferSize;
		ViewUniformShaderParameters.LumenInstanceToDFObjectIndexBuffer = LumenSceneData->LumenDFInstanceToDFObjectIndexBuffer.SRV;
		ViewUniformShaderParameters.LumenInstanceToDFObjectIndexBufferSize = LumenSceneData->LumenDFInstanceToDFObjectIndexBufferSize;
	}
	else
	{
		ViewUniformShaderParameters.PrimitiveToLumenInstanceOffsetBuffer = GIdentityPrimitiveBuffer.InstanceSceneDataBufferSRV;
		ViewUniformShaderParameters.PrimitiveToLumenInstanceOffsetBufferSize = 0;
		ViewUniformShaderParameters.LumenInstanceToDFObjectIndexBuffer = GIdentityPrimitiveBuffer.InstanceSceneDataBufferSRV;
		ViewUniformShaderParameters.LumenInstanceToDFObjectIndexBufferSize = 0;
	}
}

FIntPoint FLumenSceneData::GetCardCaptureAtlasSizeInPages() const
{
	return FMath::DivideAndRoundUp<FIntPoint>(PhysicalAtlasSize, Lumen::PhysicalPageSize * FMath::Clamp(GLumenSceneCardCaptureAtlasFactor, 1, 16));
}

FIntPoint FLumenSceneData::GetCardCaptureAtlasSize() const
{
	return GetCardCaptureAtlasSizeInPages() * Lumen::PhysicalPageSize;
}

void AllocatedCardCaptureAtlas(FRDGBuilder& GraphBuilder, FIntPoint CardCaptureAtlasSize, FCardCaptureAtlas& CardCaptureAtlas)
{
	CardCaptureAtlas.Size = CardCaptureAtlasSize;

	CardCaptureAtlas.Albedo = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(
			CardCaptureAtlasSize,
			PF_R8G8B8A8,
			FClearValueBinding::Green,
			TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_NoFastClear),
		TEXT("Lumen.CardCaptureAlbedoAtlas"));

	CardCaptureAtlas.Normal = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(
			CardCaptureAtlasSize,
			PF_R8G8B8A8,
			FClearValueBinding::Green,
			TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_NoFastClear),
		TEXT("Lumen.CardCaptureNormalAtlas"));

	CardCaptureAtlas.Emissive = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(
			CardCaptureAtlasSize,
			PF_FloatR11G11B10,
			FClearValueBinding::Green,
			TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_NoFastClear),
		TEXT("Lumen.CardCaptureEmissiveAtlas"));

	CardCaptureAtlas.DepthStencil = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(
			CardCaptureAtlasSize,
			PF_DepthStencil,
			FClearValueBinding::DepthZero,
			TexCreate_ShaderResource | TexCreate_DepthStencilTargetable | TexCreate_NoFastClear),
		TEXT("Lumen.CardCaptureDepthStencilAtlas"));
}

void UploadCardPagesToRenderIndexBuffers(
	FRDGBuilder& GraphBuilder,
	const TArray<FCardPageRenderData, SceneRenderingAllocator>& CardPagesToRender,
	FLumenCardRenderer& LumenCardRenderer)
{
	QUICK_SCOPE_CYCLE_COUNTER(UploadCardPagesToRenderIndexBuffers);

	{
		LumenCardRenderer.CardPagesToRenderIndexBuffer = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateUploadDesc(sizeof(uint32), FMath::Max(CardPagesToRender.Num(), 1)),
			TEXT("Lumen.CardPagesToRenderIndexBuffer"));

		FLumenBufferUpload* PassParameters = GraphBuilder.AllocParameters<FLumenBufferUpload>();
		PassParameters->DestBuffer = LumenCardRenderer.CardPagesToRenderIndexBuffer;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("Upload CardPagesToRenderIndexBuffer NumIndices=%d", CardPagesToRender.Num()),
			PassParameters,
			ERDGPassFlags::Copy,
			[PassParameters, CardPagesToRender](FRHICommandListImmediate& RHICmdList)
			{
				const uint32 CardPageIdBytes = sizeof(int32) * CardPagesToRender.Num();
				if (CardPageIdBytes > 0)
				{
					int32* DestCardIdPtr = (int32*)RHILockBuffer(PassParameters->DestBuffer->GetRHI(), 0, CardPageIdBytes, RLM_WriteOnly);
					for (int32 CardPageIndex = 0; CardPageIndex < CardPagesToRender.Num(); ++CardPageIndex)
					{
						DestCardIdPtr[CardPageIndex] = CardPagesToRender[CardPageIndex].PageTableIndex;
					}
					RHIUnlockBuffer(PassParameters->DestBuffer->GetRHI());
				}
			});
	}

	{
		const uint32 NumHashMapUInt32 = FLumenCardRenderer::NumCardPagesToRenderHashMapBucketUInt32;
		const uint32 NumHashMapBytes = 4 * NumHashMapUInt32;
		const uint32 NumHashMapBuckets = 32 * NumHashMapUInt32;

		LumenCardRenderer.CardPagesToRenderHashMapBuffer = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateUploadDesc(sizeof(uint32), NumHashMapUInt32),
			TEXT("Lumen.CardPagesToRenderHashMapBuffer"));

		LumenCardRenderer.CardPagesToRenderHashMap.Init(0, NumHashMapBuckets);

		for (const FCardPageRenderData& CardPageRenderData : LumenCardRenderer.CardPagesToRender)
		{
			ensure(CardPageRenderData.PageTableIndex >= 0);
			LumenCardRenderer.CardPagesToRenderHashMap[CardPageRenderData.PageTableIndex % NumHashMapBuckets] = 1;
		}

		FLumenBufferUpload* PassParameters = GraphBuilder.AllocParameters<FLumenBufferUpload>();
		PassParameters->DestBuffer = LumenCardRenderer.CardPagesToRenderHashMapBuffer;

		const void* HashMapDataPtr = LumenCardRenderer.CardPagesToRenderHashMap.GetData();

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("Upload CardPagesToRenderHashMapBuffer NumUInt32=%d", NumHashMapUInt32),
			PassParameters,
			ERDGPassFlags::Copy,
			[PassParameters, NumHashMapBytes, HashMapDataPtr](FRHICommandListImmediate& RHICmdList)
			{
				if (NumHashMapBytes > 0)
				{
					void* DestCardIdPtr = RHILockBuffer(PassParameters->DestBuffer->GetRHI(), 0, NumHashMapBytes, RLM_WriteOnly);
					FPlatformMemory::Memcpy(DestCardIdPtr, HashMapDataPtr, NumHashMapBytes);
					RHIUnlockBuffer(PassParameters->DestBuffer->GetRHI());
				}
			});
	}
}

void FDeferredShadingSceneRenderer::UpdateLumenScene(FRDGBuilder& GraphBuilder)
{
	LLM_SCOPE_BYTAG(Lumen);
	TRACE_CPUPROFILER_EVENT_SCOPE(FDeferredShadingSceneRenderer::UpdateLumenScene);

	FViewInfo& View = Views[0];
	const FPerViewPipelineState& ViewPipelineState = GetViewPipelineState(View);
	const bool bAnyLumenActive = ViewPipelineState.DiffuseIndirectMethod == EDiffuseIndirectMethod::Lumen || ViewPipelineState.ReflectionsMethod == EReflectionsMethod::Lumen;

	if (bAnyLumenActive
		// Don't update scene lighting for secondary views
		&& !View.bIsPlanarReflection 
		&& !View.bIsSceneCapture
		&& !View.bIsReflectionCapture
		&& View.ViewState)
	{
		const double StartTime = FPlatformTime::Seconds();

		FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;
		TArray<FCardPageRenderData, SceneRenderingAllocator>& CardPagesToRender = LumenCardRenderer.CardPagesToRender;

		QUICK_SCOPE_CYCLE_COUNTER(UpdateLumenScene);
		SCOPED_GPU_STAT(GraphBuilder.RHICmdList, UpdateLumenSceneBuffers);
		RDG_GPU_STAT_SCOPE(GraphBuilder, UpdateLumenScene);
		RDG_EVENT_SCOPE(GraphBuilder, "UpdateLumenScene %u card captures %.3fM texels", CardPagesToRender.Num(), LumenCardRenderer.NumCardTexelsToCapture / (1024.0f * 1024.0f));

		Lumen::UpdateCardSceneBuffer(GraphBuilder.RHICmdList, ViewFamily, Scene);

		// Recreate the view uniform buffer now that we have updated Lumen's primitive mapping buffers
		Lumen::SetupViewUniformBufferParameters(Scene, *View.CachedViewUniformShaderParameters);
		View.ViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*View.CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);

		// Init transient render targets for capturing cards
		FCardCaptureAtlas CardCaptureAtlas;
		AllocatedCardCaptureAtlas(GraphBuilder, LumenSceneData.GetCardCaptureAtlasSize(), CardCaptureAtlas);

		if (CardPagesToRender.Num() > 0)
		{
			FRHIBuffer* PrimitiveIdVertexBuffer = nullptr;
			FInstanceCullingResult InstanceCullingResult;
#if GPUCULL_TODO
			if (Scene->GPUScene.IsEnabled())
			{
				int32 MaxInstances = 0;
				int32 VisibleMeshDrawCommandsNum = 0;
				int32 NewPassVisibleMeshDrawCommandsNum = 0;

				FInstanceCullingContext InstanceCullingContext(nullptr, TArrayView<const int32>(&View.GPUSceneViewId, 1));

				SetupGPUInstancedDraws(InstanceCullingContext, LumenCardRenderer.MeshDrawCommands, false, MaxInstances, VisibleMeshDrawCommandsNum, NewPassVisibleMeshDrawCommandsNum);
				// Not supposed to do any compaction here.
				ensure(VisibleMeshDrawCommandsNum == LumenCardRenderer.MeshDrawCommands.Num());

				InstanceCullingContext.BuildRenderingCommands(GraphBuilder, Scene->GPUScene, View.DynamicPrimitiveCollector.GetPrimitiveIdRange(), InstanceCullingResult);
			}
			else
#endif // GPUCULL_TODO
			{
				// Prepare primitive Id VB for rendering mesh draw commands.
				if (LumenCardRenderer.MeshDrawPrimitiveIds.Num() > 0)
				{
					const uint32 PrimitiveIdBufferDataSize = LumenCardRenderer.MeshDrawPrimitiveIds.Num() * sizeof(int32);

					FPrimitiveIdVertexBufferPoolEntry Entry = GPrimitiveIdVertexBufferPool.Allocate(PrimitiveIdBufferDataSize);
					PrimitiveIdVertexBuffer = Entry.BufferRHI;

					void* RESTRICT Data = RHILockBuffer(PrimitiveIdVertexBuffer, 0, PrimitiveIdBufferDataSize, RLM_WriteOnly);
					FMemory::Memcpy(Data, LumenCardRenderer.MeshDrawPrimitiveIds.GetData(), PrimitiveIdBufferDataSize);
					RHIUnlockBuffer(PrimitiveIdVertexBuffer);

					GPrimitiveIdVertexBufferPool.ReturnToFreeList(Entry);
				}
			}

			FRDGBufferRef CardCaptureRectBuffer = nullptr;
			FRDGBufferSRVRef CardCaptureRectBufferSRV = nullptr;

			{
				CardCaptureRectBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateUploadDesc(sizeof(FUintVector4), FMath::RoundUpToPowerOfTwo(CardPagesToRender.Num())), TEXT("Lumen.CardCaptureRects"));
				CardCaptureRectBufferSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CardCaptureRectBuffer, PF_R32G32B32A32_UINT));

				TArray<FUintVector4, SceneRenderingAllocator> CardCaptureRectArray;
				CardCaptureRectArray.Reserve(CardPagesToRender.Num());

				for (const FCardPageRenderData& CardPageRenderData : CardPagesToRender)
				{
					FUintVector4 Rect;
					Rect.X = FMath::Max(CardPageRenderData.CardCaptureAtlasRect.Min.X, 0);
					Rect.Y = FMath::Max(CardPageRenderData.CardCaptureAtlasRect.Min.Y, 0);
					Rect.Z = FMath::Max(CardPageRenderData.CardCaptureAtlasRect.Max.X, 0);
					Rect.W = FMath::Max(CardPageRenderData.CardCaptureAtlasRect.Max.Y, 0);
					CardCaptureRectArray.Add(Rect);
				}

				FPixelShaderUtils::UploadRectBuffer(GraphBuilder, CardCaptureRectArray, CardCaptureRectBuffer);

				ClearLumenCards(GraphBuilder, View, CardCaptureAtlas, CardCaptureRectBufferSRV, CardPagesToRender.Num());
			}

			FViewInfo* SharedView = View.CreateSnapshot();
			{
				SharedView->DynamicPrimitiveCollector = FGPUScenePrimitiveCollector(&GetGPUSceneDynamicContext());
				SharedView->StereoPass = eSSP_FULL;
				SharedView->DrawDynamicFlags = EDrawDynamicFlags::ForceLowestLOD;

				// Don't do material texture mip biasing in proxy card rendering
				SharedView->MaterialTextureMipBias = 0;

				TRefCountPtr<IPooledRenderTarget> NullRef;
				FPlatformMemory::Memcpy(&SharedView->PrevViewInfo.HZB, &NullRef, sizeof(SharedView->PrevViewInfo.HZB));

				SharedView->CachedViewUniformShaderParameters = MakeUnique<FViewUniformShaderParameters>();
				SharedView->CachedViewUniformShaderParameters->PrimitiveSceneData = Scene->GPUScene.PrimitiveBuffer.SRV;
				SharedView->CachedViewUniformShaderParameters->InstanceSceneData = Scene->GPUScene.InstanceDataBuffer.SRV;
				SharedView->CachedViewUniformShaderParameters->LightmapSceneData = Scene->GPUScene.LightmapDataBuffer.SRV;
				SharedView->ViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*SharedView->CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);
			}

			FLumenCardPassUniformParameters* PassUniformParameters = GraphBuilder.AllocParameters<FLumenCardPassUniformParameters>();
			SetupSceneTextureUniformParameters(GraphBuilder, Scene->GetFeatureLevel(), /*SceneTextureSetupMode*/ ESceneTextureSetupMode::None, PassUniformParameters->SceneTextures);

			{
				FLumenCardPassParameters* PassParameters = GraphBuilder.AllocParameters<FLumenCardPassParameters>();
				PassParameters->View = Scene->UniformBuffers.LumenCardCaptureViewUniformBuffer;
				PassParameters->CardPass = GraphBuilder.CreateUniformBuffer(PassUniformParameters);
				PassParameters->RenderTargets[0] = FRenderTargetBinding(CardCaptureAtlas.Albedo, ERenderTargetLoadAction::ELoad);
				PassParameters->RenderTargets[1] = FRenderTargetBinding(CardCaptureAtlas.Normal, ERenderTargetLoadAction::ELoad);
				PassParameters->RenderTargets[2] = FRenderTargetBinding(CardCaptureAtlas.Emissive, ERenderTargetLoadAction::ELoad);
				PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(CardCaptureAtlas.DepthStencil, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilNop);

				InstanceCullingResult.GetDrawParameters(PassParameters->InstanceCullingDrawParams);

				uint32 NumPages = 0;
				uint32 NumDraws = 0;
				uint32 NumInstances = 0;
				uint32 NumTris = 0;

				// Compute some stats about non Nanite meshes which are captured
				#if RDG_EVENTS != RDG_EVENTS_NONE
				{
					for (FCardPageRenderData& CardPageRenderData : CardPagesToRender)
					{
						if (CardPageRenderData.NumMeshDrawCommands > 0)
						{
							NumPages += 1;
							NumDraws += CardPageRenderData.NumMeshDrawCommands;

							for (int32 DrawCommandIndex = CardPageRenderData.StartMeshDrawCommandIndex; DrawCommandIndex < CardPageRenderData.StartMeshDrawCommandIndex + CardPageRenderData.NumMeshDrawCommands; ++DrawCommandIndex)
							{
								const FVisibleMeshDrawCommand& VisibleDrawCommand = LumenCardRenderer.MeshDrawCommands[DrawCommandIndex];
								const FMeshDrawCommand* MeshDrawCommand = VisibleDrawCommand.MeshDrawCommand;

								uint32 NumInstancesPerDraw = 0;

								// Count number of instances to draw
								if (VisibleDrawCommand.NumRuns)
								{
									for (int32 InstanceRunIndex = 0; InstanceRunIndex < VisibleDrawCommand.NumRuns; ++InstanceRunIndex)
									{
										const int32 FirstInstance = VisibleDrawCommand.RunArray[InstanceRunIndex * 2 + 0];
										const int32 LastInstance = VisibleDrawCommand.RunArray[InstanceRunIndex * 2 + 1];
										NumInstancesPerDraw += LastInstance - FirstInstance + 1;
									}
								}
								else
								{
									NumInstancesPerDraw += MeshDrawCommand->NumInstances;
								}

								NumInstances += NumInstancesPerDraw;
								NumTris += MeshDrawCommand->NumPrimitives * NumInstancesPerDraw;
							}
						}
					}
				}
				#endif

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("MeshCardCapture Pages:%u Draws:%u Instances:%u Tris:%u", NumPages, NumDraws, NumInstances, NumTris),
					PassParameters,
					ERDGPassFlags::Raster,
					[this, Scene = Scene, PrimitiveIdVertexBuffer, SharedView, &CardPagesToRender, PassParameters](FRHICommandList& RHICmdList)
					{
						QUICK_SCOPE_CYCLE_COUNTER(MeshPass);

						for (FCardPageRenderData& CardPageRenderData : CardPagesToRender)
						{
							if (CardPageRenderData.NumMeshDrawCommands > 0)
							{
								const FIntRect ViewRect = CardPageRenderData.CardCaptureAtlasRect;
								RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

								CardPageRenderData.PatchView(RHICmdList, Scene, SharedView);
								Scene->UniformBuffers.LumenCardCaptureViewUniformBuffer.UpdateUniformBufferImmediate(*SharedView->CachedViewUniformShaderParameters);

								FGraphicsMinimalPipelineStateSet GraphicsMinimalPipelineStateSet;
#if GPUCULL_TODO
								if (Scene->GPUScene.IsEnabled())
								{
									FRHIBuffer* DrawIndirectArgsBuffer = nullptr;
									FRHIBuffer* InstanceIdOffsetBuffer = nullptr;
									FInstanceCullingDrawParams& InstanceCullingDrawParams = PassParameters->InstanceCullingDrawParams;
									if (InstanceCullingDrawParams.DrawIndirectArgsBuffer != nullptr && InstanceCullingDrawParams.InstanceIdOffsetBuffer != nullptr)
									{
										DrawIndirectArgsBuffer = InstanceCullingDrawParams.DrawIndirectArgsBuffer->GetRHI();
										InstanceIdOffsetBuffer = InstanceCullingDrawParams.InstanceIdOffsetBuffer->GetRHI();
									}

									SubmitGPUInstancedMeshDrawCommandsRange(
										LumenCardRenderer.MeshDrawCommands,
										GraphicsMinimalPipelineStateSet,
										CardPageRenderData.StartMeshDrawCommandIndex,
										CardPageRenderData.NumMeshDrawCommands,
										1,
										InstanceIdOffsetBuffer,
										DrawIndirectArgsBuffer,
										RHICmdList);
								}
								else
#endif // GPUCULL_TODO
								{
									SubmitMeshDrawCommandsRange(
										LumenCardRenderer.MeshDrawCommands,
										GraphicsMinimalPipelineStateSet,
										PrimitiveIdVertexBuffer,
										0,
										false,
										CardPageRenderData.StartMeshDrawCommandIndex,
										CardPageRenderData.NumMeshDrawCommands,
										1,
										RHICmdList);
								}
							}
						}
					}
				);
			}

			bool bAnyNaniteMeshes = false;

			for (FCardPageRenderData& CardPageRenderData : CardPagesToRender)
			{
				bAnyNaniteMeshes = bAnyNaniteMeshes || CardPageRenderData.NaniteInstanceIds.Num() > 0 || CardPageRenderData.bDistantScene;
			}

			if (UseNanite(ShaderPlatform) && ViewFamily.EngineShowFlags.NaniteMeshes && bAnyNaniteMeshes)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(NaniteMeshPass);
				QUICK_SCOPE_CYCLE_COUNTER(NaniteMeshPass);

				const FIntPoint DepthStencilAtlasSize = CardCaptureAtlas.Size;
				const FIntRect DepthAtlasRect = FIntRect(0, 0, DepthStencilAtlasSize.X, DepthStencilAtlasSize.Y);

				Nanite::FRasterContext RasterContext = Nanite::InitRasterContext(
					GraphBuilder,
					FeatureLevel,
					DepthStencilAtlasSize,
					Nanite::EOutputBufferMode::VisBuffer,
					true,
					CardCaptureRectBufferSRV,
					CardPagesToRender.Num());

				const bool bUpdateStreaming = false;
				const bool bSupportsMultiplePasses = true;
				const bool bForceHWRaster = RasterContext.RasterScheduling == Nanite::ERasterScheduling::HardwareOnly;
				const bool bPrimaryContext = false;

				Nanite::FCullingContext CullingContext = Nanite::InitCullingContext(
					GraphBuilder,
					*Scene,
					nullptr,
					FIntRect(),
					false,
					bUpdateStreaming,
					bSupportsMultiplePasses,
					bForceHWRaster,
					bPrimaryContext);

				if (GLumenSceneNaniteMultiViewCapture)
				{
					// Multi-view rendering path
					const uint32 NumCardPagesToRender = CardPagesToRender.Num();

					uint32 NextCardIndex = 0;
					while(NextCardIndex < NumCardPagesToRender)
					{
						TArray<Nanite::FPackedView, SceneRenderingAllocator> NaniteViews;
						TArray<Nanite::FInstanceDraw, SceneRenderingAllocator> NaniteInstanceDraws;

						while(NextCardIndex < NumCardPagesToRender && NaniteViews.Num() < MAX_VIEWS_PER_CULL_RASTERIZE_PASS)
						{
							const FCardPageRenderData& CardPageRenderData = CardPagesToRender[NextCardIndex];

							if(CardPageRenderData.NaniteInstanceIds.Num() > 0)
							{
								for(uint32 InstanceID : CardPageRenderData.NaniteInstanceIds)
								{
									NaniteInstanceDraws.Add(Nanite::FInstanceDraw { InstanceID, (uint32)NaniteViews.Num() });
								}

								Nanite::FPackedViewParams Params;
								Params.ViewMatrices = CardPageRenderData.ViewMatrices;
								Params.PrevViewMatrices = CardPageRenderData.ViewMatrices;
								Params.ViewRect = CardPageRenderData.CardCaptureAtlasRect;
								Params.RasterContextSize = DepthStencilAtlasSize;
								Params.LODScaleFactor = CardPageRenderData.NaniteLODScaleFactor;
								NaniteViews.Add(Nanite::CreatePackedView(Params));
							}

							NextCardIndex++;
						}

						if (NaniteInstanceDraws.Num() > 0)
						{
							RDG_EVENT_SCOPE(GraphBuilder, "Nanite::RasterizeLumenCards");

							Nanite::FRasterState RasterState;
							Nanite::CullRasterize(
								GraphBuilder,
								*Scene,
								NaniteViews,
								CullingContext,
								RasterContext,
								RasterState,
								&NaniteInstanceDraws
							);
						}
					}
				}
				else
				{
					RDG_EVENT_SCOPE(GraphBuilder, "RenderLumenCardsWithNanite");

					// One draw call per view
					for(FCardPageRenderData& CardPageRenderData : CardPagesToRender)
					{
						if(CardPageRenderData.NaniteInstanceIds.Num() > 0)
						{						
							TArray<Nanite::FInstanceDraw, SceneRenderingAllocator> NaniteInstanceDraws;
							for( uint32 InstanceID : CardPageRenderData.NaniteInstanceIds )
							{
								NaniteInstanceDraws.Add( Nanite::FInstanceDraw { InstanceID, 0u } );
							}
						
							CardPageRenderData.PatchView(GraphBuilder.RHICmdList, Scene, SharedView);
							Nanite::FPackedView PackedView = Nanite::CreatePackedViewFromViewInfo(*SharedView, DepthStencilAtlasSize, 0);

							Nanite::CullRasterize(
								GraphBuilder,
								*Scene,
								{ PackedView },
								CullingContext,
								RasterContext,
								Nanite::FRasterState(),
								&NaniteInstanceDraws
							);
						}
					}
				}

				extern float GLumenDistantSceneMinInstanceBoundsRadius;

				// Render entire scene for distant cards
				for (FCardPageRenderData& CardPageRenderData : CardPagesToRender)
				{
					if (CardPageRenderData.bDistantScene)
					{
						Nanite::FRasterState RasterState;
						RasterState.bNearClip = false;

						CardPageRenderData.PatchView(GraphBuilder.RHICmdList, Scene, SharedView);
						Nanite::FPackedView PackedView = Nanite::CreatePackedViewFromViewInfo(
							*SharedView,
							DepthStencilAtlasSize,
							/*Flags*/ 0,
							/*StreamingPriorityCategory*/ 0,
							GLumenDistantSceneMinInstanceBoundsRadius,
							Lumen::GetDistanceSceneNaniteLODScaleFactor());

						Nanite::CullRasterize(
							GraphBuilder,
							*Scene,
							{ PackedView },
							CullingContext,
							RasterContext,
							RasterState);
					}
				}

				Nanite::DrawLumenMeshCapturePass(
					GraphBuilder,
					*Scene,
					SharedView,
					CardPagesToRender,
					CullingContext,
					RasterContext,
					PassUniformParameters,
					CardCaptureRectBufferSRV,
					CardPagesToRender.Num(),
					CardCaptureAtlas.Size,
					CardCaptureAtlas.Albedo,
					CardCaptureAtlas.Normal,
					CardCaptureAtlas.Emissive,
					CardCaptureAtlas.DepthStencil
				);
			}

			UploadCardPagesToRenderIndexBuffers(GraphBuilder, CardPagesToRender, LumenCardRenderer);

			UpdateLumenSurfaceCacheAtlas(GraphBuilder, View, CardPagesToRender, CardCaptureRectBufferSRV, CardCaptureAtlas);
		}
		else
		{
			// Create empty buffers if nothing gets rendered this frame
			UploadCardPagesToRenderIndexBuffers(GraphBuilder, CardPagesToRender, LumenCardRenderer);
		}

		const float TimeElapsed = FPlatformTime::Seconds() - StartTime;

		if (TimeElapsed > .02f)
		{
			UE_LOG(LogRenderer, Log, TEXT("UpdateLumenScene %u Card Renders %.1fms"), CardPagesToRender.Num(), TimeElapsed * 1000.0f);
		}
	}

	// Reset arrays, but keep allocated memory for 1024 elements
	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;
	LumenSceneData.CardIndicesToUpdateInBuffer.Empty(1024);
	LumenSceneData.MeshCardsIndicesToUpdateInBuffer.Empty(1024);
	LumenSceneData.DFObjectIndicesToUpdateInBuffer.Empty(1024);
}
