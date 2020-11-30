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
#include "LumenCubeMapTree.h"
#include "LumenSceneUtils.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "HAL/LowLevelMemStats.h"

int32 GAllowLumenScene = 0;
FAutoConsoleVariableRef CVarLumenGILumenScene(
	TEXT("r.LumenScene"),
	GAllowLumenScene,
	TEXT("Whether to allow setup of the proxy scene used for Lumen GI."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GLumenSceneCardCapturesPerFrame = 300;
FAutoConsoleVariableRef CVarLumenSceneCardCapturesPerFrame(
	TEXT("r.LumenScene.CardCapturesPerFrame"),
	GLumenSceneCardCapturesPerFrame,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenSceneCardCaptureMargin = 2000.0f;
FAutoConsoleVariableRef CVarLumenSceneCardCaptureMargin(
	TEXT("r.LumenScene.CardCaptureMargin"),
	GLumenSceneCardCaptureMargin,
	TEXT("How far from Lumen scene range start to capture cards."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneCardResToCapturePerFrame = 1024;
FAutoConsoleVariableRef CVarLumenSceneCardResToCapturePerFrame(
	TEXT("r.LumenScene.CardResToCapturePerFrame"),
	GLumenSceneCardResToCapturePerFrame,
	TEXT("1024 means max 1024x1024 area to capture per frame"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenSceneCardAtlasSize = 4096;
FAutoConsoleVariableRef CVarLumenSceneCardAtlasSize(
	TEXT("r.LumenScene.CardAtlasSize"),
	GLumenSceneCardAtlasSize,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GLumenSceneCardAtlasAllocatorBinSize = 128 * 128;
FAutoConsoleVariableRef CVarLumenSceneCardAtlasAllocatorBinSize(
	TEXT("r.LumenScene.CardAtlasAllocatorBinSize"),
	GLumenSceneCardAtlasAllocatorBinSize,
	TEXT(""),
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
FAutoConsoleVariableRef CVarLumenSceneResetCards(
	TEXT("r.LumenScene.Reset"),
	GLumenSceneReset,
	TEXT("Reset all atlases and captured cards. 2 - for continuos reset."),
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

int32 GLumenSceneUploadCardBufferEveryFrame = 0;
FAutoConsoleVariableRef CVarLumenSceneUploadCardBufferEveryFrame(
	TEXT("r.LumenScene.UploadCardBufferEveryFrame"),
	GLumenSceneUploadCardBufferEveryFrame,
	TEXT(""),
	ECVF_RenderThreadSafe
	);

int32 GLumenSceneUploadCubeMapTreeBufferEveryFrame = 0;
FAutoConsoleVariableRef CVarLumenSceneUploadCubeMapTreeBufferEveryFrame(
	TEXT("r.LumenScene.UploadCubeMapTreeBufferEveryFrame"),
	GLumenSceneUploadCubeMapTreeBufferEveryFrame,
	TEXT(""),
	ECVF_RenderThreadSafe
);

int32 GLumenSceneUploadDFObjectToCubeMapTreeIndexBufferEveryFrame = 0;
FAutoConsoleVariableRef CVarLumenSceneUploadDFObjectToCubeMapTreeIndexBufferEveryFrame(
	TEXT("r.LumenScene.UploadDFObjectToCubeMapTreeIndexBufferEveryFrame"),
	GLumenSceneUploadDFObjectToCubeMapTreeIndexBufferEveryFrame,
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

int32 GLumenCoarseCardCulling = 1;
FAutoConsoleVariableRef CVarLumenCoarseCardCulling(
	TEXT("r.LumenScene.CoarseCardCulling"),
	GLumenCoarseCardCulling,
	TEXT("Allow coarse card culling based on estimated projected size and distance to the entire CubeMapTree (enabled by default)."),
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

#if ENABLE_LOW_LEVEL_MEM_TRACKER
DECLARE_LLM_MEMORY_STAT(TEXT("Lumen"), STAT_LumenLLM, STATGROUP_LLMFULL);
DECLARE_LLM_MEMORY_STAT(TEXT("Lumen"), STAT_LumenSummaryLLM, STATGROUP_LLM);
LLM_DEFINE_TAG(Lumen, NAME_None, NAME_None, GET_STATFNAME(STAT_LumenLLM), GET_STATFNAME(STAT_LumenSummaryLLM));
#endif // ENABLE_LOW_LEVEL_MEM_TRACKER

namespace Lumen
{
	bool AnyLumenHardwareRayTracingPassEnabled()
	{
		bool bLumenHardwareRayTracing = GAllowLumenScene != 0;

#if RHI_RAYTRACING
		static auto CVarLumenDirectLightingHardwareRayTracing = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.DirectLighting.HardwareRayTracing"));
		static auto CVarLumenScreenProbeGatherHardwareRayTracing = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Lumen.ScreenProbeGather.HardwareRayTracing"));

		bLumenHardwareRayTracing |= (CVarLumenDirectLightingHardwareRayTracing ? (CVarLumenDirectLightingHardwareRayTracing->GetInt() != 0) : false)
			|| (CVarLumenScreenProbeGatherHardwareRayTracing ? (CVarLumenScreenProbeGatherHardwareRayTracing->GetInt() != 0) : false);
#endif
		return bLumenHardwareRayTracing;
	}
}

bool Lumen::ShouldPrepareGlobalDistanceField(EShaderPlatform ShaderPlatform)
{
	return GAllowLumenScene && DoesPlatformSupportLumenGI(ShaderPlatform);
}

bool Lumen::ShouldRenderLumenForViewFamily(const FScene* Scene, const FSceneViewFamily& ViewFamily)
{
	return Scene 
		&& Scene->LumenSceneData
		&& ViewFamily.Views.Num() == 1
		&& GAllowLumenScene
		&& DoesPlatformSupportLumenGI(Scene->GetShaderPlatform())
		&& (Scene->LumenSceneData->VisibleCardsIndices.Num() > 0 || ShouldRenderDynamicSkyLight(Scene, ViewFamily))
		&& Scene->LumenSceneData->AlbedoAtlas;
}

bool Lumen::ShouldRenderLumenForViewWithoutMeshSDFs(const FScene* Scene, const FViewInfo& View)
{
	return View.Family
		&& ShouldRenderLumenForViewFamily(Scene, *View.Family)
		// Don't update scene lighting for secondary views
		&& !View.bIsPlanarReflection
		&& !View.bIsSceneCapture
		&& !View.bIsReflectionCapture
		&& View.ViewState;
}

bool Lumen::ShouldRenderLumenForView(const FScene* Scene, const FViewInfo& View)
{
	static const auto CMeshSDFVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.GenerateMeshDistanceFields"));

	return ShouldRenderLumenForViewWithoutMeshSDFs(Scene, View) && CMeshSDFVar->GetValueOnRenderThread() != 0;
}

bool Lumen::ShouldRenderLumenCardsForView(const FScene* Scene, const FViewInfo& View)
{
	return ShouldRenderLumenForView(Scene, View)
		&& Scene
		&& Scene->LumenSceneData
		&& Scene->LumenSceneData->VisibleCardsIndices.Num() > 0;
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

int32 GetLumenSceneCardResToCapturePerFrame()
{
	return FMath::TruncToInt(GLumenSceneCardResToCapturePerFrame * (GLumenFastCameraMode ? 1.5f : 1.0f));
}

DECLARE_GPU_STAT(UpdateLumenScene);

int32 GLumenSceneGeneration = 0;

IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FLumenCardPassUniformParameters, "LumenCardPass", SceneTextures);

class FLumenCardVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FLumenCardVS,MeshMaterial);

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
	DECLARE_SHADER_TYPE(FLumenCardPS,MeshMaterial);

public:
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		//@todo DynamicGI - filter
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	FLumenCardPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		:	FMeshMaterialShader(Initializer)
	{}

	FLumenCardPS() = default;
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FLumenCardPS,TEXT("/Engine/Private/Lumen/LumenCardPixelShader.usf"),TEXT("Main"),SF_Pixel);

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
			&& ShadingModels.IsLit()
			&& (PrimitiveSceneProxy && PrimitiveSceneProxy->ShouldRenderInMainPass() && PrimitiveSceneProxy->AffectsDynamicIndirectLighting())
			&& ShouldIncludeDomainInMeshPass(Material.GetMaterialDomain()))
		{
			const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;
			FVertexFactoryType* VertexFactoryType = VertexFactory->GetType();

			TMeshProcessorShaders<
				FLumenCardVS,
				FMeshMaterialShader,
				FMeshMaterialShader,
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
	PassState.SetViewUniformBuffer(Scene->UniformBuffers.LumenCardCaptureViewUniformBuffer);

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

using FLumenCardNanitePassShaders = TMeshProcessorShaders<FNaniteVS, FMeshMaterialShader, FMeshMaterialShader, FLumenCardPS>;

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
		const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
		const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);

		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;

		const EBlendMode BlendMode = Material.GetBlendMode();

		check(BlendMode == BLEND_Opaque);
		check(Material.GetMaterialDomain() == MD_Surface);

		TShaderMapRef<FNaniteVS> VertexShader(GetGlobalShaderMap(FeatureLevel));

		FLumenCardNanitePassShaders PassShaders;
		PassShaders.VertexShader = VertexShader;

		const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;
		FVertexFactoryType* VertexFactoryType = VertexFactory->GetType();
		PassShaders.PixelShader = Material.GetShader<FLumenCardPS>(VertexFactoryType);

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
	}
}

FMeshPassProcessor* CreateLumenCardNaniteMeshProcessor(
	const FScene* Scene,
	const FSceneView* InViewIfDynamicMeshCommand,
	FMeshPassDrawListContext* InDrawListContext)
{
	LLM_SCOPE_BYTAG(Lumen);

	FMeshPassProcessorRenderState PassState;
	PassState.SetViewUniformBuffer(Scene->UniformBuffers.LumenCardCaptureViewUniformBuffer);
	PassState.SetNaniteUniformBuffer(Scene->UniformBuffers.NaniteUniformBuffer);

	PassState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Equal, true, CF_Equal>::GetRHI());
	PassState.SetDepthStencilAccess(FExclusiveDepthStencil::DepthRead_StencilRead);
	PassState.SetStencilRef(STENCIL_SANDBOX_MASK);
	PassState.SetBlendState(TStaticBlendState<>::GetRHI());

	return new(FMemStack::Get()) FLumenCardNaniteMeshProcessor(Scene, InViewIfDynamicMeshCommand, PassState,InDrawListContext);
}

FCardSourceData::FCardSourceData()
{
	bVisible = false;
	bMovable = false;
	bAllocated = false;
	WorldBounds.Init();
	Origin = FVector::ZeroVector;
	LocalExtent = FVector::ZeroVector;
	LocalToWorldRotationX = FVector::ZeroVector;
	LocalToWorldRotationY = FVector::ZeroVector;
	LocalToWorldRotationZ = FVector::ZeroVector;
	FaceIndexInCubeMapTree = -1;
	IndexInVisibleCardIndexBuffer = -1;
	PrimitiveSceneInfo = nullptr;
	AtlasAllocation = FIntRect(0, 0, 0, 0);
}

FCardSourceData::~FCardSourceData()
{ 
	check(IndexInVisibleCardIndexBuffer == -1);
	check(!bAllocated); 
}

const static FVector LumenCubeMapFaceRotationFrame[6][3] =
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

void FCardSourceData::Initialize(FPrimitiveSceneInfo* InPrimitiveSceneInfo, int32 InInstanceIndexOrMergedFlag, float InResolutionScale, const FMatrix& LocalToWorld, const class FLumenCubeMapFaceBuildData& FaceBuiltData, int32 InFaceIndexInCubeMapTree)
{
	FaceIndexInCubeMapTree = InFaceIndexInCubeMapTree;
	PrimitiveSceneInfo = InPrimitiveSceneInfo;
	InstanceIndexOrMergedFlag = InInstanceIndexOrMergedFlag;
	ResolutionScale = InResolutionScale;
	bMovable = PrimitiveSceneInfo->Proxy->IsMovable();

	SetTransform(LocalToWorld, FaceBuiltData);
}

void FCardSourceData::SetTransform(const FMatrix& LocalToWorld, const class FLumenCubeMapFaceBuildData& FaceBuiltData)
{
	checkSlow(FaceBuiltData.Orientation < 6);
	const FVector& CardToLocalRotationX = LumenCubeMapFaceRotationFrame[FaceBuiltData.Orientation][0];
	const FVector& CardToLocalRotationY = LumenCubeMapFaceRotationFrame[FaceBuiltData.Orientation][1];
	const FVector& CardToLocalRotationZ = LumenCubeMapFaceRotationFrame[FaceBuiltData.Orientation][2];

	SetTransform(LocalToWorld, FaceBuiltData.Center, CardToLocalRotationX, CardToLocalRotationY, CardToLocalRotationZ, FaceBuiltData.Extent);
}

void FCardSourceData::SetTransform(
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

void FCardSourceData::RemoveFromAtlas(FLumenSceneData& LumenSceneData)
{
	LLM_SCOPE_BYTAG(Lumen);

	if (bAllocated)
	{
		bAllocated = false;

		const int32 AllocationArea = AtlasAllocation.Area();
		if (AllocationArea > 0)
		{
			LumenSceneData.NumCardTexels -= AtlasAllocation.Area();
			ensure(LumenSceneData.NumCardTexels >= 0);
			LumenSceneData.AtlasAllocator.RemoveElement(AtlasAllocation);
		}
		AtlasAllocation = FIntRect(0, 0, 0, 0);
	}
}

FLumenSceneData::FLumenSceneData(EShaderPlatform ShaderPlatform, EWorldType::Type WorldType) :
	Generation(0),
	bFinalLightingAtlasContentsValid(false),
	MaxAtlasSize(0, 0),
	AtlasAllocator(FIntPoint(1, 1), 1)
{
	LLM_SCOPE_BYTAG(Lumen);

	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MeshCardRepresentation"));

	bTrackAllPrimitives = (DoesPlatformSupportLumenGI(ShaderPlatform)) && CVar->GetValueOnGameThread() != 0 && WorldType != EWorldType::EditorPreview;
}

FLumenSceneData::~FLumenSceneData()
{
	LLM_SCOPE_BYTAG(Lumen);

	for (FLumenCubeMapTree& CubeMapTree : CubeMapTrees)
	{
		GLumenCubeMapTreeLUTAtlas.RemoveAllocation(CubeMapTree);
	}

	for (FCardSourceData& Card : Cards)
	{
		Card.IndexInVisibleCardIndexBuffer = -1;
		Card.RemoveFromAtlas(*this);
	}

	Cards.Reset();
	CubeMaps.Reset();
}

bool TrackPrimitiveForLumenScene(const FPrimitiveSceneProxy* Proxy)
{
	return Proxy->AffectsDynamicIndirectLighting()
		&& Proxy->SupportsMeshCardRepresentation()
		// For now Lumen depends on the distance field representation. 
		// This also makes sure that non opaque things won't get included in Lumen Scene
		&& Proxy->SupportsDistanceFieldRepresentation()
		&& (Proxy->IsDrawnInGame() || Proxy->CastsHiddenShadow());
}

void FLumenSceneData::AddPrimitive(FPrimitiveSceneInfo* InPrimitive)
{
	LLM_SCOPE_BYTAG(Lumen);

	const FPrimitiveSceneProxy* Proxy = InPrimitive->Proxy;

	if ((bTrackAllPrimitives)
		&& TrackPrimitiveForLumenScene(Proxy))
	{
		checkSlow(!PendingAddOperations.Contains(InPrimitive));
		checkSlow(!PendingUpdateOperations.Contains(InPrimitive));
		PendingAddOperations.Add(InPrimitive);
	}
}

void FLumenSceneData::UpdatePrimitive(FPrimitiveSceneInfo* InPrimitive)
{
	LLM_SCOPE_BYTAG(Lumen);

	const FPrimitiveSceneProxy* Proxy = InPrimitive->Proxy;

	if ((bTrackAllPrimitives) 
		&& TrackPrimitiveForLumenScene(Proxy)
		&& !PendingUpdateOperations.Contains(InPrimitive))
	{
		bool bPendingAdd = false;
		for (FLumenPrimitiveAddInfo& AddInfo : PendingAddOperations)
		{
			if (AddInfo.Primitive == InPrimitive)
			{
				AddInfo.bPendingUpdate = true;
				bPendingAdd = true;
				break;
			}
		}

		if (!bPendingAdd)
		{
			PendingUpdateOperations.Add(InPrimitive);
		}
	}
}

void FLumenSceneData::RemovePrimitive(FPrimitiveSceneInfo* InPrimitive)
{
	LLM_SCOPE_BYTAG(Lumen);

	const FPrimitiveSceneProxy* Proxy = InPrimitive->Proxy;

	if ((bTrackAllPrimitives)
		&& TrackPrimitiveForLumenScene(Proxy))
	{
		PendingAddOperations.Remove(InPrimitive);
		PendingUpdateOperations.Remove(InPrimitive);
		PendingRemoveOperations.Add(FLumenPrimitiveRemoveInfo(InPrimitive));

		InPrimitive->LumenCubeMapTreeInstanceIndices.Empty();
	}
}

void FLumenSceneData::AddCardToVisibleCardList(int32 CardIndex)
{
	if (Cards[CardIndex].IndexInVisibleCardIndexBuffer == -1)
	{
		Cards[CardIndex].IndexInVisibleCardIndexBuffer = VisibleCardsIndices.Num();
		VisibleCardsIndices.Add(CardIndex);
	}
}

void FLumenSceneData::RemoveCardFromVisibleCardList(int32 CardIndex)
{
	const int32 IndexInVisibleCardIndexBuffer = Cards[CardIndex].IndexInVisibleCardIndexBuffer;

	if (IndexInVisibleCardIndexBuffer >= 0)
	{
		// Fixup indices of the card that is being swapped
		Cards[VisibleCardsIndices.Last()].IndexInVisibleCardIndexBuffer = IndexInVisibleCardIndexBuffer;

		VisibleCardsIndices.RemoveAtSwap(IndexInVisibleCardIndexBuffer);

		Cards[CardIndex].IndexInVisibleCardIndexBuffer = -1;
	}
}

void FCardRenderData::UpdateViewMatrices(const FViewInfo& MainView)
{
	ensureMsgf(FVector::DotProduct(GetLocalToWorldRotationX(), FVector::CrossProduct(GetLocalToWorldRotationY(), GetLocalToWorldRotationZ())) < 0.0f, TEXT("Card has wrong handedness"));

	FMatrix ViewRotationMatrix = FMatrix::Identity;
	ViewRotationMatrix.SetColumn(0, GetLocalToWorldRotationX());
	ViewRotationMatrix.SetColumn(1, GetLocalToWorldRotationY());
	ViewRotationMatrix.SetColumn(2, -GetLocalToWorldRotationZ());

	FVector ViewLocation = CardData.Origin;
	
	FVector FaceLocalExtent = CardData.LocalExtent;
	// Pull the view location back so the entire preview box is in front of the near plane
	ViewLocation += FaceLocalExtent.Z * GetLocalToWorldRotationZ();

	const float OrthoWidth = FaceLocalExtent.X;
	const float OrthoHeight = FaceLocalExtent.Y;

	const float NearPlane = 0;
	const float FarPlane = FaceLocalExtent.Z * 2;

	const float ZScale = 1.0f / (FarPlane - NearPlane);
	const float ZOffset = -NearPlane;

	const FMatrix ProjectionMatrix = FReversedZOrthoMatrix(
		OrthoWidth,
		OrthoHeight,
		ZScale,
		ZOffset);

	ProjectionMatrixUnadjustedForRHI = ProjectionMatrix;

	FViewMatrices::FMinimalInitializer Initializer;
	Initializer.ViewRotationMatrix   = ViewRotationMatrix;
	Initializer.ViewOrigin           = ViewLocation;
	Initializer.ProjectionMatrix     = ProjectionMatrix;
	Initializer.ConstrainedViewRect  = MainView.SceneViewInitOptions.GetConstrainedViewRect();
	Initializer.StereoPass           = MainView.SceneViewInitOptions.StereoPass;
#if WITH_EDITOR
	Initializer.bUseFauxOrthoViewPos = MainView.SceneViewInitOptions.bUseFauxOrthoViewPos;
#endif

	ViewMatrices = FViewMatrices(Initializer);
}

FCardRenderData::FCardRenderData(FCardSourceData& InCardData, ERHIFeatureLevel::Type InFeatureLevel, int32 InCardIndex)
	: CardData(InCardData)
	, StartMeshDrawCommandIndex(0)
	, NumMeshDrawCommands(0)
	, CardIndex(InCardIndex)
	, NaniteLODScaleFactor(1.0f)
{
	if (InCardData.bDistantScene)
	{
		NaniteLODScaleFactor = Lumen::GetDistanceSceneNaniteLODScaleFactor();
	}
}

void FCardRenderData::PatchView(FRHICommandList& RHICmdList, const FScene* Scene, FViewInfo* View) const
{
	View->ProjectionMatrixUnadjustedForRHI = ProjectionMatrixUnadjustedForRHI;
	View->ViewMatrices = ViewMatrices;
	View->ViewRect = GetAtlasAllocation();

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get();

	FBox VolumeBounds[TVC_MAX];
	View->SetupUniformBufferParameters(
		SceneContext,
		VolumeBounds,
		TVC_MAX,
		*View->CachedViewUniformShaderParameters);

	View->CachedViewUniformShaderParameters->NearPlane = 0;
}

void UpdateDirtyCards(FScene* Scene, bool bReallocateAtlas, bool bLatchedRecaptureLumenSceneOnce)
{
	LLM_SCOPE_BYTAG(Lumen);
	TRACE_CPUPROFILER_EVENT_SCOPE(UpdateDirtyCards);
	QUICK_SCOPE_CYCLE_COUNTER(UpdateDirtyCards);

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	if (GLumenSceneRecaptureLumenSceneEveryFrame || bLatchedRecaptureLumenSceneOnce || bReallocateAtlas)
	{
		for (int32 CardIndex = 0; CardIndex < LumenSceneData.Cards.Num(); ++CardIndex)
		{
			if (LumenSceneData.Cards.IsAllocated(CardIndex))
			{
				LumenSceneData.CardIndicesToUpdateInBuffer.Add(CardIndex);
				LumenSceneData.Cards[CardIndex].RemoveFromAtlas(LumenSceneData);
			}
		}
	}
}

void ClearAtlas(FRDGBuilder& GraphBuilder, const TRefCountPtr<IPooledRenderTarget>& Atlas)
{
	LLM_SCOPE_BYTAG(Lumen);
	FRDGTextureRef AtlasTexture = GraphBuilder.RegisterExternalTexture(Atlas);
	AddClearRenderTargetPass(GraphBuilder, AtlasTexture);
}

void ClearAtlasesToDebugValues(FRDGBuilder& GraphBuilder, FLumenSceneData& LumenSceneData)
{
	LLM_SCOPE_BYTAG(Lumen);

	// Clear to debug values to make out of bounds reads obvious
	ClearAtlas(GraphBuilder, LumenSceneData.DepthAtlas);
	ClearAtlas(GraphBuilder, LumenSceneData.FinalLightingAtlas);
	if (Lumen::UseIrradianceAtlas())
	{
		ClearAtlas(GraphBuilder, LumenSceneData.IrradianceAtlas);
	}
	if (Lumen::UseIndirectIrradianceAtlas())
	{
		ClearAtlas(GraphBuilder, LumenSceneData.IndirectIrradianceAtlas);
	}
	ClearAtlas(GraphBuilder, LumenSceneData.RadiosityAtlas);
	ClearAtlas(GraphBuilder, LumenSceneData.OpacityAtlas);
}

FIntPoint GetDesiredAtlasSize()
{
	const int32 Pow2 = FMath::RoundUpToPowerOfTwo(GLumenSceneCardAtlasSize);
	return FIntPoint(Pow2, Pow2);
}

void AllocateCardAtlases(FRDGBuilder& GraphBuilder, FLumenSceneData& LumenSceneData)
{
	LLM_SCOPE_BYTAG(Lumen);

	const int32 NumMips = FMath::CeilLogTwo(FMath::Max(LumenSceneData.MaxAtlasSize.X, LumenSceneData.MaxAtlasSize.Y)) + 1;

	FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(LumenSceneData.MaxAtlasSize, PF_R8G8B8A8, FClearValueBinding::Green, TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_NoFastClear, false));
	Desc.AutoWritable = false;
	GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, Desc, LumenSceneData.AlbedoAtlas, TEXT("LumenSceneAlbedo"), ERenderTargetTransience::NonTransient);
	GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, Desc, LumenSceneData.NormalAtlas, TEXT("LumenSceneNormal"), ERenderTargetTransience::NonTransient);
	
	FPooledRenderTargetDesc EmissiveDesc(FPooledRenderTargetDesc::Create2DDesc(LumenSceneData.MaxAtlasSize, PF_FloatR11G11B10, FClearValueBinding::Green, TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_NoFastClear, false));
	GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, EmissiveDesc, LumenSceneData.EmissiveAtlas, TEXT("LumenSceneEmissive"), ERenderTargetTransience::NonTransient);

	FPooledRenderTargetDesc DepthBufferDesc(FPooledRenderTargetDesc::Create2DDesc(LumenSceneData.MaxAtlasSize, PF_DepthStencil, FClearValueBinding::DepthZero, TexCreate_None, TexCreate_ShaderResource | TexCreate_DepthStencilTargetable | TexCreate_NoFastClear, false));
	DepthBufferDesc.AutoWritable = false;
 	GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, DepthBufferDesc, LumenSceneData.DepthBufferAtlas, TEXT("LumenSceneDepthBuffer"), ERenderTargetTransience::NonTransient);

	FPooledRenderTargetDesc DepthDesc(FPooledRenderTargetDesc::Create2DDesc(LumenSceneData.MaxAtlasSize, PF_G16, FClearValueBinding::Black, TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_NoFastClear, false, NumMips));
	DepthDesc.AutoWritable = false;
	GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, DepthDesc, LumenSceneData.DepthAtlas, TEXT("LumenSceneDepth"), ERenderTargetTransience::NonTransient);

	FClearValueBinding CrazyGreen(FLinearColor(0.0f, 10000.0f, 0.0f, 1.0f));
	FPooledRenderTargetDesc LightingDesc(FPooledRenderTargetDesc::Create2DDesc(LumenSceneData.MaxAtlasSize, PF_FloatR11G11B10, CrazyGreen, TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_NoFastClear, false, NumMips));
	LightingDesc.AutoWritable = false;
	GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, LightingDesc, LumenSceneData.FinalLightingAtlas, TEXT("LumenSceneFinalLighting"), ERenderTargetTransience::NonTransient);
	LumenSceneData.bFinalLightingAtlasContentsValid = false;

	FPooledRenderTargetDesc RadiosityDesc(FPooledRenderTargetDesc::Create2DDesc(GetRadiosityAtlasSize(LumenSceneData.MaxAtlasSize), PF_FloatR11G11B10, FClearValueBinding::Black, TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV, false));
	RadiosityDesc.AutoWritable = false;
	GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, RadiosityDesc, LumenSceneData.RadiosityAtlas, TEXT("LumenSceneRadiosity"), ERenderTargetTransience::NonTransient);

	FPooledRenderTargetDesc OpacityDesc(FPooledRenderTargetDesc::Create2DDesc(LumenSceneData.MaxAtlasSize, PF_G8, FClearValueBinding::Black, TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_NoFastClear, false, NumMips));
	OpacityDesc.AutoWritable = false;
	GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, OpacityDesc, LumenSceneData.OpacityAtlas, TEXT("LumenSceneOpacity"), ERenderTargetTransience::NonTransient);

	ClearAtlasesToDebugValues(GraphBuilder, LumenSceneData);
}

