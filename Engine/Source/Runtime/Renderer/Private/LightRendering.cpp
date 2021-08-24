// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LightRendering.cpp: Light rendering implementation.
=============================================================================*/

#include "LightRendering.h"
#include "RendererModule.h"
#include "DeferredShadingRenderer.h"
#include "LightPropagationVolume.h"
#include "ScenePrivate.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PipelineStateCache.h"
#include "ClearQuad.h"
#include "Engine/SubsurfaceProfile.h"
#include "ShowFlags.h"
#include "VisualizeTexture.h"
#include "RayTracing/RaytracingOptions.h"
#include "SceneTextureParameters.h"
#include "HairStrands/HairStrandsRendering.h"
#include "ScreenPass.h"
#include "SkyAtmosphereRendering.h"
#include "VolumetricCloudRendering.h"

// ENABLE_DEBUG_DISCARD_PROP is used to test the lighting code by allowing to discard lights to see how performance scales
// It ought never to be enabled in a shipping build, and is probably only really useful when woring on the shading code.
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	#define ENABLE_DEBUG_DISCARD_PROP 1
#else // (UE_BUILD_SHIPPING || UE_BUILD_TEST)
	#define ENABLE_DEBUG_DISCARD_PROP 0
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

DECLARE_GPU_STAT(Lights);

IMPLEMENT_TYPE_LAYOUT(FLightFunctionSharedParameters);
IMPLEMENT_TYPE_LAYOUT(FStencilingGeometryShaderParameters);
IMPLEMENT_TYPE_LAYOUT(FOnePassPointShadowProjectionShaderParameters);
IMPLEMENT_TYPE_LAYOUT(FShadowProjectionShaderParameters);

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FDeferredLightUniformStruct, "DeferredLightUniforms");

extern int32 GUseTranslucentLightingVolumes;
ENGINE_API IPooledRenderTarget* GetSubsufaceProfileTexture_RT(FRHICommandListImmediate& RHICmdList);


static int32 GAllowDepthBoundsTest = 1;
static FAutoConsoleVariableRef CVarAllowDepthBoundsTest(
	TEXT("r.AllowDepthBoundsTest"),
	GAllowDepthBoundsTest,
	TEXT("If true, use enable depth bounds test when rendering defered lights.")
	);

static int32 bAllowSimpleLights = 1;
static FAutoConsoleVariableRef CVarAllowSimpleLights(
	TEXT("r.AllowSimpleLights"),
	bAllowSimpleLights,
	TEXT("If true, we allow simple (ie particle) lights")
);

static TAutoConsoleVariable<int32> CVarRayTracingOcclusion(
	TEXT("r.RayTracing.Shadows"),
	1,
	TEXT("0: use traditional rasterized shadow map\n")
	TEXT("1: use ray tracing shadows (default)"),
	ECVF_RenderThreadSafe);

static int32 GShadowRayTracingSamplesPerPixel = -1;
static FAutoConsoleVariableRef CVarShadowRayTracingSamplesPerPixel(
	TEXT("r.RayTracing.Shadow.SamplesPerPixel"),
	GShadowRayTracingSamplesPerPixel,
	TEXT("Sets the samples-per-pixel for directional light occlusion (default = 1)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarShadowUseDenoiser(
	TEXT("r.Shadow.Denoiser"),
	2,
	TEXT("Choose the denoising algorithm.\n")
	TEXT(" 0: Disabled (default);\n")
	TEXT(" 1: Forces the default denoiser of the renderer;\n")
	TEXT(" 2: GScreenSpaceDenoiser witch may be overriden by a third party plugin.\n"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMaxShadowDenoisingBatchSize(
	TEXT("r.Shadow.Denoiser.MaxBatchSize"), 4,
	TEXT("Maximum number of shadow to denoise at the same time."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMaxShadowRayTracingBatchSize(
	TEXT("r.RayTracing.Shadow.MaxBatchSize"), 8,
	TEXT("Maximum number of shadows to trace at the same time."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarAllowClearLightSceneExtentsOnly(
	TEXT("r.AllowClearLightSceneExtentsOnly"), 1,
	TEXT(""),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRayTracingShadowsDirectionalLight(
	TEXT("r.RayTracing.Shadows.Lights.Directional"),
	1,
	TEXT("Enables ray tracing shadows for directional lights (default = 1)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRayTracingShadowsPointLight(
	TEXT("r.RayTracing.Shadows.Lights.Point"),
	1,
	TEXT("Enables ray tracing shadows for point lights (default = 1)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRayTracingShadowsSpotLight(
	TEXT("r.RayTracing.Shadows.Lights.Spot"),
	1,
	TEXT("Enables ray tracing shadows for spot lights (default = 1)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRayTracingShadowsRectLight(
	TEXT("r.RayTracing.Shadows.Lights.Rect"),
	1,
	TEXT("Enables ray tracing shadows for rect light (default = 1)"),
	ECVF_RenderThreadSafe);

#if ENABLE_DEBUG_DISCARD_PROP
static float GDebugLightDiscardProp = 0.0f;
static FAutoConsoleVariableRef CVarDebugLightDiscardProp(
	TEXT("r.DebugLightDiscardProp"),
	GDebugLightDiscardProp,
	TEXT("[0,1]: Proportion of lights to discard for debug/performance profiling purposes.")
);
#endif // ENABLE_DEBUG_DISCARD_PROP

#if RHI_RAYTRACING

static bool ShouldRenderRayTracingShadowsForLightType(ELightComponentType LightType)
{
	switch(LightType)
	{
	case LightType_Directional:
		return !!CVarRayTracingShadowsDirectionalLight.GetValueOnRenderThread();
	case LightType_Point:
		return !!CVarRayTracingShadowsPointLight.GetValueOnRenderThread();
	case LightType_Spot:
		return !!CVarRayTracingShadowsSpotLight.GetValueOnRenderThread();
	case LightType_Rect:
		return !!CVarRayTracingShadowsRectLight.GetValueOnRenderThread();
	default:
		return true;	
	}	
}

bool ShouldRenderRayTracingShadows()
{
	const bool bIsStereo = GEngine->StereoRenderingDevice.IsValid() && GEngine->StereoRenderingDevice->IsStereoEnabled();
	const bool bHairStrands = IsHairStrandsEnabled(EHairStrandsShaderType::Strands);

	return ShouldRenderRayTracingEffect((CVarRayTracingOcclusion.GetValueOnRenderThread() > 0) && !(bIsStereo && bHairStrands) );
}

bool ShouldRenderRayTracingShadowsForLight(const FLightSceneProxy& LightProxy)
{
	return ShouldRenderRayTracingShadows() && LightProxy.CastsRaytracedShadow()
		&& ShouldRenderRayTracingShadowsForLightType((ELightComponentType)LightProxy.GetLightType());
}

bool ShouldRenderRayTracingShadowsForLight(const FLightSceneInfoCompact& LightInfo)
{
	return ShouldRenderRayTracingShadows() && LightInfo.bCastRaytracedShadow
		&& ShouldRenderRayTracingShadowsForLightType((ELightComponentType)LightInfo.LightType);
}
#endif // RHI_RAYTRACING

FDeferredLightUniformStruct GetDeferredLightParameters(const FSceneView& View, const FLightSceneInfo& LightSceneInfo)
{
	FDeferredLightUniformStruct Parameters;
	LightSceneInfo.Proxy->GetLightShaderParameters(Parameters.LightParameters);

	const bool bIsRayTracedLight = ShouldRenderRayTracingShadowsForLight(*LightSceneInfo.Proxy);

	const FVector2D FadeParams = LightSceneInfo.Proxy->GetDirectionalLightDistanceFadeParameters(View.GetFeatureLevel(), !bIsRayTracedLight && LightSceneInfo.IsPrecomputedLightingValid(), View.MaxShadowCascades);
	
	// use MAD for efficiency in the shader
	Parameters.DistanceFadeMAD = FVector2D(FadeParams.Y, -FadeParams.X * FadeParams.Y);
	
	int32 ShadowMapChannel = LightSceneInfo.Proxy->GetShadowMapChannel();

	static const auto AllowStaticLightingVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));
	const bool bAllowStaticLighting = (!AllowStaticLightingVar || AllowStaticLightingVar->GetValueOnRenderThread() != 0);

	if (!bAllowStaticLighting)
	{
		ShadowMapChannel = INDEX_NONE;
	}

	Parameters.ShadowMapChannelMask = FVector4(
		ShadowMapChannel == 0 ? 1 : 0,
		ShadowMapChannel == 1 ? 1 : 0,
		ShadowMapChannel == 2 ? 1 : 0,
		ShadowMapChannel == 3 ? 1 : 0);

	const bool bDynamicShadows = View.Family->EngineShowFlags.DynamicShadows && GetShadowQuality() > 0;
	const bool bHasLightFunction = LightSceneInfo.Proxy->GetLightFunctionMaterial() != NULL;
	Parameters.ShadowedBits = LightSceneInfo.Proxy->CastsStaticShadow() || bHasLightFunction ? 1 : 0;
	Parameters.ShadowedBits |= LightSceneInfo.Proxy->CastsDynamicShadow() && View.Family->EngineShowFlags.DynamicShadows ? 3 : 0;

	Parameters.VolumetricScatteringIntensity = LightSceneInfo.Proxy->GetVolumetricScatteringIntensity();

	static auto* ContactShadowsCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.ContactShadows"));
	static auto* IntensityCVar = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.ContactShadows.NonShadowCastingIntensity"));

	Parameters.ContactShadowLength = 0;
	Parameters.ContactShadowNonShadowCastingIntensity = 0.0f;

	if (ContactShadowsCVar && ContactShadowsCVar->GetValueOnRenderThread() != 0 && View.Family->EngineShowFlags.ContactShadows)
	{
		Parameters.ContactShadowLength = LightSceneInfo.Proxy->GetContactShadowLength();
		// Sign indicates if contact shadow length is in world space or screen space.
		// Multiply by 2 for screen space in order to preserve old values after introducing multiply by View.ClipToView[1][1] in shader.
		Parameters.ContactShadowLength *= LightSceneInfo.Proxy->IsContactShadowLengthInWS() ? -1.0f : 2.0f;

		Parameters.ContactShadowNonShadowCastingIntensity = IntensityCVar ? IntensityCVar->GetValueOnRenderThread() : 0.0f;
	}

	// When rendering reflection captures, the direct lighting of the light is actually the indirect specular from the main view
	if (View.bIsReflectionCapture)
	{
		Parameters.LightParameters.Color *= LightSceneInfo.Proxy->GetIndirectLightingScale();
	}

	const ELightComponentType LightType = (ELightComponentType)LightSceneInfo.Proxy->GetLightType();
	if ((LightType == LightType_Point || LightType == LightType_Spot || LightType == LightType_Rect) && View.IsPerspectiveProjection())
	{
		Parameters.LightParameters.Color *= GetLightFadeFactor(View, LightSceneInfo.Proxy);
	}

	Parameters.LightingChannelMask = LightSceneInfo.Proxy->GetLightingChannelMask();

	return Parameters;
}

void SetupSimpleDeferredLightParameters(
	const FSimpleLightEntry& SimpleLight,
	const FSimpleLightPerViewEntry &SimpleLightPerViewData,
	FDeferredLightUniformStruct& DeferredLightUniformsValue)
{
	DeferredLightUniformsValue.LightParameters.Position = SimpleLightPerViewData.Position;
	DeferredLightUniformsValue.LightParameters.InvRadius = 1.0f / FMath::Max(SimpleLight.Radius, KINDA_SMALL_NUMBER);
	DeferredLightUniformsValue.LightParameters.Color = SimpleLight.Color;
	DeferredLightUniformsValue.LightParameters.FalloffExponent = SimpleLight.Exponent;
	DeferredLightUniformsValue.LightParameters.Direction = FVector(1, 0, 0);
	DeferredLightUniformsValue.LightParameters.Tangent = FVector(1, 0, 0);
	DeferredLightUniformsValue.LightParameters.SpotAngles = FVector2D(-2, 1);
	DeferredLightUniformsValue.LightParameters.SpecularScale = 1.0f;
	DeferredLightUniformsValue.LightParameters.SourceRadius = 0.0f;
	DeferredLightUniformsValue.LightParameters.SoftSourceRadius = 0.0f;
	DeferredLightUniformsValue.LightParameters.SourceLength = 0.0f;
	DeferredLightUniformsValue.LightParameters.SourceTexture = GWhiteTexture->TextureRHI;
	DeferredLightUniformsValue.ContactShadowLength = 0.0f;
	DeferredLightUniformsValue.DistanceFadeMAD = FVector2D(0, 0);
	DeferredLightUniformsValue.ShadowMapChannelMask = FVector4(0, 0, 0, 0);
	DeferredLightUniformsValue.ShadowedBits = 0;
	DeferredLightUniformsValue.LightingChannelMask = 0;
}

FLightOcclusionType GetLightOcclusionType(const FLightSceneProxy& Proxy)
{
#if RHI_RAYTRACING
	return ShouldRenderRayTracingShadowsForLight(Proxy) ? FLightOcclusionType::Raytraced : FLightOcclusionType::Shadowmap;
#else
	return FLightOcclusionType::Shadowmap;
#endif
}

FLightOcclusionType GetLightOcclusionType(const FLightSceneInfoCompact& LightInfo)
{
#if RHI_RAYTRACING
	return ShouldRenderRayTracingShadowsForLight(LightInfo) ? FLightOcclusionType::Raytraced : FLightOcclusionType::Shadowmap;
#else
	return FLightOcclusionType::Shadowmap;
#endif
}

float GetLightFadeFactor(const FSceneView& View, const FLightSceneProxy* Proxy)
{
	// Distance fade
	FSphere Bounds = Proxy->GetBoundingSphere();

	const float DistanceSquared = (Bounds.Center - View.ViewMatrices.GetViewOrigin()).SizeSquared();
	extern float GMinScreenRadiusForLights;
	float SizeFade = FMath::Square(FMath::Min(0.0002f, GMinScreenRadiusForLights / Bounds.W) * View.LODDistanceFactor) * DistanceSquared;
	SizeFade = FMath::Clamp(6.0f - 6.0f * SizeFade, 0.0f, 1.0f);

	extern float GLightMaxDrawDistanceScale;
	float MaxDist = Proxy->GetMaxDrawDistance() * GLightMaxDrawDistanceScale;
	float Range = Proxy->GetFadeRange();
	float DistanceFade = MaxDist ? (MaxDist - FMath::Sqrt(DistanceSquared)) / Range : 1.0f;
	DistanceFade = FMath::Clamp(DistanceFade, 0.0f, 1.0f);
	return SizeFade * DistanceFade;
}

void StencilingGeometry::DrawSphere(FRHICommandList& RHICmdList)
{
	RHICmdList.SetStreamSource(0, StencilingGeometry::GStencilSphereVertexBuffer.VertexBufferRHI, 0);
	RHICmdList.DrawIndexedPrimitive(StencilingGeometry::GStencilSphereIndexBuffer.IndexBufferRHI, 0, 0,
		StencilingGeometry::GStencilSphereVertexBuffer.GetVertexCount(), 0,
		StencilingGeometry::GStencilSphereIndexBuffer.GetIndexCount() / 3, 1);
}

void StencilingGeometry::DrawVectorSphere(FRHICommandList& RHICmdList)
{
	RHICmdList.SetStreamSource(0, StencilingGeometry::GStencilSphereVectorBuffer.VertexBufferRHI, 0);
	RHICmdList.DrawIndexedPrimitive(StencilingGeometry::GStencilSphereIndexBuffer.IndexBufferRHI, 0, 0,
									StencilingGeometry::GStencilSphereVectorBuffer.GetVertexCount(), 0,
									StencilingGeometry::GStencilSphereIndexBuffer.GetIndexCount() / 3, 1);
}

void StencilingGeometry::DrawCone(FRHICommandList& RHICmdList)
{
	// No Stream Source needed since it will generate vertices on the fly
	RHICmdList.SetStreamSource(0, StencilingGeometry::GStencilConeVertexBuffer.VertexBufferRHI, 0);

	RHICmdList.DrawIndexedPrimitive(StencilingGeometry::GStencilConeIndexBuffer.IndexBufferRHI, 0, 0,
		FStencilConeIndexBuffer::NumVerts, 0, StencilingGeometry::GStencilConeIndexBuffer.GetIndexCount() / 3, 1);
}

/** The stencil sphere vertex buffer. */
TGlobalResource<StencilingGeometry::TStencilSphereVertexBuffer<18, 12, FVector4> > StencilingGeometry::GStencilSphereVertexBuffer;
TGlobalResource<StencilingGeometry::TStencilSphereVertexBuffer<18, 12, FVector> > StencilingGeometry::GStencilSphereVectorBuffer;

/** The stencil sphere index buffer. */
TGlobalResource<StencilingGeometry::TStencilSphereIndexBuffer<18, 12> > StencilingGeometry::GStencilSphereIndexBuffer;

TGlobalResource<StencilingGeometry::TStencilSphereVertexBuffer<4, 4, FVector4> > StencilingGeometry::GLowPolyStencilSphereVertexBuffer;
TGlobalResource<StencilingGeometry::TStencilSphereIndexBuffer<4, 4> > StencilingGeometry::GLowPolyStencilSphereIndexBuffer;

/** The (dummy) stencil cone vertex buffer. */
TGlobalResource<StencilingGeometry::FStencilConeVertexBuffer> StencilingGeometry::GStencilConeVertexBuffer;

/** The stencil cone index buffer. */
TGlobalResource<StencilingGeometry::FStencilConeIndexBuffer> StencilingGeometry::GStencilConeIndexBuffer;


// Implement a version for directional lights, and a version for point / spot lights
IMPLEMENT_SHADER_TYPE(template<>,TDeferredLightVS<false>,TEXT("/Engine/Private/DeferredLightVertexShaders.usf"),TEXT("DirectionalVertexMain"),SF_Vertex);
IMPLEMENT_SHADER_TYPE(template<>,TDeferredLightVS<true>,TEXT("/Engine/Private/DeferredLightVertexShaders.usf"),TEXT("RadialVertexMain"),SF_Vertex);


struct FRenderLightParams
{
	// Precompute transmittance
	FShaderResourceViewRHIRef DeepShadow_TransmittanceMaskBuffer = nullptr;
	uint32 DeepShadow_TransmittanceMaskBufferMaxCount = 0;
	
	// Visibility buffer data
	IPooledRenderTarget* HairCategorizationTexture = nullptr;
	IPooledRenderTarget* HairVisibilityNodeOffsetAndCount = nullptr;
	IPooledRenderTarget* HairVisibilityNodeCount = nullptr;
	FShaderResourceViewRHIRef HairVisibilityNodeCoordsSRV = nullptr;
	FShaderResourceViewRHIRef HairVisibilityNodeDataSRV = nullptr;

	IPooledRenderTarget* ScreenShadowMaskSubPixelTexture = nullptr;

	// Cloud shadow data
	FMatrix Cloud_WorldToLightClipShadowMatrix;
	float Cloud_ShadowmapFarDepthKm = 0.0f;
	IPooledRenderTarget* Cloud_ShadowmapTexture = nullptr;
	float Cloud_ShadowmapStrength = 0.0f;
};


class TDeferredLightHairVS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TDeferredLightHairVS, Global);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_HAIR"), 1);
	}

	TDeferredLightHairVS() {}
	TDeferredLightHairVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FGlobalShader(Initializer)
	{
		MaxViewportResolution.Bind(Initializer.ParameterMap, TEXT("MaxViewportResolution"));
		HairVisibilityNodeCount.Bind(Initializer.ParameterMap, TEXT("HairVisibilityNodeCount"));
	}

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View, const FHairStrandsVisibilityData* VisibilityData)
	{
		FRHIVertexShader* ShaderRHI = RHICmdList.GetBoundVertexShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);

		if (!VisibilityData)
		{
			return;
		}

		if (HairVisibilityNodeCount.IsBound() && VisibilityData->NodeCount)
		{
			SetTextureParameter(RHICmdList,	ShaderRHI, HairVisibilityNodeCount, TryGetRHI(VisibilityData->NodeCount));
		}

		SetShaderValue(RHICmdList, ShaderRHI, MaxViewportResolution, VisibilityData->SampleLightingViewportResolution);
	}

private:
	LAYOUT_FIELD(FShaderParameter, MaxViewportResolution);
	LAYOUT_FIELD(FShaderResourceParameter, HairVisibilityNodeCount);
};

IMPLEMENT_SHADER_TYPE(, TDeferredLightHairVS, TEXT("/Engine/Private/DeferredLightVertexShaders.usf"), TEXT("HairVertexMain"), SF_Vertex);


enum class ELightSourceShape
{
	Directional,
	Capsule,
	Rect,

	MAX
};


/** A pixel shader for rendering the light in a deferred pass. */
class FDeferredLightPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FDeferredLightPS, Global)

	class FSourceShapeDim		: SHADER_PERMUTATION_ENUM_CLASS("LIGHT_SOURCE_SHAPE", ELightSourceShape);
	class FSourceTextureDim		: SHADER_PERMUTATION_BOOL("USE_SOURCE_TEXTURE");
	class FIESProfileDim		: SHADER_PERMUTATION_BOOL("USE_IES_PROFILE");
	class FInverseSquaredDim	: SHADER_PERMUTATION_BOOL("INVERSE_SQUARED_FALLOFF");
	class FVisualizeCullingDim	: SHADER_PERMUTATION_BOOL("VISUALIZE_LIGHT_CULLING");
	class FLightingChannelsDim	: SHADER_PERMUTATION_BOOL("USE_LIGHTING_CHANNELS");
	class FTransmissionDim		: SHADER_PERMUTATION_BOOL("USE_TRANSMISSION");
	class FHairLighting			: SHADER_PERMUTATION_INT("USE_HAIR_LIGHTING", 2);
	class FAtmosphereTransmittance : SHADER_PERMUTATION_BOOL("USE_ATMOSPHERE_TRANSMITTANCE");
	class FCloudTransmittance 	: SHADER_PERMUTATION_BOOL("USE_CLOUD_TRANSMITTANCE");
	class FAnistropicMaterials 	: SHADER_PERMUTATION_BOOL("SUPPORTS_ANISOTROPIC_MATERIALS");

	using FPermutationDomain = TShaderPermutationDomain<
		FSourceShapeDim,
		FSourceTextureDim,
		FIESProfileDim,
		FInverseSquaredDim,
		FVisualizeCullingDim,
		FLightingChannelsDim,
		FTransmissionDim,
		FHairLighting,
		FAtmosphereTransmittance,
		FCloudTransmittance,
		FAnistropicMaterials>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if( PermutationVector.Get< FSourceShapeDim >() == ELightSourceShape::Directional && (
			PermutationVector.Get< FIESProfileDim >() ||
			PermutationVector.Get< FInverseSquaredDim >() ) )
		{
			return false;
		}

		if (PermutationVector.Get< FSourceShapeDim >() != ELightSourceShape::Directional && (PermutationVector.Get<FAtmosphereTransmittance>() || PermutationVector.Get<FCloudTransmittance>()))
		{
			return false;
		}

		if( PermutationVector.Get< FSourceShapeDim >() == ELightSourceShape::Rect )
		{
			if(	!PermutationVector.Get< FInverseSquaredDim >() )
			{
				return false;
			}
		}
		else
		{
			if( PermutationVector.Get< FSourceTextureDim >() )
			{
				return false;
			}
		}

		if (PermutationVector.Get< FHairLighting >() && (
			PermutationVector.Get< FVisualizeCullingDim >() ||
			PermutationVector.Get< FTransmissionDim >()))
		{
			return false;
		}

		if (PermutationVector.Get<FDeferredLightPS::FAnistropicMaterials>())
		{
			// Anisotropic materials do not currently support rect lights
			if (PermutationVector.Get<FSourceShapeDim>() == ELightSourceShape::Rect || PermutationVector.Get<FSourceTextureDim>())
			{
				return false;
			}

			// (Hair Lighting == 2) has its own BxDF and anisotropic BRDF is only for DefaultLit and ClearCoat materials.
			if (PermutationVector.Get<FHairLighting>() == 2)
			{
				return false;
			}

			if (!FDataDrivenShaderPlatformInfo::GetSupportsAnisotropicMaterials(Parameters.Platform))
			{
				return false;
			}
		}

		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("USE_HAIR_COMPLEX_TRANSMITTANCE"), IsHairStrandsSupported(EHairStrandsShaderType::All, Parameters.Platform) ? 1u : 0u);
	}
	
	FDeferredLightPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	:	FGlobalShader(Initializer)
	{
		LightAttenuationTexture.Bind(Initializer.ParameterMap, TEXT("LightAttenuationTexture"));
		LightAttenuationTextureSampler.Bind(Initializer.ParameterMap, TEXT("LightAttenuationTextureSampler"));
		LTCMatTexture.Bind(Initializer.ParameterMap, TEXT("LTCMatTexture"));
		LTCMatSampler.Bind(Initializer.ParameterMap, TEXT("LTCMatSampler"));
		LTCAmpTexture.Bind(Initializer.ParameterMap, TEXT("LTCAmpTexture"));
		LTCAmpSampler.Bind(Initializer.ParameterMap, TEXT("LTCAmpSampler"));
		IESTexture.Bind(Initializer.ParameterMap, TEXT("IESTexture"));
		IESTextureSampler.Bind(Initializer.ParameterMap, TEXT("IESTextureSampler"));
		LightingChannelsTexture.Bind(Initializer.ParameterMap, TEXT("LightingChannelsTexture"));
		LightingChannelsSampler.Bind(Initializer.ParameterMap, TEXT("LightingChannelsSampler"));
		TransmissionProfilesTexture.Bind(Initializer.ParameterMap, TEXT("SSProfilesTexture"));
		TransmissionProfilesLinearSampler.Bind(Initializer.ParameterMap, TEXT("TransmissionProfilesLinearSampler"));

		HairTransmittanceBuffer.Bind(Initializer.ParameterMap, TEXT("HairTransmittanceBuffer"));
		HairTransmittanceBufferMaxCount.Bind(Initializer.ParameterMap, TEXT("HairTransmittanceBufferMaxCount"));
		ScreenShadowMaskSubPixelTexture.Bind(Initializer.ParameterMap, TEXT("ScreenShadowMaskSubPixelTexture")); // TODO hook the shader itself

		HairShadowMaskValid.Bind(Initializer.ParameterMap, TEXT("HairShadowMaskValid"));
		HairDualScatteringRoughnessOverride.Bind(Initializer.ParameterMap, TEXT("HairDualScatteringRoughnessOverride"));

		HairCategorizationTexture.Bind(Initializer.ParameterMap, TEXT("HairCategorizationTexture"));
		HairVisibilityNodeOffsetAndCount.Bind(Initializer.ParameterMap, TEXT("HairVisibilityNodeOffsetAndCount"));
		HairVisibilityNodeCoords.Bind(Initializer.ParameterMap, TEXT("HairVisibilityNodeCoords"));
		HairVisibilityNodeData.Bind(Initializer.ParameterMap, TEXT("HairVisibilityNodeData"));

		DummyRectLightTextureForCapsuleCompilerWarning.Bind(Initializer.ParameterMap, TEXT("DummyRectLightTextureForCapsuleCompilerWarning"));

		CloudShadowmapTexture.Bind(Initializer.ParameterMap, TEXT("CloudShadowmapTexture"));
		CloudShadowmapSampler.Bind(Initializer.ParameterMap, TEXT("CloudShadowmapSampler"));
		CloudShadowmapFarDepthKm.Bind(Initializer.ParameterMap, TEXT("CloudShadowmapFarDepthKm"));
		CloudShadowmapWorldToLightClipMatrix.Bind(Initializer.ParameterMap, TEXT("CloudShadowmapWorldToLightClipMatrix"));
		CloudShadowmapStrength.Bind(Initializer.ParameterMap, TEXT("CloudShadowmapStrength"));
	}

	FDeferredLightPS()
	{}

public:
	void SetParameters(
		FRHICommandList& RHICmdList, 
		const FSceneView& View, 
		const FLightSceneInfo* LightSceneInfo,
		FRHITexture* ScreenShadowMaskTexture,
		FRHITexture* LightingChannelsTextureRHI,
		FRenderLightParams* RenderLightParams)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();
		SetParametersBase(RHICmdList, ShaderRHI, View, ScreenShadowMaskTexture, LightingChannelsTextureRHI, LightSceneInfo->Proxy->GetIESTextureResource(), RenderLightParams);
		SetDeferredLightParameters(RHICmdList, ShaderRHI, GetUniformBufferParameter<FDeferredLightUniformStruct>(), LightSceneInfo, View);
	}

	void SetParametersSimpleLight(FRHICommandList& RHICmdList, const FSceneView& View, const FSimpleLightEntry& SimpleLight, const FSimpleLightPerViewEntry& SimpleLightPerViewData)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();
		SetParametersBase(RHICmdList, ShaderRHI, View, nullptr, nullptr, nullptr, nullptr);
		SetSimpleDeferredLightParameters(RHICmdList, ShaderRHI, GetUniformBufferParameter<FDeferredLightUniformStruct>(), SimpleLight, SimpleLightPerViewData, View);
	}

