// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	TranslucentLighting.cpp: Translucent lighting implementation.
=============================================================================*/

#include "TranslucentLighting.h"
#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"
#include "EngineDefines.h"
#include "RHI.h"
#include "RenderResource.h"
#include "HitProxies.h"
#include "FinalPostProcessSettings.h"
#include "ShaderParameters.h"
#include "RendererInterface.h"
#include "PrimitiveViewRelevance.h"
#include "Shader.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "RHIStaticStates.h"
#include "SceneManagement.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Components/LightComponent.h"
#include "Materials/Material.h"
#include "PostProcess/SceneRenderTargets.h"
#include "LightSceneInfo.h"
#include "GlobalShader.h"
#include "MaterialShaderType.h"
#include "MaterialShader.h"
#include "MeshMaterialShaderType.h"
#include "MeshMaterialShader.h"
#include "ShadowRendering.h"
#include "SceneRendering.h"
#include "DeferredShadingRenderer.h"
#include "TranslucentRendering.h"
#include "ClearQuad.h"
#include "ScenePrivate.h"
#include "OneColorShader.h"
#include "LightRendering.h"
#include "ScreenRendering.h"
#include "AmbientCubemapParameters.h"
#include "VolumeRendering.h"
#include "VolumeLighting.h"
#include "PipelineStateCache.h"
#include "VisualizeTexture.h"
#include "MeshPassProcessor.inl"
#include "SkyAtmosphereRendering.h"
#include "VolumetricCloudRendering.h"

class FMaterial;

/** Whether to allow rendering translucency shadow depths. */
bool GUseTranslucencyShadowDepths = true;

DECLARE_GPU_STAT_NAMED(TranslucentLighting, TEXT("Translucent Lighting"));
 
int32 GUseTranslucentLightingVolumes = 1;
FAutoConsoleVariableRef CVarUseTranslucentLightingVolumes(
	TEXT("r.TranslucentLightingVolume"),
	GUseTranslucentLightingVolumes,
	TEXT("Whether to allow updating the translucent lighting volumes.\n")
	TEXT("0:off, otherwise on, default is 1"),
	ECVF_RenderThreadSafe
	);

float GTranslucentVolumeMinFOV = 45;
static FAutoConsoleVariableRef CVarTranslucentVolumeMinFOV(
	TEXT("r.TranslucentVolumeMinFOV"),
	GTranslucentVolumeMinFOV,
	TEXT("Minimum FOV for translucent lighting volume.  Prevents popping in lighting when zooming in."),
	ECVF_RenderThreadSafe
	);

float GTranslucentVolumeFOVSnapFactor = 10;
static FAutoConsoleVariableRef CTranslucentVolumeFOVSnapFactor(
	TEXT("r.TranslucentVolumeFOVSnapFactor"),
	GTranslucentVolumeFOVSnapFactor,
	TEXT("FOV will be snapped to a factor of this before computing volume bounds."),
	ECVF_RenderThreadSafe
	);