// @todo Fold into AllocateCardAtlases after changing reallocation boolean to respect optional card atlas state settings
void AllocateOptionalCardAtlases(FRDGBuilder& GraphBuilder, FLumenSceneData& LumenSceneData, bool bReallocateAtlas)
{
	FClearValueBinding CrazyGreen(FLinearColor(0.0f, 10000.0f, 0.0f, 1.0f));
	const int32 NumMips = FMath::CeilLogTwo(FMath::Max(LumenSceneData.MaxAtlasSize.X, LumenSceneData.MaxAtlasSize.Y)) + 1;
	FPooledRenderTargetDesc LightingDesc(FPooledRenderTargetDesc::Create2DDesc(LumenSceneData.MaxAtlasSize, PF_FloatR11G11B10, CrazyGreen, TexCreate_None, TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_NoFastClear, false, NumMips));
	LightingDesc.AutoWritable = false;

	const bool bUseIrradianceAtlas = Lumen::UseIrradianceAtlas();
	if (bUseIrradianceAtlas && (bReallocateAtlas || !LumenSceneData.IrradianceAtlas))
	{
		GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, LightingDesc, LumenSceneData.IrradianceAtlas, TEXT("LumenSceneIrradiance"), ERenderTargetTransience::NonTransient);
	}
	else if (!bUseIrradianceAtlas)
	{
		LumenSceneData.IrradianceAtlas = nullptr;
	}

	const bool bUseIndirectIrradianceAtlas = Lumen::UseIndirectIrradianceAtlas();
	if (bUseIndirectIrradianceAtlas && (bReallocateAtlas || !LumenSceneData.IndirectIrradianceAtlas))
	{
		GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, LightingDesc, LumenSceneData.IndirectIrradianceAtlas, TEXT("LumenSceneIndirectIrradiance"), ERenderTargetTransience::NonTransient);
	}
	else if (!bUseIndirectIrradianceAtlas)
	{
		LumenSceneData.IndirectIrradianceAtlas = nullptr;
	}
}

