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

/** Shader parameters for rendering the depth of a mesh for shadowing. */
class FShadowDepthShaderParameters
{
	DECLARE_INLINE_TYPE_LAYOUT(FShadowDepthShaderParameters, NonVirtual);
public:

	void Bind(const FShaderParameterMap& ParameterMap)
	{
		ProjectionMatrix.Bind(ParameterMap,TEXT("ProjectionMatrix"));
		ShadowParams.Bind(ParameterMap,TEXT("ShadowParams"));
		ClampToNearPlane.Bind(ParameterMap,TEXT("bClampToNearPlane"));
	}

	template<typename ShaderRHIParamRef>
	void Set(FRHICommandList& RHICmdList, ShaderRHIParamRef ShaderRHI, const FSceneView& View, const FProjectedShadowInfo* ShadowInfo, const FMaterialRenderProxy* MaterialRenderProxy)
	{
		SetShaderValue(
			RHICmdList, 
			ShaderRHI,
			ProjectionMatrix,
			FTranslationMatrix(ShadowInfo->PreShadowTranslation - View.ViewMatrices.GetPreViewTranslation()) * ShadowInfo->SubjectAndReceiverMatrix
			);

		SetShaderValue(RHICmdList, ShaderRHI, ShadowParams, FVector4(ShadowInfo->GetShaderDepthBias(), ShadowInfo->GetShaderSlopeDepthBias(), ShadowInfo->GetShaderMaxSlopeDepthBias(), ShadowInfo->InvMaxSubjectDepth));
		// Only clamp vertices to the near plane when rendering whole scene directional light shadow depths or preshadows from directional lights
		const bool bClampToNearPlaneValue = ShadowInfo->IsWholeSceneDirectionalShadow() || (ShadowInfo->bPreShadow && ShadowInfo->bDirectionalLight);
		SetShaderValue(RHICmdList, ShaderRHI,ClampToNearPlane,bClampToNearPlaneValue ? 1.0f : 0.0f);
	}

	/** Set the vertex shader parameter values. */
	void SetVertexShader(FRHICommandList& RHICmdList, FRHIVertexShader* ShaderRHI, const FSceneView& View, const FProjectedShadowInfo* ShadowInfo, const FMaterialRenderProxy* MaterialRenderProxy)
	{
		Set(RHICmdList, ShaderRHI, View, ShadowInfo, MaterialRenderProxy);
	}

	/** Set the domain shader parameter values. */
	void SetDomainShader(FRHICommandList& RHICmdList, FRHIDomainShader* ShaderRHI, const FSceneView& View, const FProjectedShadowInfo* ShadowInfo, const FMaterialRenderProxy* MaterialRenderProxy)
	{
		Set(RHICmdList, ShaderRHI, View, ShadowInfo, MaterialRenderProxy);
	}

	/** Serializer. */
	friend FArchive& operator<<(FArchive& Ar,FShadowDepthShaderParameters& P)
	{
		Ar << P.ProjectionMatrix;
		Ar << P.ShadowParams;
		Ar << P.ClampToNearPlane;
		return Ar;
	}

private:
	
		LAYOUT_FIELD(FShaderParameter, ProjectionMatrix)
		LAYOUT_FIELD(FShaderParameter, ShadowParams)
		LAYOUT_FIELD(FShaderParameter, ClampToNearPlane)
	
};

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

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FTranslucencyDepthPassUniformParameters, "TranslucentDepthPass");

void SetupTranslucencyDepthPassUniformBuffer(
	const FProjectedShadowInfo* ShadowInfo,
	FRHICommandList& RHICmdList,
	const FViewInfo& View,
	FTranslucencyDepthPassUniformParameters& TranslucencyDepthPassParameters)
{
	// Note - scene depth can be bound by the material for use in depth fades
	// This is incorrect when rendering a shadowmap as it's not from the camera's POV
	// Set the scene depth texture to something safe when rendering shadow depths
	FSceneRenderTargets& SceneRenderTargets = FSceneRenderTargets::Get(RHICmdList);
	SetupSceneTextureUniformParameters(SceneRenderTargets, View.FeatureLevel, ESceneTextureSetupMode::None, TranslucencyDepthPassParameters.SceneTextures);

	TranslucencyDepthPassParameters.ProjectionMatrix = FTranslationMatrix(ShadowInfo->PreShadowTranslation - View.ViewMatrices.GetPreViewTranslation()) * ShadowInfo->SubjectAndReceiverMatrix;

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
	{
		PassUniformBuffer.Bind(Initializer.ParameterMap, FTranslucencyDepthPassUniformParameters::StaticStructMetadata.GetShaderVariableName());
	}

	
	
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
	{
	}

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

	FTranslucencyShadowDepthPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FMeshMaterialShader(Initializer)
	{
		PassUniformBuffer.Bind(Initializer.ParameterMap, FTranslucencyDepthPassUniformParameters::StaticStructMetadata.GetShaderVariableName());

		TranslucentShadowStartOffset.Bind(Initializer.ParameterMap, TEXT("TranslucentShadowStartOffset"));
	}

	FTranslucencyShadowDepthPS() {}

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
	DECLARE_SHADER_TYPE(TTranslucencyShadowDepthPS,MeshMaterial);
public:

	TTranslucencyShadowDepthPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FTranslucencyShadowDepthPS(Initializer)
	{
	}

	TTranslucencyShadowDepthPS() {}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment )
	{
		FTranslucencyShadowDepthPS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("PERSPECTIVE_CORRECT_DEPTH"), (uint32)(ShaderMode == TranslucencyShadowDepth_PerspectiveCorrect ? 1 : 0));
	}
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