int32 GUseTranslucencyVolumeBlur = 1;
FAutoConsoleVariableRef CVarUseTranslucentLightingVolumeBlur(
	TEXT("r.TranslucencyVolumeBlur"),
	GUseTranslucencyVolumeBlur,
	TEXT("Whether to blur the translucent lighting volumes.\n")
	TEXT("0:off, otherwise on, default is 1"),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GTranslucencyLightingVolumeDim = 64;
FAutoConsoleVariableRef CVarTranslucencyLightingVolumeDim(
	TEXT("r.TranslucencyLightingVolumeDim"),
	GTranslucencyLightingVolumeDim,
	TEXT("Dimensions of the volume textures used for translucency lighting.  Larger textures result in higher resolution but lower performance."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

static TAutoConsoleVariable<float> CVarTranslucencyLightingVolumeInnerDistance(
	TEXT("r.TranslucencyLightingVolumeInnerDistance"),
	1500.0f,
	TEXT("Distance from the camera that the first volume cascade should end"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarTranslucencyLightingVolumeOuterDistance(
	TEXT("r.TranslucencyLightingVolumeOuterDistance"),
	5000.0f,
	TEXT("Distance from the camera that the second volume cascade should end"),
	ECVF_RenderThreadSafe);

/** Function returning current translucency lighting volume dimensions. */
int32 GetTranslucencyLightingVolumeDim()
{
	extern int32 GTranslucencyLightingVolumeDim;
	return FMath::Clamp(GTranslucencyLightingVolumeDim, 4, 2048);
}

void FViewInfo::CalcTranslucencyLightingVolumeBounds(FBox* InOutCascadeBoundsArray, int32 NumCascades) const
{
	for (int32 CascadeIndex = 0; CascadeIndex < NumCascades; CascadeIndex++)
	{
		float InnerDistance = CVarTranslucencyLightingVolumeInnerDistance.GetValueOnRenderThread();
		float OuterDistance = CVarTranslucencyLightingVolumeOuterDistance.GetValueOnRenderThread();

		const float FrustumStartDistance = CascadeIndex == 0 ? 0 : InnerDistance;
		const float FrustumEndDistance = CascadeIndex == 0 ? InnerDistance : OuterDistance;

		float FieldOfView = PI / 4.0f;
		float AspectRatio = 1.0f;

		if (IsPerspectiveProjection())
		{
			// Derive FOV and aspect ratio from the perspective projection matrix
			FieldOfView = FMath::Atan(1.0f / ShadowViewMatrices.GetProjectionMatrix().M[0][0]);
			// Clamp to prevent shimmering when zooming in
			FieldOfView = FMath::Max(FieldOfView, GTranslucentVolumeMinFOV * (float)PI / 180.0f);
			const float RoundFactorRadians = GTranslucentVolumeFOVSnapFactor * (float)PI / 180.0f;
			// Round up to a fixed factor
			// This causes the volume lighting to make discreet jumps as the FOV animates, instead of slowly crawling over a long period
			FieldOfView = FieldOfView + RoundFactorRadians - FMath::Fmod(FieldOfView, RoundFactorRadians);
			AspectRatio = ShadowViewMatrices.GetProjectionMatrix().M[1][1] / ShadowViewMatrices.GetProjectionMatrix().M[0][0];
		}

		const float StartHorizontalLength = FrustumStartDistance * FMath::Tan(FieldOfView);
		const FVector StartCameraRightOffset = ShadowViewMatrices.GetViewMatrix().GetColumn(0) * StartHorizontalLength;
		const float StartVerticalLength = StartHorizontalLength / AspectRatio;
		const FVector StartCameraUpOffset = ShadowViewMatrices.GetViewMatrix().GetColumn(1) * StartVerticalLength;

		const float EndHorizontalLength = FrustumEndDistance * FMath::Tan(FieldOfView);
		const FVector EndCameraRightOffset = ShadowViewMatrices.GetViewMatrix().GetColumn(0) * EndHorizontalLength;
		const float EndVerticalLength = EndHorizontalLength / AspectRatio;
		const FVector EndCameraUpOffset = ShadowViewMatrices.GetViewMatrix().GetColumn(1) * EndVerticalLength;

		FVector SplitVertices[8];
		const FVector ShadowViewOrigin = ShadowViewMatrices.GetViewOrigin();

		SplitVertices[0] = ShadowViewOrigin + GetViewDirection() * FrustumStartDistance + StartCameraRightOffset + StartCameraUpOffset;
		SplitVertices[1] = ShadowViewOrigin + GetViewDirection() * FrustumStartDistance + StartCameraRightOffset - StartCameraUpOffset;
		SplitVertices[2] = ShadowViewOrigin + GetViewDirection() * FrustumStartDistance - StartCameraRightOffset + StartCameraUpOffset;
		SplitVertices[3] = ShadowViewOrigin + GetViewDirection() * FrustumStartDistance - StartCameraRightOffset - StartCameraUpOffset;

		SplitVertices[4] = ShadowViewOrigin + GetViewDirection() * FrustumEndDistance + EndCameraRightOffset + EndCameraUpOffset;
		SplitVertices[5] = ShadowViewOrigin + GetViewDirection() * FrustumEndDistance + EndCameraRightOffset - EndCameraUpOffset;
		SplitVertices[6] = ShadowViewOrigin + GetViewDirection() * FrustumEndDistance - EndCameraRightOffset + EndCameraUpOffset;
		SplitVertices[7] = ShadowViewOrigin + GetViewDirection() * FrustumEndDistance - EndCameraRightOffset - EndCameraUpOffset;

		FVector Center(0,0,0);
		// Weight the far vertices more so that the bounding sphere will be further from the camera
		// This minimizes wasted shadowmap space behind the viewer
		const float FarVertexWeightScale = 10.0f;
		for (int32 VertexIndex = 0; VertexIndex < 8; VertexIndex++)
		{
			const float Weight = VertexIndex > 3 ? 1 / (4.0f + 4.0f / FarVertexWeightScale) : 1 / (4.0f + 4.0f * FarVertexWeightScale);
			Center += SplitVertices[VertexIndex] * Weight;
		}

		float RadiusSquared = 0;
		for (int32 VertexIndex = 0; VertexIndex < 8; VertexIndex++)
		{
			RadiusSquared = FMath::Max(RadiusSquared, (Center - SplitVertices[VertexIndex]).SizeSquared());
		}

		FSphere SphereBounds(Center, FMath::Sqrt(RadiusSquared));

		// Snap the center to a multiple of the volume dimension for stability
		const int32 TranslucencyLightingVolumeDim = GetTranslucencyLightingVolumeDim();
		SphereBounds.Center.X = SphereBounds.Center.X - FMath::Fmod(SphereBounds.Center.X, SphereBounds.W * 2 / TranslucencyLightingVolumeDim);
		SphereBounds.Center.Y = SphereBounds.Center.Y - FMath::Fmod(SphereBounds.Center.Y, SphereBounds.W * 2 / TranslucencyLightingVolumeDim);
		SphereBounds.Center.Z = SphereBounds.Center.Z - FMath::Fmod(SphereBounds.Center.Z, SphereBounds.W * 2 / TranslucencyLightingVolumeDim);

		InOutCascadeBoundsArray[CascadeIndex] = FBox(SphereBounds.Center - SphereBounds.W, SphereBounds.Center + SphereBounds.W);
	}
}

class FTranslucencyDepthShaderElementData : public FMeshMaterialShaderElementData
{
public:
	float TranslucentShadowStartOffset;
};

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FTranslucencyDepthPassUniformParameters,)
	SHADER_PARAMETER_STRUCT(FSceneTextureUniformParameters, SceneTextures)
	SHADER_PARAMETER(FMatrix, ProjectionMatrix)
	SHADER_PARAMETER(float, bClampToNearPlane)
	SHADER_PARAMETER(float, InvMaxSubjectDepth)
	SHADER_PARAMETER_STRUCT(FTranslucentSelfShadowUniformParameters, TranslucentSelfShadow)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FTranslucencyDepthPassUniformParameters, "TranslucentDepthPass", SceneTextures);

void SetupTranslucencyDepthPassUniformBuffer(
	const FProjectedShadowInfo* ShadowInfo,
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FTranslucencyDepthPassUniformParameters& TranslucencyDepthPassParameters)
{
	// Note - scene depth can be bound by the material for use in depth fades
	// This is incorrect when rendering a shadowmap as it's not from the camera's POV
	// Set the scene depth texture to something safe when rendering shadow depths
	SetupSceneTextureUniformParameters(GraphBuilder, View.FeatureLevel, ESceneTextureSetupMode::None, TranslucencyDepthPassParameters.SceneTextures);

	TranslucencyDepthPassParameters.ProjectionMatrix = FTranslationMatrix(ShadowInfo->PreShadowTranslation - View.ViewMatrices.GetPreViewTranslation()) * ShadowInfo->TranslatedWorldToClipInnerMatrix;

	// Only clamp vertices to the near plane when rendering whole scene directional light shadow depths or preshadows from directional lights
	const bool bClampToNearPlaneValue = ShadowInfo->IsWholeSceneDirectionalShadow() || (ShadowInfo->bPreShadow && ShadowInfo->bDirectionalLight);
	TranslucencyDepthPassParameters.bClampToNearPlane = bClampToNearPlaneValue ? 1.0f : 0.0f;

	TranslucencyDepthPassParameters.InvMaxSubjectDepth = ShadowInfo->InvMaxSubjectDepth;

	SetupTranslucentSelfShadowUniformParameters(ShadowInfo, TranslucencyDepthPassParameters.TranslucentSelfShadow);
}

/**
* Vertex shader used to render shadow maps for translucency.
*/
class FTranslucencyShadowDepthVS : public FMeshMaterialShader
{
	DECLARE_INLINE_TYPE_LAYOUT(FTranslucencyShadowDepthVS, NonVirtual);
public:

	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return IsTranslucentBlendMode(Parameters.MaterialParameters.BlendMode) && IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	FTranslucencyShadowDepthVS() {}
	FTranslucencyShadowDepthVS(const FMeshMaterialShaderType::CompiledShaderInitializerType& Initializer) :
		FMeshMaterialShader(Initializer)
	{}
};

enum ETranslucencyShadowDepthShaderMode
{
	TranslucencyShadowDepth_PerspectiveCorrect,
	TranslucencyShadowDepth_Standard,
};

template <ETranslucencyShadowDepthShaderMode ShaderMode>
class TTranslucencyShadowDepthVS : public FTranslucencyShadowDepthVS
{
	DECLARE_SHADER_TYPE(TTranslucencyShadowDepthVS, MeshMaterial);
public:

	TTranslucencyShadowDepthVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FTranslucencyShadowDepthVS(Initializer)
	{}

	TTranslucencyShadowDepthVS() {}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FTranslucencyShadowDepthVS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("PERSPECTIVE_CORRECT_DEPTH"), (uint32)(ShaderMode == TranslucencyShadowDepth_PerspectiveCorrect ? 1 : 0));
	}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TTranslucencyShadowDepthVS<TranslucencyShadowDepth_PerspectiveCorrect>,TEXT("/Engine/Private/TranslucentShadowDepthShaders.usf"),TEXT("MainVS"),SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TTranslucencyShadowDepthVS<TranslucencyShadowDepth_Standard>,TEXT("/Engine/Private/TranslucentShadowDepthShaders.usf"),TEXT("MainVS"),SF_Vertex);

/**
 * Pixel shader used for accumulating translucency layer densities
 */
class FTranslucencyShadowDepthPS : public FMeshMaterialShader
{
	DECLARE_INLINE_TYPE_LAYOUT(FTranslucencyShadowDepthPS, NonVirtual);
public:
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
	{
		return IsTranslucentBlendMode(Parameters.MaterialParameters.BlendMode) && IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	FTranslucencyShadowDepthPS() = default;
	FTranslucencyShadowDepthPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FMeshMaterialShader(Initializer)
	{
		TranslucentShadowStartOffset.Bind(Initializer.ParameterMap, TEXT("TranslucentShadowStartOffset"));
	}

	void GetShaderBindings(
		const FScene* Scene,
		ERHIFeatureLevel::Type FeatureLevel,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material,
		const FMeshPassProcessorRenderState& DrawRenderState,
		const FTranslucencyDepthShaderElementData& ShaderElementData,
		FMeshDrawSingleShaderBindings& ShaderBindings) const
	{
		FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, PrimitiveSceneProxy, MaterialRenderProxy, Material, DrawRenderState, ShaderElementData, ShaderBindings);

		ShaderBindings.Add(TranslucentShadowStartOffset, ShaderElementData.TranslucentShadowStartOffset);
	}

private:
	LAYOUT_FIELD(FShaderParameter, TranslucentShadowStartOffset);
};

template <ETranslucencyShadowDepthShaderMode ShaderMode>
class TTranslucencyShadowDepthPS : public FTranslucencyShadowDepthPS
{
public:
	DECLARE_SHADER_TYPE(TTranslucencyShadowDepthPS, MeshMaterial);

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FTranslucencyShadowDepthPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("PERSPECTIVE_CORRECT_DEPTH"), (uint32)(ShaderMode == TranslucencyShadowDepth_PerspectiveCorrect ? 1 : 0));
	}

	TTranslucencyShadowDepthPS() = default;
	TTranslucencyShadowDepthPS(const ShaderMetaType::CompiledShaderInitializerType & Initializer) :
		FTranslucencyShadowDepthPS(Initializer)
	{}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TTranslucencyShadowDepthPS<TranslucencyShadowDepth_PerspectiveCorrect>,TEXT("/Engine/Private/TranslucentShadowDepthShaders.usf"),TEXT("MainOpacityPS"),SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TTranslucencyShadowDepthPS<TranslucencyShadowDepth_Standard>,TEXT("/Engine/Private/TranslucentShadowDepthShaders.usf"),TEXT("MainOpacityPS"),SF_Pixel);

class FTranslucencyDepthPassMeshProcessor : public FMeshPassProcessor
{
public:
	FTranslucencyDepthPassMeshProcessor(const FScene* Scene,
		const FSceneView* InViewIfDynamicMeshCommand,
		const FMeshPassProcessorRenderState& InPassDrawRenderState,
		const FProjectedShadowInfo* InShadowInfo,
		FMeshPassDrawListContext* InDrawListContext);

	virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override final;

private:
	bool TryAddMeshBatch(
		const FMeshBatch& RESTRICT MeshBatch, 
		uint64 BatchElementMask, 
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, 
		int32 StaticMeshId,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& Material);

	template<ETranslucencyShadowDepthShaderMode ShaderMode>
	bool Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		int32 StaticMeshId,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
		const FMaterial& RESTRICT MaterialResource,
		float MaterialTranslucentShadowStartOffset,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode);

	FMeshPassProcessorRenderState PassDrawRenderState;
	const FProjectedShadowInfo* ShadowInfo;
	FShadowDepthType ShadowDepthType;
	const bool bDirectionalLight;
};

FTranslucencyDepthPassMeshProcessor::FTranslucencyDepthPassMeshProcessor(const FScene* Scene,
	const FSceneView* InViewIfDynamicMeshCommand,
	const FMeshPassProcessorRenderState& InPassDrawRenderState,
	const FProjectedShadowInfo* InShadowInfo,
	FMeshPassDrawListContext* InDrawListContext)
	: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
	, PassDrawRenderState(InPassDrawRenderState)
	, ShadowInfo(InShadowInfo)
	, ShadowDepthType(InShadowInfo->GetShadowDepthType())
	, bDirectionalLight(InShadowInfo->bDirectionalLight)
{
}

bool FTranslucencyDepthPassMeshProcessor::TryAddMeshBatch(
	const FMeshBatch& RESTRICT MeshBatch, 
	uint64 BatchElementMask, 
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, 
	int32 StaticMeshId,
	const FMaterialRenderProxy& MaterialRenderProxy,
	const FMaterial& Material)
{
	// Determine the mesh's material and blend mode.
	const EBlendMode BlendMode = Material.GetBlendMode();
	const float MaterialTranslucentShadowStartOffset = Material.GetTranslucentShadowStartOffset();
	const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
	const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material, OverrideSettings);
	const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, Material, OverrideSettings);
	const bool bIsTranslucent = IsTranslucentBlendMode(BlendMode);

	// Only render translucent meshes into the Fourier opacity maps
	if (bIsTranslucent && ShouldIncludeDomainInMeshPass(Material.GetMaterialDomain()))
	{
		if (bDirectionalLight)
		{
			return Process<TranslucencyShadowDepth_Standard>(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, MaterialRenderProxy, Material, MaterialTranslucentShadowStartOffset, MeshFillMode, MeshCullMode);
		}
		else
		{
			return Process<TranslucencyShadowDepth_PerspectiveCorrect>(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, MaterialRenderProxy, Material, MaterialTranslucentShadowStartOffset, MeshFillMode, MeshCullMode);
		}
	}

	return true;
}