struct FCardAllocationSorter
{
	bool operator()( const FCardRenderData& A, const FCardRenderData& B ) const
	{
		return A.CardData.DesiredResolution.SizeSquared() > B.CardData.DesiredResolution.SizeSquared();
	}
};

struct FCardIdSorter
{
	bool operator()(uint32 A, uint32 B ) const
	{
		return A < B;
	}
};

extern void UpdateMeshCardRepresentations(FScene* Scene);

void AddCardCaptureDraws(const FScene* Scene, 
	FRHICommandListImmediate& RHICmdList,
	FPrimitiveSceneInfo* PrimitiveSceneInfo, 
	FCardRenderData& CardRenderData, 
	FMeshCommandOneFrameArray& VisibleMeshCommands, 
	TArray<int32, SceneRenderingAllocator>& PrimitiveIds,
	TSet<FPrimitiveSceneInfo*>& PrimitivesToUpdateStaticMeshes)
{
	LLM_SCOPE_BYTAG(Lumen);
	const EMeshPass::Type MeshPass = EMeshPass::LumenCardCapture;

	if (PrimitiveSceneInfo && PrimitiveSceneInfo->Proxy->AffectsDynamicIndirectLighting())
	{
		if (PrimitiveSceneInfo->NeedsUniformBufferUpdate())
		{
			PrimitiveSceneInfo->UpdateUniformBuffer(RHICmdList);
		}

		if (PrimitiveSceneInfo->NeedsUpdateStaticMeshes())
		{
			PrimitivesToUpdateStaticMeshes.Add(PrimitiveSceneInfo);
		}

		if (PrimitiveSceneInfo->Proxy->IsNaniteMesh())
		{
			if (CardRenderData.CardData.InstanceIndexOrMergedFlag >= 0)
			{
				CardRenderData.NaniteInstanceIds.Add(PrimitiveSceneInfo->GetInstanceDataOffset() + CardRenderData.CardData.InstanceIndexOrMergedFlag);
			}
			else
			{
				const int32 NumInstances = PrimitiveSceneInfo->GetNumInstanceDataEntries();

				for (int32 InstanceIndex = 0; InstanceIndex < NumInstances; InstanceIndex++)
				{
					CardRenderData.NaniteInstanceIds.Add(PrimitiveSceneInfo->GetInstanceDataOffset() + InstanceIndex);
				}
			}

			for (const FNaniteCommandInfo& CommandInfo : PrimitiveSceneInfo->NaniteCommandInfos[ENaniteMeshPass::LumenCardCapture])
			{
				CardRenderData.NaniteCommandInfos.Add(CommandInfo);
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
							MeshDrawCommand = &Scene->CachedMeshDrawCommandStateBuckets[EMeshPass::LumenCardCapture].GetByElementId(CachedMeshDrawCommand.StateBucketId).Key;
						}
						else
						{
							MeshDrawCommand = &SceneDrawList.MeshDrawCommands[CachedMeshDrawCommand.CommandIndex];
						}

						FVisibleMeshDrawCommand NewVisibleMeshDrawCommand;

						NewVisibleMeshDrawCommand.Setup(
							MeshDrawCommand,
							PrimitiveSceneInfo->GetIndex(),
							PrimitiveSceneInfo->GetIndex(),
							CachedMeshDrawCommand.StateBucketId,
							CachedMeshDrawCommand.MeshFillMode,
							CachedMeshDrawCommand.MeshCullMode,
							CachedMeshDrawCommand.SortKey);

						VisibleMeshCommands.Add(NewVisibleMeshDrawCommand);
						PrimitiveIds.Add(PrimitiveSceneInfo->GetIndex());
					}
				}
			}
		}
	}
}