void FProjectedShadowInfo::RenderTranslucencyDepths(FRHICommandList& RHICmdList, FSceneRenderer* SceneRenderer)
{
	check(RHICmdList.IsInsideRenderPass());
	check(IsInRenderingThread());
	checkSlow(!bWholeSceneShadow);
	SCOPE_CYCLE_COUNTER(STAT_RenderPerObjectShadowDepthsTime);

	FTranslucencyDepthPassUniformParameters TranslucencyDepthPassParameters;
	SetupTranslucencyDepthPassUniformBuffer(this, RHICmdList, *ShadowDepthView, TranslucencyDepthPassParameters);
	TUniformBufferRef<FTranslucencyDepthPassUniformParameters> PassUniformBuffer = TUniformBufferRef<FTranslucencyDepthPassUniformParameters>::CreateUniformBufferImmediate(TranslucencyDepthPassParameters, UniformBuffer_SingleFrame, EUniformBufferValidation::None);

	FMeshPassProcessorRenderState DrawRenderState(*ShadowDepthView, PassUniformBuffer);
	{
#if WANTS_DRAW_MESH_EVENTS
		FString EventName;
		if (GetEmitDrawEvents())
		{
			GetShadowTypeNameForDrawEvent(EventName);
		}
		SCOPED_DRAW_EVENTF(RHICmdList, EventShadowDepthActor, *EventName);
#endif
		// Clear the shadow and its border
		RHICmdList.SetViewport(
			X,
			Y,
			0.0f,
			(X + BorderSize * 2 + ResolutionX),
			(Y + BorderSize * 2 + ResolutionY),
			1.0f
			);

		FLinearColor ClearColors[2] = {FLinearColor(0,0,0,0), FLinearColor(0,0,0,0)};
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
	}
}

/** Pixel shader used to filter a single volume lighting cascade. */
class FFilterTranslucentVolumePS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FFilterTranslucentVolumePS,Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) 
	{ 
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && (RHISupportsGeometryShaders(Parameters.Platform) || RHISupportsVertexShaderLayer(Parameters.Platform));
	}

	FFilterTranslucentVolumePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
		TexelSize.Bind(Initializer.ParameterMap, TEXT("TexelSize"));
		TranslucencyLightingVolumeAmbient.Bind(Initializer.ParameterMap, TEXT("TranslucencyLightingVolumeAmbient"));
		TranslucencyLightingVolumeAmbientSampler.Bind(Initializer.ParameterMap, TEXT("TranslucencyLightingVolumeAmbientSampler"));
		TranslucencyLightingVolumeDirectional.Bind(Initializer.ParameterMap, TEXT("TranslucencyLightingVolumeDirectional"));
		TranslucencyLightingVolumeDirectionalSampler.Bind(Initializer.ParameterMap, TEXT("TranslucencyLightingVolumeDirectionalSampler"));
	}
	FFilterTranslucentVolumePS() {}

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View, int32 VolumeCascadeIndex, const int32 ViewIndex)
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		const int32 TranslucencyLightingVolumeDim = GetTranslucencyLightingVolumeDim();
		SetShaderValue(RHICmdList, ShaderRHI, TexelSize, 1.0f / TranslucencyLightingVolumeDim);

		SetTextureParameter(
			RHICmdList,
			ShaderRHI, 
			TranslucencyLightingVolumeAmbient, 
			TranslucencyLightingVolumeAmbientSampler, 
			TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), 
			SceneContext.TranslucencyLightingVolumeAmbient[VolumeCascadeIndex + NumTranslucentVolumeRenderTargetSets * ViewIndex]->GetRenderTargetItem().ShaderResourceTexture);

		SetTextureParameter(
			RHICmdList,
			ShaderRHI, 
			TranslucencyLightingVolumeDirectional, 
			TranslucencyLightingVolumeDirectionalSampler, 
			TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), 
			SceneContext.TranslucencyLightingVolumeDirectional[VolumeCascadeIndex + NumTranslucentVolumeRenderTargetSets * ViewIndex]->GetRenderTargetItem().ShaderResourceTexture);
	}

private:
	LAYOUT_FIELD(FShaderParameter, TexelSize);
	LAYOUT_FIELD(FShaderResourceParameter, TranslucencyLightingVolumeAmbient);
	LAYOUT_FIELD(FShaderResourceParameter, TranslucencyLightingVolumeAmbientSampler);
	LAYOUT_FIELD(FShaderResourceParameter, TranslucencyLightingVolumeDirectional);
	LAYOUT_FIELD(FShaderResourceParameter, TranslucencyLightingVolumeDirectionalSampler);
};

IMPLEMENT_SHADER_TYPE(,FFilterTranslucentVolumePS,TEXT("/Engine/Private/TranslucentLightingShaders.usf"),TEXT("FilterMainPS"),SF_Pixel);

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

	/** Serializer. */ 
	/*friend FArchive& operator<<(FArchive& Ar,FTranslucentInjectParameters& P)
	{
		Ar << P.WorldToShadowMatrix;
		Ar << P.ShadowmapMinMax;
		Ar << P.VolumeCascadeIndex;
		return Ar;
	}*/

private:
	
		LAYOUT_FIELD(FShaderParameter, WorldToShadowMatrix)
		LAYOUT_FIELD(FShaderParameter, ShadowmapMinMax)
		LAYOUT_FIELD(FShaderParameter, VolumeCascadeIndex)
	
};