template<ETranslucencyShadowDepthShaderMode ShaderMode>
bool FTranslucencyDepthPassMeshProcessor::Process(
	const FMeshBatch& RESTRICT MeshBatch,
	uint64 BatchElementMask,
	int32 StaticMeshId,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	const FMaterialRenderProxy& RESTRICT MaterialRenderProxy,
	const FMaterial& RESTRICT MaterialResource,
	float MaterialTranslucentShadowStartOffset,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	TMeshProcessorShaders<
		TTranslucencyShadowDepthVS<ShaderMode>,
		TTranslucencyShadowDepthPS<ShaderMode>> PassShaders;

	FMaterialShaderTypes ShaderTypes;
	ShaderTypes.AddShaderType<TTranslucencyShadowDepthVS<ShaderMode>>();
	ShaderTypes.AddShaderType<TTranslucencyShadowDepthPS<ShaderMode>>();

	FVertexFactoryType* VertexFactoryType = VertexFactory->GetType();

	FMaterialShaders Shaders;
	if (!MaterialResource.TryGetShaders(ShaderTypes, VertexFactoryType, Shaders))
	{
		return false;
	}

	Shaders.TryGetVertexShader(PassShaders.VertexShader);
	Shaders.TryGetPixelShader(PassShaders.PixelShader);

	FMeshPassProcessorRenderState DrawRenderState(PassDrawRenderState);

	FTranslucencyDepthShaderElementData ShaderElementData;
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

	const float LocalToWorldScale = ShadowInfo->GetParentSceneInfo()->Proxy->GetLocalToWorld().GetScaleVector().GetMax();
	const float TranslucentShadowStartOffsetValue = MaterialTranslucentShadowStartOffset * LocalToWorldScale;
	ShaderElementData.TranslucentShadowStartOffset = TranslucentShadowStartOffsetValue / (ShadowInfo->MaxSubjectZ - ShadowInfo->MinSubjectZ);

	const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(PassShaders.VertexShader, PassShaders.PixelShader);

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		DrawRenderState,
		PassShaders,
		MeshFillMode,
		MeshCullMode,
		SortKey,
		EMeshPassFeatures::Default,
		ShaderElementData);

	return true;
}

void FTranslucencyDepthPassMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	if (MeshBatch.CastShadow)
	{
		const FMaterialRenderProxy* MaterialRenderProxy = MeshBatch.MaterialRenderProxy;
		while (MaterialRenderProxy)
		{
			const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
			if (Material && Material->GetRenderingThreadShaderMap())
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

BEGIN_SHADER_PARAMETER_STRUCT(FTranslucencyDepthPassParameters, )
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FTranslucencyDepthPassUniformParameters, PassUniformBuffer)
	SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void FProjectedShadowInfo::RenderTranslucencyDepths(FRDGBuilder& GraphBuilder, FSceneRenderer* SceneRenderer, const FRenderTargetBindingSlots& InRenderTargets, FInstanceCullingManager& InstanceCullingManager)
{
	check(IsInRenderingThread());
	checkSlow(!bWholeSceneShadow);
	SCOPE_CYCLE_COUNTER(STAT_RenderPerObjectShadowDepthsTime);

	BeginRenderView(GraphBuilder, SceneRenderer->Scene);

	auto* TranslucencyDepthPassParameters = GraphBuilder.AllocParameters<FTranslucencyDepthPassUniformParameters>();
	SetupTranslucencyDepthPassUniformBuffer(this, GraphBuilder, *ShadowDepthView, *TranslucencyDepthPassParameters);
	TRDGUniformBufferRef<FTranslucencyDepthPassUniformParameters> PassUniformBuffer = GraphBuilder.CreateUniformBuffer(TranslucencyDepthPassParameters);

	auto* PassParameters = GraphBuilder.AllocParameters<FTranslucencyDepthPassParameters>();
	PassParameters->View = ShadowDepthView->ViewUniformBuffer;
	PassParameters->PassUniformBuffer = PassUniformBuffer;
	PassParameters->RenderTargets = InRenderTargets;

	FSimpleMeshDrawCommandPass* SimpleMeshDrawCommandPass = GraphBuilder.AllocObject<FSimpleMeshDrawCommandPass>(*ShadowDepthView, &InstanceCullingManager);

	FMeshPassProcessorRenderState DrawRenderState;
	DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
	DrawRenderState.SetBlendState(TStaticBlendState<
		CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One,
		CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI());


	FTranslucencyDepthPassMeshProcessor TranslucencyDepthPassMeshProcessor(
		SceneRenderer->Scene,
		ShadowDepthView,
		DrawRenderState,
		this,
		SimpleMeshDrawCommandPass->GetDynamicPassMeshDrawListContext());

	for (int32 MeshBatchIndex = 0; MeshBatchIndex < DynamicSubjectTranslucentMeshElements.Num(); MeshBatchIndex++)
	{
		const FMeshBatchAndRelevance& MeshAndRelevance = DynamicSubjectTranslucentMeshElements[MeshBatchIndex];
		const uint64 BatchElementMask = ~0ull;
		TranslucencyDepthPassMeshProcessor.AddMeshBatch(*MeshAndRelevance.Mesh, BatchElementMask, MeshAndRelevance.PrimitiveSceneProxy);
	}

	for (int32 PrimitiveIndex = 0; PrimitiveIndex < SubjectTranslucentPrimitives.Num(); PrimitiveIndex++)
	{
		const FPrimitiveSceneInfo* PrimitiveSceneInfo = SubjectTranslucentPrimitives[PrimitiveIndex];
		int32 PrimitiveId = PrimitiveSceneInfo->GetIndex();
		FPrimitiveViewRelevance ViewRelevance = ShadowDepthView->PrimitiveViewRelevanceMap[PrimitiveId];

		if (!ViewRelevance.bInitializedThisFrame)
		{
			// Compute the subject primitive's view relevance since it wasn't cached
			ViewRelevance = PrimitiveSceneInfo->Proxy->GetViewRelevance(ShadowDepthView);
		}

		if (ViewRelevance.bDrawRelevance && ViewRelevance.bStaticRelevance)
		{
			for (int32 MeshIndex = 0; MeshIndex < PrimitiveSceneInfo->StaticMeshes.Num(); MeshIndex++)
			{
				const FStaticMeshBatch& StaticMeshBatch = PrimitiveSceneInfo->StaticMeshes[MeshIndex];
				const uint64 DefaultBatchElementMask = ~0ul;
				TranslucencyDepthPassMeshProcessor.AddMeshBatch(StaticMeshBatch, DefaultBatchElementMask, StaticMeshBatch.PrimitiveSceneInfo->Proxy, StaticMeshBatch.Id);
			}
		}
	}

	SimpleMeshDrawCommandPass->BuildRenderingCommands(GraphBuilder, *ShadowDepthView, SceneRenderer->Scene->GPUScene, PassParameters->InstanceCullingDrawParams);


	FString EventName;
#if WANTS_DRAW_MESH_EVENTS
	if (GetEmitDrawEvents())
	{
		GetShadowTypeNameForDrawEvent(EventName);
	}
#endif

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("%s", *EventName),
		PassParameters,
		ERDGPassFlags::Raster,
		[this, SimpleMeshDrawCommandPass, PassParameters](FRHICommandListImmediate& RHICmdList)
	{
		FMeshPassProcessorRenderState DrawRenderState;

		// Clear the shadow and its border
		RHICmdList.SetViewport(
			X,
			Y,
			0.0f,
			(X + BorderSize * 2 + ResolutionX),
			(Y + BorderSize * 2 + ResolutionY),
			1.0f
		);

		FLinearColor ClearColors[2] = { FLinearColor(0,0,0,0), FLinearColor(0,0,0,0) };
		DrawClearQuadMRT(RHICmdList, true, UE_ARRAY_COUNT(ClearColors), ClearColors, false, 1.0f, false, 0);

		// Set the viewport for the shadow.
		RHICmdList.SetViewport(
			(X + BorderSize),
			(Y + BorderSize),
			0.0f,
			(X + BorderSize + ResolutionX),
			(Y + BorderSize + ResolutionY),
			1.0f
		);
		SimpleMeshDrawCommandPass->SubmitDraw(RHICmdList, PassParameters->InstanceCullingDrawParams);
	});
}

/** Pixel shader used to filter a single volume lighting cascade. */
class FFilterTranslucentVolumePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FFilterTranslucentVolumePS);
	SHADER_USE_PARAMETER_STRUCT(FFilterTranslucentVolumePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, TranslucencyLightingVolumeAmbient)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, TranslucencyLightingVolumeDirectional)
		SHADER_PARAMETER_SAMPLER(SamplerState, TranslucencyLightingVolumeAmbientSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, TranslucencyLightingVolumeDirectionalSampler)
		SHADER_PARAMETER(float, TexelSize)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) 
	{ 
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && (RHISupportsGeometryShaders(Parameters.Platform) || RHISupportsVertexShaderLayer(Parameters.Platform));
	}
};

IMPLEMENT_GLOBAL_SHADER(FFilterTranslucentVolumePS, "/Engine/Private/TranslucentLightingShaders.usf", "FilterMainPS", SF_Pixel);