private:

	void SetParametersBase(
		FRHICommandList& RHICmdList, 
		FRHIPixelShader* ShaderRHI, 
		const FSceneView& View, 
		FRHITexture* ScreenShadowMaskTexture,
		FRHITexture* LightingChannelsTextureRHI,
		FTexture* IESTextureResource,
		FRenderLightParams* RenderLightParams)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI,View.ViewUniformBuffer);

		FSceneRenderTargets& SceneRenderTargets = FSceneRenderTargets::Get(RHICmdList);

		if(LightAttenuationTexture.IsBound())
		{
			if (!ScreenShadowMaskTexture)
			{
				ScreenShadowMaskTexture = GWhiteTexture->TextureRHI;
			}

			SetTextureParameter(
				RHICmdList,
				ShaderRHI,
				LightAttenuationTexture,
				LightAttenuationTextureSampler,
				TStaticSamplerState<SF_Point,AM_Wrap,AM_Wrap,AM_Wrap>::GetRHI(),
				ScreenShadowMaskTexture);
		}

		SetTextureParameter(
			RHICmdList,
			ShaderRHI,
			LTCMatTexture,
			LTCMatSampler,
			TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
			GSystemTextures.LTCMat->GetRenderTargetItem().ShaderResourceTexture
			);

		SetTextureParameter(
			RHICmdList,
			ShaderRHI,
			LTCAmpTexture,
			LTCAmpSampler,
			TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
			GSystemTextures.LTCAmp->GetRenderTargetItem().ShaderResourceTexture
			);

		{
			FRHITexture* TextureRHI = IESTextureResource ? IESTextureResource->TextureRHI : GWhiteTexture->TextureRHI;

			SetTextureParameter(
				RHICmdList,
				ShaderRHI,
				IESTexture,
				IESTextureSampler,
				TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
				TextureRHI
				);
		}

		if( LightingChannelsTexture.IsBound() )
		{
			SetTextureParameter(
				RHICmdList,
				ShaderRHI,
				LightingChannelsTexture,
				LightingChannelsSampler,
				TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
				LightingChannelsTextureRHI ? LightingChannelsTextureRHI : GWhiteTexture->TextureRHI.GetReference()
				);
		}

		if( TransmissionProfilesTexture.IsBound() )
		{
			FRHITexture* SubsurfaceTextureRHI = GBlackTexture->TextureRHI;

			if (auto* SubsurfaceRT = GetSubsufaceProfileTexture_RT((FRHICommandListImmediate&)RHICmdList))
			{
				// no subsurface profile was used yet
				SubsurfaceTextureRHI = SubsurfaceRT->GetShaderResourceRHI();
			}

			SetTextureParameter(RHICmdList,
				ShaderRHI,
				TransmissionProfilesTexture,
				TransmissionProfilesLinearSampler,
				TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),
				SubsurfaceTextureRHI);
		}

		if (HairTransmittanceBuffer.IsBound())
		{
			const uint32 TransmittanceBufferMaxCount = RenderLightParams ? RenderLightParams->DeepShadow_TransmittanceMaskBufferMaxCount : 0;
			SetShaderValue(
				RHICmdList,
				ShaderRHI,
				HairTransmittanceBufferMaxCount,
				TransmittanceBufferMaxCount);
			if (RenderLightParams && RenderLightParams->DeepShadow_TransmittanceMaskBuffer)
			{
				SetSRVParameter(RHICmdList, ShaderRHI, HairTransmittanceBuffer, RenderLightParams->DeepShadow_TransmittanceMaskBuffer);
			}
		}

		if (ScreenShadowMaskSubPixelTexture.IsBound())
		{
			if (RenderLightParams)
			{
				SetTextureParameter(
					RHICmdList,
					ShaderRHI,
					ScreenShadowMaskSubPixelTexture,
					LightAttenuationTextureSampler,
					TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),
					(RenderLightParams && RenderLightParams->ScreenShadowMaskSubPixelTexture) ? RenderLightParams->ScreenShadowMaskSubPixelTexture->GetRenderTargetItem().ShaderResourceTexture : GWhiteTexture->TextureRHI);

				uint32 InHairShadowMaskValid = RenderLightParams->ScreenShadowMaskSubPixelTexture ? 1 : 0;
				SetShaderValue(
					RHICmdList,
					ShaderRHI,
					HairShadowMaskValid,
					InHairShadowMaskValid);
			}
		}

		if (HairCategorizationTexture.IsBound())
		{
			if (RenderLightParams && RenderLightParams->HairCategorizationTexture)
			{
				SetTextureParameter(
					RHICmdList,
					ShaderRHI,
					HairCategorizationTexture,
					LightAttenuationTextureSampler,
					TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),
					RenderLightParams->HairCategorizationTexture->GetRenderTargetItem().TargetableTexture);
			}
		}

		if (HairVisibilityNodeOffsetAndCount.IsBound())
		{
			if (RenderLightParams && RenderLightParams->HairVisibilityNodeOffsetAndCount)
			{
				SetTextureParameter(
					RHICmdList,
					ShaderRHI,
					HairVisibilityNodeOffsetAndCount,
					LightAttenuationTextureSampler,
					TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),
					RenderLightParams->HairVisibilityNodeOffsetAndCount->GetRenderTargetItem().TargetableTexture);
			}
		}
		
		if (HairVisibilityNodeCoords.IsBound())
		{
			if (RenderLightParams && RenderLightParams->HairVisibilityNodeCoordsSRV)
			{
				FShaderResourceViewRHIRef SRV = RenderLightParams->HairVisibilityNodeCoordsSRV;
				SetSRVParameter(
					RHICmdList, 
					ShaderRHI, 
					HairVisibilityNodeCoords,
					SRV);
			}
		}

		if (HairVisibilityNodeData.IsBound())
		{
			if (RenderLightParams && RenderLightParams->HairVisibilityNodeDataSRV)
			{
				FShaderResourceViewRHIRef SRV = RenderLightParams->HairVisibilityNodeDataSRV;
				SetSRVParameter(
					RHICmdList, 
					ShaderRHI, 
					HairVisibilityNodeData, 
					SRV);
			}
		}

		if (HairDualScatteringRoughnessOverride.IsBound())
		{
			const float DualScatteringRoughness = GetHairDualScatteringRoughnessOverride();
			SetShaderValue(
				RHICmdList,
				ShaderRHI,
				HairDualScatteringRoughnessOverride,
				DualScatteringRoughness);
		}

		if (DummyRectLightTextureForCapsuleCompilerWarning.IsBound())
		{
			SetTextureParameter(
				RHICmdList,
				ShaderRHI,
				DummyRectLightTextureForCapsuleCompilerWarning,
				LTCMatSampler,
				TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),
				GSystemTextures.DepthDummy->GetRenderTargetItem().ShaderResourceTexture
			);
		}

		if (CloudShadowmapTexture.IsBound())
		{
			if (RenderLightParams && RenderLightParams->Cloud_ShadowmapTexture)
			{
				SetTextureParameter(
					RHICmdList,
					ShaderRHI,
					CloudShadowmapTexture,
					CloudShadowmapSampler,
					TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(),
					RenderLightParams->Cloud_ShadowmapTexture ? RenderLightParams->Cloud_ShadowmapTexture->GetRenderTargetItem().ShaderResourceTexture : GBlackVolumeTexture->TextureRHI);

				SetShaderValue(
					RHICmdList,
					ShaderRHI,
					CloudShadowmapFarDepthKm,
					RenderLightParams->Cloud_ShadowmapFarDepthKm);

				SetShaderValue(
					RHICmdList,
					ShaderRHI,
					CloudShadowmapWorldToLightClipMatrix,
					RenderLightParams->Cloud_WorldToLightClipShadowMatrix);

				SetShaderValue(
					RHICmdList,
					ShaderRHI,
					CloudShadowmapStrength,
					RenderLightParams->Cloud_ShadowmapStrength);
			}
		}
	}

	LAYOUT_FIELD(FShaderResourceParameter, LightAttenuationTexture);
	LAYOUT_FIELD(FShaderResourceParameter, LightAttenuationTextureSampler);
	LAYOUT_FIELD(FShaderResourceParameter, LTCMatTexture);
	LAYOUT_FIELD(FShaderResourceParameter, LTCMatSampler);
	LAYOUT_FIELD(FShaderResourceParameter, LTCAmpTexture);
	LAYOUT_FIELD(FShaderResourceParameter, LTCAmpSampler);
	LAYOUT_FIELD(FShaderResourceParameter, IESTexture);
	LAYOUT_FIELD(FShaderResourceParameter, IESTextureSampler);
	LAYOUT_FIELD(FShaderResourceParameter, LightingChannelsTexture);
	LAYOUT_FIELD(FShaderResourceParameter, LightingChannelsSampler);
	LAYOUT_FIELD(FShaderResourceParameter, TransmissionProfilesTexture);
	LAYOUT_FIELD(FShaderResourceParameter, TransmissionProfilesLinearSampler);

	LAYOUT_FIELD(FShaderParameter, HairTransmittanceBufferMaxCount);
	LAYOUT_FIELD(FShaderResourceParameter, HairTransmittanceBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, HairCategorizationTexture);
	LAYOUT_FIELD(FShaderResourceParameter, HairVisibilityNodeOffsetAndCount);
	LAYOUT_FIELD(FShaderResourceParameter, HairVisibilityNodeCoords);
	LAYOUT_FIELD(FShaderResourceParameter, HairVisibilityNodeData);
	LAYOUT_FIELD(FShaderResourceParameter, ScreenShadowMaskSubPixelTexture);

	LAYOUT_FIELD(FShaderParameter, HairShadowMaskValid);
	LAYOUT_FIELD(FShaderParameter, HairDualScatteringRoughnessOverride);

	LAYOUT_FIELD(FShaderResourceParameter, DummyRectLightTextureForCapsuleCompilerWarning);

	LAYOUT_FIELD(FShaderResourceParameter, CloudShadowmapTexture);
	LAYOUT_FIELD(FShaderResourceParameter, CloudShadowmapSampler);
	LAYOUT_FIELD(FShaderParameter, CloudShadowmapFarDepthKm);
	LAYOUT_FIELD(FShaderParameter, CloudShadowmapWorldToLightClipMatrix);
	LAYOUT_FIELD(FShaderParameter, CloudShadowmapStrength);
};