/** Pixel shader used to accumulate per-object translucent shadows into a volume texture. */
class FTranslucentObjectShadowingPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FTranslucentObjectShadowingPS,Global);
public:

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("INJECTION_PIXEL_SHADER"), 1);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && (RHISupportsGeometryShaders(Parameters.Platform) || RHISupportsVertexShaderLayer(Parameters.Platform));
	}

	FTranslucentObjectShadowingPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
		TranslucentInjectParameters.Bind(Initializer.ParameterMap);
	}
	FTranslucentObjectShadowingPS() {}

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View, const FLightSceneInfo* LightSceneInfo, const FProjectedShadowInfo* ShadowMap, uint32 VolumeCascadeIndex)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundPixelShader(), View.ViewUniformBuffer);
		TranslucentInjectParameters.Set(RHICmdList, RHICmdList.GetBoundPixelShader(), this, View, LightSceneInfo, ShadowMap, VolumeCascadeIndex, true);

		FTranslucentSelfShadowUniformParameters TranslucentSelfShadowUniformParameters;
		SetupTranslucentSelfShadowUniformParameters(ShadowMap, TranslucentSelfShadowUniformParameters);
		SetUniformBufferParameterImmediate(RHICmdList, RHICmdList.GetBoundPixelShader(), GetUniformBufferParameter<FTranslucentSelfShadowUniformParameters>(), TranslucentSelfShadowUniformParameters);
	}

private:
	LAYOUT_FIELD(FTranslucentInjectParameters, TranslucentInjectParameters);
};

IMPLEMENT_SHADER_TYPE(,FTranslucentObjectShadowingPS,TEXT("/Engine/Private/TranslucentLightingShaders.usf"),TEXT("PerObjectShadowingMainPS"),SF_Pixel);

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

			const bool bLight0CloudPerPixelTransmittance = CloudInfo && View.VolumetricCloudShadowRenderTarget[0].IsValid() && AtmosphereLight0Proxy && AtmosphereLight0Proxy == LightSceneInfo->Proxy;
			const bool bLight1CloudPerPixelTransmittance = CloudInfo && View.VolumetricCloudShadowRenderTarget[1].IsValid() && AtmosphereLight1Proxy && AtmosphereLight1Proxy == LightSceneInfo->Proxy;

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
					View.VolumetricCloudShadowRenderTarget[LightIndex]->GetRenderTargetItem().ShaderResourceTexture);

				if (bLight0CloudPerPixelTransmittance)
				{
					SetShaderValue(RHICmdList, ShaderRHI, VolumetricCloudShadowmapStrength, AtmosphereLight0Proxy->GetCloudShadowOnSurfaceStrength());
				}
				else if(bLight1CloudPerPixelTransmittance)
				{
					SetShaderValue(RHICmdList, ShaderRHI, VolumetricCloudShadowmapStrength, AtmosphereLight1Proxy->GetCloudShadowOnSurfaceStrength());
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

void FDeferredShadingSceneRenderer::ClearTranslucentVolumeLighting(FRDGBuilder& GraphBuilder, int32 ViewIndex)
{
	if (!GUseTranslucentLightingVolumes || !GSupportsVolumeTextureRendering)
	{
		return;
	}

	RDG_GPU_STAT_SCOPE(GraphBuilder, TranslucentLighting);

	AddPass(GraphBuilder, RDG_EVENT_NAME("ClearTranslucentVolumeLighting"), [this, ViewIndex](FRHICommandListImmediate& RHICmdList)
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		SceneContext.ClearTranslucentVolumeLighting(RHICmdList, ViewIndex);
	});
}

class FClearTranslucentLightingVolumeCS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FClearTranslucentLightingVolumeCS, Global)
public:

	static const int32 CLEAR_BLOCK_SIZE = 4;

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

	FClearTranslucentLightingVolumeCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		Ambient0.Bind(Initializer.ParameterMap, TEXT("Ambient0"));
		Directional0.Bind(Initializer.ParameterMap, TEXT("Directional0"));
		Ambient1.Bind(Initializer.ParameterMap, TEXT("Ambient1"));
		Directional1.Bind(Initializer.ParameterMap, TEXT("Directional1"));
	}

	FClearTranslucentLightingVolumeCS()
	{
	}

	void SetParameters(
		FRHIAsyncComputeCommandListImmediate& RHICmdList,
		FRHIUnorderedAccessView** VolumeUAVs,
		int32 NumUAVs
	)
	{
		check(NumUAVs == 4);
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();
		Ambient0.SetTexture(RHICmdList, ShaderRHI, NULL, VolumeUAVs[0]);
		Directional0.SetTexture(RHICmdList, ShaderRHI, NULL, VolumeUAVs[1]);
		Ambient1.SetTexture(RHICmdList, ShaderRHI, NULL, VolumeUAVs[2]);
		Directional1.SetTexture(RHICmdList, ShaderRHI, NULL, VolumeUAVs[3]);
	}

	void UnsetParameters(FRHIAsyncComputeCommandListImmediate& RHICmdList)
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();
		Ambient0.UnsetUAV(RHICmdList, ShaderRHI);
		Directional0.UnsetUAV(RHICmdList, ShaderRHI);
		Ambient1.UnsetUAV(RHICmdList, ShaderRHI);
		Directional1.UnsetUAV(RHICmdList, ShaderRHI);
	}

private:
	LAYOUT_FIELD(FRWShaderParameter, Ambient0);
	LAYOUT_FIELD(FRWShaderParameter, Directional0);
	LAYOUT_FIELD(FRWShaderParameter, Ambient1);
	LAYOUT_FIELD(FRWShaderParameter, Directional1);
};

IMPLEMENT_SHADER_TYPE(, FClearTranslucentLightingVolumeCS, TEXT("/Engine/Private/TranslucentLightInjectionShaders.usf"), TEXT("ClearTranslucentLightingVolumeCS"), SF_Compute)