/** Shader parameters needed to inject direct lighting into a volume. */
class FTranslucentInjectParameters
{
	DECLARE_INLINE_TYPE_LAYOUT(FTranslucentInjectParameters, NonVirtual);
public:

	void Bind(const FShaderParameterMap& ParameterMap)
	{
		WorldToShadowMatrix.Bind(ParameterMap,TEXT("WorldToShadowMatrix"));
		ShadowmapMinMax.Bind(ParameterMap,TEXT("ShadowmapMinMax"));
		VolumeCascadeIndex.Bind(ParameterMap,TEXT("VolumeCascadeIndex"));
	}

	template<typename ShaderRHIParamRef>
	void Set(
		FRHICommandList& RHICmdList, 
		const ShaderRHIParamRef ShaderRHI, 
		FShader* Shader, 
		const FViewInfo& View, 
		const FLightSceneInfo* LightSceneInfo, 
		const FProjectedShadowInfo* ShadowMap, 
		uint32 VolumeCascadeIndexValue,
		bool bDynamicallyShadowed) const
	{
		SetDeferredLightParameters(RHICmdList, ShaderRHI, Shader->GetUniformBufferParameter<FDeferredLightUniformStruct>(), LightSceneInfo, View);

		if (bDynamicallyShadowed)
		{
			FVector4 ShadowmapMinMaxValue;
			FMatrix WorldToShadowMatrixValue = ShadowMap->GetWorldToShadowMatrix(ShadowmapMinMaxValue);

			SetShaderValue(RHICmdList, ShaderRHI, WorldToShadowMatrix, WorldToShadowMatrixValue);
			SetShaderValue(RHICmdList, ShaderRHI, ShadowmapMinMax, ShadowmapMinMaxValue);
		}

		SetShaderValue(RHICmdList, ShaderRHI, VolumeCascadeIndex, VolumeCascadeIndexValue);
	}

private:
	LAYOUT_FIELD(FShaderParameter, WorldToShadowMatrix)
	LAYOUT_FIELD(FShaderParameter, ShadowmapMinMax)
	LAYOUT_FIELD(FShaderParameter, VolumeCascadeIndex)
};

/** Shader that adds direct lighting contribution from the given light to the current volume lighting cascade. */
class FTranslucentLightingInjectPS : public FMaterialShader
{
	DECLARE_SHADER_TYPE(FTranslucentLightingInjectPS,Material);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualShadowMapSamplingParameters, VirtualShadowMapSamplingParameters)
	END_SHADER_PARAMETER_STRUCT()

	class FRadialAttenuation	: SHADER_PERMUTATION_BOOL("RADIAL_ATTENUATION");
	class FDynamicallyShadowed	: SHADER_PERMUTATION_BOOL("DYNAMICALLY_SHADOWED");
	class FLightFunction		: SHADER_PERMUTATION_BOOL("APPLY_LIGHT_FUNCTION");
	class FInverseSquared		: SHADER_PERMUTATION_BOOL("INVERSE_SQUARED_FALLOFF");
	class FVirtualShadowMap		: SHADER_PERMUTATION_BOOL("VIRTUAL_SHADOW_MAP");

	using FPermutationDomain = TShaderPermutationDomain<
		FRadialAttenuation,
		FDynamicallyShadowed,
		FLightFunction,
		FInverseSquared,
		FVirtualShadowMap >;

public:

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment )
	{
		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("INJECTION_PIXEL_SHADER"), 1);
	}

	/**
	  * Makes sure only shaders for materials that are explicitly flagged
	  * as 'UsedAsLightFunction' in the Material Editor gets compiled into
	  * the shader cache.
	  */
	static bool ShouldCompilePermutation(const FMaterialShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if( !PermutationVector.Get<FRadialAttenuation>() && !PermutationVector.Get<FInverseSquared>() )
		{
			return false;
		}

		return (Parameters.MaterialParameters.MaterialDomain == MD_LightFunction || Parameters.MaterialParameters.bIsSpecialEngineMaterial) &&
			(IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) &&
			(RHISupportsGeometryShaders(Parameters.Platform) || RHISupportsVertexShaderLayer(Parameters.Platform)));
	}

	FTranslucentLightingInjectPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FMaterialShader(Initializer)
	{
		Bindings.BindForLegacyShaderParameters( 
			this, 
			Initializer.PermutationId, 
			Initializer.ParameterMap,
			*FParameters::FTypeInfo::GetStructMetadata(), 
			// Don't require full bindings, we use FMaterialShader::SetParameters
			false); 

		VolumeShadowingParameters.Bind(Initializer.ParameterMap);
		SpotlightMask.Bind(Initializer.ParameterMap, TEXT("SpotlightMask"));
		LightFunctionParameters.Bind(Initializer.ParameterMap);
		TranslucentInjectParameters.Bind(Initializer.ParameterMap);
		LightFunctionWorldToLight.Bind(Initializer.ParameterMap, TEXT("LightFunctionWorldToLight"));
		VirtualShadowMapIdParameter.Bind(Initializer.ParameterMap, TEXT("VirtualShadowMapId"));

		VolumetricCloudWorldToLightClipShadowMatrix.Bind(Initializer.ParameterMap, TEXT("VolumetricCloudWorldToLightClipShadowMatrix"));
		VolumetricCloudShadowmapFarDepthKm.Bind(Initializer.ParameterMap, TEXT("VolumetricCloudShadowmapFarDepthKm"));
		VolumetricCloudShadowEnabled.Bind(Initializer.ParameterMap, TEXT("VolumetricCloudShadowEnabled"));
		VolumetricCloudShadowmapStrength.Bind(Initializer.ParameterMap, TEXT("VolumetricCloudShadowmapStrength"));
		VolumetricCloudShadowmapTexture.Bind(Initializer.ParameterMap, TEXT("VolumetricCloudShadowmapTexture"));
		VolumetricCloudShadowmapTextureSampler.Bind(Initializer.ParameterMap, TEXT("VolumetricCloudShadowmapTextureSampler"));
		AtmospherePerPixelTransmittanceEnabled.Bind(Initializer.ParameterMap, TEXT("AtmospherePerPixelTransmittanceEnabled"));
	}
	FTranslucentLightingInjectPS() {}

	// @param InnerSplitIndex which CSM shadow map level, INDEX_NONE if no directional light
	// @param VolumeCascadeIndexValue which volume we render to
	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FViewInfo& View, 
		const FLightSceneInfo* LightSceneInfo, 
		const FMaterialRenderProxy* MaterialProxy, 
		const FProjectedShadowInfo* ShadowMap, 
		int32 InnerSplitIndex, 
		int32 VolumeCascadeIndexValue,
		int32 VirtualShadowMapId)
	{
		bool bDynamicallyShadowed = ShadowMap != nullptr;
		
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();

		const FMaterial& Material = MaterialProxy->GetMaterialWithFallback(View.GetFeatureLevel(), MaterialProxy);
		FMaterialShader::SetViewParameters(RHICmdList, ShaderRHI, View, View.ViewUniformBuffer);
		FMaterialShader::SetParameters(RHICmdList, ShaderRHI, MaterialProxy, Material, View);
		
		VolumeShadowingParameters.Set(RHICmdList, ShaderRHI, View, LightSceneInfo, ShadowMap, InnerSplitIndex, bDynamicallyShadowed);

		bool bIsSpotlight = LightSceneInfo->Proxy->GetLightType() == LightType_Spot;
		//@todo - needs to be a permutation to reduce shadow filtering work
		SetShaderValue(RHICmdList, ShaderRHI, SpotlightMask, (bIsSpotlight ? 1.0f : 0.0f));

		LightFunctionParameters.Set(RHICmdList, ShaderRHI, LightSceneInfo, 1);
		TranslucentInjectParameters.Set(RHICmdList, ShaderRHI, this, View, LightSceneInfo, ShadowMap, VolumeCascadeIndexValue, bDynamicallyShadowed);

		if (LightFunctionWorldToLight.IsBound())
		{
			const FVector Scale = LightSceneInfo->Proxy->GetLightFunctionScale();
			// Switch x and z so that z of the user specified scale affects the distance along the light direction
			const FVector InverseScale = FVector( 1.f / Scale.Z, 1.f / Scale.Y, 1.f / Scale.X );
			const FMatrix WorldToLight = LightSceneInfo->Proxy->GetWorldToLight() * FScaleMatrix(FVector(InverseScale));	

			SetShaderValue(RHICmdList, ShaderRHI, LightFunctionWorldToLight, WorldToLight);
		}

		SetShaderValue(RHICmdList, ShaderRHI, VirtualShadowMapIdParameter, VirtualShadowMapId);

		FLightSceneProxy* AtmosphereLight0Proxy = LightSceneInfo->Scene->AtmosphereLights[0] ? LightSceneInfo->Scene->AtmosphereLights[0]->Proxy : nullptr;
		FLightSceneProxy* AtmosphereLight1Proxy = LightSceneInfo->Scene->AtmosphereLights[1] ? LightSceneInfo->Scene->AtmosphereLights[1]->Proxy : nullptr;

		if (VolumetricCloudShadowmapTexture.IsBound())
		{
			FVolumetricCloudRenderSceneInfo* CloudInfo = LightSceneInfo->Scene->GetVolumetricCloudSceneInfo();

			const bool bLight0CloudPerPixelTransmittance = CloudInfo && View.VolumetricCloudShadowRenderTarget[0] != nullptr && AtmosphereLight0Proxy && AtmosphereLight0Proxy == LightSceneInfo->Proxy;
			const bool bLight1CloudPerPixelTransmittance = CloudInfo && View.VolumetricCloudShadowRenderTarget[1] != nullptr && AtmosphereLight1Proxy && AtmosphereLight1Proxy == LightSceneInfo->Proxy;

			if (bLight0CloudPerPixelTransmittance || bLight1CloudPerPixelTransmittance)
			{
				uint32 LightIndex = bLight1CloudPerPixelTransmittance ? 1 : 0;
				SetShaderValue(RHICmdList, ShaderRHI, VolumetricCloudShadowEnabled, 1);
				SetShaderValue(RHICmdList, ShaderRHI, VolumetricCloudWorldToLightClipShadowMatrix, CloudInfo->GetVolumetricCloudCommonShaderParameters().CloudShadowmapWorldToLightClipMatrix[LightIndex]);
				SetShaderValue(RHICmdList, ShaderRHI, VolumetricCloudShadowmapFarDepthKm, CloudInfo->GetVolumetricCloudCommonShaderParameters().CloudShadowmapFarDepthKm[LightIndex].X);
				SetTextureParameter(
					RHICmdList,
					ShaderRHI,
					VolumetricCloudShadowmapTexture,
					VolumetricCloudShadowmapTextureSampler,
					TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),
					View.VolumetricCloudShadowExtractedRenderTarget[LightIndex]->GetShaderResourceRHI());

				if (bLight0CloudPerPixelTransmittance)
				{
					SetShaderValue(RHICmdList, ShaderRHI, VolumetricCloudShadowmapStrength, AtmosphereLight0Proxy->GetCloudShadowOnAtmosphereStrength());
				}
				else if(bLight1CloudPerPixelTransmittance)
				{
					SetShaderValue(RHICmdList, ShaderRHI, VolumetricCloudShadowmapStrength, AtmosphereLight1Proxy->GetCloudShadowOnAtmosphereStrength());
				}
			}
			else
			{
				SetShaderValue(RHICmdList, ShaderRHI, VolumetricCloudShadowEnabled, 0);
				SetShaderValue(RHICmdList, ShaderRHI, VolumetricCloudWorldToLightClipShadowMatrix, FMatrix::Identity);
				SetShaderValue(RHICmdList, ShaderRHI, VolumetricCloudShadowmapFarDepthKm, 1.0f);
				SetTextureParameter(
					RHICmdList,
					ShaderRHI,
					VolumetricCloudShadowmapTexture,
					VolumetricCloudShadowmapTextureSampler,
					TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),
					GBlackTexture->TextureRHI);
				SetShaderValue(RHICmdList, ShaderRHI, VolumetricCloudShadowmapStrength, 0.0f);
			}
		}
		
		const bool bLightAtmospherePerPixelTransmittance = ShouldRenderSkyAtmosphere(LightSceneInfo->Scene, View.Family->EngineShowFlags) &&
														(  (AtmosphereLight0Proxy == LightSceneInfo->Proxy && AtmosphereLight0Proxy && AtmosphereLight0Proxy->GetUsePerPixelAtmosphereTransmittance())
														|| (AtmosphereLight1Proxy == LightSceneInfo->Proxy && AtmosphereLight1Proxy && AtmosphereLight1Proxy->GetUsePerPixelAtmosphereTransmittance()));
		SetShaderValue(RHICmdList, ShaderRHI, AtmospherePerPixelTransmittanceEnabled, bLightAtmospherePerPixelTransmittance ? 1 : 0);
	}