IMPLEMENT_GLOBAL_SHADER(FDeferredLightPS, "/Engine/Private/DeferredLightPixelShaders.usf", "DeferredLightPixelMain", SF_Pixel);


/** Shader used to visualize stationary light overlap. */
template<bool bRadialAttenuation>
class TDeferredLightOverlapPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(TDeferredLightOverlapPS,Global)
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("RADIAL_ATTENUATION"), (uint32)bRadialAttenuation);
	}

	TDeferredLightOverlapPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
	:	FGlobalShader(Initializer)
	{
		HasValidChannel.Bind(Initializer.ParameterMap, TEXT("HasValidChannel"));
	}

	TDeferredLightOverlapPS()
	{
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView& View, const FLightSceneInfo* LightSceneInfo)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI,View.ViewUniformBuffer);
		const float HasValidChannelValue = LightSceneInfo->Proxy->GetPreviewShadowMapChannel() == INDEX_NONE ? 0.0f : 1.0f;
		SetShaderValue(RHICmdList, ShaderRHI, HasValidChannel, HasValidChannelValue);
		SetDeferredLightParameters(RHICmdList, ShaderRHI, GetUniformBufferParameter<FDeferredLightUniformStruct>(), LightSceneInfo, View);
	}

private:
	LAYOUT_FIELD(FShaderParameter, HasValidChannel);
};

IMPLEMENT_SHADER_TYPE(template<>, TDeferredLightOverlapPS<true>, TEXT("/Engine/Private/StationaryLightOverlapShaders.usf"), TEXT("OverlapRadialPixelMain"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, TDeferredLightOverlapPS<false>, TEXT("/Engine/Private/StationaryLightOverlapShaders.usf"), TEXT("OverlapDirectionalPixelMain"), SF_Pixel);

static void SplitSimpleLightsByView(TArrayView<const FViewInfo> Views, const FSimpleLightArray& SimpleLights, TArrayView<FSimpleLightArray> SimpleLightsByView)
{
	check(SimpleLightsByView.Num() == Views.Num());

	for (int32 LightIndex = 0; LightIndex < SimpleLights.InstanceData.Num(); ++LightIndex)
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			FSimpleLightPerViewEntry PerViewEntry = SimpleLights.GetViewDependentData(LightIndex, ViewIndex, Views.Num());
			SimpleLightsByView[ViewIndex].InstanceData.Add(SimpleLights.InstanceData[LightIndex]);
			SimpleLightsByView[ViewIndex].PerViewData.Add(PerViewEntry);
		}
	}
}

/** Gathers simple lights from visible primtives in the passed in views. */
void FSceneRenderer::GatherSimpleLights(const FSceneViewFamily& ViewFamily, const TArray<FViewInfo>& Views, FSimpleLightArray& SimpleLights)
{
	TArray<const FPrimitiveSceneInfo*, SceneRenderingAllocator> PrimitivesWithSimpleLights;

	// Gather visible primitives from all views that might have simple lights
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FViewInfo& View = Views[ViewIndex];
		for (int32 PrimitiveIndex = 0; PrimitiveIndex < View.VisibleDynamicPrimitivesWithSimpleLights.Num(); PrimitiveIndex++)
		{
			const FPrimitiveSceneInfo* PrimitiveSceneInfo = View.VisibleDynamicPrimitivesWithSimpleLights[PrimitiveIndex];

			// TArray::AddUnique is slow, but not expecting many entries in PrimitivesWithSimpleLights
			PrimitivesWithSimpleLights.AddUnique(PrimitiveSceneInfo);
		}
	}

	// Gather simple lights from the primitives
	for (int32 PrimitiveIndex = 0; PrimitiveIndex < PrimitivesWithSimpleLights.Num(); PrimitiveIndex++)
	{
		const FPrimitiveSceneInfo* Primitive = PrimitivesWithSimpleLights[PrimitiveIndex];
		Primitive->Proxy->GatherSimpleLights(ViewFamily, SimpleLights);
	}
}

/** Gets a readable light name for use with a draw event. */
void FSceneRenderer::GetLightNameForDrawEvent(const FLightSceneProxy* LightProxy, FString& LightNameWithLevel)
{
#if WANTS_DRAW_MESH_EVENTS
	if (GetEmitDrawEvents())
	{
		FString FullLevelName = LightProxy->GetLevelName().ToString();
		const int32 LastSlashIndex = FullLevelName.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);

		if (LastSlashIndex != INDEX_NONE)
		{
			// Trim the leading path before the level name to make it more readable
			// The level FName was taken directly from the Outermost UObject, otherwise we would do this operation on the game thread
			FullLevelName.MidInline(LastSlashIndex + 1, FullLevelName.Len() - (LastSlashIndex + 1), false);
		}

		LightNameWithLevel = FullLevelName + TEXT(".") + LightProxy->GetComponentName().ToString();
	}
#endif
}

extern int32 GbEnableAsyncComputeTranslucencyLightingVolumeClear;

uint32 GetShadowQuality();

static bool LightRequiresDenosier(const FLightSceneInfo& LightSceneInfo)
{
	ELightComponentType LightType = ELightComponentType(LightSceneInfo.Proxy->GetLightType());
	if (LightType == LightType_Directional)
	{
		return LightSceneInfo.Proxy->GetLightSourceAngle() > 0;
	}
	else if (LightType == LightType_Point || LightType == LightType_Spot)
	{
		return LightSceneInfo.Proxy->GetSourceRadius() > 0;
	}
	else if (LightType == LightType_Rect)
	{
		return true;
	}
	else
	{
		check(0);
	}
	return false;
}