void FDeferredShadingSceneRenderer::ClearTranslucentVolumeLightingAsyncCompute(FRHICommandListImmediate& RHICmdList)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	const int32 NumUAVs = 4;

	TShaderMapRef<FClearTranslucentLightingVolumeCS> ComputeShader(GetGlobalShaderMap(FeatureLevel));

	TArray<FRHIUnorderedAccessView*, SceneRenderingAllocator> VolumeUAVs;
	VolumeUAVs.Reserve(Views.Num() * NumUAVs);
	for (int i = 0; i < Views.Num(); ++i)
	{
		VolumeUAVs.Add(SceneContext.TranslucencyLightingVolumeAmbient[(i * NumTranslucentVolumeRenderTargetSets)]->GetRenderTargetItem().UAV);
		VolumeUAVs.Add(SceneContext.TranslucencyLightingVolumeDirectional[(i * NumTranslucentVolumeRenderTargetSets)]->GetRenderTargetItem().UAV);
		VolumeUAVs.Add(SceneContext.TranslucencyLightingVolumeAmbient[(i * NumTranslucentVolumeRenderTargetSets) + 1]->GetRenderTargetItem().UAV);
		VolumeUAVs.Add(SceneContext.TranslucencyLightingVolumeDirectional[(i * NumTranslucentVolumeRenderTargetSets) + 1]->GetRenderTargetItem().UAV);
	}

	TArray<FRHITransitionInfo, SceneRenderingAllocator> UAVTransitions;
	TArray<FRHITransitionInfo, SceneRenderingAllocator> SRVTransitions;
	for (FRHIUnorderedAccessView* UAV : VolumeUAVs)
	{
		UAVTransitions.Add(FRHITransitionInfo(UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
		SRVTransitions.Add(FRHITransitionInfo(UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
	}

	// Transition should have already been ended before
	check(TranslucencyLightingVolumeClearEndTransition == nullptr);
	const FRHITransition* UAVTransition = RHICreateTransition(ERHIPipeline::Graphics, ERHIPipeline::AsyncCompute, ERHICreateTransitionFlags::None, MakeArrayView(UAVTransitions.GetData(), UAVTransitions.Num()));
	TranslucencyLightingVolumeClearEndTransition = RHICreateTransition(ERHIPipeline::AsyncCompute, ERHIPipeline::Graphics, ERHICreateTransitionFlags::None, MakeArrayView(SRVTransitions.GetData(), SRVTransitions.Num()));

	// Begin the UAV transition on the Gfx pipe so the async clear compute shader won't clear until the Gfx pipe is caught up.
	RHICmdList.BeginTransition(UAVTransition);

	const int32 TranslucencyLightingVolumeDim = GetTranslucencyLightingVolumeDim();

	//Grab the async compute commandlist.
	FRHIAsyncComputeCommandListImmediate& RHICmdListComputeImmediate = FRHICommandListExecutor::GetImmediateAsyncComputeCommandList();
	{
		SCOPED_COMPUTE_EVENTF(RHICmdListComputeImmediate, ClearTranslucencyLightingVolume, TEXT("ClearTranslucencyLightingVolumeCompute %d"), TranslucencyLightingVolumeDim);

		// we must wait on the transition from the Gfx pipe to let us know all our dependencies are ready.
		RHICmdListComputeImmediate.EndTransition(UAVTransition);

		for (int i = 0; i < Views.Num(); ++i)
		{
			//standard compute setup, but on the async commandlist.
			RHICmdListComputeImmediate.SetComputeShader(ComputeShader.GetComputeShader());

			ComputeShader->SetParameters(RHICmdListComputeImmediate, VolumeUAVs.GetData() + i * NumUAVs, NumUAVs);
		
			int32 GroupsPerDim = TranslucencyLightingVolumeDim / FClearTranslucentLightingVolumeCS::CLEAR_BLOCK_SIZE;
			DispatchComputeShader(RHICmdListComputeImmediate, ComputeShader.GetShader(), GroupsPerDim, GroupsPerDim, GroupsPerDim);

			ComputeShader->UnsetParameters(RHICmdListComputeImmediate);
		}

		//transition the output to readable on the async compute queue - gfx will call end transition when needed
		RHICmdListComputeImmediate.BeginTransition(TranslucencyLightingVolumeClearEndTransition);
	}

	//immediately dispatch our async compute commands to the RHI thread to be submitted to the GPU as soon as possible.
	//dispatch after the scope so the drawevent pop is inside the dispatch
	FRHIAsyncComputeCommandListImmediate::ImmediateDispatch(RHICmdListComputeImmediate);
}

/** Encapsulates a pixel shader that is adding ambient cubemap to the volume. */
class FInjectAmbientCubemapPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FInjectAmbientCubemapPS, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	/** Default constructor. */
	FInjectAmbientCubemapPS() {}

public:
	LAYOUT_FIELD(FCubemapShaderParameters, CubemapShaderParameters)

	/** Initialization constructor. */
	FInjectAmbientCubemapPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		CubemapShaderParameters.Bind(Initializer.ParameterMap);
	}

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View, const FFinalPostProcessSettings::FCubemapEntry& CubemapEntry)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		CubemapShaderParameters.SetParameters(RHICmdList, ShaderRHI, CubemapEntry);
	}
};

IMPLEMENT_SHADER_TYPE(,FInjectAmbientCubemapPS,TEXT("/Engine/Private/TranslucentLightingShaders.usf"),TEXT("InjectAmbientCubemapMainPS"),SF_Pixel);