private:
	LAYOUT_FIELD(FVolumeShadowingParameters, VolumeShadowingParameters);
	LAYOUT_FIELD(FShaderParameter, SpotlightMask);
	LAYOUT_FIELD(FLightFunctionSharedParameters, LightFunctionParameters);
	LAYOUT_FIELD(FTranslucentInjectParameters, TranslucentInjectParameters);
	LAYOUT_FIELD(FShaderParameter, LightFunctionWorldToLight);
	LAYOUT_FIELD(FShaderParameter, VirtualShadowMapIdParameter);

	LAYOUT_FIELD(FShaderParameter, VolumetricCloudWorldToLightClipShadowMatrix);
	LAYOUT_FIELD(FShaderParameter, VolumetricCloudShadowmapFarDepthKm);
	LAYOUT_FIELD(FShaderParameter, VolumetricCloudShadowEnabled);
	LAYOUT_FIELD(FShaderParameter, VolumetricCloudShadowmapStrength);
	LAYOUT_FIELD(FShaderResourceParameter, VolumetricCloudShadowmapTexture);
	LAYOUT_FIELD(FShaderResourceParameter, VolumetricCloudShadowmapTextureSampler);
	LAYOUT_FIELD(FShaderParameter, AtmospherePerPixelTransmittanceEnabled);
};

IMPLEMENT_MATERIAL_SHADER_TYPE(,FTranslucentLightingInjectPS, TEXT("/Engine/Private/TranslucentLightInjectionShaders.usf"), TEXT("InjectMainPS"), SF_Pixel);

class FClearTranslucentLightingVolumeCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FClearTranslucentLightingVolumeCS);
	SHADER_USE_PARAMETER_STRUCT(FClearTranslucentLightingVolumeCS, FGlobalShader)

	static const int32 CLEAR_BLOCK_SIZE = 4;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWAmbient0)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWDirectional0)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWAmbient1)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWDirectional1)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("CLEAR_COMPUTE_SHADER"), 1);
		OutEnvironment.SetDefine(TEXT("CLEAR_BLOCK_SIZE"), CLEAR_BLOCK_SIZE);
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearTranslucentLightingVolumeCS, "/Engine/Private/TranslucentLightInjectionShaders.usf", "ClearTranslucentLightingVolumeCS", SF_Compute);

void InitTranslucencyLightingVolumeTextures(FRDGBuilder& GraphBuilder, TArrayView<const FViewInfo> Views, ERDGPassFlags PassFlags, FTranslucencyLightingVolumeTextures& OutTextures)
{
	check(PassFlags == ERDGPassFlags::Compute || PassFlags == ERDGPassFlags::AsyncCompute);

	RDG_GPU_STAT_SCOPE(GraphBuilder, TranslucentLighting);

	OutTextures.VolumeDim = GetTranslucencyLightingVolumeDim();
	const FIntVector TranslucencyLightingVolumeDim(OutTextures.VolumeDim);

	{
		// TODO: We can skip the and TLV allocations when rendering in forward shading mode
		const ETextureCreateFlags TranslucencyTargetFlags = TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_ReduceMemoryWithTilingMode | TexCreate_UAV;

		const int32 ViewCount = Views.Num();
		check(ViewCount > 0);

		OutTextures.Ambient.SetNum(ViewCount * TVC_MAX);
		OutTextures.Directional.SetNum(ViewCount * TVC_MAX);

		for (int32 ViewIndex = 0; ViewIndex < ViewCount; ++ViewIndex)
		{
			for (int32 CascadeIndex = 0; CascadeIndex < TVC_MAX; ++CascadeIndex)
			{
				const uint32 TextureIndex = FTranslucencyLightingVolumeTextures::GetIndex(ViewIndex, CascadeIndex);

				const FRDGEventName& AmbientName = *GraphBuilder.AllocObject<FRDGEventName>(RDG_EVENT_NAME("TranslucentVolumeAmbient%d", TextureIndex));
				const FRDGEventName& DirectionalName = *GraphBuilder.AllocObject<FRDGEventName>(RDG_EVENT_NAME("TranslucentVolumeDirectional%d", TextureIndex));

				FRDGTextureRef AmbientTexture = GraphBuilder.CreateTexture(
					FRDGTextureDesc::Create3D(
						TranslucencyLightingVolumeDim,
						PF_FloatRGBA,
						FClearValueBinding::Transparent,
						TranslucencyTargetFlags),
					AmbientName.GetTCHAR());

				FRDGTextureRef DirectionalTexture = GraphBuilder.CreateTexture(
					FRDGTextureDesc::Create3D(
						TranslucencyLightingVolumeDim,
						PF_FloatRGBA,
						FClearValueBinding::Transparent,
						TranslucencyTargetFlags),
					DirectionalName.GetTCHAR());

				OutTextures.Ambient[TextureIndex] = AmbientTexture;
				OutTextures.Directional[TextureIndex] = DirectionalTexture;
			}
		}
	}

	const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(TranslucencyLightingVolumeDim, FClearTranslucentLightingVolumeCS::CLEAR_BLOCK_SIZE);

	TShaderMapRef<FClearTranslucentLightingVolumeCS> ComputeShader(Views[0].ShaderMap);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		auto* PassParameters = GraphBuilder.AllocParameters<FClearTranslucentLightingVolumeCS::FParameters>();
		PassParameters->RWAmbient0 = GraphBuilder.CreateUAV(OutTextures.Ambient[FTranslucencyLightingVolumeTextures::GetIndex(ViewIndex, 0)]);
		PassParameters->RWAmbient1 = GraphBuilder.CreateUAV(OutTextures.Ambient[FTranslucencyLightingVolumeTextures::GetIndex(ViewIndex, 1)]);
		PassParameters->RWDirectional0 = GraphBuilder.CreateUAV(OutTextures.Directional[FTranslucencyLightingVolumeTextures::GetIndex(ViewIndex, 0)]);
		PassParameters->RWDirectional1 = GraphBuilder.CreateUAV(OutTextures.Directional[FTranslucencyLightingVolumeTextures::GetIndex(ViewIndex, 1)]);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ClearTranslucencyLightingVolumeCompute %d", OutTextures.VolumeDim),
			PassFlags,
			ComputeShader,
			PassParameters,
			GroupCount);
	}
}