void FDeferredShadingSceneRenderer::UpdateLumenCardAtlasAllocation(FRDGBuilder& GraphBuilder, const FViewInfo& MainView, bool bReallocateAtlas, bool bRecaptureLumenSceneOnce)
{
	LLM_SCOPE_BYTAG(Lumen);
	TRACE_CPUPROFILER_EVENT_SCOPE(UpdateCardAtlasAllocation);
	QUICK_SCOPE_CYCLE_COUNTER(UpdateCardAtlasAllocation);

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;
	TArray<FCardRenderData, SceneRenderingAllocator>& CardsToRender = LumenCardRenderer.CardsToRender;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Sort);
		CardsToRender.Sort(FCardAllocationSorter());
	}

	for (int32 CardRenderIndex = 0; CardRenderIndex < CardsToRender.Num(); CardRenderIndex++)
	{
		FCardRenderData& CardRenderData = CardsToRender[CardRenderIndex];

		bool bAllocated = false;
		FIntPoint AllocationMin = FIntPoint::ZeroValue;
		FIntPoint AllocationSize = CardRenderData.CardData.DesiredResolution;

		do
		{
			bAllocated = LumenSceneData.AtlasAllocator.AddElement(AllocationSize, AllocationMin);

			if (!bAllocated)
			{
				AllocationSize /= 2;
			}
		} while (!bAllocated && AllocationSize.X >= 2 && AllocationSize.Y >= 2);

		if (bAllocated)
		{
			LumenSceneData.NumCardTexels += AllocationSize.X * AllocationSize.Y;
			CardRenderData.SetAtlasAllocation(FIntRect(AllocationMin, AllocationMin + AllocationSize));

			CardRenderData.CardData.bAllocated = true;
			CardRenderData.UpdateViewMatrices(MainView);
		}
		else
		{
			if (CardRenderData.CardData.bVisible && CardRenderData.CardData.CubeMapTreeIndex >= 0)
			{
				LumenSceneData.CubeMapTreeBounds[CardRenderData.CardData.CubeMapTreeIndex].DecrementVisible();
			}

			CardRenderData.CardData.bVisible = false;
			LumenSceneData.RemoveCardFromVisibleCardList(CardRenderData.CardIndex);
		}
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RemoveInvisibleCards);

		// Remove cards which became invisible because we couldn't allocate them. 
		// Needs to happen after the allocation loop as multiple cards may share single CardData.
		for (int32 CardRenderIndex = CardsToRender.Num() - 1; CardRenderIndex >= 0; --CardRenderIndex)
		{
			FCardRenderData& CardRenderData = CardsToRender[CardRenderIndex];
			if (!CardsToRender[CardRenderIndex].CardData.bVisible)
			{
				CardRenderData.CardData.RemoveFromAtlas(LumenSceneData);
				CardsToRender.RemoveAtSwap(CardRenderIndex, 1);
			}
		}
	}

	if (bReallocateAtlas || !LumenSceneData.AlbedoAtlas)
	{
		AllocateCardAtlases(GraphBuilder, LumenSceneData);
	}

	if (GLumenSceneRecaptureLumenSceneEveryFrame || bRecaptureLumenSceneOnce)
	{
		ClearAtlas(GraphBuilder, LumenSceneData.DepthAtlas);
		ClearAtlas(GraphBuilder, LumenSceneData.OpacityAtlas);
		ClearAtlas(GraphBuilder, LumenSceneData.AlbedoAtlas);
	}
}

struct FCardToAllocate
{
	FIntPoint TextureAllocationSize;
	int32 CardId;
	uint8 CardPriority;
};

constexpr int32 CUBEMAPS_PER_PACKET = 546; // Up to 2040 cards per packet, chosen such that CARDS_PER_PACKET * (sizeof(FCardToAllocate) + sizeof(int32)) <= 64KB
constexpr int32 MAX_CARD_PRIORITY = 255;

struct FLumenCardUpdatePacket
{
public:

	static constexpr int32 CARDS_PER_PACKET = FLumenCubeMapTreeBounds::MaxCards * CUBEMAPS_PER_PACKET;

	FLumenCardUpdatePacket(
		const TSparseSpanArray<FLumenCubeMapTreeBounds>& InCubeMapTreeBounds,
		const TSparseSpanArray<FCardSourceData>& InCards,
		FVector InViewOrigin,
		float InMaxDistanceFromCamera,
		int32 InFirstCubeMapIndex)
		: NumCardsToReallocate(0)
		, NumTexelsToAllocate(0)
		, CubeMapTreeBounds(InCubeMapTreeBounds)
		, Cards(InCards)
		, ViewOrigin(InViewOrigin)
		, FirstCubeMapIndex(InFirstCubeMapIndex)
		, MaxDistanceFromCamera(InMaxDistanceFromCamera)
		, TexelDensityScale(GetCardCameraDistanceTexelDensityScale())
		, MaxTexelDensity(GLumenSceneCardMaxTexelDensity)
	{
		bAllowCoarseCulling = GLumenCoarseCardCulling != 0
			&& GLumenSceneCardFixedDebugTexelDensity <= 0; // FixedDebugTexelDensity support is not implemented for coarse culling

		CardsToAllocate.Reserve(CARDS_PER_PACKET);
		CardsToRemove.Reserve(CARDS_PER_PACKET);

		static constexpr uint32 BytesPerPacket = CARDS_PER_PACKET * (sizeof(FCardToAllocate) + sizeof(int32));
		static_assert(BytesPerPacket >= 65000 && BytesPerPacket <= 65536,
			"Must keep dynamic memory allocation per packet as close as possible to 64KB to avoid wasting SceneRenderingAllocator space while minimizing number of allocations");
	}

	// Output
	TArray<FCardToAllocate, SceneRenderingAllocator> CardsToAllocate;
	TArray<int32, SceneRenderingAllocator> CardsToRemove;
	int32 NumCardsToReallocate;
	int32 NumTexelsToAllocate;

	// Stats
	int32 NumSlowCubeMaps = 0;
	int32 NumSkippedCubeMaps = 0;
	int32 NumSmallCubeMaps = 0;

	void AnyThreadTask()
	{
		const int32 LastCubeMapIndex = FMath::Min(FirstCubeMapIndex + CUBEMAPS_PER_PACKET, CubeMapTreeBounds.Num());
		const float MaxDistanceSquared = MaxDistanceFromCamera * MaxDistanceFromCamera;

		for (int32 CubeMapIndex = FirstCubeMapIndex; CubeMapIndex < LastCubeMapIndex; ++CubeMapIndex)
		{
			if (!CubeMapTreeBounds.IsAllocated(CubeMapIndex))
			{
				continue;
			}

			const FLumenCubeMapTreeBounds& CubeMap = CubeMapTreeBounds[CubeMapIndex];

			const float DistanceSquared = bAllowCoarseCulling ? CubeMap.ComputeSquaredDistanceFromBoxToPoint(ViewOrigin) : 0.0f;

			if (CubeMap.HasVisibleCards() || DistanceSquared <= MaxDistanceSquared)
			{
				if (bAllowCoarseCulling)
				{
					const FVector CubeMapExtent = CubeMap.GetWorldBoundsExtent();
					const float ExtentMax = CubeMapExtent.GetMax();
					const float ViewerDistance = FMath::Max(FMath::Sqrt(DistanceSquared), 1.0f);
					const float ProjectedSize = FMath::Min(
						TexelDensityScale * ExtentMax * CubeMap.GetResolutionScale() / ViewerDistance,
						MaxTexelDensity * ExtentMax);

					const int32 SnappedSize = FMath::RoundUpToPowerOfTwo(FMath::TruncToInt(ProjectedSize));

					if (SnappedSize < 2 && !CubeMap.HasVisibleCards())
					{
						NumSmallCubeMaps++;
						continue;
					}
				}

				NumSlowCubeMaps++;
				ProcessCards(CubeMap.GetFirstCardIndex(), CubeMap.GetLastCardIndex());
			}
			else
			{
				NumSkippedCubeMaps++;
			}
		}
	}

private:

	void ProcessCards(const int32 FirstCardIndex, const int32 LastCardIndex)
	{
		for (int32 CardIndex = FirstCardIndex; CardIndex < LastCardIndex; ++CardIndex)
		{
			if (Cards.IsAllocated(CardIndex))
			{
				const FCardSourceData& CardData = Cards[CardIndex];
				const FVector CardSpaceViewOrigin = CardData.TransformWorldPositionToCardLocal(ViewOrigin);
				const FBox CardBox(-CardData.LocalExtent, CardData.LocalExtent);

				const float ViewerDistance = FMath::Max(FMath::Sqrt(CardBox.ComputeSquaredDistanceToPoint(CardSpaceViewOrigin)), 1.0f);

				FIntPoint TextureAllocationSize;

				FVector FaceLocalExtent = CardData.LocalExtent;

				float ProjectedSizeX = FMath::Min(
					TexelDensityScale * FaceLocalExtent.X * CardData.ResolutionScale / ViewerDistance,
					MaxTexelDensity * FaceLocalExtent.X);

				if (GLumenSceneCardFixedDebugTexelDensity > 0)
				{
					ProjectedSizeX = GLumenSceneCardFixedDebugTexelDensity * FaceLocalExtent.X;
				}

				const float ProjectedSizeY = ProjectedSizeX * (FaceLocalExtent.Y / FaceLocalExtent.X);

				const int32 SnappedX = FMath::RoundUpToPowerOfTwo(FMath::TruncToInt(ProjectedSizeX));
				TextureAllocationSize.X = FMath::Clamp<uint32>(SnappedX, 4, GetCardMaxResolution());
				const int32 SnappedY = FMath::RoundUpToPowerOfTwo(FMath::TruncToInt(ProjectedSizeY));
				TextureAllocationSize.Y = FMath::Clamp<uint32>(SnappedY, 4, GetCardMaxResolution());

				bool bNewVisible = ViewerDistance < MaxDistanceFromCamera && (SnappedX > 2 || SnappedY > 2);

				if (bNewVisible != CardData.bVisible && !bNewVisible)
				{
					CardsToRemove.Add(CardIndex);
				}

				const bool bNeedsAtlasAllocation = !CardData.bAllocated && bNewVisible;
				const bool bNeedsAtlasReallocation = CardData.bAllocated && TextureAllocationSize != CardData.DesiredResolution;
				if (bNeedsAtlasAllocation || bNeedsAtlasReallocation)
				{
					float PriorityBias = 0.0f;

					if (bNeedsAtlasReallocation)
					{
						++NumCardsToReallocate;

						// Make reallocation less important than capturing new cards.
						const float ResChangeFactor = FMath::Abs(FMath::Log2(TextureAllocationSize.X * TextureAllocationSize.Y) - FMath::Log2(CardData.AtlasAllocation.Area()));

						PriorityBias += MAX_CARD_PRIORITY * FMath::Clamp(1.0f - (ResChangeFactor - 1.0f) / 3.0f, 0.0f, 1.0f);
					}

					uint8 CardPriority = FMath::Clamp<float>((ViewerDistance - 100.0f) / 100.0f + PriorityBias, 0.0f, MAX_CARD_PRIORITY);

					FCardToAllocate CardToAllocate;
					CardToAllocate.CardId = CardIndex;
					CardToAllocate.TextureAllocationSize = TextureAllocationSize;
					CardToAllocate.CardPriority = CardPriority;
					CardsToAllocate.Add(CardToAllocate);

					NumTexelsToAllocate += TextureAllocationSize.X * TextureAllocationSize.Y;
				}
			}
		}
	}