void FDeferredShadingSceneRenderer::InjectAmbientCubemapTranslucentVolumeLighting(FRDGBuilder& GraphBuilder, const FViewInfo& View, int32 ViewIndex)
{
	if (!GUseTranslucentLightingVolumes || !GSupportsVolumeTextureRendering || !View.FinalPostProcessSettings.ContributingCubemaps.Num())
	{
		return;
	}

	AddUntrackedAccessPass(GraphBuilder, [this, &View, ViewIndex](FRHICommandList& RHICmdList)
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

		SCOPED_DRAW_EVENT(RHICmdList, InjectAmbientCubemapTranslucentVolumeLighting);
		SCOPED_GPU_STAT(RHICmdList, TranslucentLighting);

		const int32 TranslucencyLightingVolumeDim = GetTranslucencyLightingVolumeDim();

		const FVolumeBounds VolumeBounds(TranslucencyLightingVolumeDim);

		auto ShaderMap = GetGlobalShaderMap(FeatureLevel);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI();

		for (int32 VolumeCascadeIndex = 0; VolumeCascadeIndex < TVC_MAX; VolumeCascadeIndex++)
		{
			//Checks to detect/prevent UE-31578
			const IPooledRenderTarget* RT0 = SceneContext.TranslucencyLightingVolumeAmbient[VolumeCascadeIndex + NumTranslucentVolumeRenderTargetSets * ViewIndex];

			// we don't update the directional volume (could be a HQ option)
			FRHIRenderPassInfo RPInfo(RT0->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::Load_Store);
			TransitionRenderPassTargets(RHICmdList, RPInfo);
			RHICmdList.BeginRenderPass(RPInfo, TEXT("InjectAmbientCubemapTranslucentVolumeLighting"));
			{
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

				TShaderMapRef<FWriteToSliceVS> VertexShader(ShaderMap);
				TOptionalShaderMapRef<FWriteToSliceGS> GeometryShader(ShaderMap);
				TShaderMapRef<FInjectAmbientCubemapPS> PixelShader(ShaderMap);

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

				uint32 Count = View.FinalPostProcessSettings.ContributingCubemaps.Num();
				for (uint32 i = 0; i < Count; ++i)
				{
					const FFinalPostProcessSettings::FCubemapEntry& CubemapEntry = View.FinalPostProcessSettings.ContributingCubemaps[i];

					PixelShader->SetParameters(RHICmdList, View, CubemapEntry);

					RasterizeToVolumeTexture(RHICmdList, VolumeBounds);
				}
			}
			RHICmdList.EndRenderPass();
			RHICmdList.CopyToResolveTarget(RT0->GetRenderTargetItem().TargetableTexture, RT0->GetRenderTargetItem().ShaderResourceTexture, FResolveParams());
		}
	});
}