FTranslucencyLightingVolumeParameters GetTranslucencyLightingVolumeParameters(FRDGBuilder& GraphBuilder, const FTranslucencyLightingVolumeTextures& Textures, uint32 ViewIndex)
{
	FTranslucencyLightingVolumeParameters Parameters;
	if (Textures.IsValid())
	{
		const uint32 InnerIndex = FTranslucencyLightingVolumeTextures::GetIndex(ViewIndex, TVC_Inner);
		const uint32 OuterIndex = FTranslucencyLightingVolumeTextures::GetIndex(ViewIndex, TVC_Outer);

		Parameters.TranslucencyLightingVolumeAmbientInner = Textures.Ambient[InnerIndex];
		Parameters.TranslucencyLightingVolumeAmbientOuter = Textures.Ambient[OuterIndex];
		Parameters.TranslucencyLightingVolumeDirectionalInner = Textures.Directional[InnerIndex];
		Parameters.TranslucencyLightingVolumeDirectionalOuter = Textures.Directional[OuterIndex];
	}
	else
	{
		const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);
		Parameters.TranslucencyLightingVolumeAmbientInner = SystemTextures.VolumetricBlack;
		Parameters.TranslucencyLightingVolumeAmbientOuter = SystemTextures.VolumetricBlack;
		Parameters.TranslucencyLightingVolumeDirectionalInner = SystemTextures.VolumetricBlack;
		Parameters.TranslucencyLightingVolumeDirectionalOuter = SystemTextures.VolumetricBlack;
	}
	return Parameters;
}

class FInjectAmbientCubemapPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FInjectAmbientCubemapPS);
	SHADER_USE_PARAMETER_STRUCT(FInjectAmbientCubemapPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FAmbientCubemapParameters, AmbientCubemap)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FInjectAmbientCubemapPS, "/Engine/Private/TranslucentLightingShaders.usf", "InjectAmbientCubemapMainPS", SF_Pixel);

void InjectTranslucencyLightingVolumeAmbientCubemap(
	FRDGBuilder& GraphBuilder,
	const TArrayView<const FViewInfo> Views,
	const FTranslucencyLightingVolumeTextures& Textures)
{
	if (!GUseTranslucentLightingVolumes || !GSupportsVolumeTextureRendering)
	{
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "InjectAmbientCubemapTranslucentVolumeLighting");
	RDG_GPU_STAT_SCOPE(GraphBuilder, TranslucentLighting);

	const int32 TranslucencyLightingVolumeDim = Textures.VolumeDim;
	const FVolumeBounds VolumeBounds(TranslucencyLightingVolumeDim);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		const FViewInfo& View = Views[ViewIndex];
		RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
		RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

		for (int32 VolumeCascadeIndex = 0; VolumeCascadeIndex < TVC_MAX; ++VolumeCascadeIndex)
		{
			FRDGTextureRef VolumeAmbientTexture = Textures.Ambient[FTranslucencyLightingVolumeTextures::GetIndex(ViewIndex, VolumeCascadeIndex)];

			for (const FFinalPostProcessSettings::FCubemapEntry& CubemapEntry : View.FinalPostProcessSettings.ContributingCubemaps)
			{
				auto* PassParameters = GraphBuilder.AllocParameters<FInjectAmbientCubemapPS::FParameters>();
				SetupAmbientCubemapParameters(CubemapEntry, &PassParameters->AmbientCubemap);
				PassParameters->RenderTargets[0] = FRenderTargetBinding(VolumeAmbientTexture, ERenderTargetLoadAction::ELoad);
				PassParameters->View = View.ViewUniformBuffer;

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("Cascade %d", VolumeCascadeIndex),
					PassParameters,
					ERDGPassFlags::Raster,
					[&View, PassParameters, VolumeBounds, TranslucencyLightingVolumeDim](FRHICommandList& RHICmdList)
				{
					TShaderMapRef<FWriteToSliceVS> VertexShader(View.ShaderMap);
					TOptionalShaderMapRef<FWriteToSliceGS> GeometryShader(View.ShaderMap);
					TShaderMapRef<FInjectAmbientCubemapPS> PixelShader(View.ShaderMap);

					FGraphicsPipelineStateInitializer GraphicsPSOInit;
					RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

					GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
					GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI();
					GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GScreenVertexDeclaration.VertexDeclarationRHI;
					GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
					GraphicsPSOInit.BoundShaderState.GeometryShaderRHI = GeometryShader.GetGeometryShader();
#endif
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
					GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;
					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

					VertexShader->SetParameters(RHICmdList, VolumeBounds, FIntVector(TranslucencyLightingVolumeDim));
					if (GeometryShader.IsValid())
					{
						GeometryShader->SetParameters(RHICmdList, VolumeBounds.MinZ);
					}
					SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);
				});
			}
		}
	}
}

/** Calculates volume texture bounds for the given light in the given translucent lighting volume cascade. */
FVolumeBounds CalculateLightVolumeBounds(const FSphere& LightBounds, const FViewInfo& View, uint32 VolumeCascadeIndex, bool bDirectionalLight)
{
	const int32 TranslucencyLightingVolumeDim = GetTranslucencyLightingVolumeDim();

	FVolumeBounds VolumeBounds;

	if (bDirectionalLight)
	{
		VolumeBounds = FVolumeBounds(TranslucencyLightingVolumeDim);
	}
	else
	{
		// Determine extents in the volume texture
		const FVector MinPosition = (LightBounds.Center - LightBounds.W - View.TranslucencyLightingVolumeMin[VolumeCascadeIndex]) / View.TranslucencyVolumeVoxelSize[VolumeCascadeIndex];
		const FVector MaxPosition = (LightBounds.Center + LightBounds.W - View.TranslucencyLightingVolumeMin[VolumeCascadeIndex]) / View.TranslucencyVolumeVoxelSize[VolumeCascadeIndex];

		VolumeBounds.MinX = FMath::Max(FMath::TruncToInt(MinPosition.X), 0);
		VolumeBounds.MinY = FMath::Max(FMath::TruncToInt(MinPosition.Y), 0);
		VolumeBounds.MinZ = FMath::Max(FMath::TruncToInt(MinPosition.Z), 0);

		VolumeBounds.MaxX = FMath::Min(FMath::TruncToInt(MaxPosition.X) + 1, TranslucencyLightingVolumeDim);
		VolumeBounds.MaxY = FMath::Min(FMath::TruncToInt(MaxPosition.Y) + 1, TranslucencyLightingVolumeDim);
		VolumeBounds.MaxZ = FMath::Min(FMath::TruncToInt(MaxPosition.Z) + 1, TranslucencyLightingVolumeDim);
	}

	return VolumeBounds;
}

/** 
 * Information about a light to be injected.
 * Cached in this struct to avoid recomputing multiple times (multiple cascades).
 */
struct FTranslucentLightInjectionData
{
	// must not be 0
	const FLightSceneInfo* LightSceneInfo;
	// can be 0
	const FProjectedShadowInfo* ProjectedShadowInfo;
	//
	bool bApplyLightFunction;
	// must not be 0
	const FMaterialRenderProxy* LightFunctionMaterialProxy;
};

/**
 * Adds a light to LightInjectionData if it should be injected into the translucent volume, and caches relevant information in a FTranslucentLightInjectionData.
 * @param InProjectedShadowInfo is 0 for unshadowed lights
 */
static void AddLightForInjection(
	const FViewInfo& View,
	TArrayView<const FVisibleLightInfo> VisibleLightInfos,
	const FLightSceneInfo& LightSceneInfo,
	const FProjectedShadowInfo* InProjectedShadowInfo,
	TArray<FTranslucentLightInjectionData, SceneRenderingAllocator>& LightInjectionData)
{
	if (LightSceneInfo.Proxy->AffectsTranslucentLighting())
	{
		const FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightSceneInfo.Id];
		const ERHIFeatureLevel::Type FeatureLevel = View.FeatureLevel;

		const bool bApplyLightFunction = (View.Family->EngineShowFlags.LightFunctions &&
			LightSceneInfo.Proxy->GetLightFunctionMaterial() && 
			LightSceneInfo.Proxy->GetLightFunctionMaterial()->GetIncompleteMaterialWithFallback(FeatureLevel).IsLightFunction());

		const FMaterialRenderProxy* MaterialProxy = bApplyLightFunction ? 
			LightSceneInfo.Proxy->GetLightFunctionMaterial() : 
			UMaterial::GetDefaultMaterial(MD_LightFunction)->GetRenderProxy();

		// Skip rendering if the DefaultLightFunctionMaterial isn't compiled yet
		if (MaterialProxy->GetIncompleteMaterialWithFallback(FeatureLevel).IsLightFunction())
		{
			FTranslucentLightInjectionData InjectionData;
			InjectionData.LightSceneInfo = &LightSceneInfo;
			InjectionData.ProjectedShadowInfo = InProjectedShadowInfo;
			InjectionData.bApplyLightFunction = bApplyLightFunction;
			InjectionData.LightFunctionMaterialProxy = MaterialProxy;
			LightInjectionData.Add(InjectionData);
		}
	}
}

static FRDGTextureRef GetSkyTransmittanceLutTexture(FRDGBuilder& GraphBuilder, const FScene* Scene, const FViewInfo& View)
{
	FRDGTextureRef TransmittanceLutTexture = nullptr;
	if (ShouldRenderSkyAtmosphere(Scene, View.Family->EngineShowFlags))
	{
		if (const FSkyAtmosphereRenderSceneInfo* SkyInfo = Scene->GetSkyAtmosphereSceneInfo())
		{
			TransmittanceLutTexture = SkyInfo->GetTransmittanceLutTexture(GraphBuilder);
		}
	}
	return TransmittanceLutTexture;
}

BEGIN_SHADER_PARAMETER_STRUCT(FInjectTranslucentLightArrayParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FTranslucentLightingInjectPS::FParameters, PS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FVolumetricCloudShadowAOParameters, CloudShadowAO)
	RDG_TEXTURE_ACCESS(TransmittanceLutTexture, ERHIAccess::SRVGraphics)
	RDG_TEXTURE_ACCESS(ShadowDepthTexture, ERHIAccess::SRVGraphics)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