void FSceneRenderer::GatherAndSortLights(FSortedLightSetSceneInfo& OutSortedLights)
{
	if (bAllowSimpleLights)
	{
		GatherSimpleLights(ViewFamily, Views, OutSortedLights.SimpleLights);
	}
	FSimpleLightArray &SimpleLights = OutSortedLights.SimpleLights;
	TArray<FSortedLightSceneInfo, SceneRenderingAllocator> &SortedLights = OutSortedLights.SortedLights;

	// NOTE: we allocate space also for simple lights such that they can be referenced in the same sorted range
	SortedLights.Empty(Scene->Lights.Num() + SimpleLights.InstanceData.Num());

	bool bDynamicShadows = ViewFamily.EngineShowFlags.DynamicShadows && GetShadowQuality() > 0;

#if ENABLE_DEBUG_DISCARD_PROP
	int Total = Scene->Lights.Num() + SimpleLights.InstanceData.Num();
	int NumToKeep = int(float(Total) * (1.0f - GDebugLightDiscardProp));
	const float DebugDiscardStride = float(NumToKeep) / float(Total);
	float DebugDiscardCounter = 0.0f;
#endif // ENABLE_DEBUG_DISCARD_PROP
	// Build a list of visible lights.
	for (TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights); LightIt; ++LightIt)
	{
		const FLightSceneInfoCompact& LightSceneInfoCompact = *LightIt;
		const FLightSceneInfo* const LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;

#if ENABLE_DEBUG_DISCARD_PROP
		{
			int PrevCounter = int(DebugDiscardCounter);
			DebugDiscardCounter += DebugDiscardStride;
			if (PrevCounter >= int(DebugDiscardCounter))
			{
				continue;
			}
		}
#endif // ENABLE_DEBUG_DISCARD_PROP

		if (LightSceneInfo->ShouldRenderLightViewIndependent()
			// Reflection override skips direct specular because it tends to be blindingly bright with a perfectly smooth surface
			&& !ViewFamily.EngineShowFlags.ReflectionOverride)
		{
			// Check if the light is visible in any of the views.
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				if (LightSceneInfo->ShouldRenderLight(Views[ViewIndex]))
				{
					FSortedLightSceneInfo* SortedLightInfo = new(SortedLights) FSortedLightSceneInfo(LightSceneInfo);

					// Check for shadows and light functions.
					SortedLightInfo->SortKey.Fields.LightType = LightSceneInfoCompact.LightType;
					SortedLightInfo->SortKey.Fields.bTextureProfile = ViewFamily.EngineShowFlags.TexturedLightProfiles && LightSceneInfo->Proxy->GetIESTextureResource();
					SortedLightInfo->SortKey.Fields.bShadowed = bDynamicShadows && CheckForProjectedShadows(LightSceneInfo);
					SortedLightInfo->SortKey.Fields.bLightFunction = ViewFamily.EngineShowFlags.LightFunctions && CheckForLightFunction(LightSceneInfo);
					SortedLightInfo->SortKey.Fields.bUsesLightingChannels = Views[ViewIndex].bUsesLightingChannels && LightSceneInfo->Proxy->GetLightingChannelMask() != GetDefaultLightingChannelMask();

					// These are not simple lights.
					SortedLightInfo->SortKey.Fields.bIsNotSimpleLight = 1;


					// tiled and clustered deferred lighting only supported for certain lights that don't use any additional features
					// And also that are not directional (mostly because it does'nt make so much sense to insert them into every grid cell in the universe)
					// In the forward case one directional light gets put into its own variables, and in the deferred case it gets a full-screen pass.
					// Usually it'll have shadows and stuff anyway.
					// Rect lights are not supported as the performance impact is significant even if not used, for now, left for trad. deferred.
					const bool bTiledOrClusteredDeferredSupported =
						!SortedLightInfo->SortKey.Fields.bTextureProfile &&
						!SortedLightInfo->SortKey.Fields.bShadowed &&
						!SortedLightInfo->SortKey.Fields.bLightFunction &&
						!SortedLightInfo->SortKey.Fields.bUsesLightingChannels
						&& LightSceneInfoCompact.LightType != LightType_Directional
						&& LightSceneInfoCompact.LightType != LightType_Rect;

					SortedLightInfo->SortKey.Fields.bTiledDeferredNotSupported = !(bTiledOrClusteredDeferredSupported && LightSceneInfo->Proxy->IsTiledDeferredLightingSupported());

					SortedLightInfo->SortKey.Fields.bClusteredDeferredNotSupported = !bTiledOrClusteredDeferredSupported;
					break;
				}
			}
		}
	}
	// Add the simple lights also
	for (int32 SimpleLightIndex = 0; SimpleLightIndex < SimpleLights.InstanceData.Num(); SimpleLightIndex++)
	{
#if ENABLE_DEBUG_DISCARD_PROP
		{
			int PrevCounter = int(DebugDiscardCounter);
			DebugDiscardCounter += DebugDiscardStride;
			if (PrevCounter >= int(DebugDiscardCounter))
			{
				continue;
			}
		}
#endif // ENABLE_DEBUG_DISCARD_PROP

		FSortedLightSceneInfo* SortedLightInfo = new(SortedLights) FSortedLightSceneInfo(SimpleLightIndex);
		SortedLightInfo->SortKey.Fields.LightType = LightType_Point;
		SortedLightInfo->SortKey.Fields.bTextureProfile = 0;
		SortedLightInfo->SortKey.Fields.bShadowed = 0;
		SortedLightInfo->SortKey.Fields.bLightFunction = 0;
		SortedLightInfo->SortKey.Fields.bUsesLightingChannels = 0;

		// These are simple lights.
		SortedLightInfo->SortKey.Fields.bIsNotSimpleLight = 0;

		// Simple lights are ok to use with tiled and clustered deferred lighting
		SortedLightInfo->SortKey.Fields.bTiledDeferredNotSupported = 0;
		SortedLightInfo->SortKey.Fields.bClusteredDeferredNotSupported = 0;
	}

	// Sort non-shadowed, non-light function lights first to avoid render target switches.
	struct FCompareFSortedLightSceneInfo
	{
		FORCEINLINE bool operator()( const FSortedLightSceneInfo& A, const FSortedLightSceneInfo& B ) const
		{
			return A.SortKey.Packed < B.SortKey.Packed;
		}
	};
	SortedLights.Sort( FCompareFSortedLightSceneInfo() );

	// Scan and find ranges.
	OutSortedLights.SimpleLightsEnd = SortedLights.Num();
	OutSortedLights.TiledSupportedEnd = SortedLights.Num();
	OutSortedLights.ClusteredSupportedEnd = SortedLights.Num();
	OutSortedLights.AttenuationLightStart = SortedLights.Num();

	// Iterate over all lights to be rendered and build ranges for tiled deferred and unshadowed lights
	for (int32 LightIndex = 0; LightIndex < SortedLights.Num(); LightIndex++)
	{
		const FSortedLightSceneInfo& SortedLightInfo = SortedLights[LightIndex];
		const bool bDrawShadows = SortedLightInfo.SortKey.Fields.bShadowed;
		const bool bDrawLightFunction = SortedLightInfo.SortKey.Fields.bLightFunction;
		const bool bTextureLightProfile = SortedLightInfo.SortKey.Fields.bTextureProfile;
		const bool bLightingChannels = SortedLightInfo.SortKey.Fields.bUsesLightingChannels;

		if (SortedLightInfo.SortKey.Fields.bIsNotSimpleLight && OutSortedLights.SimpleLightsEnd == SortedLights.Num())
		{
			// Mark the first index to not be simple
			OutSortedLights.SimpleLightsEnd = LightIndex;
		}

		if (SortedLightInfo.SortKey.Fields.bTiledDeferredNotSupported && OutSortedLights.TiledSupportedEnd == SortedLights.Num())
		{
			// Mark the first index to not support tiled deferred
			OutSortedLights.TiledSupportedEnd = LightIndex;
		}

		if (SortedLightInfo.SortKey.Fields.bClusteredDeferredNotSupported && OutSortedLights.ClusteredSupportedEnd == SortedLights.Num())
		{
			// Mark the first index to not support clustered deferred
			OutSortedLights.ClusteredSupportedEnd = LightIndex;
		}

		if (bDrawShadows || bDrawLightFunction || bLightingChannels)
		{
			// Once we find a shadowed light, we can exit the loop, these lights should never support tiled deferred rendering either
			check(SortedLightInfo.SortKey.Fields.bTiledDeferredNotSupported);
			OutSortedLights.AttenuationLightStart = LightIndex;
			break;
		}
	}

	// Make sure no obvious things went wrong!
	check(OutSortedLights.TiledSupportedEnd >= OutSortedLights.SimpleLightsEnd);
	check(OutSortedLights.ClusteredSupportedEnd >= OutSortedLights.TiledSupportedEnd);
	check(OutSortedLights.AttenuationLightStart >= OutSortedLights.ClusteredSupportedEnd);
}

static bool HasHairStrandsClusters(int32 ViewIndex, const FHairStrandsRenderingData* HairDatas)
{
	return HairDatas && ViewIndex < HairDatas->MacroGroupsPerViews.Views.Num() && HairDatas->MacroGroupsPerViews.Views[ViewIndex].Datas.Num() > 0;
};

static FHairStrandsOcclusionResources GetHairStrandsResources(int32 ViewIndex, FRDGBuilder& GraphBuilder, const FHairStrandsRenderingData* HairDatas)
{
	FHairStrandsOcclusionResources Out;
	if (HairDatas && ViewIndex < HairDatas->HairVisibilityViews.HairDatas.Num())
	{
		if (HairDatas->HairVisibilityViews.HairDatas[ViewIndex].CategorizationTexture)
		{
			Out.CategorizationTexture = HairDatas->HairVisibilityViews.HairDatas[ViewIndex].CategorizationTexture;
		}
		if (HairDatas->HairVisibilityViews.HairDatas[ViewIndex].LightChannelMaskTexture)
		{
			Out.LightChannelMaskTexture = HairDatas->HairVisibilityViews.HairDatas[ViewIndex].LightChannelMaskTexture;
		}

		Out.VoxelResources = &HairDatas->MacroGroupsPerViews.Views[ViewIndex].VirtualVoxelResources;
	}
	return Out;
}