void FDeferredShadingSceneRenderer::ClearTranslucentVolumePerObjectShadowing(FRHICommandList& RHICmdList, const int32 ViewIndex)
{
	if (GUseTranslucentLightingVolumes && GSupportsVolumeTextureRendering)
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		SCOPED_DRAW_EVENT(RHICmdList, ClearTranslucentVolumePerLightShadowing);
		SCOPED_GPU_STAT(RHICmdList, TranslucentLighting);

		static_assert(TVC_MAX == 2, "Only expecting two translucency lighting cascades.");
		FRHITexture* RenderTargets[2];
		RenderTargets[0] = SceneContext.GetTranslucencyVolumeAmbient(TVC_Inner, ViewIndex)->GetRenderTargetItem().TargetableTexture;
		RenderTargets[1] = SceneContext.GetTranslucencyVolumeDirectional(TVC_Inner, ViewIndex)->GetRenderTargetItem().TargetableTexture;

		FLinearColor ClearColors[2];
		ClearColors[0] = FLinearColor(1, 1, 1, 1);
		ClearColors[1] = FLinearColor(1, 1, 1, 1);

		FSceneRenderTargets::ClearVolumeTextures<UE_ARRAY_COUNT(RenderTargets)>(RHICmdList, FeatureLevel, RenderTargets, ClearColors);
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

void FDeferredShadingSceneRenderer::AccumulateTranslucentVolumeObjectShadowing(FRHICommandList& RHICmdList, const FProjectedShadowInfo* InProjectedShadowInfo, bool bClearVolume, const FViewInfo& View, const int32 ViewIndex)
{
	const FLightSceneInfo* LightSceneInfo = &InProjectedShadowInfo->GetLightSceneInfo();

	if (bClearVolume)
	{
		ClearTranslucentVolumePerObjectShadowing(RHICmdList, ViewIndex);
	}

	if (GUseTranslucentLightingVolumes && GSupportsVolumeTextureRendering)
	{
		SCOPED_DRAW_EVENT(RHICmdList, AccumulateTranslucentVolumeShadowing);
		SCOPED_GPU_STAT(RHICmdList, TranslucentLighting);

		auto ShaderMap = GetGlobalShaderMap(FeatureLevel);

		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		// Inject into each volume cascade
		for (uint32 VolumeCascadeIndex = 0; VolumeCascadeIndex < TVC_MAX; VolumeCascadeIndex++)
		{
			const bool bDirectionalLight = LightSceneInfo->Proxy->GetLightType() == LightType_Directional;
			const FVolumeBounds VolumeBounds = CalculateLightVolumeBounds(LightSceneInfo->Proxy->GetBoundingSphere(), View, VolumeCascadeIndex, bDirectionalLight);

			if (VolumeBounds.IsValid())
			{
				FRHITexture* RenderTarget;

				if (VolumeCascadeIndex == 0)
				{
					RenderTarget = SceneContext.GetTranslucencyVolumeAmbient(TVC_Inner, ViewIndex)->GetRenderTargetItem().TargetableTexture;
				}
				else
				{
					RenderTarget = SceneContext.GetTranslucencyVolumeDirectional(TVC_Inner, ViewIndex)->GetRenderTargetItem().TargetableTexture;
				}

				FRHIRenderPassInfo RPInfo(RenderTarget, ERenderTargetActions::Load_Store);
				RHICmdList.BeginRenderPass(RPInfo, TEXT("AccumulateVolumeObjectShadowing"));
				{
					FGraphicsPipelineStateInitializer GraphicsPSOInit;
					RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

					GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

					// Modulate the contribution of multiple object shadows in rgb
					GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_DestColor, BF_Zero>::GetRHI();

					TShaderMapRef<FWriteToSliceVS> VertexShader(ShaderMap);
					TOptionalShaderMapRef<FWriteToSliceGS> GeometryShader(ShaderMap);
					TShaderMapRef<FTranslucentObjectShadowingPS> PixelShader(ShaderMap);

					GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GScreenVertexDeclaration.VertexDeclarationRHI;
					GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
					GraphicsPSOInit.BoundShaderState.GeometryShaderRHI = GeometryShader.GetGeometryShader();
#endif
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
					GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

					const int32 TranslucencyLightingVolumeDim = GetTranslucencyLightingVolumeDim();

					VertexShader->SetParameters(RHICmdList, VolumeBounds, FIntVector(TranslucencyLightingVolumeDim));
					if(GeometryShader.IsValid())
					{
						GeometryShader->SetParameters(RHICmdList, VolumeBounds.MinZ);
					}
					PixelShader->SetParameters(RHICmdList, View, LightSceneInfo, InProjectedShadowInfo, VolumeCascadeIndex);
				
					RasterizeToVolumeTexture(RHICmdList, VolumeBounds);
				}
				RHICmdList.EndRenderPass();

				RHICmdList.CopyToResolveTarget(SceneContext.GetTranslucencyVolumeAmbient((ETranslucencyVolumeCascade)VolumeCascadeIndex, ViewIndex)->GetRenderTargetItem().TargetableTexture,
					SceneContext.GetTranslucencyVolumeAmbient((ETranslucencyVolumeCascade)VolumeCascadeIndex, ViewIndex)->GetRenderTargetItem().ShaderResourceTexture, FResolveParams());
			}
		}
	}
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
END_SHADER_PARAMETER_STRUCT()

/** Injects all the lights in LightInjectionData into the translucent lighting volume textures. */
static void InjectTranslucentLightArray(
	FRDGBuilder& GraphBuilder,
	FScene* Scene,
	const FViewInfo& View,
	const TArray<FTranslucentLightInjectionData, SceneRenderingAllocator>& LightInjectionData,
	int32 ViewIndex)
{
 	FInjectTranslucentLightArrayParameters* PassParameters = GraphBuilder.AllocParameters<FInjectTranslucentLightArrayParameters>();
 	PassParameters->TransmittanceLutTexture = GetSkyTransmittanceLutTexture(GraphBuilder, Scene, View);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("InjectTranslucentLightArray"),
		PassParameters,
		ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass | ERDGPassFlags::UntrackedAccess | ERDGPassFlags::NeverCull,
		[&View, &LightInjectionData, ViewIndex](FRHICommandListImmediate& RHICmdList)
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
		INC_DWORD_STAT_BY(STAT_NumLightsInjectedIntoTranslucency, LightInjectionData.Num());

		// Inject into each volume cascade
		// Operate on one cascade at a time to reduce render target switches
		for (uint32 VolumeCascadeIndex = 0; VolumeCascadeIndex < TVC_MAX; VolumeCascadeIndex++)
		{
			IPooledRenderTarget* RT0 = SceneContext.TranslucencyLightingVolumeAmbient[VolumeCascadeIndex + NumTranslucentVolumeRenderTargetSets * ViewIndex];
			IPooledRenderTarget* RT1 = SceneContext.TranslucencyLightingVolumeDirectional[VolumeCascadeIndex + NumTranslucentVolumeRenderTargetSets * ViewIndex];

			GVisualizeTexture.SetCheckPoint(RHICmdList, RT0);
			GVisualizeTexture.SetCheckPoint(RHICmdList, RT1);

			FRHITexture* RenderTargets[2];
			RenderTargets[0] = RT0->GetRenderTargetItem().TargetableTexture;
			RenderTargets[1] = RT1->GetRenderTargetItem().TargetableTexture;

			FRHIRenderPassInfo RPInfo(UE_ARRAY_COUNT(RenderTargets), RenderTargets, ERenderTargetActions::Load_Store);
			TransitionRenderPassTargets(RHICmdList, RPInfo);

			RHICmdList.BeginRenderPass(RPInfo, TEXT("InjectTranslucentLightArray"));
			{
				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
				GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

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
					}
				}
			}
			RHICmdList.EndRenderPass();
			RHICmdList.CopyToResolveTarget(RT0->GetRenderTargetItem().TargetableTexture, RT0->GetRenderTargetItem().ShaderResourceTexture, FResolveParams());
			RHICmdList.CopyToResolveTarget(RT1->GetRenderTargetItem().TargetableTexture, RT1->GetRenderTargetItem().ShaderResourceTexture, FResolveParams());
		}
	});
}

void FDeferredShadingSceneRenderer::InjectTranslucentVolumeLighting(FRDGBuilder& GraphBuilder, const FLightSceneInfo& LightSceneInfo, const FProjectedShadowInfo* InProjectedShadowInfo, const FViewInfo& View, int32 ViewIndex)
{
	if (GUseTranslucentLightingVolumes && GSupportsVolumeTextureRendering)
	{
		SCOPE_CYCLE_COUNTER(STAT_TranslucentInjectTime);

		auto& LightInjectionData = *GraphBuilder.AllocObject<TArray<FTranslucentLightInjectionData, SceneRenderingAllocator>>();

		AddLightForInjection(*this, LightSceneInfo, InProjectedShadowInfo, LightInjectionData);

		// shadowed or unshadowed (InProjectedShadowInfo==0)
		InjectTranslucentLightArray(GraphBuilder, Scene, View, LightInjectionData, ViewIndex);
	}
}