/** Injects all the lights in LightInjectionData into the translucent lighting volume textures. */
static void InjectTranslucentLightArray(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const uint32 ViewIndex,
	const FScene* Scene,
	const FSceneRenderer& Renderer,
	const FTranslucencyLightingVolumeTextures& Textures,
	TArrayView<const FTranslucentLightInjectionData> LightInjectionData)
{
	INC_DWORD_STAT_BY(STAT_NumLightsInjectedIntoTranslucency, LightInjectionData.Num());

	const FVolumetricCloudShadowAOParameters CloudShadowAOParameters = GetCloudShadowAOParameters(GraphBuilder, View, Scene->GetVolumetricCloudSceneInfo());

	FRDGTextureRef TransmittanceLutTexture = GetSkyTransmittanceLutTexture(GraphBuilder, Scene, View);

	// Inject into each volume cascade. Operate on one cascade at a time to reduce render target switches.
	for (uint32 VolumeCascadeIndex = 0; VolumeCascadeIndex < TVC_MAX; VolumeCascadeIndex++)
	{
		const uint32 TextureIndex = FTranslucencyLightingVolumeTextures::GetIndex(ViewIndex, VolumeCascadeIndex);
		FRDGTextureRef VolumeAmbientTexture = Textures.Ambient[TextureIndex];
		FRDGTextureRef VolumeDirectionalTexture = Textures.Directional[TextureIndex];

		for (int32 LightIndex = 0; LightIndex < LightInjectionData.Num(); LightIndex++)
		{
			const FTranslucentLightInjectionData& InjectionData = LightInjectionData[LightIndex];
			const FLightSceneInfo* const LightSceneInfo = InjectionData.LightSceneInfo;
			const bool bInverseSquared = LightSceneInfo->Proxy->IsInverseSquared();
			const bool bDirectionalLight = LightSceneInfo->Proxy->GetLightType() == LightType_Directional;
			const bool bUseVSM = Renderer.VirtualShadowMapArray.IsAllocated();

			const FVolumeBounds VolumeBounds = CalculateLightVolumeBounds(LightSceneInfo->Proxy->GetBoundingSphere(), View, VolumeCascadeIndex, bDirectionalLight);
			if (VolumeBounds.IsValid())
			{
				TShaderMapRef<FWriteToSliceVS> VertexShader(View.ShaderMap);
				TOptionalShaderMapRef<FWriteToSliceGS> GeometryShader(View.ShaderMap);

				FRDGTextureRef ShadowDepthTexture = nullptr;

				if (InjectionData.ProjectedShadowInfo)
				{
					ShadowDepthTexture = TryRegisterExternalTexture(GraphBuilder, InjectionData.ProjectedShadowInfo->RenderTargets.DepthTarget);
				}

				auto* PassParameters = GraphBuilder.AllocParameters< FInjectTranslucentLightArrayParameters >();
				PassParameters->TransmittanceLutTexture = TransmittanceLutTexture;
				PassParameters->ShadowDepthTexture = ShadowDepthTexture;
				PassParameters->CloudShadowAO = CloudShadowAOParameters;
				PassParameters->PS.VirtualShadowMapSamplingParameters = Renderer.VirtualShadowMapArray.GetSamplingParameters(GraphBuilder);
				PassParameters->RenderTargets[0] = FRenderTargetBinding(VolumeAmbientTexture, ERenderTargetLoadAction::ELoad);
				PassParameters->RenderTargets[1] = FRenderTargetBinding(VolumeDirectionalTexture, ERenderTargetLoadAction::ELoad);

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("InjectTranslucentLightArray"),
					PassParameters,
					ERDGPassFlags::Raster,
					[PassParameters, VertexShader, GeometryShader, &View, &Renderer, &InjectionData, LightSceneInfo, bInverseSquared, bDirectionalLight, bUseVSM, VolumeBounds, VolumeCascadeIndex](FRHICommandList& RHICmdList)
				{
					FGraphicsPipelineStateInitializer GraphicsPSOInit;
					RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

					GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
					GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

					if (bDirectionalLight)
					{
						// Accumulate the contribution of multiple lights
						// Directional lights write their shadowing into alpha of the ambient texture
						GraphicsPSOInit.BlendState = TStaticBlendState<
							CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One,
							CW_RGB, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI();
					}
					else
					{
						// Accumulate the contribution of multiple lights
						GraphicsPSOInit.BlendState = TStaticBlendState<
							CW_RGB, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_One,
							CW_RGB, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_One>::GetRHI();
					}

					const FMaterialRenderProxy* MaterialProxy = InjectionData.LightFunctionMaterialProxy;
					const FMaterial& Material = MaterialProxy->GetMaterialWithFallback( View.GetFeatureLevel(), MaterialProxy );
					const FMaterialShaderMap* MaterialShaderMap = Material.GetRenderingThreadShaderMap();

					FTranslucentLightingInjectPS::FPermutationDomain PermutationVector;
					PermutationVector.Set< FTranslucentLightingInjectPS::FRadialAttenuation >( !bDirectionalLight );
					PermutationVector.Set< FTranslucentLightingInjectPS::FDynamicallyShadowed >( InjectionData.ProjectedShadowInfo != nullptr );
					PermutationVector.Set< FTranslucentLightingInjectPS::FLightFunction >( InjectionData.bApplyLightFunction );
					PermutationVector.Set< FTranslucentLightingInjectPS::FInverseSquared >( bInverseSquared );
					PermutationVector.Set< FTranslucentLightingInjectPS::FVirtualShadowMap >( bUseVSM );

					auto PixelShader = MaterialShaderMap->GetShader< FTranslucentLightingInjectPS >( PermutationVector );
	
					GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GScreenVertexDeclaration.VertexDeclarationRHI;
					GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
					GraphicsPSOInit.BoundShaderState.GeometryShaderRHI = GeometryShader.GetGeometryShader();
				#endif
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

					const int32 TranslucencyLightingVolumeDim = GetTranslucencyLightingVolumeDim();
					const int32 VirtualShadowMapId = Renderer.VisibleLightInfos[LightSceneInfo->Id].GetVirtualShadowMapId( &View );

					VertexShader->SetParameters(RHICmdList, VolumeBounds, FIntVector(TranslucencyLightingVolumeDim));
					if (GeometryShader.IsValid())
					{
						GeometryShader->SetParameters(RHICmdList, VolumeBounds.MinZ);
					}

					PixelShader->SetParameters(RHICmdList, View, LightSceneInfo,
						InjectionData.LightFunctionMaterialProxy,
						InjectionData.ProjectedShadowInfo,
						InjectionData.ProjectedShadowInfo ? InjectionData.ProjectedShadowInfo->CascadeSettings.ShadowSplitIndex : INDEX_NONE,
						VolumeCascadeIndex,
						VirtualShadowMapId);

					SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PS);

					RasterizeToVolumeTexture(RHICmdList, VolumeBounds);
				});
			}
		}
	}
}

void InjectTranslucencyLightingVolume(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const uint32 ViewIndex,
	const FScene* Scene,
	const FSceneRenderer& Renderer,
	const FTranslucencyLightingVolumeTextures& Textures,
	TArrayView<const FVisibleLightInfo> VisibleLightInfos,
	const FLightSceneInfo& LightSceneInfo,
	const FProjectedShadowInfo* ProjectedShadowInfo)
{
	if (GUseTranslucentLightingVolumes && GSupportsVolumeTextureRendering)
	{
		SCOPE_CYCLE_COUNTER(STAT_TranslucentInjectTime);

		auto& LightInjectionData = *GraphBuilder.AllocObject<TArray<FTranslucentLightInjectionData, SceneRenderingAllocator>>();
		AddLightForInjection(View, VisibleLightInfos, LightSceneInfo, ProjectedShadowInfo, LightInjectionData);
		InjectTranslucentLightArray(GraphBuilder, View, ViewIndex, Scene, Renderer, Textures, LightInjectionData);
	}
}

void InjectTranslucencyLightingVolumeArray(
	FRDGBuilder& GraphBuilder,
	const TArrayView<const FViewInfo> Views,
	const FScene* Scene,
	const FSceneRenderer& Renderer,
	const FTranslucencyLightingVolumeTextures& Textures,
	const TArrayView<const FVisibleLightInfo> VisibleLightInfos,
	TArrayView<const FSortedLightSceneInfo> SortedLights,
	TInterval<int32> SortedLightInterval)
{
	SCOPE_CYCLE_COUNTER(STAT_TranslucentInjectTime);

	using FLightInjectionData = TArray<TArray<FTranslucentLightInjectionData, SceneRenderingAllocator>>;
	auto& LightInjectionData = *GraphBuilder.AllocObject<FLightInjectionData>();
	LightInjectionData.SetNum(Views.Num());

	for (int32 ViewIndex = 0; ViewIndex < LightInjectionData.Num(); ++ViewIndex)
	{
		LightInjectionData[ViewIndex].Reserve(SortedLightInterval.Size());
	}

	for (int32 LightIndex = SortedLightInterval.Min; LightIndex < SortedLightInterval.Max; LightIndex++)
	{
		const FSortedLightSceneInfo& SortedLightInfo = SortedLights[LightIndex];
		const FLightSceneInfo* const LightSceneInfo = SortedLightInfo.LightSceneInfo;
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			const FViewInfo& View = Views[ViewIndex];
			if (LightSceneInfo->ShouldRenderLight(View))
			{
				AddLightForInjection(View, VisibleLightInfos, *LightSceneInfo, nullptr, LightInjectionData[ViewIndex]);
			}
		}
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];

		// non-shadowed, non-light function lights
		InjectTranslucentLightArray(GraphBuilder, View, ViewIndex, Scene, Renderer, Textures, LightInjectionData[ViewIndex]);
	}
}

class FSimpleLightTranslucentLightingInjectPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSimpleLightTranslucentLightingInjectPS);
	SHADER_USE_PARAMETER_STRUCT(FSimpleLightTranslucentLightingInjectPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER(FVector4, SimpleLightPositionAndRadius)
		SHADER_PARAMETER(FVector4, SimpleLightColorAndExponent)
		SHADER_PARAMETER(uint32, VolumeCascadeIndex)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) 
	{ 
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && (RHISupportsGeometryShaders(Parameters.Platform) || RHISupportsVertexShaderLayer(Parameters.Platform));
	}
};

IMPLEMENT_GLOBAL_SHADER(FSimpleLightTranslucentLightingInjectPS, "/Engine/Private/TranslucentLightInjectionShaders.usf", "SimpleLightInjectMainPS", SF_Pixel);

void InjectSimpleTranslucencyLightingVolumeArray(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const uint32 ViewIndex,
	const uint32 ViewCount,
	const FTranslucencyLightingVolumeTextures& Textures,
	const FSimpleLightArray& SimpleLights)
{
	SCOPE_CYCLE_COUNTER(STAT_TranslucentInjectTime);

	int32 NumLightsToInject = 0;

	for (int32 LightIndex = 0; LightIndex < SimpleLights.InstanceData.Num(); LightIndex++)
	{
		if (SimpleLights.InstanceData[LightIndex].bAffectTranslucency)
		{
			NumLightsToInject++;
		}
	}

	if (NumLightsToInject > 0)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "InjectSimpleTranslucentLightArray");

		INC_DWORD_STAT_BY(STAT_NumLightsInjectedIntoTranslucency, NumLightsToInject);

		const int32 TranslucencyLightingVolumeDim = GetTranslucencyLightingVolumeDim();

		// Inject into each volume cascade
		// Operate on one cascade at a time to reduce render target switches
		for (int32 VolumeCascadeIndex = 0; VolumeCascadeIndex < TVC_MAX; VolumeCascadeIndex++)
		{
			const uint32 TextureIndex = FTranslucencyLightingVolumeTextures::GetIndex(ViewIndex, VolumeCascadeIndex);

			RDG_EVENT_SCOPE(GraphBuilder, "Cascade%d", VolumeCascadeIndex);
			FRDGTextureRef VolumeAmbientTexture = Textures.Ambient[TextureIndex];
			FRDGTextureRef VolumeDirectionalTexture = Textures.Directional[TextureIndex];

			for (int32 LightIndex = 0; LightIndex < SimpleLights.InstanceData.Num(); LightIndex++)
			{
				const FSimpleLightEntry& SimpleLight = SimpleLights.InstanceData[LightIndex];
				const FSimpleLightPerViewEntry& SimpleLightPerViewData = SimpleLights.GetViewDependentData(LightIndex, ViewIndex, ViewCount);

				if (SimpleLight.bAffectTranslucency)
				{
					const FSphere LightBounds(SimpleLightPerViewData.Position, SimpleLight.Radius);
					const FVolumeBounds VolumeBounds = CalculateLightVolumeBounds(LightBounds, View, VolumeCascadeIndex, false);

					if (VolumeBounds.IsValid())
					{
						auto* PassParameters = GraphBuilder.AllocParameters<FSimpleLightTranslucentLightingInjectPS::FParameters>();
						PassParameters->View = View.ViewUniformBuffer;
						PassParameters->VolumeCascadeIndex = VolumeCascadeIndex;
						PassParameters->SimpleLightPositionAndRadius = FVector4(SimpleLightPerViewData.Position, SimpleLight.Radius);
						PassParameters->SimpleLightColorAndExponent = FVector4(SimpleLight.Color, SimpleLight.Exponent);
						PassParameters->RenderTargets[0] = FRenderTargetBinding(VolumeAmbientTexture, ERenderTargetLoadAction::ELoad);
						PassParameters->RenderTargets[1] = FRenderTargetBinding(VolumeDirectionalTexture, ERenderTargetLoadAction::ELoad);

						TShaderMapRef<FWriteToSliceVS> VertexShader(View.ShaderMap);
						TOptionalShaderMapRef<FWriteToSliceGS> GeometryShader(View.ShaderMap);
						TShaderMapRef<FSimpleLightTranslucentLightingInjectPS> PixelShader(View.ShaderMap);

						GraphBuilder.AddPass(
							{},
							PassParameters,
							ERDGPassFlags::Raster,
							[VertexShader, GeometryShader, PixelShader, PassParameters, VolumeBounds, TranslucencyLightingVolumeDim](FRHICommandList& RHICmdList)
						{
							FGraphicsPipelineStateInitializer GraphicsPSOInit;
							RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

							GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
							GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
							// Accumulate the contribution of multiple lights
							GraphicsPSOInit.BlendState = TStaticBlendState<
								CW_RGB, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_One,
								CW_RGB, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_One>::GetRHI();
							GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

							GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GScreenVertexDeclaration.VertexDeclarationRHI;
							GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
							GraphicsPSOInit.BoundShaderState.GeometryShaderRHI = GeometryShader.GetGeometryShader();
#endif
							GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
							SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

							VertexShader->SetParameters(RHICmdList, VolumeBounds, FIntVector(TranslucencyLightingVolumeDim));
							if (GeometryShader.IsValid())
							{
								GeometryShader->SetParameters(RHICmdList, VolumeBounds.MinZ);
							}
							SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);
							RasterizeToVolumeTexture(RHICmdList, VolumeBounds);
						});
					}
				}
			}
		}
	}
}

void FilterTranslucencyLightingVolume(
	FRDGBuilder& GraphBuilder,
	const TArrayView<const FViewInfo> Views,
	FTranslucencyLightingVolumeTextures& Textures)
{
	if (!GUseTranslucentLightingVolumes || !GSupportsVolumeTextureRendering || !GUseTranslucencyVolumeBlur)
	{
		return;
	}

	FRHISamplerState* SamplerStateRHI = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	const int32 TranslucencyLightingVolumeDim = GetTranslucencyLightingVolumeDim();
	RDG_EVENT_SCOPE(GraphBuilder, "FilterTranslucentVolume %dx%dx%d Cascades:%d", TranslucencyLightingVolumeDim, TranslucencyLightingVolumeDim, TranslucencyLightingVolumeDim, TVC_MAX);
	RDG_GPU_STAT_SCOPE(GraphBuilder, TranslucentLighting);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		const FViewInfo& View = Views[ViewIndex];
		RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
		RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, Views.Num() > 1, "View%d", ViewIndex);

		for (int32 VolumeCascadeIndex = 0; VolumeCascadeIndex < TVC_MAX; VolumeCascadeIndex++)
		{
			const uint32 TextureIndex = FTranslucencyLightingVolumeTextures::GetIndex(ViewIndex, VolumeCascadeIndex);

			FRDGTextureRef InputVolumeAmbientTexture = Textures.Ambient[TextureIndex];
			FRDGTextureRef InputVolumeDirectionalTexture = Textures.Directional[TextureIndex];

			FRDGTextureRef OutputVolumeAmbientTexture = GraphBuilder.CreateTexture(InputVolumeAmbientTexture->Desc, InputVolumeAmbientTexture->Name);
			FRDGTextureRef OutputVolumeDirectionalTexture = GraphBuilder.CreateTexture(InputVolumeDirectionalTexture->Desc, InputVolumeDirectionalTexture->Name);

			Textures.Ambient[TextureIndex] = OutputVolumeAmbientTexture;
			Textures.Directional[TextureIndex] = OutputVolumeDirectionalTexture;

			auto* PassParameters = GraphBuilder.AllocParameters<FFilterTranslucentVolumePS::FParameters>();
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->TexelSize = 1.0f / TranslucencyLightingVolumeDim;
			PassParameters->TranslucencyLightingVolumeAmbient = InputVolumeAmbientTexture;
			PassParameters->TranslucencyLightingVolumeDirectional = InputVolumeDirectionalTexture;
			PassParameters->TranslucencyLightingVolumeAmbientSampler = SamplerStateRHI;
			PassParameters->TranslucencyLightingVolumeDirectionalSampler = SamplerStateRHI;
			PassParameters->RenderTargets[0] = FRenderTargetBinding(OutputVolumeAmbientTexture, ERenderTargetLoadAction::ENoAction);
			PassParameters->RenderTargets[1] = FRenderTargetBinding(OutputVolumeDirectionalTexture, ERenderTargetLoadAction::ENoAction);

			const FVolumeBounds VolumeBounds(TranslucencyLightingVolumeDim);
			TShaderMapRef<FWriteToSliceVS> VertexShader(View.ShaderMap);
			TOptionalShaderMapRef<FWriteToSliceGS> GeometryShader(View.ShaderMap);
			TShaderMapRef<FFilterTranslucentVolumePS> PixelShader(View.ShaderMap);

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("Cascade%d", VolumeCascadeIndex),
				PassParameters,
				ERDGPassFlags::Raster,
				[VertexShader, GeometryShader, PixelShader, PassParameters, VolumeBounds, TranslucencyLightingVolumeDim](FRHICommandList& RHICmdList)
			{
				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
				GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GScreenVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
				GraphicsPSOInit.BoundShaderState.GeometryShaderRHI = GeometryShader.GetGeometryShader();
#endif
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

				VertexShader->SetParameters(RHICmdList, VolumeBounds, FIntVector(TranslucencyLightingVolumeDim));
				if (GeometryShader.IsValid())
				{
					GeometryShader->SetParameters(RHICmdList, VolumeBounds.MinZ);
				}
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);
				RasterizeToVolumeTexture(RHICmdList, VolumeBounds);
			});
		}
	}
}