	const TSparseSpanArray<FLumenCubeMapTreeBounds>& CubeMapTreeBounds;
	const TSparseSpanArray<FCardSourceData>& Cards;
	FVector ViewOrigin;
	int32 FirstCubeMapIndex;
	float MaxDistanceFromCamera;
	float TexelDensityScale;
	float MaxTexelDensity;

	bool bAllowCoarseCulling;
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
		const float HalfVoxelSize = LastClipmapExtent / GLumenSceneClipmapResolution;

		MaxCardDistanceFromCamera = LastClipmapExtent + HalfVoxelSize;
	}

	// Indirect card tracing distance (skip lumen scene visualize so it is possible to see which cards are generated in the vis mode)
	extern float GDiffuseCardTraceEndDistanceFromCamera;
	MaxCardDistanceFromCamera = FMath::Max(MaxCardDistanceFromCamera, GDiffuseCardTraceEndDistanceFromCamera);

	return MaxCardDistanceFromCamera + FMath::Max(GLumenSceneCardCaptureMargin, 0.0f);
}

void FDeferredShadingSceneRenderer::BeginUpdateLumenSceneTasks(FRDGBuilder& GraphBuilder)
{
	LLM_SCOPE_BYTAG(Lumen);

	const FViewInfo& MainView = Views[0];

	if (GAllowLumenScene
		&& DoesPlatformSupportLumenGI(ShaderPlatform)
		&& !ViewFamily.EngineShowFlags.HitProxies
		&& ViewFamily.EngineShowFlags.Lighting
		// Don't update scene lighting for secondary views
		&& !MainView.bIsPlanarReflection 
		&& !MainView.bIsSceneCapture
		&& !MainView.bIsReflectionCapture
		&& MainView.ViewState)
	{
		SCOPED_NAMED_EVENT(FDeferredShadingSceneRenderer_BeginUpdateLumenSceneTasks, FColor::Emerald);
		QUICK_SCOPE_CYCLE_COUNTER(BeginUpdateLumenSceneTasks);
		const double StartTime = FPlatformTime::Seconds();

		FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;
		TArray<FCardRenderData, SceneRenderingAllocator>& CardsToRender = LumenCardRenderer.CardsToRender;
		LumenCardRenderer.Reset();

		const int32 LocalLumenSceneGeneration = GLumenSceneGeneration;
		const bool bRecaptureLumenSceneOnce = LumenSceneData.Generation != LocalLumenSceneGeneration;
		LumenSceneData.Generation = LocalLumenSceneGeneration;
		const bool bReallocateAtlas = LumenSceneData.MaxAtlasSize != GetDesiredAtlasSize() 
			|| (LumenSceneData.RadiosityAtlas && LumenSceneData.RadiosityAtlas->GetDesc().Extent != GetRadiosityAtlasSize(LumenSceneData.MaxAtlasSize))
			|| GLumenSceneReset;

		if (GLumenSceneReset != 2)
		{
			GLumenSceneReset = 0;
		}

		LumenSceneData.NumCardsLeftToCapture = 0;
		LumenSceneData.NumCardsLeftToReallocate = 0;
		LumenSceneData.NumTexelsLeftToCapture = 0;

		UpdateDirtyCards(Scene, bReallocateAtlas, bRecaptureLumenSceneOnce);
		UpdateMeshCardRepresentations(Scene);
		UpdateDistantScene(Scene, Views[0]);

		const FVector LumenSceneCameraOrigin = GetLumenSceneViewOrigin(MainView, GetNumLumenVoxelClipmaps() - 1);

		if (bReallocateAtlas)
		{
			LumenSceneData.MaxAtlasSize = GetDesiredAtlasSize();
			// Everything should have been freed before recreating the layout
			ensure(LumenSceneData.NumCardTexels == 0);

			LumenSceneData.AtlasAllocator = FBinnedTextureLayout(LumenSceneData.MaxAtlasSize, GLumenSceneCardAtlasAllocatorBinSize);
		}

		const int32 CardCapturesPerFrame = GLumenSceneRecaptureLumenSceneEveryFrame != 0 ? LumenSceneData.Cards.Num() : GetMaxLumenSceneCardCapturesPerFrame();
		const int32 CardTexelsToCapturePerFrame = GLumenSceneRecaptureLumenSceneEveryFrame != 0 ? INT_MAX : GetLumenSceneCardResToCapturePerFrame() * GetLumenSceneCardResToCapturePerFrame();

		if (CardCapturesPerFrame > 0 && CardTexelsToCapturePerFrame > 0)
		{
			QUICK_SCOPE_CYCLE_COUNTER(FillCardsToRender);

			TArray<FLumenCardUpdatePacket, SceneRenderingAllocator> Packets;
			TArray<FCardToAllocate, SceneRenderingAllocator> CardsToAllocateSortedByPriority;

			{
				TRACE_CPUPROFILER_EVENT_SCOPE(PrepareFillCardsToRenderTasks);

				CardsToRender.Reset(FMath::Min(LumenSceneData.Cards.Num(), GetMaxLumenSceneCardCapturesPerFrame()));

				const float MaxCardUpdateDistanceFromCamera = ComputeMaxCardUpdateDistanceFromCamera();

				const int32 NumPackets = FMath::DivideAndRoundUp(LumenSceneData.CubeMapTreeBounds.Num(), CUBEMAPS_PER_PACKET);

				Packets.Reserve(NumPackets);
				for (int32 PacketIndex = 0; PacketIndex < NumPackets; ++PacketIndex)
				{
					Packets.Emplace(
						LumenSceneData.CubeMapTreeBounds,
						LumenSceneData.Cards,
						LumenSceneCameraOrigin,
						MaxCardUpdateDistanceFromCamera,
						PacketIndex * CUBEMAPS_PER_PACKET);
				}
			}

			{
				TRACE_CPUPROFILER_EVENT_SCOPE(RunFillCardsToRenderTasks);
				const bool bExecuteInParallel = FApp::ShouldUseThreadingForPerformance();

				ParallelFor(Packets.Num(),
					[&Packets](int32 Index)
					{
						Packets[Index].AnyThreadTask();
					},
					!bExecuteInParallel
				);
			}

			{
				TRACE_CPUPROFILER_EVENT_SCOPE(PacketResults);

				const float CARD_DISTANCE_BUCKET_SIZE = 100.0f;
				uint32 NumCardsToAllocatePerBucket[MAX_CARD_PRIORITY + 1];

				for (int32 BucketIndex = 0; BucketIndex < UE_ARRAY_COUNT(NumCardsToAllocatePerBucket); ++BucketIndex)
				{
					NumCardsToAllocatePerBucket[BucketIndex] = 0;
				}

				// Count how many cards fall into each bucket
				for (int32 PacketIndex = 0; PacketIndex < Packets.Num(); ++PacketIndex)
				{
					const FLumenCardUpdatePacket& Packet = Packets[PacketIndex];

					LumenSceneData.NumCardsLeftToCapture += Packet.CardsToAllocate.Num();
					LumenSceneData.NumCardsLeftToReallocate += Packet.NumCardsToReallocate;
					LumenSceneData.NumTexelsLeftToCapture += Packet.NumTexelsToAllocate;

					for (int32 CardIndex = 0; CardIndex < Packet.CardsToAllocate.Num(); ++CardIndex)
					{
						const FCardToAllocate& CardToAllocate = Packet.CardsToAllocate[CardIndex];

						++NumCardsToAllocatePerBucket[CardToAllocate.CardPriority];
					}
				}

				int32 NumCardsInBucketsUpToMaxBucket = 0;
				int32 MaxBucketIndexToAllocate = 0;

				// Select first N buckets for allocation
				for (int32 BucketIndex = 0; BucketIndex < UE_ARRAY_COUNT(NumCardsToAllocatePerBucket); ++BucketIndex)
				{
					NumCardsInBucketsUpToMaxBucket += NumCardsToAllocatePerBucket[BucketIndex];
					MaxBucketIndexToAllocate = BucketIndex;

					if (NumCardsInBucketsUpToMaxBucket > CardCapturesPerFrame)
					{
						break;
					}
				}

				CardsToAllocateSortedByPriority.Reserve(CardCapturesPerFrame);

				extern int32 GLumenUpdateDistantSceneCaptures;

				if (GLumenUpdateDistantSceneCaptures)
				{
					for (int32 DistantCardIndex : LumenSceneData.DistantCardIndices)
					{
						const FCardSourceData& DistantCard = LumenSceneData.Cards[DistantCardIndex];
						FCardToAllocate CardToAllocate;
						CardToAllocate.CardId = DistantCardIndex;
						extern int32 GLumenDistantSceneCardResolution;
						CardToAllocate.TextureAllocationSize = FIntPoint(GLumenDistantSceneCardResolution, GLumenDistantSceneCardResolution);
						CardToAllocate.CardPriority = 0;
						CardsToAllocateSortedByPriority.Add(CardToAllocate);
					}
				}

				// Copy first N buckets into CardsToAllocateSortedByDistance
				for (int32 PacketIndex = 0; PacketIndex < Packets.Num(); ++PacketIndex)
				{
					const FLumenCardUpdatePacket& Packet = Packets[PacketIndex];

					for (int32 CardIndex = 0; CardIndex < Packet.CardsToAllocate.Num() && CardsToAllocateSortedByPriority.Num() < CardCapturesPerFrame; ++CardIndex)
					{
						const FCardToAllocate& CardToAllocate = Packet.CardsToAllocate[CardIndex];

						if (CardToAllocate.CardPriority <= MaxBucketIndexToAllocate)
						{
							CardsToAllocateSortedByPriority.Add(CardToAllocate);
						}
					}
				}			

				// Remove all cards which became invisible
				for (int32 PacketIndex = 0; PacketIndex < Packets.Num(); ++PacketIndex)
				{
					const FLumenCardUpdatePacket& Packet = Packets[PacketIndex];

					for (int32 CardToRemoveIndex = 0; CardToRemoveIndex < Packet.CardsToRemove.Num(); ++CardToRemoveIndex)
					{
						const int32 CardIndex = Packet.CardsToRemove[CardToRemoveIndex];
						FCardSourceData& CardData = LumenSceneData.Cards[CardIndex];

						CardData.RemoveFromAtlas(LumenSceneData);

						if (CardData.bVisible && CardData.CubeMapTreeIndex >= 0)
						{
							LumenSceneData.CubeMapTreeBounds[CardData.CubeMapTreeIndex].DecrementVisible();
						}

						CardData.bVisible = false;

						LumenSceneData.RemoveCardFromVisibleCardList(CardIndex);
						LumenSceneData.CardIndicesToUpdateInBuffer.Add(CardIndex);
					}
				}
			}

			// Allocate new cards
			for (int32 SortedCardIndex = 0; SortedCardIndex < CardsToAllocateSortedByPriority.Num(); ++SortedCardIndex)
			{
				const FCardToAllocate& CardToAllocate = CardsToAllocateSortedByPriority[SortedCardIndex];

				const int32 CardIndex = CardToAllocate.CardId;

				FCardSourceData& CardData = LumenSceneData.Cards[CardIndex];

				LumenSceneData.AddCardToVisibleCardList(CardIndex);
				
				if (!CardData.bVisible && CardData.CubeMapTreeIndex >= 0)
				{
					LumenSceneData.CubeMapTreeBounds[CardData.CubeMapTreeIndex].IncrementVisible();
				}

				CardData.bVisible = true;

				// Remove from atlas
				CardData.RemoveFromAtlas(LumenSceneData);

				LumenSceneData.CardIndicesToUpdateInBuffer.Add(CardIndex);

				CardData.DesiredResolution = CardToAllocate.TextureAllocationSize;

				CardsToRender.Add(FCardRenderData(CardData, FeatureLevel, CardIndex));

				if (!CardData.bDistantScene)
				{
					LumenCardRenderer.NumCardTexelsToCapture += CardData.AtlasAllocation.Area();
				}

				if (CardsToRender.Num() >= CardCapturesPerFrame
					|| LumenCardRenderer.NumCardTexelsToCapture >= CardTexelsToCapturePerFrame)
				{
					break;
				}
			}
		}

		AllocateOptionalCardAtlases(GraphBuilder, LumenSceneData, bReallocateAtlas);
		if (CardsToRender.Num() > 0)
		{
			UpdateLumenCardAtlasAllocation(GraphBuilder, MainView, bReallocateAtlas, bRecaptureLumenSceneOnce);

			{
				QUICK_SCOPE_CYCLE_COUNTER(MeshPassSetup);

				// Set of unique primitives requiring static mesh update
				TSet<FPrimitiveSceneInfo*> PrimitivesToUpdateStaticMeshes;

				for (FCardRenderData& CardRenderData : CardsToRender)
				{
					CardRenderData.StartMeshDrawCommandIndex = LumenCardRenderer.MeshDrawCommands.Num();
					CardRenderData.NumMeshDrawCommands = 0;
					int32 NumNanitePrimitives = 0;

					checkSlow(CardRenderData.CardData.bVisible && CardRenderData.CardData.bAllocated);
					AddCardCaptureDraws(Scene, 
						GraphBuilder.RHICmdList, 
						CardRenderData.CardData.PrimitiveSceneInfo, 
						CardRenderData, 
						LumenCardRenderer.MeshDrawCommands, 
						LumenCardRenderer.MeshDrawPrimitiveIds, 
						PrimitivesToUpdateStaticMeshes);

					CardRenderData.NumMeshDrawCommands = LumenCardRenderer.MeshDrawCommands.Num() - CardRenderData.StartMeshDrawCommandIndex;
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

			const float TimeElapsed = FPlatformTime::Seconds() - StartTime;

			if (TimeElapsed > .03f)
			{
				UE_LOG(LogRenderer, Log, TEXT("BeginUpdateLumenSceneTasks %u Card Renders %.1fms"), CardsToRender.Num(), TimeElapsed * 1000.0f);
			}
		}
	}

	LumenScenePDIVisualization();
}

class FLumenCardGPUData
{
public:
	// Must match usf
	enum { DataStrideInFloat4s = 5 };
	enum { DataStrideInBytes = DataStrideInFloat4s * 16 };

	static void FillData(const class FCardSourceData& RESTRICT CardData, FVector2D InvAtlasSize, FVector4* RESTRICT OutData)
	{
		// Note: layout must match GetLumenCardData in usf

		OutData[0] = FVector4(CardData.LocalToWorldRotationX[0], CardData.LocalToWorldRotationY[0], CardData.LocalToWorldRotationZ[0], CardData.Origin.X);
		OutData[1] = FVector4(CardData.LocalToWorldRotationX[1], CardData.LocalToWorldRotationY[1], CardData.LocalToWorldRotationZ[1], CardData.Origin.Y);
		OutData[2] = FVector4(CardData.LocalToWorldRotationX[2], CardData.LocalToWorldRotationY[2], CardData.LocalToWorldRotationZ[2], CardData.Origin.Z);

		float MaxMip = 0.0f;
		const FVector2D CardSizeTexels = CardData.AtlasAllocation.Max - CardData.AtlasAllocation.Min;
		if (CardSizeTexels.X > 0 && CardSizeTexels.Y > 0)
		{
			const float MaxMipX = FMath::Log2(CardSizeTexels.X);
			const float MaxMipY = FMath::Log2(CardSizeTexels.Y);

			// Stop at 4x4 texels as 1x1 isn't prepared in the atlas and we use trilinear sampling
			MaxMip = FMath::Max<int32>(FMath::Min(MaxMipX, MaxMipY) - 2, 0);
		}
		const float VisibleSign = CardData.bVisible ? 1.0f : -1.0f;
		OutData[3] = FVector4(CardData.LocalExtent.X, VisibleSign * CardData.LocalExtent.Y, CardData.LocalExtent.Z, MaxMip);

		const FVector2D AtlasScale = CardSizeTexels * InvAtlasSize;
		const FVector FaceLocalExtent = CardData.LocalExtent;
		const FVector2D LocalPositionToAtlasUVScale = AtlasScale / (2 * FVector2D(FaceLocalExtent.X, -FaceLocalExtent.Y));
		const FVector2D AtlasBias = (FVector2D)CardData.AtlasAllocation.Min * InvAtlasSize;
		const FVector2D LocalPositionToAtlasUVBias = AtlasBias + .5f * AtlasScale;
		OutData[4] = FVector4(LocalPositionToAtlasUVScale, LocalPositionToAtlasUVBias);

		static_assert(DataStrideInFloat4s == 5, "Data stride doesn't match");
	}
};

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FLumenCardScene, "LumenCardScene");

class FNullCardBuffers : public FRenderResource
{
public:

	virtual void InitRHI() override
	{
		EPixelFormat BufferFormat = PF_A32B32G32R32F;
		uint32 BytesPerElement = GPixelFormats[BufferFormat].BlockBytes;
		CardData.Initialize(BytesPerElement, 1, 0, TEXT("FNullCardBuffers"));
	}

	virtual void ReleaseRHI() override
	{
		CardData.Release();
	}

	FRWBufferStructured CardData;
};

TGlobalResource<FNullCardBuffers> GNullCardBuffers;


void SetupLumenCardSceneParameters(FScene* Scene, FLumenCardScene& OutParameters)
{
	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	OutParameters.NumCards = LumenSceneData.Cards.Num();
	OutParameters.MaxConeSteps = GLumenGIMaxConeSteps;
	OutParameters.AtlasSize = LumenSceneData.MaxAtlasSize;
	const int32 NumMips = FMath::CeilLogTwo(FMath::Max(LumenSceneData.MaxAtlasSize.X, LumenSceneData.MaxAtlasSize.Y)) + 1;
	OutParameters.NumMips = NumMips;
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

	if (LumenSceneData.Cards.Num() > 0)
	{
		OutParameters.CardData = LumenSceneData.CardBuffer.SRV;
	}
	else
	{
		OutParameters.CardData = GNullCardBuffers.CardData.SRV;
	}

	if (LumenSceneData.AlbedoAtlas.IsValid())
	{
		OutParameters.AlbedoAtlas = LumenSceneData.AlbedoAtlas->GetRenderTargetItem().ShaderResourceTexture;
		OutParameters.NormalAtlas = LumenSceneData.NormalAtlas->GetRenderTargetItem().ShaderResourceTexture;
		OutParameters.EmissiveAtlas = LumenSceneData.EmissiveAtlas->GetRenderTargetItem().ShaderResourceTexture;
		OutParameters.DepthBufferAtlas = LumenSceneData.DepthBufferAtlas->GetRenderTargetItem().ShaderResourceTexture;
	}
	else
	{
		OutParameters.AlbedoAtlas = GSystemTextures.BlackDummy->GetRenderTargetItem().ShaderResourceTexture;
		OutParameters.NormalAtlas = GSystemTextures.BlackDummy->GetRenderTargetItem().ShaderResourceTexture;
		OutParameters.EmissiveAtlas = GSystemTextures.BlackDummy->GetRenderTargetItem().ShaderResourceTexture;
		OutParameters.DepthBufferAtlas = GSystemTextures.DepthDummy->GetRenderTargetItem().ShaderResourceTexture;
	}
	
	OutParameters.CubeMapData = LumenSceneData.CubeMapBuffer.SRV;
	OutParameters.CubeMapTreeData = LumenSceneData.CubeMapTreeBuffer.SRV;
	OutParameters.DFObjectToCubeMapTreeIndexBuffer = LumenSceneData.DFObjectToCubeMapTreeIndexBuffer.SRV;
	OutParameters.PrimitiveToDFObjectIndexBuffer = LumenSceneData.PrimitiveToDFObjectIndexBuffer.SRV;
}

DECLARE_GPU_STAT(UpdateCardSceneBuffer);

void UpdateCardSceneBuffer(FRHICommandListImmediate& RHICmdList, const FSceneViewFamily& ViewFamily, FScene* Scene)
{
	LLM_SCOPE_BYTAG(Lumen);

	TRACE_CPUPROFILER_EVENT_SCOPE(UpdateCardSceneBuffer);
	QUICK_SCOPE_CYCLE_COUNTER(UpdateCardSceneBuffer);
	SCOPED_DRAW_EVENT(RHICmdList, UpdateCardSceneBuffer);

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;

	{
		const int32 NumCardEntries = LumenSceneData.Cards.Num();
		const uint32 CardSceneNumFloat4s = NumCardEntries * FLumenCardGPUData::DataStrideInFloat4s;
		const uint32 CardSceneNumBytes = FMath::DivideAndRoundUp(CardSceneNumFloat4s, 16384u) * 16384 * sizeof(FVector4);
		// Reserve enough space
		ResizeResourceIfNeeded(RHICmdList, LumenSceneData.CardBuffer, FMath::RoundUpToPowerOfTwo(CardSceneNumFloat4s) * sizeof(FVector4), TEXT("Cards0"));
	}

	if (GLumenSceneUploadCardBufferEveryFrame)
	{
		LumenSceneData.CardIndicesToUpdateInBuffer.Reset();

		for (int32 i = 0; i < LumenSceneData.Cards.Num(); i++)
		{
			LumenSceneData.CardIndicesToUpdateInBuffer.Add(i);
		}
	}

	const int32 NumCardDataUploads = LumenSceneData.CardIndicesToUpdateInBuffer.Num();

	if (NumCardDataUploads > 0)
	{
		FCardSourceData NullCard;

		LumenSceneData.UploadBuffer.Init(NumCardDataUploads, FLumenCardGPUData::DataStrideInBytes, true, TEXT("LumenSceneUploadBuffer"));

		FVector2D InvAtlasSize(1.0f / LumenSceneData.MaxAtlasSize.X, 1.0f / LumenSceneData.MaxAtlasSize.Y);

		for (int32 Index : LumenSceneData.CardIndicesToUpdateInBuffer)
		{
			if (Index < LumenSceneData.Cards.Num())
			{
				const FCardSourceData& Card = LumenSceneData.Cards.IsAllocated(Index) ? LumenSceneData.Cards[Index] : NullCard;

				FVector4* Data = (FVector4*) LumenSceneData.UploadBuffer.Add_GetRef(Index);
				FLumenCardGPUData::FillData(Card, InvAtlasSize, Data);
			}
		}

		RHICmdList.Transition(FRHITransitionInfo(LumenSceneData.CardBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
		LumenSceneData.UploadBuffer.ResourceUploadTo(RHICmdList, LumenSceneData.CardBuffer, false);
		RHICmdList.Transition(FRHITransitionInfo(LumenSceneData.CardBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
	}

	UpdateLumenCubeMapTrees(Scene->DistanceFieldSceneData, LumenSceneData, RHICmdList, Scene->Primitives.Num());

	{
		FLumenCardScene LumenCardSceneParameters;
		SetupLumenCardSceneParameters(Scene, LumenCardSceneParameters);
		LumenSceneData.UniformBuffer = CreateUniformBufferImmediate(LumenCardSceneParameters, UniformBuffer_MultiFrame);
	}

	const uint32 MaxUploadBufferSize = 4096;
	if (LumenSceneData.UploadBuffer.GetNumBytes() > MaxUploadBufferSize)
	{
		LumenSceneData.UploadBuffer.Release();
	}
}

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

IMPLEMENT_GLOBAL_SHADER(FClearLumenCardsPS, "/Engine/Private/Lumen/LumenSceneLighting.usf", "ClearLumenCardsPS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FClearLumenCardsParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FPixelShaderUtils::FRasterizeToRectsVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FClearLumenCardsPS::FParameters, PS)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void ClearLumenCards(FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FRDGTextureRef AlbedoAtlas,
	FRDGTextureRef NormalAtlas,
	FRDGTextureRef EmissiveAtlas,
	FRDGTextureRef DepthBufferAtlas,
	FIntPoint ViewportSize,
	FRDGBufferSRVRef RectMinMaxBufferSRV,
	uint32 NumRects)
{
	LLM_SCOPE_BYTAG(Lumen);

	FClearLumenCardsParameters* PassParameters = GraphBuilder.AllocParameters<FClearLumenCardsParameters>();

	PassParameters->RenderTargets[0] = FRenderTargetBinding(AlbedoAtlas, ERenderTargetLoadAction::ELoad);
	PassParameters->RenderTargets[1] = FRenderTargetBinding(NormalAtlas, ERenderTargetLoadAction::ELoad);
	PassParameters->RenderTargets[2] = FRenderTargetBinding(EmissiveAtlas, ERenderTargetLoadAction::ELoad);
	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(DepthBufferAtlas, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilWrite);

	auto PixelShader = View.ShaderMap->GetShader<FClearLumenCardsPS>();

	FPixelShaderUtils::AddRasterizeToRectsPass<FClearLumenCardsPS>(GraphBuilder,
		View.ShaderMap,
		RDG_EVENT_NAME("ClearLumenCards"),
		PixelShader,
		PassParameters,
		ViewportSize,
		RectMinMaxBufferSRV,
		NumRects,
		TStaticBlendState<>::GetRHI(),
		TStaticRasterizerState<>::GetRHI(),
		TStaticDepthStencilState<true, CF_Always,
		true, CF_Always, SO_Replace, SO_Replace, SO_Replace,
		false, CF_Always, SO_Replace, SO_Replace, SO_Replace,
		0xff, 0xff>::GetRHI());
}

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardIdUpload, )
	RDG_BUFFER_ACCESS(CardIds, ERHIAccess::CopyDest)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FLumenCardPassParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardPassUniformParameters, CardPass)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void FDeferredShadingSceneRenderer::UpdateLumenScene(FRDGBuilder& GraphBuilder)
{
	LLM_SCOPE_BYTAG(Lumen);

	if (GAllowLumenScene
		&& ViewFamily.EngineShowFlags.Lighting
		&& DoesPlatformSupportLumenGI(ShaderPlatform)
		// Don't update scene lighting for secondary views
		&& !Views[0].bIsPlanarReflection 
		&& !Views[0].bIsSceneCapture
		&& !Views[0].bIsReflectionCapture
		&& Views[0].ViewState)
	{
		const double StartTime = FPlatformTime::Seconds();

		FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;
		TArray<FCardRenderData, SceneRenderingAllocator>& CardsToRender = LumenCardRenderer.CardsToRender;

		QUICK_SCOPE_CYCLE_COUNTER(UpdateLumenScene);
		RDG_GPU_STAT_SCOPE(GraphBuilder, UpdateLumenScene);
		RDG_EVENT_SCOPE(GraphBuilder, "UpdateLumenScene: %u card captures %.3fM texels", CardsToRender.Num(), LumenCardRenderer.NumCardTexelsToCapture / 1e6f);

		UpdateCardSceneBuffer(GraphBuilder.RHICmdList, ViewFamily, Scene);
		
		LumenCardRenderer.CardIdsToRender.Empty(CardsToRender.Num());

		if (CardsToRender.Num() > 0)
		{
			// Prepare primitive Id VB for rendering mesh draw commands.
			FRHIVertexBuffer* PrimitiveIdVertexBuffer = nullptr;
			if (LumenCardRenderer.MeshDrawPrimitiveIds.Num() > 0)
			{
				const uint32 PrimitiveIdBufferDataSize = LumenCardRenderer.MeshDrawPrimitiveIds.Num() * sizeof(int32);

				FPrimitiveIdVertexBufferPoolEntry Entry = GPrimitiveIdVertexBufferPool.Allocate(PrimitiveIdBufferDataSize);
				PrimitiveIdVertexBuffer = Entry.BufferRHI;

				void* RESTRICT Data = RHILockVertexBuffer(PrimitiveIdVertexBuffer, 0, PrimitiveIdBufferDataSize, RLM_WriteOnly);
				FMemory::Memcpy(Data, LumenCardRenderer.MeshDrawPrimitiveIds.GetData(), PrimitiveIdBufferDataSize);
				RHIUnlockVertexBuffer(PrimitiveIdVertexBuffer);

				GPrimitiveIdVertexBufferPool.ReturnToFreeList(Entry);
			}

			FRDGTextureRef DepthAtlasTexture = GraphBuilder.RegisterExternalTexture(LumenSceneData.DepthBufferAtlas);
			FRDGTextureRef AlbedoAtlasTexture = GraphBuilder.RegisterExternalTexture(LumenSceneData.AlbedoAtlas);
			FRDGTextureRef NormalAtlasTexture = GraphBuilder.RegisterExternalTexture(LumenSceneData.NormalAtlas);
			FRDGTextureRef EmissiveAtlasTexture = GraphBuilder.RegisterExternalTexture(LumenSceneData.EmissiveAtlas);

			uint32 NumRects = 0;
			FRDGBufferRef RectMinMaxBuffer = nullptr;
			{
				// Upload card Ids for batched draws operating on cards to render.
				TArray<FUintVector4, SceneRenderingAllocator> RectMinMaxToRender;
				RectMinMaxToRender.Reserve(CardsToRender.Num());
				for (const FCardRenderData& CardRenderData : CardsToRender)
				{
					FIntRect AtlasRect = CardRenderData.GetAtlasAllocation();

					FUintVector4 Rect;
					Rect.X = FMath::Max(AtlasRect.Min.X, 0);
					Rect.Y = FMath::Max(AtlasRect.Min.Y, 0);
					Rect.Z = FMath::Max(AtlasRect.Max.X, 0);
					Rect.W = FMath::Max(AtlasRect.Max.Y, 0);
					RectMinMaxToRender.Add(Rect);
				}

				NumRects = CardsToRender.Num();
				RectMinMaxBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateUploadDesc(sizeof(FUintVector4), FMath::RoundUpToPowerOfTwo(NumRects)), TEXT("RectMinMaxBuffer"));

				FPixelShaderUtils::UploadRectMinMaxBuffer(GraphBuilder, RectMinMaxToRender, RectMinMaxBuffer);

				FRDGBufferSRVRef RectMinMaxBufferSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(RectMinMaxBuffer, PF_R32G32B32A32_UINT));
				ClearLumenCards(GraphBuilder, Views[0], AlbedoAtlasTexture, NormalAtlasTexture, EmissiveAtlasTexture, DepthAtlasTexture, LumenSceneData.MaxAtlasSize, RectMinMaxBufferSRV, NumRects);
			}

			FViewInfo* SharedView = Views[0].CreateSnapshot();
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
				PassParameters->CardPass = GraphBuilder.CreateUniformBuffer(PassUniformParameters);
				PassParameters->RenderTargets[0] = FRenderTargetBinding(AlbedoAtlasTexture, ERenderTargetLoadAction::ELoad);
				PassParameters->RenderTargets[1] = FRenderTargetBinding(NormalAtlasTexture, ERenderTargetLoadAction::ELoad);
				PassParameters->RenderTargets[2] = FRenderTargetBinding(EmissiveAtlasTexture, ERenderTargetLoadAction::ELoad);
				PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(DepthAtlasTexture, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilNop);

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("MeshCardCapture"),
					PassParameters,
					ERDGPassFlags::Raster,
					[this, Scene = Scene, PrimitiveIdVertexBuffer, SharedView, &CardsToRender, PassParameters](FRHICommandList& RHICmdList)
					{
						QUICK_SCOPE_CYCLE_COUNTER(MeshPass);

						for (FCardRenderData& CardRenderData : CardsToRender)
						{
							if (CardRenderData.NumMeshDrawCommands > 0)
							{
								FIntRect AtlasRect = CardRenderData.GetAtlasAllocation();
								RHICmdList.SetViewport(AtlasRect.Min.X, AtlasRect.Min.Y, 0.0f, AtlasRect.Max.X, AtlasRect.Max.Y, 1.0f);

								CardRenderData.PatchView(RHICmdList, Scene, SharedView);
								Scene->UniformBuffers.LumenCardCaptureViewUniformBuffer.UpdateUniformBufferImmediate(*SharedView->CachedViewUniformShaderParameters);

								FGraphicsMinimalPipelineStateSet GraphicsMinimalPipelineStateSet;
								SubmitMeshDrawCommandsRange(
									LumenCardRenderer.MeshDrawCommands,
									GraphicsMinimalPipelineStateSet,
									PrimitiveIdVertexBuffer,
									0,
									false,
									CardRenderData.StartMeshDrawCommandIndex,
									CardRenderData.NumMeshDrawCommands,
									1,
									RHICmdList);
							}
						}
					}
				);
			}

			bool bAnyNaniteMeshes = false;

			for (FCardRenderData& CardRenderData : CardsToRender)
			{
				bAnyNaniteMeshes = bAnyNaniteMeshes || CardRenderData.NaniteInstanceIds.Num() > 0 || CardRenderData.CardData.bDistantScene;
				LumenCardRenderer.CardIdsToRender.Add(CardRenderData.CardIndex);
			}

			if (DoesPlatformSupportNanite(ShaderPlatform) && bAnyNaniteMeshes)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(NaniteMeshPass);
				QUICK_SCOPE_CYCLE_COUNTER(NaniteMeshPass);

				const FIntPoint DepthAtlasSize = LumenSceneData.DepthBufferAtlas->GetDesc().Extent;
				const FIntRect DepthAtlasRect = FIntRect(0, 0, DepthAtlasSize.X, DepthAtlasSize.Y);
				FRDGBufferSRVRef RectMinMaxBufferSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(RectMinMaxBuffer, PF_R32G32B32A32_UINT));

				Nanite::FRasterContext RasterContext = Nanite::InitRasterContext(
					GraphBuilder,
					DepthAtlasSize,
					Nanite::EOutputBufferMode::VisBuffer,
					true,
					RectMinMaxBufferSRV,
					NumRects);

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
					const uint32 NumCardsToRender = CardsToRender.Num();

					uint32 NextCardIndex = 0;
					while(NextCardIndex < NumCardsToRender)
					{
						TArray<Nanite::FPackedView, SceneRenderingAllocator> NaniteViews;
						TArray<Nanite::FInstanceDraw, SceneRenderingAllocator> NaniteInstanceDraws;

						while(NextCardIndex < NumCardsToRender && NaniteViews.Num() < MAX_VIEWS_PER_CULL_RASTERIZE_PASS)
						{
							const FCardRenderData& CardRenderData = CardsToRender[NextCardIndex];

							if(CardRenderData.NaniteInstanceIds.Num() > 0)
							{
								for(uint32 InstanceID : CardRenderData.NaniteInstanceIds)
								{
									NaniteInstanceDraws.Add(Nanite::FInstanceDraw { InstanceID, (uint32)NaniteViews.Num() });
								}

								Nanite::FPackedViewParams Params;
								Params.ViewMatrices = CardRenderData.ViewMatrices;
								Params.PrevViewMatrices = CardRenderData.ViewMatrices;
								Params.ViewRect = CardRenderData.GetAtlasAllocation();
								Params.RasterContextSize = DepthAtlasSize;
								Params.LODScaleFactor = CardRenderData.NaniteLODScaleFactor;
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
					for(FCardRenderData& CardRenderData : CardsToRender)
					{
						if(CardRenderData.NaniteInstanceIds.Num() > 0)
						{						
							TArray<Nanite::FInstanceDraw, SceneRenderingAllocator> NaniteInstanceDraws;
							for( uint32 InstanceID : CardRenderData.NaniteInstanceIds )
							{
								NaniteInstanceDraws.Add( Nanite::FInstanceDraw { InstanceID, 0u } );
							}
						
							CardRenderData.PatchView(GraphBuilder.RHICmdList, Scene, SharedView);
							Nanite::FPackedView PackedView = Nanite::CreatePackedViewFromViewInfo(*SharedView, DepthAtlasSize, 0);

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
				for (FCardRenderData& CardRenderData : CardsToRender)
				{
					if (CardRenderData.CardData.bDistantScene)
					{
						Nanite::FRasterState RasterState;
						RasterState.bNearClip = false;

						CardRenderData.PatchView(GraphBuilder.RHICmdList, Scene, SharedView);
						Nanite::FPackedView PackedView = Nanite::CreatePackedViewFromViewInfo(
							*SharedView,
							DepthAtlasSize,
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
					CardsToRender,
					CullingContext,
					RasterContext,
					PassUniformParameters,
					RectMinMaxBufferSRV,
					NumRects,
					LumenSceneData.MaxAtlasSize,
					AlbedoAtlasTexture,
					NormalAtlasTexture,
					EmissiveAtlasTexture,
					DepthAtlasTexture
				);
			}
		}

		{
			QUICK_SCOPE_CYCLE_COUNTER(UploadCardIndexBuffers);

			{
				FRDGBufferRef CardIndexBuffer = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateUploadDesc(sizeof(uint32), FMath::Max(LumenCardRenderer.CardIdsToRender.Num(), 1)),
					TEXT("CardsToRenderIndexBuffer"));

				FLumenCardIdUpload* PassParameters = GraphBuilder.AllocParameters<FLumenCardIdUpload>();
				PassParameters->CardIds = CardIndexBuffer;

				const uint32 CardIdBytes = LumenCardRenderer.CardIdsToRender.GetTypeSize() * LumenCardRenderer.CardIdsToRender.Num();
				const void* CardIdPtr = LumenCardRenderer.CardIdsToRender.GetData();

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("Upload CardsToRenderIndexBuffer NumIndices=%d", LumenCardRenderer.CardIdsToRender.Num()),
					PassParameters,
					ERDGPassFlags::Copy,
					[PassParameters, CardIdBytes, CardIdPtr](FRHICommandListImmediate& RHICmdList)
					{
						if (CardIdBytes > 0)
						{
							void* DestCardIdPtr = RHILockVertexBuffer(PassParameters->CardIds->GetRHI(), 0, CardIdBytes, RLM_WriteOnly);
							FPlatformMemory::Memcpy(DestCardIdPtr, CardIdPtr, CardIdBytes);
							RHIUnlockVertexBuffer(PassParameters->CardIds->GetRHI());
						}
					});

				ConvertToExternalBuffer(GraphBuilder, CardIndexBuffer, LumenCardRenderer.CardsToRenderIndexBuffer);
			}

			{
				const uint32 NumHashMapUInt32 = FLumenCardRenderer::NumCardsToRenderHashMapBucketUInt32;
				const uint32 NumHashMapBytes = 4 * NumHashMapUInt32;
				const uint32 NumHashMapBuckets = 32 * NumHashMapUInt32;

				FRDGBufferRef CardHashMapBuffer = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateUploadDesc(sizeof(uint32), NumHashMapUInt32),
					TEXT("CardsToRenderHashMapBuffer"));

				LumenCardRenderer.CardsToRenderHashMap.Init(0, NumHashMapBuckets);

				for (int32 CardIndex : LumenCardRenderer.CardIdsToRender)
				{
					LumenCardRenderer.CardsToRenderHashMap[CardIndex % NumHashMapBuckets] = 1;
				}

				FLumenCardIdUpload* PassParameters = GraphBuilder.AllocParameters<FLumenCardIdUpload>();
				PassParameters->CardIds = CardHashMapBuffer;

				const void* HashMapDataPtr = LumenCardRenderer.CardsToRenderHashMap.GetData();

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("Upload CardsToRenderHashMapBuffer NumUInt32=%d", NumHashMapUInt32),
					PassParameters,
					ERDGPassFlags::Copy,
					[PassParameters, NumHashMapBytes, HashMapDataPtr](FRHICommandListImmediate& RHICmdList)
					{
						if (NumHashMapBytes > 0)
						{
							void* DestCardIdPtr = RHILockVertexBuffer(PassParameters->CardIds->GetRHI(), 0, NumHashMapBytes, RLM_WriteOnly);
							FPlatformMemory::Memcpy(DestCardIdPtr, HashMapDataPtr, NumHashMapBytes);
							RHIUnlockVertexBuffer(PassParameters->CardIds->GetRHI());
						}
					});

				ConvertToExternalBuffer(GraphBuilder, CardHashMapBuffer, LumenCardRenderer.CardsToRenderHashMapBuffer);
			}

			{
				FRDGBufferRef VisibleCardsIndexBuffer = GraphBuilder.CreateBuffer(
					FRDGBufferDesc::CreateUploadDesc(sizeof(uint32), FMath::Max(LumenSceneData.VisibleCardsIndices.Num(), 1)),
					TEXT("VisibleCardsIndexBuffer"));

				FLumenCardIdUpload* PassParameters = GraphBuilder.AllocParameters<FLumenCardIdUpload>();
				PassParameters->CardIds = VisibleCardsIndexBuffer;

				const uint32 CardIdBytes = sizeof(uint32) * LumenSceneData.VisibleCardsIndices.Num();
				const void* CardIdPtr = LumenSceneData.VisibleCardsIndices.GetData();

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("Upload VisibleCardIndices NumIndices=%d", LumenSceneData.VisibleCardsIndices.Num()),
					PassParameters,
					ERDGPassFlags::Copy,
					[PassParameters, CardIdBytes, CardIdPtr](FRHICommandListImmediate& RHICmdList)
					{
						if (CardIdBytes > 0)
						{
							void* DestCardIdPtr = RHILockVertexBuffer(PassParameters->CardIds->GetRHI(), 0, CardIdBytes, RLM_WriteOnly);
							FPlatformMemory::Memcpy(DestCardIdPtr, CardIdPtr, CardIdBytes);
							RHIUnlockVertexBuffer(PassParameters->CardIds->GetRHI());
						}
					});

				ConvertToExternalBuffer(GraphBuilder, VisibleCardsIndexBuffer, LumenSceneData.VisibleCardsIndexBuffer);
			}
		}

		if (LumenCardRenderer.CardIdsToRender.Num() > 0)
		{
			PrefilterLumenSceneDepth(GraphBuilder, LumenCardRenderer.CardIdsToRender, Views[0]);
		}

		const float TimeElapsed = FPlatformTime::Seconds() - StartTime;

		if (TimeElapsed > .02f)
		{
			UE_LOG(LogRenderer, Log, TEXT("UpdateLumenScene %u Card Renders %.1fms"), CardsToRender.Num(), TimeElapsed * 1000.0f);
		}
	}

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;
	LumenSceneData.CardIndicesToUpdateInBuffer.Reset();
	LumenSceneData.CubeMapTreeIndicesToUpdateInBuffer.Reset();
	LumenSceneData.CubeMapTreeIndicesToAllocate.Reset();
	LumenSceneData.CubeMapIndicesToUpdateInBuffer.Reset();
	LumenSceneData.DFObjectIndicesToUpdateInBuffer.Reset();
}