/** Shader parameters to use when creating a RenderLight(...) pass. */
BEGIN_SHADER_PARAMETER_STRUCT(FRenderLightParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
	RDG_TEXTURE_ACCESS(HairCategorizationTexture, ERHIAccess::SRVGraphics)
	RDG_TEXTURE_ACCESS(ShadowMaskTexture, ERHIAccess::SRVGraphics)
	RDG_TEXTURE_ACCESS(LightingChannelsTexture, ERHIAccess::SRVGraphics) 
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void GetRenderLightParameters(
	FRDGTextureRef SceneColorTexture,
	FRDGTextureRef SceneDepthTexture,
	FRDGTextureRef ShadowMaskTexture,
	FRDGTextureRef LightingChannelsTexture,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	const FHairStrandsVisibilityViews* InHairVisibilityViews,
	FRenderLightParameters& Parameters)
{
	Parameters.SceneTextures = SceneTexturesUniformBuffer;
	Parameters.ShadowMaskTexture = ShadowMaskTexture;
	Parameters.LightingChannelsTexture = LightingChannelsTexture;
	Parameters.HairCategorizationTexture = InHairVisibilityViews && InHairVisibilityViews->HairDatas.Num() > 0 ? InHairVisibilityViews->HairDatas[0].CategorizationTexture : nullptr;
	Parameters.RenderTargets[0] = FRenderTargetBinding(SceneColorTexture, ERenderTargetLoadAction::ELoad);

	if (SceneDepthTexture)
	{
		Parameters.RenderTargets.DepthStencil = FDepthStencilBinding(SceneDepthTexture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthRead_StencilWrite);
	}
}

FHairStrandsTransmittanceMaskData CreateDummyHairStrandsTransmittanceMaskData(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap);

FRDGTextureRef FDeferredShadingSceneRenderer::RenderLights(
	FRDGBuilder& GraphBuilder,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	FRDGTextureRef SceneColorTexture,
	FRDGTextureRef SceneDepthTexture,
	FRDGTextureRef LightingChannelsTexture,
	FSortedLightSetSceneInfo &SortedLightSet,
	const FHairStrandsRenderingData* HairDatas)
{
	const EShaderPlatform ShaderPlatformForFeatureLevel = GShaderPlatformForFeatureLevel[FeatureLevel];

	const bool bUseHairLighting = HairDatas != nullptr && HairDatas->HairVisibilityViews.HairDatas.Num() > 0 && HairDatas->HairVisibilityViews.HairDatas[0].CategorizationTexture;
	const FHairStrandsVisibilityViews* InHairVisibilityViews = bUseHairLighting ? &HairDatas->HairVisibilityViews : nullptr;

	RDG_EVENT_SCOPE(GraphBuilder, "Lights");
	RDG_GPU_STAT_SCOPE(GraphBuilder, Lights);

	SCOPED_NAMED_EVENT(FDeferredShadingSceneRenderer_RenderLights, FColor::Emerald);
	SCOPE_CYCLE_COUNTER(STAT_LightingDrawTime);
	SCOPE_CYCLE_COUNTER(STAT_LightRendering);

	const FSimpleLightArray &SimpleLights = SortedLightSet.SimpleLights;
	const TArray<FSortedLightSceneInfo, SceneRenderingAllocator> &SortedLights = SortedLightSet.SortedLights;
	const int32 AttenuationLightStart = SortedLightSet.AttenuationLightStart;
	const int32 SimpleLightsEnd = SortedLightSet.SimpleLightsEnd;

	FHairStrandsTransmittanceMaskData DummyTransmittanceMaskData;
	if (bUseHairLighting && Views.Num() > 0)
	{
		DummyTransmittanceMaskData = CreateDummyHairStrandsTransmittanceMaskData(GraphBuilder, Views[0].ShaderMap);
	}

	{
		RDG_EVENT_SCOPE(GraphBuilder, "DirectLighting");

		if (GbEnableAsyncComputeTranslucencyLightingVolumeClear && GSupportsEfficientAsyncCompute)
		{
			AddPass(GraphBuilder, [this](FRHICommandList& RHICmdList)
			{
				//Gfx pipe must wait for the async compute clear of the translucency volume clear.
				check(TranslucencyLightingVolumeClearEndTransition);
				RHICmdList.EndTransition(TranslucencyLightingVolumeClearEndTransition);
				TranslucencyLightingVolumeClearEndTransition = nullptr;
			});
		}

		if(ViewFamily.EngineShowFlags.DirectLighting)
		{
			RDG_EVENT_SCOPE(GraphBuilder, "NonShadowedLights");
			INC_DWORD_STAT_BY(STAT_NumUnshadowedLights, AttenuationLightStart);

			// Currently they have a special path anyway in case of standard deferred so always skip the simple lights
			int32 StandardDeferredStart = SortedLightSet.SimpleLightsEnd;

			bool bRenderSimpleLightsStandardDeferred = SortedLightSet.SimpleLights.InstanceData.Num() > 0;

			UE_CLOG(ShouldUseClusteredDeferredShading() && !AreClusteredLightsInLightGrid(), LogRenderer, Warning,
				TEXT("Clustered deferred shading is enabled, but lights were not injected in grid, falling back to other methods (hint 'r.LightCulling.Quality' may cause this)."));

			// True if the clustered shading is enabled and the feature level is there, and that the light grid had lights injected.
			if (ShouldUseClusteredDeferredShading() && AreClusteredLightsInLightGrid())
			{
				// Tell the trad. deferred that the clustered deferred capable lights are taken care of.
				// This includes the simple lights
				StandardDeferredStart = SortedLightSet.ClusteredSupportedEnd;
				// Tell the trad. deferred that the simple lights are spoken for.
				bRenderSimpleLightsStandardDeferred = false;

				AddClusteredDeferredShadingPass(GraphBuilder, SceneColorTexture, SceneTexturesUniformBuffer, SortedLightSet);
			}
			else if (CanUseTiledDeferred())
			{
				bool bAnyViewIsStereo = false;
				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
				{
					if (IStereoRendering::IsStereoEyeView(Views[ViewIndex]))
					{
						bAnyViewIsStereo = true;
						break;
					}
				}

				// Use tiled deferred shading on any unshadowed lights without a texture light profile
				if (ShouldUseTiledDeferred(SortedLightSet.TiledSupportedEnd) && !bAnyViewIsStereo)
				{
					// Update the range that needs to be processed by standard deferred to exclude the lights done with tiled
					StandardDeferredStart = SortedLightSet.TiledSupportedEnd;
					bRenderSimpleLightsStandardDeferred = false;

					SceneColorTexture = RenderTiledDeferredLighting(GraphBuilder, SceneColorTexture, SceneTexturesUniformBuffer, SortedLights, SortedLightSet.SimpleLightsEnd, SortedLightSet.TiledSupportedEnd, SimpleLights);
				}
			}

			if (bRenderSimpleLightsStandardDeferred)
			{
				RenderSimpleLightsStandardDeferred(GraphBuilder, SceneColorTexture, SceneDepthTexture, SceneTexturesUniformBuffer, SortedLightSet.SimpleLights);
			}

			{
				FRenderLightParameters* PassParameters = GraphBuilder.AllocParameters<FRenderLightParameters>();
				GetRenderLightParameters(SceneColorTexture, SceneDepthTexture, nullptr, LightingChannelsTexture, SceneTexturesUniformBuffer, HairDatas ? &HairDatas->HairVisibilityViews : nullptr, *PassParameters);

				GraphBuilder.AddPass(
					RDG_EVENT_NAME("StandardDeferredLighting"),
					PassParameters,
					ERDGPassFlags::Raster,
					[this, &SortedLights, LightingChannelsTexture, StandardDeferredStart, AttenuationLightStart](FRHICommandList& RHICmdList)
				{
					// Draw non-shadowed non-light function lights without changing render targets between them
					for (int32 LightIndex = StandardDeferredStart; LightIndex < AttenuationLightStart; LightIndex++)
					{
						const FSortedLightSceneInfo& SortedLightInfo = SortedLights[LightIndex];
						const FLightSceneInfo* const LightSceneInfo = SortedLightInfo.LightSceneInfo;

						// Render the light to the scene color buffer, using a 1x1 white texture as input
						RenderLight(RHICmdList, LightSceneInfo, nullptr, TryGetRHI(LightingChannelsTexture), nullptr, false, false);
					}
				});
			}

			// Add a special version when hair rendering is enabled for getting lighting on hair. 
			if (bUseHairLighting)
			{
				FRDGTextureRef NullScreenShadowMaskSubPixelTexture = nullptr;
				// Draw non-shadowed non-light function lights without changing render targets between them
				for (int32 LightIndex = StandardDeferredStart; LightIndex < AttenuationLightStart; LightIndex++)
				{
					const FSortedLightSceneInfo& SortedLightInfo = SortedLights[LightIndex];
					const FLightSceneInfo* const LightSceneInfo = SortedLightInfo.LightSceneInfo;
					RenderLightForHair(GraphBuilder, SceneTexturesUniformBuffer, LightSceneInfo, NullScreenShadowMaskSubPixelTexture, LightingChannelsTexture, DummyTransmittanceMaskData, InHairVisibilityViews);
				}
			}

			if (GUseTranslucentLightingVolumes && GSupportsVolumeTextureRendering)
			{
				if (AttenuationLightStart)
				{
					// Inject non-shadowed, non-simple, non-light function lights in to the volume.
					InjectTranslucentVolumeLightingArray(GraphBuilder, SortedLights, SimpleLightsEnd, AttenuationLightStart);
				}

				if (SimpleLights.InstanceData.Num() > 0)
				{
					auto& SimpleLightsByView = *GraphBuilder.AllocObject<TArray<FSimpleLightArray, SceneRenderingAllocator>>();
					SimpleLightsByView.SetNum(Views.Num());

					SplitSimpleLightsByView(Views, SimpleLights, SimpleLightsByView);

					for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
					{
						FSimpleLightArray& SimpleLightArray = SimpleLightsByView[ViewIndex];

						if (SimpleLightArray.InstanceData.Num() > 0)
						{
							FViewInfo& View = Views[ViewIndex];
							RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
							RDG_EVENT_SCOPE(GraphBuilder, "InjectSimpleLightsTranslucentLighting");
							InjectSimpleTranslucentVolumeLightingArray(GraphBuilder, SimpleLightArray, View, ViewIndex);
						}
					}
				}
			}
		}

		if ( IsFeatureLevelSupported(ShaderPlatformForFeatureLevel, ERHIFeatureLevel::SM5) )
		{
			RDG_EVENT_SCOPE(GraphBuilder, "IndirectLighting");
			bool bRenderedRSM = false;
			// Render Reflective shadow maps
			// Draw shadowed and light function lights
			for (int32 LightIndex = AttenuationLightStart; LightIndex < SortedLights.Num(); LightIndex++)
			{
				const FSortedLightSceneInfo& SortedLightInfo = SortedLights[LightIndex];
				const FLightSceneInfo& LightSceneInfo = *SortedLightInfo.LightSceneInfo;
				// Render any reflective shadow maps (if necessary)
				if (LightSceneInfo.Proxy && LightSceneInfo.Proxy->NeedsLPVInjection())
				{
					if (LightSceneInfo.Proxy->HasReflectiveShadowMap())
					{
						INC_DWORD_STAT(STAT_NumReflectiveShadowMapLights);
						AddUntrackedAccessPass(GraphBuilder, [this, &LightSceneInfo](FRHICommandListImmediate& RHICmdList)
						{
							InjectReflectiveShadowMaps(RHICmdList, &LightSceneInfo);
						});
						bRenderedRSM = true;
					}
				}
			}

			// LPV Direct Light Injection
			if (bRenderedRSM)
			{
				for (int32 LightIndex = SimpleLightsEnd; LightIndex < SortedLights.Num(); LightIndex++)
				{
					const FSortedLightSceneInfo& SortedLightInfo = SortedLights[LightIndex];
					const FLightSceneInfo* const LightSceneInfo = SortedLightInfo.LightSceneInfo;

					// Render any reflective shadow maps (if necessary)
					if (LightSceneInfo && LightSceneInfo->Proxy && LightSceneInfo->Proxy->NeedsLPVInjection())
					{
						if (!LightSceneInfo->Proxy->HasReflectiveShadowMap())
						{
							// Inject the light directly into all relevant LPVs
							for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
							{
								FViewInfo& View = Views[ViewIndex];
								RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);

								if (LightSceneInfo->ShouldRenderLight(View))
								{
									FSceneViewState* ViewState = (FSceneViewState*)View.State;
									if (ViewState)
									{
										FLightPropagationVolume* Lpv = ViewState->GetLightPropagationVolume(View.GetFeatureLevel());
										if (Lpv && LightSceneInfo->Proxy)
										{
											AddUntrackedAccessPass(GraphBuilder, [Lpv, LightSceneInfo, &View](FRHICommandListImmediate& RHICmdList)
											{
												Lpv->InjectLightDirect(RHICmdList, *LightSceneInfo->Proxy, View);
											});
										}
									}
								}
							}
						}
					}
				}
			}

			// Kickoff the LPV update (asynchronously if possible)
			AddUntrackedAccessPass(GraphBuilder, [this](FRHICommandListImmediate& RHICmdList)
			{
				UpdateLPVs(RHICmdList);
			});
		}

		{
			RDG_EVENT_SCOPE(GraphBuilder, "ShadowedLights");

			const int32 DenoiserMode = CVarShadowUseDenoiser.GetValueOnRenderThread();

			const IScreenSpaceDenoiser* DefaultDenoiser = IScreenSpaceDenoiser::GetDefaultDenoiser();
			const IScreenSpaceDenoiser* DenoiserToUse = DenoiserMode == 1 ? DefaultDenoiser : GScreenSpaceDenoiser;

			TArray<FRDGTextureRef, SceneRenderingAllocator> PreprocessedShadowMaskTextures;
			TArray<FRDGTextureRef, SceneRenderingAllocator> PreprocessedShadowMaskSubPixelTextures;

			const int32 MaxDenoisingBatchSize = FMath::Clamp(CVarMaxShadowDenoisingBatchSize.GetValueOnRenderThread(), 1, IScreenSpaceDenoiser::kMaxBatchSize);
			const int32 MaxRTShadowBatchSize = CVarMaxShadowRayTracingBatchSize.GetValueOnRenderThread();
			const bool bDoShadowDenoisingBatching = DenoiserMode != 0 && MaxDenoisingBatchSize > 1;

			//#dxr_todo: support multiview for the batching case
			const bool bDoShadowBatching = (bDoShadowDenoisingBatching || MaxRTShadowBatchSize > 1) && Views.Num() == 1;

			// Optimisations: batches all shadow ray tracing denoising. Definitely could be smarter to avoid high VGPR pressure if this entire
			// function was converted to render graph, and want least intrusive change as possible. So right not it trades render target memory pressure
			// for denoising perf.
			if (RHI_RAYTRACING && bDoShadowBatching)
			{
				const uint32 ViewIndex = 0;
				FViewInfo& View = Views[ViewIndex];

				// Allocate PreprocessedShadowMaskTextures once so QueueTextureExtraction can deferred write.
				{
					if (!View.bStatePrevViewInfoIsReadOnly)
					{
						View.ViewState->PrevFrameViewInfo.ShadowHistories.Empty();
						View.ViewState->PrevFrameViewInfo.ShadowHistories.Reserve(SortedLights.Num());
					}

					PreprocessedShadowMaskTextures.SetNum(SortedLights.Num());
				}

				PreprocessedShadowMaskTextures.SetNum(SortedLights.Num());

				if (HasHairStrandsClusters(ViewIndex, HairDatas))
				{ 
					PreprocessedShadowMaskSubPixelTextures.SetNum(SortedLights.Num());
				}
			} // if (RHI_RAYTRACING)

			const bool bDirectLighting = ViewFamily.EngineShowFlags.DirectLighting;

			const FIntPoint SceneTextureExtent = SceneDepthTexture->Desc.Extent;
			const FRDGTextureDesc SharedScreenShadowMaskTextureDesc(FRDGTextureDesc::Create2D(SceneTextureExtent, PF_B8G8R8A8, FClearValueBinding::White, TexCreate_RenderTargetable | TexCreate_ShaderResource | GFastVRamConfig.ScreenSpaceShadowMask));

			FRDGTextureRef SharedScreenShadowMaskTexture = nullptr;
			FRDGTextureRef SharedScreenShadowMaskSubPixelTexture = nullptr;

			// Draw shadowed and light function lights
			for (int32 LightIndex = AttenuationLightStart; LightIndex < SortedLights.Num(); LightIndex++)
			{
				const FSortedLightSceneInfo& SortedLightInfo = SortedLights[LightIndex];
				const FLightSceneInfo& LightSceneInfo = *SortedLightInfo.LightSceneInfo;
				const FLightSceneProxy& LightSceneProxy = *LightSceneInfo.Proxy;

				// Note: Skip shadow mask generation for rect light if direct illumination is computed
				//		 stochastically (rather than analytically + shadow mask)
				const bool bDrawShadows = SortedLightInfo.SortKey.Fields.bShadowed;
				const bool bDrawLightFunction = SortedLightInfo.SortKey.Fields.bLightFunction;
				const bool bDrawPreviewIndicator = ViewFamily.EngineShowFlags.PreviewShadowsIndicator && !LightSceneInfo.IsPrecomputedLightingValid() && LightSceneProxy.HasStaticShadowing();
				const bool bDrawHairShadow = bDrawShadows && bUseHairLighting;
				const bool bUseHairDeepShadow = bDrawShadows && bUseHairLighting && LightSceneProxy.CastsHairStrandsDeepShadow();
				bool bInjectedTranslucentVolume = false;
				bool bUsedShadowMaskTexture = false;

				FScopeCycleCounter Context(LightSceneProxy.GetStatId());

				FRDGTextureRef ScreenShadowMaskTexture = nullptr;
				FRDGTextureRef ScreenShadowMaskSubPixelTexture = nullptr;

				if (bDrawShadows || bDrawLightFunction || bDrawPreviewIndicator)
				{
					if (!SharedScreenShadowMaskTexture)
					{
						SharedScreenShadowMaskTexture = GraphBuilder.CreateTexture(SharedScreenShadowMaskTextureDesc, TEXT("ShadowMaskTexture"));

						if (bUseHairLighting)
						{
							SharedScreenShadowMaskSubPixelTexture = GraphBuilder.CreateTexture(SharedScreenShadowMaskTextureDesc, TEXT("ShadowMaskSubPixelTexture"));
						}
					}
					ScreenShadowMaskTexture = SharedScreenShadowMaskTexture;
					ScreenShadowMaskSubPixelTexture = SharedScreenShadowMaskSubPixelTexture;
				}

				FString LightNameWithLevel;
				GetLightNameForDrawEvent(&LightSceneProxy, LightNameWithLevel);
				RDG_EVENT_SCOPE(GraphBuilder, "%s", *LightNameWithLevel);

				if (bDrawShadows)
				{
					INC_DWORD_STAT(STAT_NumShadowedLights);

					const FLightOcclusionType OcclusionType = GetLightOcclusionType(LightSceneProxy);

					// Inline ray traced shadow batching, launches shadow batches when needed
					// reduces memory overhead while keeping shadows batched to optimize costs
					{
						const uint32 ViewIndex = 0;
						FViewInfo& View = Views[ViewIndex];

						IScreenSpaceDenoiser::FShadowRayTracingConfig RayTracingConfig;
						RayTracingConfig.RayCountPerPixel = GShadowRayTracingSamplesPerPixel > -1? GShadowRayTracingSamplesPerPixel : LightSceneProxy.GetSamplesPerPixel();

						const bool bDenoiserCompatible = !LightRequiresDenosier(LightSceneInfo) || IScreenSpaceDenoiser::EShadowRequirements::PenumbraAndClosestOccluder == DenoiserToUse->GetShadowRequirements(View, LightSceneInfo, RayTracingConfig);

						const bool bWantsBatchedShadow = OcclusionType == FLightOcclusionType::Raytraced && 
							bDoShadowBatching &&
							bDenoiserCompatible &&
							SortedLightInfo.SortKey.Fields.bShadowed;

						// determine if this light doesn't yet have a precomuted shadow and execute a batch to amortize costs if one is needed
						if (
							RHI_RAYTRACING &&
							bWantsBatchedShadow &&
							(PreprocessedShadowMaskTextures.Num() == 0 || !PreprocessedShadowMaskTextures[LightIndex - AttenuationLightStart]))
						{
							RDG_EVENT_SCOPE(GraphBuilder, "ShadowBatch");
							TStaticArray<IScreenSpaceDenoiser::FShadowVisibilityParameters, IScreenSpaceDenoiser::kMaxBatchSize> DenoisingQueue;
							TStaticArray<int32, IScreenSpaceDenoiser::kMaxBatchSize> LightIndices;

							FSceneTextureParameters SceneTextures = GetSceneTextureParameters(GraphBuilder, SceneTexturesUniformBuffer);

							int32 ProcessShadows = 0;

							const auto QuickOffDenoisingBatch = [&]
							{
								int32 InputParameterCount = 0;
								for (int32 i = 0; i < IScreenSpaceDenoiser::kMaxBatchSize; i++)
								{
									InputParameterCount += DenoisingQueue[i].LightSceneInfo != nullptr ? 1 : 0;
								}

								check(InputParameterCount >= 1);

								TStaticArray<IScreenSpaceDenoiser::FShadowVisibilityOutputs, IScreenSpaceDenoiser::kMaxBatchSize> Outputs;

								RDG_EVENT_SCOPE(GraphBuilder, "%s%s(Shadow BatchSize=%d) %dx%d",
									DenoiserToUse != DefaultDenoiser ? TEXT("ThirdParty ") : TEXT(""),
									DenoiserToUse->GetDebugName(),
									InputParameterCount,
									View.ViewRect.Width(), View.ViewRect.Height());

								DenoiserToUse->DenoiseShadowVisibilityMasks(
									GraphBuilder,
									View,
									&View.PrevViewInfo,
									SceneTextures,
									DenoisingQueue,
									InputParameterCount,
									Outputs);

								for (int32 i = 0; i < InputParameterCount; i++)
								{
									const FLightSceneInfo* LocalLightSceneInfo = DenoisingQueue[i].LightSceneInfo;

									int32 LocalLightIndex = LightIndices[i];
									FRDGTextureRef& RefDestination = PreprocessedShadowMaskTextures[LocalLightIndex - AttenuationLightStart];
									check(RefDestination == nullptr);
									RefDestination = Outputs[i].Mask;
									DenoisingQueue[i].LightSceneInfo = nullptr;
								}
							}; // QuickOffDenoisingBatch

							// Ray trace shadows of light that needs, and quick off denoising batch.
							for (int32 LightBatchIndex = LightIndex; LightBatchIndex < SortedLights.Num(); LightBatchIndex++)
							{
								const FSortedLightSceneInfo& BatchSortedLightInfo = SortedLights[LightBatchIndex];
								const FLightSceneInfo& BatchLightSceneInfo = *BatchSortedLightInfo.LightSceneInfo;

								// Denoiser do not support texture rect light important sampling.
								const bool bBatchDrawShadows = BatchSortedLightInfo.SortKey.Fields.bShadowed;

								if (!bBatchDrawShadows)
									continue;

								const FLightOcclusionType BatchOcclusionType = GetLightOcclusionType(*BatchLightSceneInfo.Proxy);
								if (BatchOcclusionType != FLightOcclusionType::Raytraced)
									continue;

								const bool bRequiresDenoiser = LightRequiresDenosier(BatchLightSceneInfo) && DenoiserMode > 0;

								IScreenSpaceDenoiser::FShadowRayTracingConfig BatchRayTracingConfig;
								BatchRayTracingConfig.RayCountPerPixel = GShadowRayTracingSamplesPerPixel > -1 ? GShadowRayTracingSamplesPerPixel : BatchLightSceneInfo.Proxy->GetSamplesPerPixel();

								IScreenSpaceDenoiser::EShadowRequirements DenoiserRequirements = bRequiresDenoiser ?
									DenoiserToUse->GetShadowRequirements(View, BatchLightSceneInfo, BatchRayTracingConfig) :
									IScreenSpaceDenoiser::EShadowRequirements::Bailout;

								// Not worth batching and increase memory pressure if the denoiser do not support this ray tracing config.
								// TODO: add suport for batch with multiple SPP.
								if (bRequiresDenoiser && DenoiserRequirements != IScreenSpaceDenoiser::EShadowRequirements::PenumbraAndClosestOccluder)
								{
									continue;
								}

								// Ray trace the shadow.
								//#dxr_todo: support multiview for the batching case
								FRDGTextureRef RayTracingShadowMaskTexture;
								{
									FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
										SceneTextures.SceneDepthTexture->Desc.Extent,
										PF_FloatRGBA,
										FClearValueBinding::Black,
										TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV);
									RayTracingShadowMaskTexture = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingOcclusion"));
								}

								FRDGTextureRef RayDistanceTexture;
								{
									FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
										SceneTextures.SceneDepthTexture->Desc.Extent,
										PF_R16F,
										FClearValueBinding::Black,
										TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV);
									RayDistanceTexture = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingOcclusionDistance"));
								}

								FRDGTextureRef SubPixelRayTracingShadowMaskTexture = nullptr;
								FRDGTextureUAV* SubPixelRayTracingShadowMaskUAV = nullptr;
								if (bUseHairLighting)
								{
									FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
										SceneTextures.SceneDepthTexture->Desc.Extent,
										PF_FloatRGBA,
										FClearValueBinding::Black,
										TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV);
									SubPixelRayTracingShadowMaskTexture = GraphBuilder.CreateTexture(Desc, TEXT("SubPixelRayTracingOcclusion"));
									SubPixelRayTracingShadowMaskUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SubPixelRayTracingShadowMaskTexture));
								}

								FString BatchLightNameWithLevel;
								GetLightNameForDrawEvent(BatchLightSceneInfo.Proxy, BatchLightNameWithLevel);

								FRDGTextureUAV* RayTracingShadowMaskUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RayTracingShadowMaskTexture));
								FRDGTextureUAV* RayHitDistanceUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RayDistanceTexture));
								FHairStrandsOcclusionResources HairResources = GetHairStrandsResources(ViewIndex, GraphBuilder, HairDatas);
								HairResources.bUseHairVoxel = !BatchLightSceneInfo.Proxy->CastsHairStrandsDeepShadow();
								{
									RDG_EVENT_SCOPE(GraphBuilder, "%s", *BatchLightNameWithLevel);

									// Ray trace the shadow cast by opaque geometries on to hair strands geometries
									// Note: No denoiser is required on this output, as the hair strands are geometrically noisy, which make it hard to denoise
									RenderRayTracingShadows(
										GraphBuilder,
										SceneTextures,
										View,
										BatchLightSceneInfo,
										BatchRayTracingConfig,
										DenoiserRequirements,
										&HairResources,
										LightingChannelsTexture,
										RayTracingShadowMaskUAV,
										RayHitDistanceUAV,
										SubPixelRayTracingShadowMaskUAV);
									
									if (HasHairStrandsClusters(ViewIndex, HairDatas))
									{
										FRDGTextureRef& RefDestination = PreprocessedShadowMaskSubPixelTextures[LightBatchIndex - AttenuationLightStart];
										check(RefDestination == nullptr);
										RefDestination = SubPixelRayTracingShadowMaskTexture;
									}
								}

								bool bBatchFull = false;

								if (bRequiresDenoiser)
								{
									// Queue the ray tracing output for shadow denoising.
									for (int32 i = 0; i < IScreenSpaceDenoiser::kMaxBatchSize; i++)
									{
										if (DenoisingQueue[i].LightSceneInfo == nullptr)
										{
											DenoisingQueue[i].LightSceneInfo = &BatchLightSceneInfo;
											DenoisingQueue[i].RayTracingConfig = RayTracingConfig;
											DenoisingQueue[i].InputTextures.Mask = RayTracingShadowMaskTexture;
											DenoisingQueue[i].InputTextures.ClosestOccluder = RayDistanceTexture;
											LightIndices[i] = LightBatchIndex;

											// If queue for this light type is full, quick of the batch.
											if ((i + 1) == MaxDenoisingBatchSize)
											{
												QuickOffDenoisingBatch();
												bBatchFull = true;
											}
											break;
										}
										else
										{
											check((i - 1) < IScreenSpaceDenoiser::kMaxBatchSize);
										}
									}
								}
								else
								{
									PreprocessedShadowMaskTextures[LightBatchIndex - AttenuationLightStart] = RayTracingShadowMaskTexture;
								}

								// terminate batch if we filled a denoiser batch or hit our max light batch
								ProcessShadows++;
								if (bBatchFull || ProcessShadows == MaxRTShadowBatchSize)
								{
									break;
								}
							}

							// Ensures all denoising queues are processed.
							if (DenoisingQueue[0].LightSceneInfo)
							{
								QuickOffDenoisingBatch();
							}
						}
					} // end inline batched raytraced shadow

					if (RHI_RAYTRACING && PreprocessedShadowMaskTextures.Num() > 0 && PreprocessedShadowMaskTextures[LightIndex - AttenuationLightStart])
					{
						const uint32 ShadowMaskIndex = LightIndex - AttenuationLightStart;
						ScreenShadowMaskTexture = PreprocessedShadowMaskTextures[ShadowMaskIndex];
						PreprocessedShadowMaskTextures[ShadowMaskIndex] = nullptr;

						// Subp-ixel shadow for hair strands geometries
						if (bUseHairLighting && ShadowMaskIndex < uint32(PreprocessedShadowMaskSubPixelTextures.Num()))
						{
							ScreenShadowMaskSubPixelTexture = PreprocessedShadowMaskSubPixelTextures[ShadowMaskIndex];
							PreprocessedShadowMaskSubPixelTextures[ShadowMaskIndex] = nullptr;
						}

						// Inject deep shadow mask if the light supports it
						if (bUseHairDeepShadow)
						{
							RenderHairStrandsShadowMask(GraphBuilder, Views, &LightSceneInfo, HairDatas, ScreenShadowMaskTexture);
						}
					}
					else if (OcclusionType == FLightOcclusionType::Raytraced)
					{
						FSceneTextureParameters SceneTextures = GetSceneTextureParameters(GraphBuilder, SceneTexturesUniformBuffer);

						FRDGTextureRef RayTracingShadowMaskTexture;
						{
							FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
								SceneTextures.SceneDepthTexture->Desc.Extent,
								PF_FloatRGBA,
								FClearValueBinding::Black,
								TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV);
							RayTracingShadowMaskTexture = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingOcclusion"));
						}

						FRDGTextureRef RayDistanceTexture;
						{
							FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
								SceneTextures.SceneDepthTexture->Desc.Extent,
								PF_R16F,
								FClearValueBinding::Black,
								TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV);
							RayDistanceTexture = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingOcclusionDistance"));
						}

						FRDGTextureUAV* RayTracingShadowMaskUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RayTracingShadowMaskTexture));
						FRDGTextureUAV* RayHitDistanceUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RayDistanceTexture));

						FRDGTextureRef SubPixelRayTracingShadowMaskTexture = nullptr;
						FRDGTextureUAV* SubPixelRayTracingShadowMaskUAV = nullptr;
						if (bUseHairLighting)
						{
							FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
								SceneTextures.SceneDepthTexture->Desc.Extent,
								PF_FloatRGBA,
								FClearValueBinding::Black,
								TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV);
							SubPixelRayTracingShadowMaskTexture = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingOcclusion"));
							SubPixelRayTracingShadowMaskUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SubPixelRayTracingShadowMaskTexture));
						}


						FRDGTextureRef RayTracingShadowMaskTileTexture;
						{
							FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(
								SceneTextures.SceneDepthTexture->Desc.Extent,
								PF_FloatRGBA,
								FClearValueBinding::Black,
								TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV);
							RayTracingShadowMaskTileTexture = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingOcclusionTile"));
						}

						bool bIsMultiview = Views.Num() > 0;

						for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
						{
							FViewInfo& View = Views[ViewIndex];
							RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);

							IScreenSpaceDenoiser::FShadowRayTracingConfig RayTracingConfig;
							RayTracingConfig.RayCountPerPixel = GShadowRayTracingSamplesPerPixel > -1 ? GShadowRayTracingSamplesPerPixel : LightSceneProxy.GetSamplesPerPixel();

							IScreenSpaceDenoiser::EShadowRequirements DenoiserRequirements = IScreenSpaceDenoiser::EShadowRequirements::Bailout;
							if (DenoiserMode != 0 && LightRequiresDenosier(LightSceneInfo))
							{
								DenoiserRequirements = DenoiserToUse->GetShadowRequirements(View, LightSceneInfo, RayTracingConfig);
							}

							FHairStrandsOcclusionResources HairResources = GetHairStrandsResources(ViewIndex, GraphBuilder, HairDatas);
							HairResources.bUseHairVoxel = !bUseHairDeepShadow;

							RenderRayTracingShadows(
								GraphBuilder,
								SceneTextures,
								View,
								LightSceneInfo,
								RayTracingConfig,
								DenoiserRequirements,
								&HairResources,
								LightingChannelsTexture,
								RayTracingShadowMaskUAV,
								RayHitDistanceUAV,
								SubPixelRayTracingShadowMaskUAV);

							if (DenoiserRequirements != IScreenSpaceDenoiser::EShadowRequirements::Bailout)
							{
								TStaticArray<IScreenSpaceDenoiser::FShadowVisibilityParameters, IScreenSpaceDenoiser::kMaxBatchSize> InputParameters;
								TStaticArray<IScreenSpaceDenoiser::FShadowVisibilityOutputs, IScreenSpaceDenoiser::kMaxBatchSize> Outputs;

								InputParameters[0].InputTextures.Mask = RayTracingShadowMaskTexture;
								InputParameters[0].InputTextures.ClosestOccluder = RayDistanceTexture;
								InputParameters[0].LightSceneInfo = &LightSceneInfo;
								InputParameters[0].RayTracingConfig = RayTracingConfig;

								int32 InputParameterCount = 1;

								RDG_EVENT_SCOPE(GraphBuilder, "%s%s(Shadow BatchSize=%d) %dx%d",
									DenoiserToUse != DefaultDenoiser ? TEXT("ThirdParty ") : TEXT(""),
									DenoiserToUse->GetDebugName(),
									InputParameterCount,
									View.ViewRect.Width(), View.ViewRect.Height());

								DenoiserToUse->DenoiseShadowVisibilityMasks(
									GraphBuilder,
									View,
									&View.PrevViewInfo,
									SceneTextures,
									InputParameters,
									InputParameterCount,
									Outputs);

								if (bIsMultiview)
								{
									AddDrawTexturePass(GraphBuilder, View, Outputs[0].Mask, RayTracingShadowMaskTileTexture, View.ViewRect.Min, View.ViewRect.Min, View.ViewRect.Size());
									ScreenShadowMaskTexture = RayTracingShadowMaskTileTexture;
								}
								else
								{
									ScreenShadowMaskTexture = Outputs[0].Mask;
								}
							}
							else
							{
								ScreenShadowMaskTexture = RayTracingShadowMaskTexture;
							}

							if (HasHairStrandsClusters(ViewIndex, HairDatas))
							{
								ScreenShadowMaskSubPixelTexture = SubPixelRayTracingShadowMaskTexture;
							}
						}

						// Inject deep shadow mask if the light supports it
						if (HairDatas && bUseHairDeepShadow)
						{
							RenderHairStrandsShadowMask(GraphBuilder, Views, &LightSceneInfo, HairDatas, ScreenShadowMaskTexture);
						}
					}
					else // (OcclusionType == FOcclusionType::Shadowmap)
					{
						for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
						{
							const FViewInfo& View = Views[ViewIndex];
							RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
							View.HeightfieldLightingViewInfo.ClearShadowing(GraphBuilder, View, LightSceneInfo);
						}
					
						const auto ClearShadowMask = [&](FRDGTextureRef InScreenShadowMaskTexture)
						{
							// Clear light attenuation for local lights with a quad covering their extents
							const bool bClearLightScreenExtentsOnly = CVarAllowClearLightSceneExtentsOnly.GetValueOnRenderThread() && SortedLightInfo.SortKey.Fields.LightType != LightType_Directional;

							if (bClearLightScreenExtentsOnly)
							{
								FRenderTargetParameters* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
								PassParameters->RenderTargets[0] = FRenderTargetBinding(InScreenShadowMaskTexture, ERenderTargetLoadAction::ENoAction);

								GraphBuilder.AddPass(
									RDG_EVENT_NAME("ClearQuad"),
									PassParameters,
									ERDGPassFlags::Raster,
									[this, &LightSceneProxy](FRHICommandList& RHICmdList)
								{
									for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
									{
										const FViewInfo& View = Views[ViewIndex];
										SCOPED_GPU_MASK(RHICmdList, View.GPUMask);

										FIntRect ScissorRect;
										if (!LightSceneProxy.GetScissorRect(ScissorRect, View, View.ViewRect))
										{
											ScissorRect = View.ViewRect;
										}

										if (ScissorRect.Min.X < ScissorRect.Max.X && ScissorRect.Min.Y < ScissorRect.Max.Y)
										{
											RHICmdList.SetViewport(ScissorRect.Min.X, ScissorRect.Min.Y, 0.0f, ScissorRect.Max.X, ScissorRect.Max.Y, 1.0f);
											DrawClearQuad(RHICmdList, true, FLinearColor(1, 1, 1, 1), false, 0, false, 0);
										}
										else
										{
											LightSceneProxy.GetScissorRect(ScissorRect, View, View.ViewRect);
										}
									}
								});
							}
							else
							{
								AddClearRenderTargetPass(GraphBuilder, InScreenShadowMaskTexture);
							}
						};

						ClearShadowMask(ScreenShadowMaskTexture);
						if (ScreenShadowMaskSubPixelTexture)
						{
							ClearShadowMask(ScreenShadowMaskSubPixelTexture);
						}

						RenderDeferredShadowProjections(GraphBuilder, SceneTexturesUniformBuffer,  &LightSceneInfo, ScreenShadowMaskTexture, ScreenShadowMaskSubPixelTexture, SceneDepthTexture, HairDatas, bInjectedTranslucentVolume);
					}

					bUsedShadowMaskTexture = true;
				}

				for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
				{
					const FViewInfo& View = Views[ViewIndex];
					RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);
					View.HeightfieldLightingViewInfo.ComputeLighting(GraphBuilder, View, LightSceneInfo);
				}

				// Render light function to the attenuation buffer.
				if (bDirectLighting)
				{
					if (bDrawLightFunction)
					{
						const bool bLightFunctionRendered = RenderLightFunction(GraphBuilder, SceneDepthTexture, SceneTexturesUniformBuffer, &LightSceneInfo, ScreenShadowMaskTexture, bDrawShadows, false);
						bUsedShadowMaskTexture |= bLightFunctionRendered;
					}

					if (bDrawPreviewIndicator)
					{
						RenderPreviewShadowsIndicator(GraphBuilder, SceneDepthTexture, SceneTexturesUniformBuffer, &LightSceneInfo, ScreenShadowMaskTexture, bUsedShadowMaskTexture);
					}

					if (!bDrawShadows)
					{
						INC_DWORD_STAT(STAT_NumLightFunctionOnlyLights);
					}
				}

				if(bDirectLighting && !bInjectedTranslucentVolume)
				{
					for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
					{
						FViewInfo& View = Views[ViewIndex];
						RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);

						// Accumulate this light's unshadowed contribution to the translucency lighting volume
						InjectTranslucentVolumeLighting(GraphBuilder, LightSceneInfo, nullptr, View, ViewIndex);
					}
				}

				// If we never rendered into the mask, don't attempt to read from it.
				if (!bUsedShadowMaskTexture)
				{
					ScreenShadowMaskTexture = nullptr;
					ScreenShadowMaskSubPixelTexture = nullptr;
				}

				// Render the light to the scene color buffer, conditionally using the attenuation buffer or a 1x1 white texture as input 
				if (bDirectLighting)
				{
					const bool bRenderOverlap = false;
					RenderLight(GraphBuilder, SceneColorTexture, SceneDepthTexture, SceneTexturesUniformBuffer, &LightSceneInfo, ScreenShadowMaskTexture, LightingChannelsTexture, InHairVisibilityViews, bRenderOverlap);
				}

				if (bUseHairLighting)
				{
					FHairStrandsTransmittanceMaskData TransmittanceMaskData = DummyTransmittanceMaskData;
					if (bDrawHairShadow)
					{
						TransmittanceMaskData = RenderHairStrandsTransmittanceMask(GraphBuilder, Views, &LightSceneInfo, HairDatas, ScreenShadowMaskSubPixelTexture);

						// Note: ideally the light should still be evaluated for hair when not casting shadow, but for preserving the old behavior, and not adding 
						// any perf. regression, we disable this light for hair rendering 
						RenderLightForHair(GraphBuilder, SceneTexturesUniformBuffer, &LightSceneInfo, ScreenShadowMaskSubPixelTexture, LightingChannelsTexture, TransmittanceMaskData, InHairVisibilityViews);
					}
				}
			}
		}
	}

	return SceneColorTexture;
}