void FDeferredShadingSceneRenderer::InjectTranslucentVolumeLightingArray(FRDGBuilder& GraphBuilder, const TArray<FSortedLightSceneInfo, SceneRenderingAllocator>& SortedLights, int32 FirstLightIndex, int32 LightsEndIndex)
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
		InjectTranslucentLightArray(GraphBuilder, Scene, Views[ViewIndex], LightInjectionData[ViewIndex], ViewIndex);
	}
}

/** Pixel shader used to inject simple lights into the translucent lighting volume */
class FSimpleLightTranslucentLightingInjectPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FSimpleLightTranslucentLightingInjectPS,Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) 
	{ 
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && (RHISupportsGeometryShaders(Parameters.Platform) || RHISupportsVertexShaderLayer(Parameters.Platform));
	}

	FSimpleLightTranslucentLightingInjectPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer):
		FGlobalShader(Initializer)
	{
		VolumeCascadeIndex.Bind(Initializer.ParameterMap, TEXT("VolumeCascadeIndex"));
		SimpleLightPositionAndRadius.Bind(Initializer.ParameterMap, TEXT("SimpleLightPositionAndRadius"));
		SimpleLightColorAndExponent.Bind(Initializer.ParameterMap, TEXT("SimpleLightColorAndExponent"));
	}
	FSimpleLightTranslucentLightingInjectPS() {}

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View, const FSimpleLightEntry& SimpleLight, const FSimpleLightPerViewEntry& SimpleLightPerViewData, int32 VolumeCascadeIndexValue)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundPixelShader(), View.ViewUniformBuffer);

		FVector4 PositionAndRadius(SimpleLightPerViewData.Position, SimpleLight.Radius);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), VolumeCascadeIndex, VolumeCascadeIndexValue);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), SimpleLightPositionAndRadius, PositionAndRadius);

		FVector4 LightColorAndExponent(SimpleLight.Color, SimpleLight.Exponent);

		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), SimpleLightColorAndExponent, LightColorAndExponent);
	}

private:
	LAYOUT_FIELD(FShaderParameter, VolumeCascadeIndex);
	LAYOUT_FIELD(FShaderParameter, SimpleLightPositionAndRadius);
	LAYOUT_FIELD(FShaderParameter, SimpleLightColorAndExponent);
};

IMPLEMENT_SHADER_TYPE(,FSimpleLightTranslucentLightingInjectPS,TEXT("/Engine/Private/TranslucentLightInjectionShaders.usf"),TEXT("SimpleLightInjectMainPS"),SF_Pixel);

void FDeferredShadingSceneRenderer::InjectSimpleTranslucentVolumeLightingArray(FRDGBuilder& GraphBuilder, const FSimpleLightArray& SimpleLights, const FViewInfo& View, const int32 ViewIndex)
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
		AddUntrackedAccessPass(GraphBuilder, [this, &SimpleLights, &View, ViewIndex, NumLightsToInject](FRHICommandListImmediate& RHICmdList)
		{
			INC_DWORD_STAT_BY(STAT_NumLightsInjectedIntoTranslucency, NumLightsToInject);
			FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

			// Inject into each volume cascade
			// Operate on one cascade at a time to reduce render target switches
			for (int32 VolumeCascadeIndex = 0; VolumeCascadeIndex < TVC_MAX; VolumeCascadeIndex++)
			{
				IPooledRenderTarget* RT0 = SceneContext.TranslucencyLightingVolumeAmbient[VolumeCascadeIndex + NumTranslucentVolumeRenderTargetSets * ViewIndex];
				IPooledRenderTarget* RT1 = SceneContext.TranslucencyLightingVolumeDirectional[VolumeCascadeIndex + NumTranslucentVolumeRenderTargetSets * ViewIndex];

				GVisualizeTexture.SetCheckPoint(RHICmdList, RT0);
				GVisualizeTexture.SetCheckPoint(RHICmdList, RT1);

				FRHITexture* RenderTargets[2];
				RenderTargets[0] = RT0->GetRenderTargetItem().TargetableTexture;
				RenderTargets[1] = RT1->GetRenderTargetItem().TargetableTexture;

				FRHIRenderPassInfo RPInfo(UE_ARRAY_COUNT(RenderTargets), RenderTargets, ERenderTargetActions::Load_Store);
				TransitionRenderPassTargets(RHICmdList, RPInfo);
				RHICmdList.BeginRenderPass(RPInfo, TEXT("InjectSimpleTranslucentVolumeLightingArray"));
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
								TShaderMapRef<FWriteToSliceVS> VertexShader(View.ShaderMap);
								TOptionalShaderMapRef<FWriteToSliceGS> GeometryShader(View.ShaderMap);
								TShaderMapRef<FSimpleLightTranslucentLightingInjectPS> PixelShader(View.ShaderMap);

								GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GScreenVertexDeclaration.VertexDeclarationRHI;
								GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
								GraphicsPSOInit.BoundShaderState.GeometryShaderRHI = GeometryShader.GetGeometryShader();
#endif
								GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

								SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

								const int32 TranslucencyLightingVolumeDim = GetTranslucencyLightingVolumeDim();

								VertexShader->SetParameters(RHICmdList, VolumeBounds, FIntVector(TranslucencyLightingVolumeDim));
								if (GeometryShader.IsValid())
								{
									GeometryShader->SetParameters(RHICmdList, VolumeBounds.MinZ);
								}
								PixelShader->SetParameters(RHICmdList, View, SimpleLight, SimpleLightPerViewData, VolumeCascadeIndex);



								RasterizeToVolumeTexture(RHICmdList, VolumeBounds);
							}
						}
					}
				}
				RHICmdList.EndRenderPass();
				RHICmdList.CopyToResolveTarget(RT0->GetRenderTargetItem().TargetableTexture, RT0->GetRenderTargetItem().ShaderResourceTexture, FResolveParams());
				RHICmdList.CopyToResolveTarget(RT1->GetRenderTargetItem().TargetableTexture, RT1->GetRenderTargetItem().ShaderResourceTexture, FResolveParams());
			}
		});
	}
}

