// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	TranslucentLighting.cpp: Translucent lighting implementation.
=============================================================================*/

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

	template<ETranslucencyShadowDepthShaderMode ShaderMode>
	void Process(
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

template<ETranslucencyShadowDepthShaderMode ShaderMode>
void FTranslucencyDepthPassMeshProcessor::Process(
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
		FMeshMaterialShader,
		FMeshMaterialShader,
		TTranslucencyShadowDepthPS<ShaderMode>> PassShaders;

	PassShaders.VertexShader = MaterialResource.GetShader<TTranslucencyShadowDepthVS<ShaderMode> >(VertexFactory->GetType());
	PassShaders.PixelShader = MaterialResource.GetShader<TTranslucencyShadowDepthPS<ShaderMode> >(VertexFactory->GetType());

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
}

void FTranslucencyDepthPassMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	if (MeshBatch.CastShadow)
	{
		// Determine the mesh's material and blend mode.
		const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
		const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);
		const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;

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
				Process<TranslucencyShadowDepth_Standard>(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, MaterialRenderProxy, Material, MaterialTranslucentShadowStartOffset, MeshFillMode, MeshCullMode);
			}
			else
			{
				Process<TranslucencyShadowDepth_PerspectiveCorrect>(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, MaterialRenderProxy, Material, MaterialTranslucentShadowStartOffset, MeshFillMode, MeshCullMode);
			}
		}
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FTranslucencyDepthPassParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FTranslucencyDepthPassUniformParameters, PassUniformBuffer)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void FProjectedShadowInfo::RenderTranslucencyDepths(FRDGBuilder& GraphBuilder, FSceneRenderer* SceneRenderer, const FRenderTargetBindingSlots& InRenderTargets)
{
	check(IsInRenderingThread());
	checkSlow(!bWholeSceneShadow);
	SCOPE_CYCLE_COUNTER(STAT_RenderPerObjectShadowDepthsTime);

	BeginRenderView(GraphBuilder, SceneRenderer->Scene);

	auto* TranslucencyDepthPassParameters = GraphBuilder.AllocParameters<FTranslucencyDepthPassUniformParameters>();
	SetupTranslucencyDepthPassUniformBuffer(this, GraphBuilder, *ShadowDepthView, *TranslucencyDepthPassParameters);
	TRDGUniformBufferRef<FTranslucencyDepthPassUniformParameters> PassUniformBuffer = GraphBuilder.CreateUniformBuffer(TranslucencyDepthPassParameters);

	auto* PassParameters = GraphBuilder.AllocParameters<FTranslucencyDepthPassParameters>();
	PassParameters->PassUniformBuffer = PassUniformBuffer;
	PassParameters->RenderTargets = InRenderTargets;

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
		[this, SceneRenderer](FRHICommandListImmediate& RHICmdList)
	{
		FMeshPassProcessorRenderState DrawRenderState(*ShadowDepthView);

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

		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
		DrawRenderState.SetBlendState(TStaticBlendState<
			CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One,
			CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI());


		FMeshCommandOneFrameArray VisibleMeshDrawCommands;
		FDynamicPassMeshDrawListContext TranslucencyDepthContext(DynamicMeshDrawCommandStorage, VisibleMeshDrawCommands, GraphicsMinimalPipelineStateSet, NeedsShaderInitialisation);

		FTranslucencyDepthPassMeshProcessor TranslucencyDepthPassMeshProcessor(
			SceneRenderer->Scene,
			ShadowDepthView,
			DrawRenderState,
			this,
			&TranslucencyDepthContext);

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

		if (VisibleMeshDrawCommands.Num() > 0)
		{
			const bool bDynamicInstancing = IsDynamicInstancingEnabled(ShadowDepthView->FeatureLevel);

			FRHIVertexBuffer* PrimitiveIdVertexBuffer = nullptr;
			ApplyViewOverridesToMeshDrawCommands(*ShadowDepthView, VisibleMeshDrawCommands, DynamicMeshDrawCommandStorage, GraphicsMinimalPipelineStateSet, NeedsShaderInitialisation);
			SortAndMergeDynamicPassMeshDrawCommands(SceneRenderer->FeatureLevel, VisibleMeshDrawCommands, DynamicMeshDrawCommandStorage, PrimitiveIdVertexBuffer, 1);
			SubmitMeshDrawCommands(VisibleMeshDrawCommands, GraphicsMinimalPipelineStateSet, PrimitiveIdVertexBuffer, 0, bDynamicInstancing, 1, RHICmdList);
		}
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
template<ELightComponentType InjectionType, bool bDynamicallyShadowed, bool bApplyLightFunction, bool bInverseSquared>
class TTranslucentLightingInjectPS : public FMaterialShader
{
	DECLARE_SHADER_TYPE(TTranslucentLightingInjectPS,Material);
public:

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment )
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("RADIAL_ATTENUATION"), (uint32)(InjectionType != LightType_Directional));
		OutEnvironment.SetDefine(TEXT("INJECTION_PIXEL_SHADER"), 1);
		OutEnvironment.SetDefine(TEXT("DYNAMICALLY_SHADOWED"), (uint32)bDynamicallyShadowed);
		OutEnvironment.SetDefine(TEXT("APPLY_LIGHT_FUNCTION"), (uint32)bApplyLightFunction);
		OutEnvironment.SetDefine(TEXT("INVERSE_SQUARED_FALLOFF"), (uint32)bInverseSquared);
	}

	/**
	  * Makes sure only shaders for materials that are explicitly flagged
	  * as 'UsedAsLightFunction' in the Material Editor gets compiled into
	  * the shader cache.
	  */
	static bool ShouldCompilePermutation(const FMaterialShaderPermutationParameters& Parameters)
	{
		return (Parameters.MaterialParameters.MaterialDomain == MD_LightFunction || Parameters.MaterialParameters.bIsSpecialEngineMaterial) &&
			(IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) &&
			(RHISupportsGeometryShaders(Parameters.Platform) || RHISupportsVertexShaderLayer(Parameters.Platform)));
	}

	TTranslucentLightingInjectPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FMaterialShader(Initializer)
	{
		VolumeShadowingParameters.Bind(Initializer.ParameterMap);
		SpotlightMask.Bind(Initializer.ParameterMap, TEXT("SpotlightMask"));
		LightFunctionParameters.Bind(Initializer.ParameterMap);
		TranslucentInjectParameters.Bind(Initializer.ParameterMap);
		LightFunctionWorldToLight.Bind(Initializer.ParameterMap, TEXT("LightFunctionWorldToLight"));

		VolumetricCloudWorldToLightClipShadowMatrix.Bind(Initializer.ParameterMap, TEXT("VolumetricCloudWorldToLightClipShadowMatrix"));
		VolumetricCloudShadowmapFarDepthKm.Bind(Initializer.ParameterMap, TEXT("VolumetricCloudShadowmapFarDepthKm"));
		VolumetricCloudShadowEnabled.Bind(Initializer.ParameterMap, TEXT("VolumetricCloudShadowEnabled"));
		VolumetricCloudShadowmapStrength.Bind(Initializer.ParameterMap, TEXT("VolumetricCloudShadowmapStrength"));
		VolumetricCloudShadowmapTexture.Bind(Initializer.ParameterMap, TEXT("VolumetricCloudShadowmapTexture"));
		VolumetricCloudShadowmapTextureSampler.Bind(Initializer.ParameterMap, TEXT("VolumetricCloudShadowmapTextureSampler"));
		AtmospherePerPixelTransmittanceEnabled.Bind(Initializer.ParameterMap, TEXT("AtmospherePerPixelTransmittanceEnabled"));
	}
	TTranslucentLightingInjectPS() {}

	// @param InnerSplitIndex which CSM shadow map level, INDEX_NONE if no directional light
	// @param VolumeCascadeIndexValue which volume we render to
	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FViewInfo& View, 
		const FLightSceneInfo* LightSceneInfo, 
		const FMaterialRenderProxy* MaterialProxy, 
		const FProjectedShadowInfo* ShadowMap, 
		int32 InnerSplitIndex, 
		int32 VolumeCascadeIndexValue)
	{
		check(ShadowMap || !bDynamicallyShadowed);
		
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

		FLightSceneProxy* AtmosphereLight0Proxy = LightSceneInfo->Scene->AtmosphereLights[0] ? LightSceneInfo->Scene->AtmosphereLights[0]->Proxy : nullptr;
		FLightSceneProxy* AtmosphereLight1Proxy = LightSceneInfo->Scene->AtmosphereLights[1] ? LightSceneInfo->Scene->AtmosphereLights[1]->Proxy : nullptr;

		if (VolumetricCloudShadowmapTexture.IsBound())
		{
			FVolumetricCloudRenderSceneInfo* CloudInfo = LightSceneInfo->Scene->GetVolumetricCloudSceneInfo();

			const bool bLight0CloudPerPixelTransmittance = CloudInfo && View.ViewState && View.ViewState->VolumetricCloudShadowRenderTarget[0].CurrentIsValid() && AtmosphereLight0Proxy && AtmosphereLight0Proxy == LightSceneInfo->Proxy;
			const bool bLight1CloudPerPixelTransmittance = CloudInfo && View.ViewState && View.ViewState->VolumetricCloudShadowRenderTarget[1].CurrentIsValid() && AtmosphereLight1Proxy && AtmosphereLight1Proxy == LightSceneInfo->Proxy;

			if (bLight0CloudPerPixelTransmittance || bLight1CloudPerPixelTransmittance)
			{
				uint32 LightIndex = bLight1CloudPerPixelTransmittance ? 1 : 0;
				SetShaderValue(RHICmdList, ShaderRHI, VolumetricCloudShadowEnabled, 1);
				SetShaderValue(RHICmdList, ShaderRHI, VolumetricCloudWorldToLightClipShadowMatrix, CloudInfo->GetVolumetricCloudCommonShaderParameters().CloudShadowmapFarDepthKm[LightIndex]);
				SetShaderValue(RHICmdList, ShaderRHI, VolumetricCloudShadowmapFarDepthKm, CloudInfo->GetVolumetricCloudCommonShaderParameters().CloudShadowmapWorldToLightClipMatrix[LightIndex]);
				SetTextureParameter(
					RHICmdList,
					ShaderRHI,
					VolumetricCloudShadowmapTexture,
					VolumetricCloudShadowmapTextureSampler,
					TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),
					View.ViewState->GetVolumetricCloudShadowRenderTarget(LightIndex)->GetRenderTargetItem().ShaderResourceTexture);

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

	LAYOUT_FIELD(FShaderParameter, VolumetricCloudWorldToLightClipShadowMatrix);
	LAYOUT_FIELD(FShaderParameter, VolumetricCloudShadowmapFarDepthKm);
	LAYOUT_FIELD(FShaderParameter, VolumetricCloudShadowEnabled);
	LAYOUT_FIELD(FShaderParameter, VolumetricCloudShadowmapStrength);
	LAYOUT_FIELD(FShaderResourceParameter, VolumetricCloudShadowmapTexture);
	LAYOUT_FIELD(FShaderResourceParameter, VolumetricCloudShadowmapTextureSampler);
	LAYOUT_FIELD(FShaderParameter, AtmospherePerPixelTransmittanceEnabled);
};

#define IMPLEMENT_INJECTION_PIXELSHADER_TYPE(LightType,bDynamicallyShadowed,bApplyLightFunction,bInverseSquared) \
	typedef TTranslucentLightingInjectPS<LightType,bDynamicallyShadowed,bApplyLightFunction,bInverseSquared> TTranslucentLightingInjectPS##LightType##bDynamicallyShadowed##bApplyLightFunction##bInverseSquared; \
	IMPLEMENT_MATERIAL_SHADER_TYPE(template<>,TTranslucentLightingInjectPS##LightType##bDynamicallyShadowed##bApplyLightFunction##bInverseSquared,TEXT("/Engine/Private/TranslucentLightInjectionShaders.usf"),TEXT("InjectMainPS"),SF_Pixel);

/** Versions with a light function. */
IMPLEMENT_INJECTION_PIXELSHADER_TYPE(LightType_Directional,true,true,false); 
IMPLEMENT_INJECTION_PIXELSHADER_TYPE(LightType_Directional,false,true,false); 
IMPLEMENT_INJECTION_PIXELSHADER_TYPE(LightType_Point,true,true,true); 
IMPLEMENT_INJECTION_PIXELSHADER_TYPE(LightType_Point,false,true,true); 
IMPLEMENT_INJECTION_PIXELSHADER_TYPE(LightType_Point,true,true,false); 
IMPLEMENT_INJECTION_PIXELSHADER_TYPE(LightType_Point,false,true,false); 

/** Versions without a light function. */
IMPLEMENT_INJECTION_PIXELSHADER_TYPE(LightType_Directional,true,false,false); 
IMPLEMENT_INJECTION_PIXELSHADER_TYPE(LightType_Directional,false,false,false); 
IMPLEMENT_INJECTION_PIXELSHADER_TYPE(LightType_Point,true,false,true); 
IMPLEMENT_INJECTION_PIXELSHADER_TYPE(LightType_Point,false,false,true); 
IMPLEMENT_INJECTION_PIXELSHADER_TYPE(LightType_Point,true,false,false); 
IMPLEMENT_INJECTION_PIXELSHADER_TYPE(LightType_Point,false,false,false); 

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

void FDeferredShadingSceneRenderer::InitTranslucentVolumeLighting(FRDGBuilder& GraphBuilder, ERDGPassFlags PassFlags, FTranslucentVolumeLightingTextures& Textures)
{
	RDG_GPU_STAT_SCOPE(GraphBuilder, TranslucentLighting);

	Textures.VolumeDim = GetTranslucencyLightingVolumeDim();
	const FIntVector TranslucencyLightingVolumeDim(Textures.VolumeDim);

	{
		// TODO: We can skip the and TLV allocations when rendering in forward shading mode
		const ETextureCreateFlags TranslucencyTargetFlags = TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_ReduceMemoryWithTilingMode | TexCreate_UAV;

		const int32 ViewCount = Views.Num();
		Textures.Ambient.SetNum(ViewCount * TVC_MAX);
		Textures.Directional.SetNum(ViewCount * TVC_MAX);

		int32 TextureIndex = 0;
		for (int32 ViewIndex = 0; ViewIndex < ViewCount; ++ViewIndex)
		{
			for (int32 CascadeIndex = 0; CascadeIndex < TVC_MAX; ++CascadeIndex)
			{
				FRDGEventName& AmbientName = *GraphBuilder.AllocObject<FRDGEventName>(RDG_EVENT_NAME("TranslucentVolumeAmbient%d", TextureIndex));
				FRDGEventName& DirectionalName = *GraphBuilder.AllocObject<FRDGEventName>(RDG_EVENT_NAME("TranslucentVolumeDirectional%d", TextureIndex));

				FRDGTextureRef AmbientTexture = GraphBuilder.CreateTexture(
					FRDGTextureDesc::Create3D(
						TranslucencyLightingVolumeDim,
						PF_FloatRGBA,
						FClearValueBinding::Transparent,
						TranslucencyTargetFlags),
					AmbientName.GetTCHAR());

				Textures.SetAmbient(ViewIndex, CascadeIndex, AmbientTexture);

				FRDGTextureRef DirectionalTexture = GraphBuilder.CreateTexture(
					FRDGTextureDesc::Create3D(
						TranslucencyLightingVolumeDim,
						PF_FloatRGBA,
						FClearValueBinding::Transparent,
						TranslucencyTargetFlags),
					DirectionalName.GetTCHAR());

				Textures.SetDirectional(ViewIndex, CascadeIndex, DirectionalTexture);
				TextureIndex++;
			}
		}
	}

	const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(TranslucencyLightingVolumeDim, FClearTranslucentLightingVolumeCS::CLEAR_BLOCK_SIZE);

	TShaderMapRef<FClearTranslucentLightingVolumeCS> ComputeShader(GetGlobalShaderMap(FeatureLevel));

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		auto* PassParameters = GraphBuilder.AllocParameters<FClearTranslucentLightingVolumeCS::FParameters>();
		PassParameters->RWAmbient0 = GraphBuilder.CreateUAV(Textures.GetAmbient(ViewIndex, 0));
		PassParameters->RWAmbient1 = GraphBuilder.CreateUAV(Textures.GetAmbient(ViewIndex, 1));
		PassParameters->RWDirectional0 = GraphBuilder.CreateUAV(Textures.GetDirectional(ViewIndex, 0));
		PassParameters->RWDirectional1 = GraphBuilder.CreateUAV(Textures.GetDirectional(ViewIndex, 1));

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ClearTranslucencyLightingVolumeCompute %d", Textures.VolumeDim),
			PassFlags,
			ComputeShader,
			PassParameters,
			GroupCount);
	}
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

void FDeferredShadingSceneRenderer::InjectAmbientCubemapTranslucentVolumeLighting(
	FRDGBuilder& GraphBuilder,
	const FTranslucentVolumeLightingTextures& Textures,
	const FViewInfo& View,
	int32 ViewIndex)
{
	if (!GUseTranslucentLightingVolumes || !GSupportsVolumeTextureRendering || !View.FinalPostProcessSettings.ContributingCubemaps.Num())
	{
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "InjectAmbientCubemapTranslucentVolumeLighting");
	RDG_GPU_STAT_SCOPE(GraphBuilder, TranslucentLighting);

	const int32 TranslucencyLightingVolumeDim = Textures.VolumeDim;
	const FVolumeBounds VolumeBounds(TranslucencyLightingVolumeDim);

	auto ShaderMap = GetGlobalShaderMap(FeatureLevel);

	for (int32 VolumeCascadeIndex = 0; VolumeCascadeIndex < TVC_MAX; ++VolumeCascadeIndex)
	{
		FRDGTextureRef VolumeAmbientTexture = Textures.GetAmbient(ViewIndex, VolumeCascadeIndex);

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
				[&View, PassParameters, ShaderMap, VolumeBounds, TranslucencyLightingVolumeDim](FRHICommandList& RHICmdList)
			{
				TShaderMapRef<FWriteToSliceVS> VertexShader(ShaderMap);
				TOptionalShaderMapRef<FWriteToSliceGS> GeometryShader(ShaderMap);
				TShaderMapRef<FInjectAmbientCubemapPS> PixelShader(ShaderMap);

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
 * Helper function for finding and setting the right version of TTranslucentLightingInjectPS given template parameters.
 * @param MaterialProxy must not be 0
 * @param InnerSplitIndex todo: get from ShadowMap, INDEX_NONE if no directional light
 */
template<ELightComponentType InjectionType, bool bDynamicallyShadowed>
void SetInjectionShader(
	FRHICommandList& RHICmdList,
	FGraphicsPipelineStateInitializer& GraphicsPSOInit,
	const FViewInfo& View, 
	const FMaterialRenderProxy* MaterialProxy,
	const FLightSceneInfo* LightSceneInfo, 
	const FProjectedShadowInfo* ShadowMap, 
	int32 InnerSplitIndex, 
	int32 VolumeCascadeIndexValue,
	const TShaderRef<FWriteToSliceVS>& VertexShader,
	const TShaderRef<FWriteToSliceGS>& GeometryShader,
	bool bApplyLightFunction,
	bool bInverseSquared)
{
	check(ShadowMap || !bDynamicallyShadowed);

	const FMaterialShaderMap* MaterialShaderMap = MaterialProxy->GetMaterialWithFallback(View.GetFeatureLevel(), MaterialProxy).GetRenderingThreadShaderMap();
	TShaderRef<FMaterialShader> PixelShader;

	const bool Directional = InjectionType == LightType_Directional;

	if (bApplyLightFunction)
	{
		if( bInverseSquared )
		{
			auto InjectionPixelShader = MaterialShaderMap->GetShader< TTranslucentLightingInjectPS<InjectionType, bDynamicallyShadowed, true, true && !Directional> >();
			PixelShader = InjectionPixelShader;
		}
		else
		{
			auto InjectionPixelShader = MaterialShaderMap->GetShader< TTranslucentLightingInjectPS<InjectionType, bDynamicallyShadowed, true, false> >();
			PixelShader = InjectionPixelShader;
		}
	}
	else
	{
		if( bInverseSquared )
		{
			auto InjectionPixelShader = MaterialShaderMap->GetShader< TTranslucentLightingInjectPS<InjectionType, bDynamicallyShadowed, false, true && !Directional> >();
			PixelShader = InjectionPixelShader;
		}
		else
		{
			auto InjectionPixelShader = MaterialShaderMap->GetShader< TTranslucentLightingInjectPS<InjectionType, bDynamicallyShadowed, false, false> >();
			PixelShader = InjectionPixelShader;
		}
	}
	
	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GScreenVertexDeclaration.VertexDeclarationRHI;
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
	GraphicsPSOInit.BoundShaderState.GeometryShaderRHI = GeometryShader.GetGeometryShader();
#endif
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

	// Now shader is set, bind parameters
	if (bApplyLightFunction)
	{
		if( bInverseSquared )
		{
			auto InjectionPixelShader = MaterialShaderMap->GetShader< TTranslucentLightingInjectPS<InjectionType, bDynamicallyShadowed, true, true && !Directional> >();
			InjectionPixelShader->SetParameters(RHICmdList, View, LightSceneInfo, MaterialProxy, ShadowMap, InnerSplitIndex, VolumeCascadeIndexValue);
		}
		else
		{
			auto InjectionPixelShader = MaterialShaderMap->GetShader< TTranslucentLightingInjectPS<InjectionType, bDynamicallyShadowed, true, false> >();
			InjectionPixelShader->SetParameters(RHICmdList, View, LightSceneInfo, MaterialProxy, ShadowMap, InnerSplitIndex, VolumeCascadeIndexValue);
		}
	}
	else
	{
		if( bInverseSquared )
		{
			auto InjectionPixelShader = MaterialShaderMap->GetShader< TTranslucentLightingInjectPS<InjectionType, bDynamicallyShadowed, false, true && !Directional> >();
			InjectionPixelShader->SetParameters(RHICmdList, View, LightSceneInfo, MaterialProxy, ShadowMap, InnerSplitIndex, VolumeCascadeIndexValue);
		}
		else
		{
			auto InjectionPixelShader = MaterialShaderMap->GetShader< TTranslucentLightingInjectPS<InjectionType, bDynamicallyShadowed, false, false> >();
			InjectionPixelShader->SetParameters(RHICmdList, View, LightSceneInfo, MaterialProxy, ShadowMap, InnerSplitIndex, VolumeCascadeIndexValue);
		}
	}
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
	FDeferredShadingSceneRenderer& SceneRenderer,
	const FLightSceneInfo& LightSceneInfo, 
	const FProjectedShadowInfo* InProjectedShadowInfo,
	TArray<FTranslucentLightInjectionData, SceneRenderingAllocator>& LightInjectionData)
{
	if (LightSceneInfo.Proxy->AffectsTranslucentLighting())
	{
		const FVisibleLightInfo& VisibleLightInfo = SceneRenderer.VisibleLightInfos[LightSceneInfo.Id];

		const ERHIFeatureLevel::Type FeatureLevel = SceneRenderer.Scene->GetFeatureLevel();

		const bool bApplyLightFunction = (SceneRenderer.ViewFamily.EngineShowFlags.LightFunctions &&
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

static FRDGTextureRef GetSkyTransmittanceLutTexture(FRDGBuilder& GraphBuilder, FScene* Scene, const FViewInfo& View)
{
	FRDGTextureRef TransmittanceLutTexture = nullptr;
	if (ShouldRenderSkyAtmosphere(Scene, View.Family->EngineShowFlags))
	{
		if (FSkyAtmosphereRenderSceneInfo* SkyInfo = Scene->GetSkyAtmosphereSceneInfo())
		{
			TRefCountPtr<IPooledRenderTarget>& TransmittanceLutTarget = SkyInfo->GetTransmittanceLutTexture();
			TransmittanceLutTexture = GraphBuilder.RegisterExternalTexture(TransmittanceLutTarget, TEXT("TransmittanceLutTexture"));
		}
	}
	return TransmittanceLutTexture;
}

BEGIN_SHADER_PARAMETER_STRUCT(FInjectTranslucentLightArrayParameters, )
	RDG_TEXTURE_ACCESS(TransmittanceLutTexture, ERHIAccess::SRVGraphics)
	RDG_TEXTURE_ACCESS(ShadowDepthTexture, ERHIAccess::SRVGraphics)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

/** Injects all the lights in LightInjectionData into the translucent lighting volume textures. */
static void InjectTranslucentLightArray(
	FRDGBuilder& GraphBuilder,
	const FTranslucentVolumeLightingTextures& Textures,
	FScene* Scene,
	const FViewInfo& View,
	TArrayView<const FTranslucentLightInjectionData> LightInjectionData,
	int32 ViewIndex)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get();
	INC_DWORD_STAT_BY(STAT_NumLightsInjectedIntoTranslucency, LightInjectionData.Num());

	FRDGTextureRef TransmittanceLutTexture = GetSkyTransmittanceLutTexture(GraphBuilder, Scene, View);

	// Inject into each volume cascade
	// Operate on one cascade at a time to reduce render target switches
	for (uint32 VolumeCascadeIndex = 0; VolumeCascadeIndex < TVC_MAX; VolumeCascadeIndex++)
	{
		FRDGTextureRef VolumeAmbientTexture = Textures.GetAmbient(ViewIndex, VolumeCascadeIndex);
		FRDGTextureRef VolumeDirectionalTexture = Textures.GetDirectional(ViewIndex, VolumeCascadeIndex);

		for (int32 LightIndex = 0; LightIndex < LightInjectionData.Num(); LightIndex++)
		{
			const FTranslucentLightInjectionData& InjectionData = LightInjectionData[LightIndex];
			const FLightSceneInfo* const LightSceneInfo = InjectionData.LightSceneInfo;
			const bool bInverseSquared = LightSceneInfo->Proxy->IsInverseSquared();
			const bool bDirectionalLight = LightSceneInfo->Proxy->GetLightType() == LightType_Directional;
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

				auto* PassParameters = GraphBuilder.AllocParameters<FInjectTranslucentLightArrayParameters>();
				PassParameters->TransmittanceLutTexture = TransmittanceLutTexture;
				PassParameters->ShadowDepthTexture = ShadowDepthTexture;
				PassParameters->RenderTargets[0] = FRenderTargetBinding(VolumeAmbientTexture, ERenderTargetLoadAction::ELoad);
				PassParameters->RenderTargets[1] = FRenderTargetBinding(VolumeDirectionalTexture, ERenderTargetLoadAction::ELoad);

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("InjectTranslucentLightArray"),
					PassParameters,
					ERDGPassFlags::Raster,
					[VertexShader, GeometryShader, &View, &InjectionData, LightSceneInfo, bInverseSquared, bDirectionalLight, VolumeBounds, VolumeCascadeIndex](FRHICommandList& RHICmdList)
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

						if (InjectionData.ProjectedShadowInfo)
						{
							// shadows, restricting light contribution to the cascade bounds (except last cascade far to get light functions and no shadows there)
							SetInjectionShader<LightType_Directional, true>(RHICmdList, GraphicsPSOInit, View, InjectionData.LightFunctionMaterialProxy, LightSceneInfo,
								InjectionData.ProjectedShadowInfo, InjectionData.ProjectedShadowInfo->CascadeSettings.ShadowSplitIndex, VolumeCascadeIndex,
								VertexShader, GeometryShader, InjectionData.bApplyLightFunction, false);
						}
						else
						{
							// no shadows
							SetInjectionShader<LightType_Directional, false>(RHICmdList, GraphicsPSOInit, View, InjectionData.LightFunctionMaterialProxy, LightSceneInfo,
								InjectionData.ProjectedShadowInfo, INDEX_NONE, VolumeCascadeIndex,
								VertexShader, GeometryShader, InjectionData.bApplyLightFunction, false);
						}
					}
					else
					{
						// Accumulate the contribution of multiple lights
						GraphicsPSOInit.BlendState = TStaticBlendState<
							CW_RGB, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_One,
							CW_RGB, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_One>::GetRHI();

						if (InjectionData.ProjectedShadowInfo)
						{
							SetInjectionShader<LightType_Point, true>(RHICmdList, GraphicsPSOInit, View, InjectionData.LightFunctionMaterialProxy, LightSceneInfo,
								InjectionData.ProjectedShadowInfo, INDEX_NONE, VolumeCascadeIndex,
								VertexShader, GeometryShader, InjectionData.bApplyLightFunction, bInverseSquared);
						}
						else
						{
							SetInjectionShader<LightType_Point, false>(RHICmdList, GraphicsPSOInit, View, InjectionData.LightFunctionMaterialProxy, LightSceneInfo,
								InjectionData.ProjectedShadowInfo, INDEX_NONE, VolumeCascadeIndex,
								VertexShader, GeometryShader, InjectionData.bApplyLightFunction, bInverseSquared);
						}
					}

					const int32 TranslucencyLightingVolumeDim = GetTranslucencyLightingVolumeDim();

					VertexShader->SetParameters(RHICmdList, VolumeBounds, FIntVector(TranslucencyLightingVolumeDim));
					if (GeometryShader.IsValid())
					{
						GeometryShader->SetParameters(RHICmdList, VolumeBounds.MinZ);
					}
					RasterizeToVolumeTexture(RHICmdList, VolumeBounds);
				});
			}
		}
	}
}

void FDeferredShadingSceneRenderer::InjectTranslucentVolumeLighting(
	FRDGBuilder& GraphBuilder,
	const FTranslucentVolumeLightingTextures& Textures,
	const FLightSceneInfo& LightSceneInfo,
	const FProjectedShadowInfo* InProjectedShadowInfo,
	const FViewInfo& View,
	int32 ViewIndex)
{
	if (GUseTranslucentLightingVolumes && GSupportsVolumeTextureRendering)
	{
		SCOPE_CYCLE_COUNTER(STAT_TranslucentInjectTime);

		auto& LightInjectionData = *GraphBuilder.AllocObject<TArray<FTranslucentLightInjectionData, SceneRenderingAllocator>>();

		AddLightForInjection(*this, LightSceneInfo, InProjectedShadowInfo, LightInjectionData);

		// shadowed or unshadowed (InProjectedShadowInfo==0)
		InjectTranslucentLightArray(GraphBuilder, Textures, Scene, View, LightInjectionData, ViewIndex);
	}
}

void FDeferredShadingSceneRenderer::InjectTranslucentVolumeLightingArray(
	FRDGBuilder& GraphBuilder,
	const FTranslucentVolumeLightingTextures& Textures,
	const TArray<FSortedLightSceneInfo, SceneRenderingAllocator>& SortedLights,
	int32 FirstLightIndex,
	int32 LightsEndIndex)
{
	SCOPE_CYCLE_COUNTER(STAT_TranslucentInjectTime);

	using FLightInjectionData = TArray<TArray<FTranslucentLightInjectionData, SceneRenderingAllocator>>;
	auto& LightInjectionData = *GraphBuilder.AllocObject<FLightInjectionData>();
	LightInjectionData.SetNum(Views.Num());

	for (int32 ViewIndex = 0; ViewIndex < LightInjectionData.Num(); ++ViewIndex)
	{
		LightInjectionData[ViewIndex].Reserve(LightsEndIndex - FirstLightIndex);
	}

	for (int32 LightIndex = FirstLightIndex; LightIndex < LightsEndIndex; LightIndex++)
	{
		const FSortedLightSceneInfo& SortedLightInfo = SortedLights[LightIndex];
		const FLightSceneInfo* const LightSceneInfo = SortedLightInfo.LightSceneInfo;
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (LightSceneInfo->ShouldRenderLight(Views[ViewIndex]))
			{
				AddLightForInjection(*this, *LightSceneInfo, NULL, LightInjectionData[ViewIndex]);
			}
		}
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		// non-shadowed, non-light function lights
		InjectTranslucentLightArray(GraphBuilder, Textures, Scene, Views[ViewIndex], LightInjectionData[ViewIndex], ViewIndex);
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

void FDeferredShadingSceneRenderer::InjectSimpleTranslucentVolumeLightingArray(
	FRDGBuilder& GraphBuilder,
	const FTranslucentVolumeLightingTextures& Textures,
	const FSimpleLightArray& SimpleLights,
	const FViewInfo& View,
	const int32 ViewIndex)
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
			RDG_EVENT_SCOPE(GraphBuilder, "Cascade%d", VolumeCascadeIndex);
			FRDGTextureRef VolumeAmbientTexture = Textures.GetAmbient(ViewIndex, VolumeCascadeIndex);
			FRDGTextureRef VolumeDirectionalTexture = Textures.GetDirectional(ViewIndex, VolumeCascadeIndex);

			for (int32 LightIndex = 0; LightIndex < SimpleLights.InstanceData.Num(); LightIndex++)
			{
				const FSimpleLightEntry& SimpleLight = SimpleLights.InstanceData[LightIndex];
				const FSimpleLightPerViewEntry& SimpleLightPerViewData = SimpleLights.GetViewDependentData(LightIndex, ViewIndex, Views.Num());

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

void FDeferredShadingSceneRenderer::FilterTranslucentVolumeLighting(
	FRDGBuilder& GraphBuilder,
	FTranslucentVolumeLightingTextures& Textures,
	const FViewInfo& View,
	const int32 ViewIndex)
{
	if (!GUseTranslucentLightingVolumes || !GSupportsVolumeTextureRendering || !GUseTranslucencyVolumeBlur)
	{
		return;
	}

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get();

	FRHISamplerState* SamplerStateRHI = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	const int32 TranslucencyLightingVolumeDim = GetTranslucencyLightingVolumeDim();
	RDG_EVENT_SCOPE(GraphBuilder, "FilterTranslucentVolume %dx%dx%d Cascades:%d", TranslucencyLightingVolumeDim, TranslucencyLightingVolumeDim, TranslucencyLightingVolumeDim, TVC_MAX);
	RDG_GPU_STAT_SCOPE(GraphBuilder, TranslucentLighting);

	for (int32 VolumeCascadeIndex = 0; VolumeCascadeIndex < TVC_MAX; VolumeCascadeIndex++)
	{
		FRDGTextureRef InputVolumeAmbientTexture = Textures.GetAmbient(ViewIndex, VolumeCascadeIndex);
		FRDGTextureRef InputVolumeDirectionalTexture = Textures.GetDirectional(ViewIndex, VolumeCascadeIndex);

		FRDGTextureRef OutputVolumeAmbientTexture = GraphBuilder.CreateTexture(InputVolumeAmbientTexture->Desc, InputVolumeAmbientTexture->Name);
		FRDGTextureRef OutputVolumeDirectionalTexture = GraphBuilder.CreateTexture(InputVolumeDirectionalTexture->Desc, InputVolumeDirectionalTexture->Name);

		Textures.SetAmbient(ViewIndex, VolumeCascadeIndex, OutputVolumeAmbientTexture);
		Textures.SetDirectional(ViewIndex, VolumeCascadeIndex, OutputVolumeDirectionalTexture);

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