void FDeferredShadingSceneRenderer::RenderLightArrayForOverlapViewmode(
	FRHICommandList& RHICmdList,
	FRHITexture* LightingChannelsTexture,
	const TSparseArray<FLightSceneInfoCompact>& LightArray)
{
	for (TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(LightArray); LightIt; ++LightIt)
	{
		const FLightSceneInfoCompact& LightSceneInfoCompact = *LightIt;
		const FLightSceneInfo* LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;

		// Nothing to do for black lights.
		if(LightSceneInfoCompact.Color.IsAlmostBlack())
		{
			continue;
		}

		bool bShouldRender = false;

		// Check if the light is visible in any of the views.
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			bShouldRender |= LightSceneInfo->ShouldRenderLight(Views[ViewIndex]);
		}

		if (bShouldRender
			// Only render shadow casting stationary lights
			&& LightSceneInfo->Proxy->HasStaticShadowing()
			&& !LightSceneInfo->Proxy->HasStaticLighting()
			&& LightSceneInfo->Proxy->CastsStaticShadow())
		{
			RenderLight(RHICmdList, LightSceneInfo, nullptr, LightingChannelsTexture, nullptr, true, false);
		}
	}
}

void FDeferredShadingSceneRenderer::RenderStationaryLightOverlap(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef SceneColorTexture,
	FRDGTextureRef SceneDepthTexture,
	FRDGTextureRef LightingChannelsTexture,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer)
{
	if (Scene->bIsEditorScene)
	{
		FRenderLightParameters* PassParameters = GraphBuilder.AllocParameters<FRenderLightParameters>();
		GetRenderLightParameters(SceneColorTexture, SceneDepthTexture, nullptr, LightingChannelsTexture, SceneTexturesUniformBuffer, nullptr, *PassParameters);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("StationaryLightOverlap"),
			PassParameters,
			ERDGPassFlags::Raster,
			[this, LightingChannelsTexture](FRHICommandList& RHICmdList)
		{
			FRHITexture* LightingChannelsTextureRHI = TryGetRHI(LightingChannelsTexture);

			// Clear to discard base pass values in scene color since we didn't skip that, to have valid scene depths
			DrawClearQuad(RHICmdList, FLinearColor::Black);

			RenderLightArrayForOverlapViewmode(RHICmdList, LightingChannelsTextureRHI, Scene->Lights);

			//Note: making use of FScene::InvisibleLights, which contains lights that haven't been added to the scene in the same way as visible lights
			// So code called by RenderLightArrayForOverlapViewmode must be careful what it accesses
			RenderLightArrayForOverlapViewmode(RHICmdList, LightingChannelsTextureRHI, Scene->InvisibleLights);
		});
	}
}