void FDeferredShadingSceneRenderer::FilterTranslucentVolumeLighting(FRDGBuilder& GraphBuilder, const FViewInfo& View, const int32 ViewIndex)
{
	if (!GUseTranslucentLightingVolumes || !GSupportsVolumeTextureRendering || !GUseTranslucencyVolumeBlur)
	{
		return;
	}

	AddUntrackedAccessPass(GraphBuilder, [this, &View, ViewIndex](FRHICommandListImmediate& RHICmdList)
	{
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

		const int32 TranslucencyLightingVolumeDim = GetTranslucencyLightingVolumeDim();
		SCOPED_DRAW_EVENTF(RHICmdList, FilterTranslucentVolume, TEXT("FilterTranslucentVolume %dx%dx%d Cascades:%d"),
			TranslucencyLightingVolumeDim, TranslucencyLightingVolumeDim, TranslucencyLightingVolumeDim, TVC_MAX);

		SCOPED_GPU_STAT(RHICmdList, TranslucentLighting);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();

		bool bTransitionedToWriteable = (GUseTranslucentLightingVolumes && GSupportsVolumeTextureRendering && View.FinalPostProcessSettings.ContributingCubemaps.Num());

		// Filter each cascade
		for (int32 VolumeCascadeIndex = 0; VolumeCascadeIndex < TVC_MAX; VolumeCascadeIndex++)
		{
			IPooledRenderTarget* RT0 = SceneContext.GetTranslucencyVolumeAmbient((ETranslucencyVolumeCascade)VolumeCascadeIndex, ViewIndex);
			IPooledRenderTarget* RT1 = SceneContext.GetTranslucencyVolumeDirectional((ETranslucencyVolumeCascade)VolumeCascadeIndex, ViewIndex);

			const IPooledRenderTarget* Input0 = SceneContext.TranslucencyLightingVolumeAmbient[VolumeCascadeIndex + NumTranslucentVolumeRenderTargetSets * ViewIndex];
			const IPooledRenderTarget* Input1 = SceneContext.TranslucencyLightingVolumeDirectional[VolumeCascadeIndex + NumTranslucentVolumeRenderTargetSets * ViewIndex];

			GVisualizeTexture.SetCheckPoint(RHICmdList, RT0);
			GVisualizeTexture.SetCheckPoint(RHICmdList, RT1);

			FRHITexture* RenderTargets[2];
			RenderTargets[0] = RT0->GetRenderTargetItem().TargetableTexture;
			RenderTargets[1] = RT1->GetRenderTargetItem().TargetableTexture;

			FRHITransitionInfo Inputs[2];
			Inputs[0] = FRHITransitionInfo(Input0->GetRenderTargetItem().TargetableTexture, ERHIAccess::Unknown, ERHIAccess::SRVGraphics);
			Inputs[1] = FRHITransitionInfo(Input1->GetRenderTargetItem().TargetableTexture, ERHIAccess::Unknown, ERHIAccess::SRVGraphics);

			static_assert(TVC_MAX == 2, "Final transition logic should change");

			//the volume textures should still be writable from the injection phase on the first loop.
			if (!bTransitionedToWriteable || VolumeCascadeIndex > 0)
			{
				FRHITransitionInfo RTVTransitions[2];
				for (int RT = 0; RT < 2; ++RT)
				{
					RTVTransitions[RT] = FRHITransitionInfo(RenderTargets[RT], ERHIAccess::Unknown, ERHIAccess::RTV);
				}
				RHICmdList.Transition(MakeArrayView(RTVTransitions, 2));
			}
			RHICmdList.Transition(MakeArrayView(Inputs, 2));

			FRHIRenderPassInfo RPInfo(UE_ARRAY_COUNT(RenderTargets), RenderTargets, ERenderTargetActions::Load_Store);
			TransitionRenderPassTargets(RHICmdList, RPInfo);
			RHICmdList.BeginRenderPass(RPInfo, TEXT("FilterTranslucentVolumeLighting"));
			{
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

				const FVolumeBounds VolumeBounds(TranslucencyLightingVolumeDim);
				TShaderMapRef<FWriteToSliceVS> VertexShader(View.ShaderMap);
				TOptionalShaderMapRef<FWriteToSliceGS> GeometryShader(View.ShaderMap);
				TShaderMapRef<FFilterTranslucentVolumePS> PixelShader(View.ShaderMap);

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
				PixelShader->SetParameters(RHICmdList, View, VolumeCascadeIndex, ViewIndex);

				RasterizeToVolumeTexture(RHICmdList, VolumeBounds);
			}
			RHICmdList.EndRenderPass();

			//only do readable transition on the final loop since the other ones will do this up front.
			//if (VolumeCascadeIndex == TVC_MAX - 1)
			{
				FRHITransitionInfo SRVTransitions[2];
				for (int RT = 0; RT < 2; ++RT)
				{
					SRVTransitions[RT] = FRHITransitionInfo(RenderTargets[RT], ERHIAccess::Unknown, ERHIAccess::SRVMask);
				}
				RHICmdList.Transition(MakeArrayView(SRVTransitions, 2));
			}
		}
	});
}