/** Sets up rasterizer and depth state for rendering bounding geometry in a deferred pass. */
void SetBoundingGeometryRasterizerAndDepthState(FGraphicsPipelineStateInitializer& GraphicsPSOInit, const FViewInfo& View, const FSphere& LightBounds)
{
	const bool bCameraInsideLightGeometry = ((FVector)View.ViewMatrices.GetViewOrigin() - LightBounds.Center).SizeSquared() < FMath::Square(LightBounds.W * 1.05f + View.NearClippingDistance * 2.0f)
		// Always draw backfaces in ortho
		//@todo - accurate ortho camera / light intersection
		|| !View.IsPerspectiveProjection();

	if (bCameraInsideLightGeometry)
	{
		// Render backfaces with depth tests disabled since the camera is inside (or close to inside) the light geometry
		GraphicsPSOInit.RasterizerState = View.bReverseCulling ? TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI() : TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI();
	}
	else
	{
		// Render frontfaces with depth tests on to get the speedup from HiZ since the camera is outside the light geometry
		GraphicsPSOInit.RasterizerState = View.bReverseCulling ? TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI() : TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI();
	}

	GraphicsPSOInit.DepthStencilState =
		bCameraInsideLightGeometry
		? TStaticDepthStencilState<false, CF_Always>::GetRHI()
		: TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI();
}

template<bool bUseIESProfile, bool bRadialAttenuation, bool bInverseSquaredFalloff>
static void SetShaderTemplLightingSimple(
	FRHICommandList& RHICmdList,
	FGraphicsPipelineStateInitializer& GraphicsPSOInit,
	const FViewInfo& View,
	const TShaderRef<FShader>& VertexShader,
	const FSimpleLightEntry& SimpleLight,
	const FSimpleLightPerViewEntry& SimpleLightPerViewData)
{
	FDeferredLightPS::FPermutationDomain PermutationVector;
	PermutationVector.Set< FDeferredLightPS::FSourceShapeDim >( ELightSourceShape::Capsule );
	PermutationVector.Set< FDeferredLightPS::FIESProfileDim >( bUseIESProfile );
	PermutationVector.Set< FDeferredLightPS::FInverseSquaredDim >( bInverseSquaredFalloff );
	PermutationVector.Set< FDeferredLightPS::FVisualizeCullingDim >( View.Family->EngineShowFlags.VisualizeLightCulling );
	PermutationVector.Set< FDeferredLightPS::FLightingChannelsDim >( false );
	PermutationVector.Set< FDeferredLightPS::FAnistropicMaterials >(false);
	PermutationVector.Set< FDeferredLightPS::FTransmissionDim >( false );
	PermutationVector.Set< FDeferredLightPS::FHairLighting>( 0 );
	PermutationVector.Set< FDeferredLightPS::FAtmosphereTransmittance >( false );
	PermutationVector.Set< FDeferredLightPS::FCloudTransmittance >( false );

	TShaderMapRef< FDeferredLightPS > PixelShader( View.ShaderMap, PermutationVector );
	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
	PixelShader->SetParametersSimpleLight(RHICmdList, View, SimpleLight, SimpleLightPerViewData);
}

// Use DBT to allow work culling on shadow lights
void CalculateLightNearFarDepthFromBounds(const FViewInfo& View, const FSphere &LightBounds, float &NearDepth, float &FarDepth)
{
	const FMatrix ViewProjection = View.ViewMatrices.GetViewProjectionMatrix();
	const FVector ViewDirection = View.GetViewDirection();

	// push camera relative bounds center along view vec by its radius
	const FVector FarPoint = LightBounds.Center + LightBounds.W * ViewDirection;
	const FVector4 FarPoint4 = FVector4(FarPoint, 1.f);
	const FVector4 FarPoint4Clip = ViewProjection.TransformFVector4(FarPoint4);
	FarDepth = FarPoint4Clip.Z / FarPoint4Clip.W;

	// pull camera relative bounds center along -view vec by its radius
	const FVector NearPoint = LightBounds.Center - LightBounds.W * ViewDirection;
	const FVector4 NearPoint4 = FVector4(NearPoint, 1.f);
	const FVector4 NearPoint4Clip = ViewProjection.TransformFVector4(NearPoint4);
	NearDepth = NearPoint4Clip.Z / NearPoint4Clip.W;

	// negative means behind view, but we use a NearClipPlane==1.f depth

	if (NearPoint4Clip.W < 0)
		NearDepth = 1;

	if (FarPoint4Clip.W < 0)
		FarDepth = 1;

	NearDepth = FMath::Clamp(NearDepth, 0.0f, 1.0f);
	FarDepth = FMath::Clamp(FarDepth, 0.0f, 1.0f);

}

/**
 * Used by RenderLights to render a light to the scene color buffer.
 *
 * @param LightSceneInfo Represents the current light
 * @param LightIndex The light's index into FScene::Lights
 * @return true if anything got rendered
 */

void FDeferredShadingSceneRenderer::RenderLight(
	FRHICommandList& RHICmdList,
	const FLightSceneInfo* LightSceneInfo,
	FRHITexture* ScreenShadowMaskTexture,
	FRHITexture* LightingChannelsTexture,
	const FHairStrandsVisibilityViews* InHairVisibilityViews,
	bool bRenderOverlap, bool bIssueDrawEvent)
{
	SCOPE_CYCLE_COUNTER(STAT_DirectLightRenderingTime);
	INC_DWORD_STAT(STAT_NumLightsUsingStandardDeferred);
	SCOPED_CONDITIONAL_DRAW_EVENT(RHICmdList, StandardDeferredLighting, bIssueDrawEvent);

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	const FSphere LightBounds = LightSceneInfo->Proxy->GetBoundingSphere();
	const bool bTransmission = LightSceneInfo->Proxy->Transmission();

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];
		SCOPED_GPU_MASK(RHICmdList, View.GPUMask);

		// Ensure the light is valid for this view
		if (!LightSceneInfo->ShouldRenderLight(View))
		{
			continue;
		}

		bool bUseIESTexture = false;

		if(View.Family->EngineShowFlags.TexturedLightProfiles)
		{
			bUseIESTexture = (LightSceneInfo->Proxy->GetIESTextureResource() != 0);
		}

		// Set the device viewport for the view.
		RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

		FRenderLightParams RenderLightParams;
		const int32 HairViewIndex = 0; // HAIR_TODO multiview support
		const bool bHairLighting = InHairVisibilityViews && HairViewIndex < InHairVisibilityViews->HairDatas.Num() && InHairVisibilityViews->HairDatas[HairViewIndex].CategorizationTexture != nullptr;
		if (bHairLighting)
		{
			RenderLightParams.HairCategorizationTexture = InHairVisibilityViews->HairDatas[HairViewIndex].CategorizationTexture->GetPooledRenderTarget();
		}
		if (LightSceneInfo->Proxy->GetLightType() == LightType_Directional)
		{
			// Turn DBT back off
			GraphicsPSOInit.bDepthBounds = false;
			TShaderMapRef<TDeferredLightVS<false> > VertexShader(View.ShaderMap);

			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

			if (bRenderOverlap)
			{
				TShaderMapRef<TDeferredLightOverlapPS<false> > PixelShader(View.ShaderMap);
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
				PixelShader->SetParameters(RHICmdList, View, LightSceneInfo);
			}
			else
			{
				const bool bAtmospherePerPixelTransmittance = LightSceneInfo->Proxy->IsUsedAsAtmosphereSunLight() 
					&& LightSceneInfo->Proxy->GetUsePerPixelAtmosphereTransmittance() && ShouldRenderSkyAtmosphere(Scene, View.Family->EngineShowFlags);

				// Only atmospheric light 0 supports cloud shadow as of today.
				FLightSceneProxy* AtmosphereLight0Proxy = Scene->AtmosphereLights[0] ? Scene->AtmosphereLights[0]->Proxy : nullptr;
				FLightSceneProxy* AtmosphereLight1Proxy = Scene->AtmosphereLights[1] ? Scene->AtmosphereLights[1]->Proxy : nullptr;
				FVolumetricCloudRenderSceneInfo* CloudInfo = Scene->GetVolumetricCloudSceneInfo();
				const bool VolumetricCloudShadowMap0Valid = View.VolumetricCloudShadowRenderTarget[0].IsValid();
				const bool VolumetricCloudShadowMap1Valid = View.VolumetricCloudShadowRenderTarget[1].IsValid();
				const bool bLight0CloudPerPixelTransmittance = CloudInfo && VolumetricCloudShadowMap0Valid && AtmosphereLight0Proxy == LightSceneInfo->Proxy && AtmosphereLight0Proxy && AtmosphereLight0Proxy->GetCloudShadowOnSurfaceStrength() > 0.0f;
				const bool bLight1CloudPerPixelTransmittance = CloudInfo && VolumetricCloudShadowMap1Valid && AtmosphereLight1Proxy == LightSceneInfo->Proxy && AtmosphereLight1Proxy && AtmosphereLight1Proxy->GetCloudShadowOnSurfaceStrength() > 0.0f;
				if (bLight0CloudPerPixelTransmittance)
				{
					RenderLightParams.Cloud_ShadowmapTexture = View.VolumetricCloudShadowRenderTarget[0];
					RenderLightParams.Cloud_ShadowmapFarDepthKm = CloudInfo->GetVolumetricCloudCommonShaderParameters().CloudShadowmapFarDepthKm[0].X;
					RenderLightParams.Cloud_WorldToLightClipShadowMatrix = CloudInfo->GetVolumetricCloudCommonShaderParameters().CloudShadowmapWorldToLightClipMatrix[0];
					RenderLightParams.Cloud_ShadowmapStrength = AtmosphereLight0Proxy->GetCloudShadowOnSurfaceStrength();
				}
				else if(bLight1CloudPerPixelTransmittance)
				{
					RenderLightParams.Cloud_ShadowmapTexture = View.VolumetricCloudShadowRenderTarget[1];
					RenderLightParams.Cloud_ShadowmapFarDepthKm = CloudInfo->GetVolumetricCloudCommonShaderParameters().CloudShadowmapFarDepthKm[1].X;
					RenderLightParams.Cloud_WorldToLightClipShadowMatrix = CloudInfo->GetVolumetricCloudCommonShaderParameters().CloudShadowmapWorldToLightClipMatrix[1];
					RenderLightParams.Cloud_ShadowmapStrength = AtmosphereLight1Proxy->GetCloudShadowOnSurfaceStrength();
				}

				FDeferredLightPS::FPermutationDomain PermutationVector;
				PermutationVector.Set< FDeferredLightPS::FSourceShapeDim >( ELightSourceShape::Directional );
				PermutationVector.Set< FDeferredLightPS::FIESProfileDim >( false );
				PermutationVector.Set< FDeferredLightPS::FInverseSquaredDim >( false );
				PermutationVector.Set< FDeferredLightPS::FVisualizeCullingDim >( View.Family->EngineShowFlags.VisualizeLightCulling );
				PermutationVector.Set< FDeferredLightPS::FLightingChannelsDim >( View.bUsesLightingChannels );
				PermutationVector.Set< FDeferredLightPS::FAnistropicMaterials >(ShouldRenderAnisotropyPass());
				PermutationVector.Set< FDeferredLightPS::FTransmissionDim >( bTransmission );
				PermutationVector.Set< FDeferredLightPS::FHairLighting>(0);
				// Only directional lights are rendered in this path, so we only need to check if it is use to light the atmosphere
				PermutationVector.Set< FDeferredLightPS::FAtmosphereTransmittance >(bAtmospherePerPixelTransmittance);
				PermutationVector.Set< FDeferredLightPS::FCloudTransmittance >(bLight0CloudPerPixelTransmittance || bLight1CloudPerPixelTransmittance);

				TShaderMapRef< FDeferredLightPS > PixelShader( View.ShaderMap, PermutationVector );
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
				PixelShader->SetParameters(RHICmdList, View, LightSceneInfo, ScreenShadowMaskTexture, LightingChannelsTexture, &RenderLightParams);
			}

			VertexShader->SetParameters(RHICmdList, View, LightSceneInfo);

			// Apply the directional light as a full screen quad
			DrawRectangle(
				RHICmdList,
				0, 0,
				View.ViewRect.Width(), View.ViewRect.Height(),
				View.ViewRect.Min.X, View.ViewRect.Min.Y,
				View.ViewRect.Width(), View.ViewRect.Height(),
				View.ViewRect.Size(),
				FSceneRenderTargets::Get(RHICmdList).GetBufferSizeXY(),
				VertexShader,
				EDRF_UseTriangleOptimization);
		}
		else
		{
			// Use DBT to allow work culling on shadow lights
			// Disable depth bound when hair rendering is enabled as this rejects partially covered pixel write (with opaque background)
			GraphicsPSOInit.bDepthBounds = GSupportsDepthBoundsTest && GAllowDepthBoundsTest != 0;

			TShaderMapRef<TDeferredLightVS<true> > VertexShader(View.ShaderMap);

			SetBoundingGeometryRasterizerAndDepthState(GraphicsPSOInit, View, LightBounds);

			if (bRenderOverlap)
			{
				TShaderMapRef<TDeferredLightOverlapPS<true> > PixelShader(View.ShaderMap);
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
				PixelShader->SetParameters(RHICmdList, View, LightSceneInfo);
			}
			else
			{
				FDeferredLightPS::FPermutationDomain PermutationVector;
				PermutationVector.Set< FDeferredLightPS::FSourceShapeDim >( LightSceneInfo->Proxy->IsRectLight() ? ELightSourceShape::Rect : ELightSourceShape::Capsule );
				PermutationVector.Set< FDeferredLightPS::FSourceTextureDim >( LightSceneInfo->Proxy->IsRectLight() && LightSceneInfo->Proxy->HasSourceTexture() );
				PermutationVector.Set< FDeferredLightPS::FIESProfileDim >( bUseIESTexture );
				PermutationVector.Set< FDeferredLightPS::FInverseSquaredDim >( LightSceneInfo->Proxy->IsInverseSquared() );
				PermutationVector.Set< FDeferredLightPS::FVisualizeCullingDim >( View.Family->EngineShowFlags.VisualizeLightCulling );
				PermutationVector.Set< FDeferredLightPS::FLightingChannelsDim >( View.bUsesLightingChannels );
				PermutationVector.Set< FDeferredLightPS::FAnistropicMaterials >(ShouldRenderAnisotropyPass() && !LightSceneInfo->Proxy->IsRectLight());
				PermutationVector.Set< FDeferredLightPS::FTransmissionDim >( bTransmission );
				PermutationVector.Set< FDeferredLightPS::FHairLighting>(0);
				PermutationVector.Set < FDeferredLightPS::FAtmosphereTransmittance >(false);
				PermutationVector.Set< FDeferredLightPS::FCloudTransmittance >(false);

				TShaderMapRef< FDeferredLightPS > PixelShader( View.ShaderMap, PermutationVector );
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
				PixelShader->SetParameters(RHICmdList, View, LightSceneInfo, ScreenShadowMaskTexture, LightingChannelsTexture, (bHairLighting) ? &RenderLightParams : nullptr);
			}

			VertexShader->SetParameters(RHICmdList, View, LightSceneInfo);

			// Use DBT to allow work culling on shadow lights
			if (GraphicsPSOInit.bDepthBounds)
			{
				// Can use the depth bounds test to skip work for pixels which won't be touched by the light (i.e outside the depth range)
				float NearDepth = 1.f;
				float FarDepth = 0.f;
				CalculateLightNearFarDepthFromBounds(View,LightBounds,NearDepth,FarDepth);

				if (NearDepth <= FarDepth)
				{
					NearDepth = 1.0f;
					FarDepth = 0.0f;
				}

				// UE4 uses reversed depth, so far < near
				RHICmdList.SetDepthBounds(FarDepth, NearDepth);
			}

			if( LightSceneInfo->Proxy->GetLightType() == LightType_Point ||
				LightSceneInfo->Proxy->GetLightType() == LightType_Rect )
			{
				// Apply the point or spot light with some approximate bounding geometry,
				// So we can get speedups from depth testing and not processing pixels outside of the light's influence.
				StencilingGeometry::DrawSphere(RHICmdList);
			}
			else if (LightSceneInfo->Proxy->GetLightType() == LightType_Spot)
			{
				StencilingGeometry::DrawCone(RHICmdList);
			}
		}
	}
}

void FDeferredShadingSceneRenderer::RenderLight(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef SceneColorTexture,
	FRDGTextureRef SceneDepthTexture,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	const FLightSceneInfo* LightSceneInfo,
	FRDGTextureRef ScreenShadowMaskTexture,
	FRDGTextureRef LightingChannelsTexture,
	const FHairStrandsVisibilityViews* InHairVisibilityViews,
	bool bRenderOverlap)
{
	FRenderLightParameters* PassParameters = GraphBuilder.AllocParameters<FRenderLightParameters>();
	GetRenderLightParameters(SceneColorTexture, SceneDepthTexture, ScreenShadowMaskTexture, LightingChannelsTexture, SceneTexturesUniformBuffer, InHairVisibilityViews, *PassParameters);

	ERDGPassFlags PassFlags = ERDGPassFlags::Raster;

	if (InHairVisibilityViews)
	{
		PassFlags |= ERDGPassFlags::UntrackedAccess;
	}

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("StandardDeferredLighting"),
		PassParameters,
		PassFlags,
		[this, LightSceneInfo, ScreenShadowMaskTexture, LightingChannelsTexture, InHairVisibilityViews, bRenderOverlap](FRHICommandList& RHICmdList)
	{
		RenderLight(
			RHICmdList,
			LightSceneInfo,
			TryGetRHI(ScreenShadowMaskTexture),
			TryGetRHI(LightingChannelsTexture),
			InHairVisibilityViews,
			bRenderOverlap,
			false);
	});
}

BEGIN_SHADER_PARAMETER_STRUCT(FRenderLightForHairParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FRenderLightParameters, Light)
	RDG_TEXTURE_ACCESS(HairIndexAndCountTexture, ERHIAccess::SRVGraphics)
	RDG_TEXTURE_ACCESS(HairNodeCount, ERHIAccess::SRVGraphics)
	RDG_BUFFER_ACCESS(HairTransmittanceMask, ERHIAccess::SRVGraphics)
	RDG_BUFFER_ACCESS(HairVisibilityNodeData, ERHIAccess::SRVGraphics)
	RDG_BUFFER_ACCESS(HairVisibilityNodeCoords, ERHIAccess::SRVGraphics)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, HairTransmittanceMaskSRV)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, HairVisibilityNodeDataSRV)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, HairVisibilityNodeCoordsSRV)
END_SHADER_PARAMETER_STRUCT()

void FDeferredShadingSceneRenderer::RenderLightForHair(
	FRDGBuilder& GraphBuilder,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	const FLightSceneInfo* LightSceneInfo,
	FRDGTextureRef HairShadowMaskTexture,
	FRDGTextureRef LightingChannelsTexture,
	const FHairStrandsTransmittanceMaskData& InTransmittanceMaskData,
	const FHairStrandsVisibilityViews* InHairVisibilityViews)
{
	const bool bHairRenderingEnabled = InHairVisibilityViews && (LightSceneInfo->Proxy->CastsHairStrandsDeepShadow() || IsHairStrandsVoxelizationEnable());
	if (!bHairRenderingEnabled)
	{
		return;
	}
	
	SCOPE_CYCLE_COUNTER(STAT_DirectLightRenderingTime);
	INC_DWORD_STAT(STAT_NumLightsUsingStandardDeferred);
	RDG_EVENT_SCOPE(GraphBuilder, "StandardDeferredLighting_Hair");

	const FSphere LightBounds = LightSceneInfo->Proxy->GetBoundingSphere();
	const bool bTransmission = LightSceneInfo->Proxy->Transmission();

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];
		RDG_GPU_MASK_SCOPE(GraphBuilder, View.GPUMask);

		// Ensure the light is valid for this view
		if (!LightSceneInfo->ShouldRenderLight(View) || ViewIndex >= InHairVisibilityViews->HairDatas.Num())
		{
			continue;
		}

		const FHairStrandsVisibilityData& HairVisibilityData = InHairVisibilityViews->HairDatas[ViewIndex];
		if (!HairVisibilityData.SampleLightingBuffer)
		{
			continue;
		}

		FRenderLightForHairParameters* PassParameters = GraphBuilder.AllocParameters<FRenderLightForHairParameters>();
		GetRenderLightParameters(HairVisibilityData.SampleLightingBuffer, nullptr, HairShadowMaskTexture, LightingChannelsTexture, SceneTexturesUniformBuffer, InHairVisibilityViews, PassParameters->Light);
		PassParameters->HairIndexAndCountTexture = HairVisibilityData.NodeIndex;

		PassParameters->HairTransmittanceMask = InTransmittanceMaskData.TransmittanceMask;
		PassParameters->HairVisibilityNodeData = HairVisibilityData.NodeData;
		PassParameters->HairVisibilityNodeCoords = HairVisibilityData.NodeCoord;
		PassParameters->HairNodeCount = HairVisibilityData.NodeCount;
		PassParameters->HairTransmittanceMaskSRV = GraphBuilder.CreateSRV(InTransmittanceMaskData.TransmittanceMask);
		PassParameters->HairVisibilityNodeDataSRV = GraphBuilder.CreateSRV(HairVisibilityData.NodeData);
		PassParameters->HairVisibilityNodeCoordsSRV = GraphBuilder.CreateSRV(HairVisibilityData.NodeCoord);

		const bool bIsShadowMaskValid = !!PassParameters->Light.ShadowMaskTexture;
		const uint32 MaxTransmittanceElementCount = InTransmittanceMaskData.TransmittanceMask ? InTransmittanceMaskData.TransmittanceMask->Desc.NumElements : 0;
		GraphBuilder.AddPass(
			{},
			PassParameters,
			ERDGPassFlags::Raster | ERDGPassFlags::UntrackedAccess,
			[&HairVisibilityData, &View, PassParameters, LightSceneInfo, MaxTransmittanceElementCount, HairShadowMaskTexture, LightingChannelsTexture, bIsShadowMaskValid](FRHICommandList& RHICmdList)
		{
			RHICmdList.SetViewport(0, 0, 0.0f, HairVisibilityData.SampleLightingViewportResolution.X, HairVisibilityData.SampleLightingViewportResolution.Y, 1.0f);

			FRenderLightParams RenderLightParams;
			RenderLightParams.DeepShadow_TransmittanceMaskBufferMaxCount = MaxTransmittanceElementCount;
			RenderLightParams.ScreenShadowMaskSubPixelTexture = bIsShadowMaskValid ? PassParameters->Light.ShadowMaskTexture->GetPooledRenderTarget() : nullptr;
			RenderLightParams.DeepShadow_TransmittanceMaskBuffer = PassParameters->HairTransmittanceMaskSRV->GetRHI();
			RenderLightParams.HairVisibilityNodeOffsetAndCount = PassParameters->HairIndexAndCountTexture->GetPooledRenderTarget();
			RenderLightParams.HairVisibilityNodeDataSRV = PassParameters->HairVisibilityNodeDataSRV->GetRHI();
			RenderLightParams.HairVisibilityNodeCoordsSRV = PassParameters->HairVisibilityNodeCoordsSRV->GetRHI();
			RenderLightParams.HairCategorizationTexture = PassParameters->Light.HairCategorizationTexture->GetPooledRenderTarget();

			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Max, BF_SourceAlpha, BF_DestAlpha>::GetRHI();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			FDeferredLightPS::FPermutationDomain PermutationVector;
			if (LightSceneInfo->Proxy->GetLightType() == LightType_Directional)
			{
				PermutationVector.Set< FDeferredLightPS::FSourceShapeDim >(ELightSourceShape::Directional);
				PermutationVector.Set< FDeferredLightPS::FSourceTextureDim >(false);
				PermutationVector.Set< FDeferredLightPS::FIESProfileDim >(false);
				PermutationVector.Set< FDeferredLightPS::FInverseSquaredDim >(false);
			}
			else
			{
				const bool bUseIESTexture = View.Family->EngineShowFlags.TexturedLightProfiles && LightSceneInfo->Proxy->GetIESTextureResource() != 0;
				PermutationVector.Set< FDeferredLightPS::FSourceShapeDim >(LightSceneInfo->Proxy->IsRectLight() ? ELightSourceShape::Rect : ELightSourceShape::Capsule);
				PermutationVector.Set< FDeferredLightPS::FSourceTextureDim >(LightSceneInfo->Proxy->IsRectLight() && LightSceneInfo->Proxy->HasSourceTexture());
				PermutationVector.Set< FDeferredLightPS::FIESProfileDim >(bUseIESTexture);
				PermutationVector.Set< FDeferredLightPS::FInverseSquaredDim >(LightSceneInfo->Proxy->IsInverseSquared());
			}
			PermutationVector.Set< FDeferredLightPS::FLightingChannelsDim >(View.bUsesLightingChannels);
			PermutationVector.Set< FDeferredLightPS::FVisualizeCullingDim >(false);
			PermutationVector.Set< FDeferredLightPS::FTransmissionDim >(false);
			PermutationVector.Set< FDeferredLightPS::FHairLighting>(1);

			TShaderMapRef<TDeferredLightHairVS> VertexShader(View.ShaderMap);
			TShaderMapRef<FDeferredLightPS> PixelShader(View.ShaderMap, PermutationVector);

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
			GraphicsPSOInit.bDepthBounds = false;
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			VertexShader->SetParameters(RHICmdList, View, &HairVisibilityData);
			PixelShader->SetParameters(
				RHICmdList,
				View,
				LightSceneInfo,
				TryGetRHI(HairShadowMaskTexture),
				TryGetRHI(LightingChannelsTexture),
				&RenderLightParams);

			RHICmdList.SetStreamSource(0, nullptr, 0);
			RHICmdList.DrawPrimitive(0, 1, 1);
		});
	}
}

// Forward lighting version for hair
void FDeferredShadingSceneRenderer::RenderLightsForHair(
	FRDGBuilder& GraphBuilder,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	FSortedLightSetSceneInfo &SortedLightSet,
	const FHairStrandsRenderingData* HairDatas,
	FRDGTextureRef ScreenShadowMaskSubPixelTexture,
	FRDGTextureRef LightingChannelsTexture)
{
	const FSimpleLightArray &SimpleLights = SortedLightSet.SimpleLights;
	const TArray<FSortedLightSceneInfo, SceneRenderingAllocator> &SortedLights = SortedLightSet.SortedLights;
	const int32 AttenuationLightStart = SortedLightSet.AttenuationLightStart;
	const int32 SimpleLightsEnd = SortedLightSet.SimpleLightsEnd;

	const bool bUseHairLighting = HairDatas != nullptr;
	if (ViewFamily.EngineShowFlags.DirectLighting && bUseHairLighting)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "DirectLighting");

		FHairStrandsTransmittanceMaskData DummyTransmittanceMaskData;
		if (bUseHairLighting && Views.Num() > 0)
		{
			DummyTransmittanceMaskData = CreateDummyHairStrandsTransmittanceMaskData(GraphBuilder, Views[0].ShaderMap);
		}

		for (int32 LightIndex = AttenuationLightStart; LightIndex < SortedLights.Num(); LightIndex++)
		{
			const FSortedLightSceneInfo& SortedLightInfo = SortedLights[LightIndex];
			const FLightSceneInfo& LightSceneInfo = *SortedLightInfo.LightSceneInfo;
			if (LightSceneInfo.Proxy)
			{
				const bool bDrawHairShadow = SortedLightInfo.SortKey.Fields.bShadowed;
				FHairStrandsTransmittanceMaskData TransmittanceMaskData = DummyTransmittanceMaskData;
				if (bDrawHairShadow)
				{
					TransmittanceMaskData = RenderHairStrandsTransmittanceMask(GraphBuilder, Views, &LightSceneInfo, HairDatas, ScreenShadowMaskSubPixelTexture);
				}

				RenderLightForHair(
					GraphBuilder,
					SceneTexturesUniformBuffer,
					&LightSceneInfo,
					ScreenShadowMaskSubPixelTexture,
					LightingChannelsTexture,
					TransmittanceMaskData,
					&HairDatas->HairVisibilityViews);
			}
		}
	}
}

BEGIN_SHADER_PARAMETER_STRUCT(FSimpleLightsStandardDeferredParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void FDeferredShadingSceneRenderer::RenderSimpleLightsStandardDeferred(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef SceneColorTexture,
	FRDGTextureRef SceneDepthTexture,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	const FSimpleLightArray& SimpleLights)
{
	SCOPE_CYCLE_COUNTER(STAT_DirectLightRenderingTime);
	INC_DWORD_STAT_BY(STAT_NumLightsUsingStandardDeferred, SimpleLights.InstanceData.Num());

	FSimpleLightsStandardDeferredParameters* PassParameters = GraphBuilder.AllocParameters<FSimpleLightsStandardDeferredParameters>();
	PassParameters->SceneTextures = SceneTexturesUniformBuffer;
	PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColorTexture, ERenderTargetLoadAction::ELoad);
	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(SceneDepthTexture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthRead_StencilWrite);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("StandardDeferredSimpleLights"),
		PassParameters,
		ERDGPassFlags::Raster,
		[this, &SimpleLights](FRHICommandList& RHICmdList)
	{
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		// Use additive blending for color
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		const int32 NumViews = Views.Num();
		for (int32 LightIndex = 0; LightIndex < SimpleLights.InstanceData.Num(); LightIndex++)
		{
			const FSimpleLightEntry& SimpleLight = SimpleLights.InstanceData[LightIndex];

			for (int32 ViewIndex = 0; ViewIndex < NumViews; ViewIndex++)
			{
				const FSimpleLightPerViewEntry& SimpleLightPerViewData = SimpleLights.GetViewDependentData(LightIndex, ViewIndex, NumViews);
				const FSphere LightBounds(SimpleLightPerViewData.Position, SimpleLight.Radius);

				const FViewInfo& View = Views[ViewIndex];

				// Set the device viewport for the view.
				RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

				TShaderMapRef<TDeferredLightVS<true> > VertexShader(View.ShaderMap);

				SetBoundingGeometryRasterizerAndDepthState(GraphicsPSOInit, View, LightBounds);

				if (SimpleLight.Exponent == 0)
				{
					// inverse squared
					SetShaderTemplLightingSimple<false, true, true>(RHICmdList, GraphicsPSOInit, View, VertexShader, SimpleLight, SimpleLightPerViewData);
				}
				else
				{
					// light's exponent, not inverse squared
					SetShaderTemplLightingSimple<false, true, false>(RHICmdList, GraphicsPSOInit, View, VertexShader, SimpleLight, SimpleLightPerViewData);
				}

				VertexShader->SetSimpleLightParameters(RHICmdList, View, LightBounds);

				// Apply the point or spot light with some approximately bounding geometry,
				// So we can get speedups from depth testing and not processing pixels outside of the light's influence.
				StencilingGeometry::DrawSphere(RHICmdList);
			}
		}
	});
}
