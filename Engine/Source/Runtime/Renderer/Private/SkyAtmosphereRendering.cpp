// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SkyAtmosphereRendering.cpp
=============================================================================*/

#include "SkyAtmosphereRendering.h"
#include "CanvasTypes.h"
#include "Components/SkyAtmosphereComponent.h"
#include "DeferredShadingRenderer.h"
#include "Engine/MapBuildDataRegistry.h"
#include "LightSceneInfo.h"
#include "PixelShaderUtils.h"
#include "RenderTargetTemp.h"
#include "ScenePrivate.h"
#include "SceneRenderTargetParameters.h"

//#pragma optimize( "", off )


// The runtime ON/OFF toggle
static TAutoConsoleVariable<int32> CVarSkyAtmosphere(
	TEXT("r.SkyAtmosphere"), 1,
	TEXT("SkyAtmosphere components are rendered when this is not 0, otherwise ignored.\n"),
	ECVF_RenderThreadSafe);

// The project settings (disable runtime and shader code)
static TAutoConsoleVariable<int32> CVarSupportSkyAtmosphere(
	TEXT("r.SupportSkyAtmosphere"),
	1,
	TEXT("Enables SkyAtmosphere rendering and shader code."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

////////////////////////////////////////////////////////////////////////// Regular sky 

static TAutoConsoleVariable<float> CVarSkyAtmosphereSampleCountMin(
	TEXT("r.SkyAtmosphere.SampleCountMin"), 2.0f,
	TEXT("The minimum sample count used to compute sky/atmosphere scattering and transmittance.\n")
	TEXT("The minimal value will be clamped to 1.\n"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereSampleCountMax(
	TEXT("r.SkyAtmosphere.SampleCountMax"), 16.0f,
	TEXT("The maximum sample count used to compute sky/atmosphere scattering and transmittance.\n")
	TEXT("The minimal value will be clamped to r.SkyAtmosphere.SampleCountMin + 1.\n"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereDistanceToSampleCountMax(
	TEXT("r.SkyAtmosphere.DistanceToSampleCountMax"), 150.0f,
	TEXT("The distance in kilometer after which at which SampleCountMax samples will be used to ray march the atmosphere."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

////////////////////////////////////////////////////////////////////////// Fast sky

static TAutoConsoleVariable<int32> CVarSkyAtmosphereFastSkyLUT(
	TEXT("r.SkyAtmosphere.FastSkyLUT"), 0,
	TEXT("When enabled, a look up texture is used to render the sky.\n")
	TEXT("It is faster but can result in visual artefacts if there are some high frequency details\n")
	TEXT("in the sky such as earth shadow or scattering lob."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereFastSkyLUTSampleCountMin(
	TEXT("r.SkyAtmosphere.FastSkyLUT.SampleCountMin"), 4.0f,
	TEXT("Fast sky minimum sample count used to compute sky/atmosphere scattering and transmittance.\n")
	TEXT("The minimal value will be clamped to 1.\n"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereFastSkyLUTSampleCountMax(
	TEXT("r.SkyAtmosphere.FastSkyLUT.SampleCountMax"), 32.0f,
	TEXT("Fast sky maximum sample count used to compute sky/atmosphere scattering and transmittance.\n")
	TEXT("The minimal value will be clamped to r.SkyAtmosphere.FastSkyLUT.SampleCountMin + 1.\n"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereFastSkyLUTDistanceToSampleCountMax(
	TEXT("r.SkyAtmosphere.FastSkyLUT.DistanceToSampleCountMax"), 150.0f,
	TEXT("Fast sky distance in kilometer after which at which SampleCountMax samples will be used to ray march the atmosphere."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereFastSkyLUTWidth(
	TEXT("r.SkyAtmosphere.FastSkyLUT.Width"), 192,
	TEXT(""),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereFastSkyLUTHeight(
	TEXT("r.SkyAtmosphere.FastSkyLUT.Height"), 104,
	TEXT(""),
	ECVF_RenderThreadSafe | ECVF_Scalability);

////////////////////////////////////////////////////////////////////////// Aerial perspective

static TAutoConsoleVariable<float> CVarSkyAtmosphereAerialPerspectiveStartDepth(
	TEXT("r.SkyAtmosphere.AerialPerspective.StartDepth"), 0.1f,
	TEXT("The distance at which we start evaluate the aerial pespective. Default: 100 meters."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarSkyAtmosphereAerialPerspectiveDepthTest(
	TEXT("r.SkyAtmosphere.AerialPerspective.DepthTest"), 1,
	TEXT("When enabled, a depth test will be used to not write pixel closer to the camera than StartDepth, effectively improving performance."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

////////////////////////////////////////////////////////////////////////// Aerial perspective LUT

static TAutoConsoleVariable<float> CVarSkyAtmosphereAerialPerspectiveLUTDepthResolution(
	TEXT("r.SkyAtmosphere.AerialPerspectiveLUT.DepthResolution"), 16.0f,
	TEXT("The number of depth slice to use for the aerial perspective volume texture."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereAerialPerspectiveLUTDepth(
	TEXT("r.SkyAtmosphere.AerialPerspectiveLUT.Depth"), 96.0f,
	TEXT("The length of the LUT in kilometers (default = 96km to get nice cloud/atmosphere interactions in the distance for default sky). Further than this distance, the last slice is used."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereAerialPerspectiveLUTSampleCountPerSlice(
	TEXT("r.SkyAtmosphere.AerialPerspectiveLUT.SampleCountPerSlice"), 2.0f,
	TEXT("The sample count used per slice to evaluate aerial perspective\n")
	TEXT("scattering and transmittance in camera frustum space froxel.\n"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereAerialPerspectiveLUTWidth(
	TEXT("r.SkyAtmosphere.AerialPerspectiveLUT.Width"), 32,
	TEXT(""),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarSkyAtmosphereAerialPerspectiveApplyOnOpaque(
	TEXT("r.SkyAtmosphere.AerialPerspectiveLUT.FastApplyOnOpaque"), 0,
	TEXT("When enabled, the low resolution camera frustum/froxel volume containing atmospheric fog\n")
	TEXT(", usually used for fog on translucent surface, is used to render fog on opaque.\n")
	TEXT("It is faster but can result in visual artefacts if there are some high frequency details\n")
	TEXT("such as earth shadow or scattering lob."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

////////////////////////////////////////////////////////////////////////// Transmittance LUT

static TAutoConsoleVariable<float> CVarSkyAtmosphereTranstmittanceLUTSampleCount(
	TEXT("r.SkyAtmosphere.TransmittanceLUT.SampleCount"), 10.0f,
	TEXT("The sample count used to evaluate transmittance."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarSkyAtmosphereTransmittanceLUTUseSmallFormat(
	TEXT("r.SkyAtmosphere.TransmittanceLUT.UseSmallFormat"), 0,
	TEXT("If true, the transmittance LUT will use a small R8BG8B8A8 format to store data at lower quality."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereTransmittanceLUTWidth(
	TEXT("r.SkyAtmosphere.TransmittanceLUT.Width"), 256,
	TEXT(""),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereTransmittanceLUTHeight(
	TEXT("r.SkyAtmosphere.TransmittanceLUT.Height"), 64,
	TEXT(""),
	ECVF_RenderThreadSafe | ECVF_Scalability);

////////////////////////////////////////////////////////////////////////// Multi-scattering LUT

static TAutoConsoleVariable<float> CVarSkyAtmosphereMultiScatteringLUTSampleCount(
	TEXT("r.SkyAtmosphere.MultiScatteringLUT.SampleCount"), 15.0f,
	TEXT("The sample count used to evaluate multi-scattering.\n"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarSkyAtmosphereMultiScatteringLUTUseSmallFormat(
	TEXT("r.SkyAtmosphere.MultiScatteringLUT.UseSmallFormat"), 0,
	TEXT("If true, the multi-scattering LUT will use a small R8BG8B8A8 format to store data at lower quality."),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereMultiScatteringLUTWidth(
	TEXT("r.SkyAtmosphere.MultiScatteringLUT.Width"), 32,
	TEXT(""),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereMultiScatteringLUTHeight(
	TEXT("r.SkyAtmosphere.MultiScatteringLUT.Height"), 32,
	TEXT(""),
	ECVF_RenderThreadSafe | ECVF_Scalability);

////////////////////////////////////////////////////////////////////////// Distant Sky Light LUT

static TAutoConsoleVariable<int32> CVarSkyAtmosphereDistantSkyLightLUT(
	TEXT("r.SkyAtmosphere.DistantSkyLightLUT"), 1,
	TEXT("Enable the generation the sky ambient lighting value.\n"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

static TAutoConsoleVariable<float> CVarSkyAtmosphereDistantSkyLightLUTAltitude(
	TEXT("r.SkyAtmosphere.DistantSkyLightLUT.Altitude"), 6.0f,
	TEXT("The altitude at which the sky samples are taken to integrate the sky lighting. Default to 6km, typicaly cirrus clouds altitude.\n"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

////////////////////////////////////////////////////////////////////////// Debug / Visualisation

TAutoConsoleVariable<int32> CVarSkyAtmosphereVisualize(
	TEXT("r.SkyAtmosphere.Visualize"),
	0,
	TEXT("DRAW DEBUG"),
	ECVF_RenderThreadSafe);


// Extra internal constants shared between all passes.
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FSkyAtmosphereInternalCommonParameters, )
	SHADER_PARAMETER(float, SampleCountMin)
	SHADER_PARAMETER(float, SampleCountMax)
	SHADER_PARAMETER(float, DistanceToSampleCountMaxInv)

	SHADER_PARAMETER(float, FastSkySampleCountMin)
	SHADER_PARAMETER(float, FastSkySampleCountMax)
	SHADER_PARAMETER(float, FastSkyDistanceToSampleCountMaxInv)

	SHADER_PARAMETER(FVector4, CameraAerialPerspectiveVolumeSizeAndInvSize)
	SHADER_PARAMETER(float, CameraAerialPerspectiveVolumeDepthResolution)		// Also on View UB
	SHADER_PARAMETER(float, CameraAerialPerspectiveVolumeDepthResolutionInv)	// Also on View UB
	SHADER_PARAMETER(float, CameraAerialPerspectiveVolumeDepthSliceLength)		// Also on View UB
	SHADER_PARAMETER(float, CameraAerialPerspectiveVolumeDepthSliceLengthInv)	// Also on View UB
	SHADER_PARAMETER(float, CameraAerialPerspectiveSampleCountPerSlice)
	SHADER_PARAMETER(float, AerialPerspectiveStartDepth)

	SHADER_PARAMETER(FVector4, TransmittanceLutSizeAndInvSize)
	SHADER_PARAMETER(FVector4, MultiScatteredLuminanceLutSizeAndInvSize)
	SHADER_PARAMETER(FVector4, SkyViewLutSizeAndInvSize)						// Also on View UB

	SHADER_PARAMETER(float, TransmittanceSampleCount)
	SHADER_PARAMETER(float, MultiScatteringSampleCount)
	SHADER_PARAMETER(float, AerialPespectiveViewDistanceScale)

	SHADER_PARAMETER(FVector, SkyLuminanceFactor)

END_GLOBAL_SHADER_PARAMETER_STRUCT()

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FAtmosphereUniformShaderParameters, "Atmosphere");
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FSkyAtmosphereInternalCommonParameters, "SkyAtmosphere");


#define GET_VALID_DATA_FROM_CVAR \
	auto ValidateLUTResolution = [](int32 Value) \
	{ \
		return Value < 4 ? 4 : Value; \
	}; \
	int32 TransmittanceLutWidth = ValidateLUTResolution(CVarSkyAtmosphereTransmittanceLUTWidth.GetValueOnRenderThread()); \
	int32 TransmittanceLutHeight = ValidateLUTResolution(CVarSkyAtmosphereTransmittanceLUTHeight.GetValueOnRenderThread()); \
	int32 MultiScatteredLuminanceLutWidth = ValidateLUTResolution(CVarSkyAtmosphereMultiScatteringLUTWidth.GetValueOnRenderThread()); \
	int32 MultiScatteredLuminanceLutHeight = ValidateLUTResolution(CVarSkyAtmosphereMultiScatteringLUTHeight.GetValueOnRenderThread()); \
	int32 SkyViewLutWidth = ValidateLUTResolution(CVarSkyAtmosphereFastSkyLUTWidth.GetValueOnRenderThread()); \
	int32 SkyViewLutHeight = ValidateLUTResolution(CVarSkyAtmosphereFastSkyLUTHeight.GetValueOnRenderThread()); \
	int32 CameraAerialPerspectiveVolumeScreenResolution = ValidateLUTResolution(CVarSkyAtmosphereAerialPerspectiveLUTWidth.GetValueOnRenderThread()); \
	int32 CameraAerialPerspectiveVolumeDepthResolution = ValidateLUTResolution(CVarSkyAtmosphereAerialPerspectiveLUTDepthResolution.GetValueOnRenderThread()); \
	float AerialPerspectiveStartDepth = CVarSkyAtmosphereAerialPerspectiveStartDepth.GetValueOnRenderThread(); \
	AerialPerspectiveStartDepth = AerialPerspectiveStartDepth < 0.0f ? 0.0f : AerialPerspectiveStartDepth; \
	float CameraAerialPerspectiveVolumeDepth = CVarSkyAtmosphereAerialPerspectiveLUTDepth.GetValueOnRenderThread(); \
	CameraAerialPerspectiveVolumeDepth = CameraAerialPerspectiveVolumeDepth < 1.0f ? 1.0f : CameraAerialPerspectiveVolumeDepth;	/* 1 kilometer minimum */ \
	float CameraAerialPerspectiveVolumeDepthSliceLength = CameraAerialPerspectiveVolumeDepth / CameraAerialPerspectiveVolumeDepthResolution;


static bool ShouldPipelineCompileSkyAtmosphereShader(EShaderPlatform ShaderPlatform)
{
	// Requires SM5 or ES3_1 (GL/Vulkan) for compute shaders and volume textures support.
	return RHISupportsComputeShaders(ShaderPlatform);
}

bool ShouldRenderSkyAtmosphere(const FSkyAtmosphereRenderSceneInfo* SkyAtmosphere, EShaderPlatform ShaderPlatform)
{
	const bool ShadersCompiled = ShouldPipelineCompileSkyAtmosphereShader(ShaderPlatform);
	return FReadOnlyCVARCache::Get().bSupportSkyAtmosphere && SkyAtmosphere && ShadersCompiled && CVarSkyAtmosphere.GetValueOnRenderThread() > 0;
	// TODO: Add new or reuse EngineShowFlags.AtmosphericFog? ALso take into account EngineShowFlags.Fog as previously?
}

void SetupSkyAtmosphereViewSharedUniformShaderParameters(const class FViewInfo& View, FSkyAtmosphereViewSharedUniformShaderParameters& OutParameters)
{
	GET_VALID_DATA_FROM_CVAR;

	FRHITexture* SkyAtmosphereCameraAerialPerspectiveVolume = nullptr;
	if (View.SkyAtmosphereCameraAerialPerspectiveVolume)
	{
		SkyAtmosphereCameraAerialPerspectiveVolume = View.SkyAtmosphereCameraAerialPerspectiveVolume->GetRenderTargetItem().ShaderResourceTexture;
	}

	OutParameters.ApplyCameraAerialPerspectiveVolume = View.SkyAtmosphereCameraAerialPerspectiveVolume == nullptr ? 0.0f : 1.0f;
	OutParameters.CameraAerialPerspectiveVolumeDepthResolution = float(CameraAerialPerspectiveVolumeDepthResolution);
	OutParameters.CameraAerialPerspectiveVolumeDepthResolutionInv = 1.0f / OutParameters.CameraAerialPerspectiveVolumeDepthResolution;
	OutParameters.CameraAerialPerspectiveVolumeDepthSliceLength = CameraAerialPerspectiveVolumeDepthSliceLength;
	OutParameters.CameraAerialPerspectiveVolumeDepthSliceLengthInv = 1.0f / OutParameters.CameraAerialPerspectiveVolumeDepthSliceLength;

	OutParameters.AerialPerspectiveStartDepth = AerialPerspectiveStartDepth;

	SetBlackAlpha13DIfNull(SkyAtmosphereCameraAerialPerspectiveVolume); // Needs to be after we set ApplyCameraAerialPerspectiveVolume
}

static void CopyAtmosphereSetupToUniformShaderParameters(FAtmosphereUniformShaderParameters& out, const FAtmosphereSetup& Atmosphere)
{
#define COPYMACRO(MemberName) out.MemberName = Atmosphere.MemberName 
	COPYMACRO(MultiScatteringFactor);
	COPYMACRO(BottomRadius);
	COPYMACRO(TopRadius);
	COPYMACRO(RayleighDensityExpScale);
	COPYMACRO(RayleighScattering);
	COPYMACRO(MieScattering);
	COPYMACRO(MieDensityExpScale);
	COPYMACRO(MieExtinction);
	COPYMACRO(MiePhaseG);
	COPYMACRO(MieAbsorption);
	COPYMACRO(AbsorptionDensity0LayerWidth);
	COPYMACRO(AbsorptionDensity0ConstantTerm);
	COPYMACRO(AbsorptionDensity0LinearTerm);
	COPYMACRO(AbsorptionDensity1ConstantTerm);
	COPYMACRO(AbsorptionDensity1LinearTerm);
	COPYMACRO(AbsorptionExtinction);
	COPYMACRO(GroundAlbedo);
#undef COPYMACRO
}

static FLinearColor GetLightDiskLuminance(FLightSceneInfo& Light, FLinearColor LightIlluminance)
{
	const float SunSolidAngle = 2.0f * PI * (1.0f - FMath::Cos(Light.Proxy->GetSunLightHalfApexAngleRadian())); // Solid angle from aperture https://en.wikipedia.org/wiki/Solid_angle 
	return LightIlluminance / SunSolidAngle; // approximation
}


/*=============================================================================
	FSkyAtmosphereRenderSceneInfo implementation.
=============================================================================*/



FSkyAtmosphereRenderSceneInfo::FSkyAtmosphereRenderSceneInfo(const USkyAtmosphereComponent* InComponent)
	: bStaticLightingBuilt(false)
	, AtmosphereSetup(*InComponent)
	, TransmittanceAtZenith(AtmosphereSetup.GetTransmittanceAtGroundLevel(FVector(0.0f, 0.0f, 1.0f)))
{
	SkyLuminanceFactor = InComponent->SkyLuminanceFactor;
	AerialPespectiveViewDistanceScale = InComponent->AerialPespectiveViewDistanceScale;

	// Create a multiframe uniform buffer. A render command is used because FSkyAtmosphereRenderSceneInfo ctor is called on the Game thread.
	TUniformBufferRef<FAtmosphereUniformShaderParameters>* AtmosphereUniformBufferPtr = &AtmosphereUniformBuffer;
	FAtmosphereUniformShaderParameters* AtmosphereUniformShaderParametersPtr = &AtmosphereUniformShaderParameters;
	CopyAtmosphereSetupToUniformShaderParameters(AtmosphereUniformShaderParameters, AtmosphereSetup);
	ENQUEUE_RENDER_COMMAND(FCreateUniformBuffer)(
		[AtmosphereUniformBufferPtr, AtmosphereUniformShaderParametersPtr](FRHICommandListImmediate& RHICmdList)
	{
		*AtmosphereUniformBufferPtr = TUniformBufferRef<FAtmosphereUniformShaderParameters>::CreateUniformBufferImmediate(*AtmosphereUniformShaderParametersPtr, UniformBuffer_MultiFrame);
	});
}

FSkyAtmosphereRenderSceneInfo::~FSkyAtmosphereRenderSceneInfo()
{
	// Destructor is called on the render thread so safe to release here
	AtmosphereUniformBuffer->Release();
	//TransmittanceLutTexture->Release(); This is automatically tracked by FRenderTargetPool and RefCount
}

void FSkyAtmosphereRenderSceneInfo::PrepareSunLightProxy(FLightSceneInfo& SunLight) const
{
	// See explanation in https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/s2016-pbs-frostbite-sky-clouds-new.pdf page 26
	const bool bAtmosphereAffectsSunIlluminance = true;
	FLinearColor TransmittanceTowardSun = bAtmosphereAffectsSunIlluminance ? AtmosphereSetup.GetTransmittanceAtGroundLevel(-SunLight.Proxy->GetDirection()) : FLinearColor(FLinearColor::White);
	FLinearColor TransmittanceAtZenithFinal = bAtmosphereAffectsSunIlluminance ? TransmittanceAtZenith : FLinearColor(FLinearColor::White);

	FLinearColor SunZenithIlluminance = SunLight.Proxy->GetColor();
	FLinearColor SunOuterSpaceIlluminance = SunZenithIlluminance / TransmittanceAtZenithFinal;
	FLinearColor SunDiskOuterSpaceLuminance = GetLightDiskLuminance(SunLight, SunOuterSpaceIlluminance);

	SunLight.Proxy->SetAtmosphereRelatedProperties(TransmittanceTowardSun / TransmittanceAtZenithFinal, SunDiskOuterSpaceLuminance);
}

FTextureRHIRef FSkyAtmosphereRenderSceneInfo::GetDistantSkyLightLutTextureRHI()
{
	if (CVarSkyAtmosphereDistantSkyLightLUT.GetValueOnRenderThread() > 0)
	{
		return DistantSkyLightLutTexture->GetRenderTargetItem().ShaderResourceTexture;
	}
	return GBlackTexture->TextureRHI;
}



/*=============================================================================
	FScene functions
=============================================================================*/

void FScene::AddSkyAtmosphere(const USkyAtmosphereComponent* SkyAtmosphereComponent, bool bStaticLightingBuilt)
{
	check(SkyAtmosphereComponent);

	FSkyAtmosphereRenderSceneInfo* SceneInfo = new FSkyAtmosphereRenderSceneInfo(SkyAtmosphereComponent);
	FScene* Scene = this;
	ENQUEUE_RENDER_COMMAND(FAddSkyAtmosphereCommand)(
		[Scene, SceneInfo, bStaticLightingBuilt](FRHICommandListImmediate& RHICmdList)
		{
			if (Scene->SkyAtmosphere)
			{
				delete Scene->SkyAtmosphere;
			}
			Scene->SkyAtmosphere = SceneInfo;
			Scene->SkyAtmosphere->bStaticLightingBuilt = bStaticLightingBuilt;
			if (!Scene->SkyAtmosphere->bStaticLightingBuilt)
			{
				FPlatformAtomics::InterlockedIncrement(&Scene->NumUncachedStaticLightingInteractions);
			}
		} );
}

void FScene::RemoveSkyAtmosphere(const USkyAtmosphereComponent* SkyAtmosphereComponent)
{
	FScene* Scene = this;
	check(SkyAtmosphereComponent);

	ENQUEUE_RENDER_COMMAND(FRemoveSkyAtmosphereCommand)(
		[Scene](FRHICommandListImmediate& RHICmdList)
		{
			if (Scene->SkyAtmosphere)
			{
				if (Scene->SkyAtmosphere && !Scene->SkyAtmosphere->bStaticLightingBuilt)
				{
					FPlatformAtomics::InterlockedDecrement(&Scene->NumUncachedStaticLightingInteractions);
				}
				delete Scene->SkyAtmosphere;
				Scene->SkyAtmosphere = NULL;
			}
		} );
}

void FScene::ResetAtmosphereLightsProperties()
{
	// Also rest the current atmospheric light to default atmosphere
	for (int32 LightIndex = 0; LightIndex < NUM_ATMOSPHERE_LIGHTS; ++LightIndex)
	{
		FLightSceneInfo* Light = AtmosphereLights[LightIndex];
		if (Light)
		{
			FLinearColor LightZenithIlluminance = Light->Proxy->GetColor();
			Light->Proxy->SetAtmosphereRelatedProperties(FLinearColor::White, GetLightDiskLuminance(*Light, LightZenithIlluminance));
		}
	}
}



/*=============================================================================
	Sky/Atmosphere rendering functions
=============================================================================*/



namespace
{

class FSkyPermutationMultiScatteringApprox : SHADER_PERMUTATION_BOOL("MULTISCATTERING_APPROX_ENABLED");
class FFastSky : SHADER_PERMUTATION_BOOL("FASTSKY_ENABLED");
class FFastAerialPespective : SHADER_PERMUTATION_BOOL("FASTAERIALPERSPECTIVE_ENABLED");
class FSourceDiskEnabled : SHADER_PERMUTATION_BOOL("SOURCE_DISK_ENABLED");
class FSecondAtmosphereLight : SHADER_PERMUTATION_BOOL("SECOND_ATMOSPHERE_LIGHT_ENABLED");
class FRenderSky : SHADER_PERMUTATION_BOOL("RENDERSKY_ENABLED");

}

//////////////////////////////////////////////////////////////////////////

class FRenderSkyAtmosphereVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRenderSkyAtmosphereVS);
	SHADER_USE_PARAMETER_STRUCT(FRenderSkyAtmosphereVS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(float, StartDepthZ)
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldPipelineCompileSkyAtmosphereShader(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};
IMPLEMENT_GLOBAL_SHADER(FRenderSkyAtmosphereVS, "/Engine/Private/SkyAtmosphere.usf", "SkyAtmosphereVS", SF_Vertex);

class FRenderSkyAtmospherePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRenderSkyAtmospherePS);
	SHADER_USE_PARAMETER_STRUCT(FRenderSkyAtmospherePS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<FSkyPermutationMultiScatteringApprox, FFastSky, FFastAerialPespective, FSourceDiskEnabled, FSecondAtmosphereLight, FRenderSky>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FAtmosphereUniformShaderParameters, Atmosphere)
		SHADER_PARAMETER_STRUCT_REF(FSkyAtmosphereInternalCommonParameters, SkyAtmosphere)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		RENDER_TARGET_BINDING_SLOTS()
		SHADER_PARAMETER_STRUCT_REF(FSceneTexturesUniformParameters, SceneTextures)
		SHADER_PARAMETER_TEXTURE(Texture2D<float3>, TransmittanceLutTexture)
		SHADER_PARAMETER_TEXTURE(Texture2D<float3>, MultiScatteredLuminanceLutTexture)
		SHADER_PARAMETER_TEXTURE(Texture2D<float3>, SkyViewLutTexture)
		SHADER_PARAMETER_TEXTURE(Texture3D<float4>, CameraAerialPerspectiveVolumeTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmittanceLutTextureSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, MultiScatteredLuminanceLutTextureSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, SkyViewLutTextureSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, CameraAerialPerspectiveVolumeTextureSampler)
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		// If not rendering the sky, ignore the fastsky and sundisk permutations
		if (PermutationVector.Get<FRenderSky>() == false)
		{
			PermutationVector.Set<FFastSky>(false);
			PermutationVector.Set<FSourceDiskEnabled>(false);
		}

		// See comment below
		if (PermutationVector.Get<FFastAerialPespective>())
		{
			PermutationVector.Set<FSkyPermutationMultiScatteringApprox>(false);
		}

		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		// If not rendering the sky, ignore the FFastSky and FSourceDiskEnabled permutations
		if (PermutationVector.Get<FRenderSky>() == false && (
			PermutationVector.Get<FFastSky>() ||
			PermutationVector.Get<FSourceDiskEnabled>() ))
		{
			return false;
		}

		// When rendering using FFastAerialPespective, we can ignore FSkyPermutationMultiScatteringApprox because it has been all baked in already:
		//	- Aerial perspective will contain it using the camera volume
		//	- Sky will contain it using SkyView LUT or camera volume. 
		// This is possible because we do not allow FFastAerialPespective to be used with ray-marched sky. It must be used with the FFastSky SkyView LUT oir nothing.
		if (PermutationVector.Get<FFastAerialPespective>() && PermutationVector.Get<FSkyPermutationMultiScatteringApprox>())
		{
			return false;
		}

		return ShouldPipelineCompileSkyAtmosphereShader(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("PER_PIXEL_NOISE"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FRenderSkyAtmospherePS, "/Engine/Private/SkyAtmosphere.usf", "RenderSkyAtmosphereRayMarchingPS", SF_Pixel);

//////////////////////////////////////////////////////////////////////////

class FRenderTransmittanceLutCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRenderTransmittanceLutCS);
	SHADER_USE_PARAMETER_STRUCT(FRenderTransmittanceLutCS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

public:
	const static uint32 GroupSize = 8;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FAtmosphereUniformShaderParameters, Atmosphere)
		SHADER_PARAMETER_STRUCT_REF(FSkyAtmosphereInternalCommonParameters, SkyAtmosphere)
		SHADER_PARAMETER_UAV(RWTexture2D<float3>, TransmittanceLutUAV)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldPipelineCompileSkyAtmosphereShader(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GroupSize);
		OutEnvironment.SetDefine(TEXT("WHITE_TRANSMITTANCE"),1); // Workaround for some compiler not culling enough unused code (e.g. when computing TransmittanceLUT, Transmittance texture is still requested but we are computing it) 
		OutEnvironment.SetDefine(TEXT("TRANSMITTANCE_PASS"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FRenderTransmittanceLutCS, "/Engine/Private/SkyAtmosphere.usf", "RenderTransmittanceLutCS", SF_Compute);

//////////////////////////////////////////////////////////////////////////

class FRenderMultiScatteredLuminanceLutCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRenderMultiScatteredLuminanceLutCS);
	SHADER_USE_PARAMETER_STRUCT(FRenderMultiScatteredLuminanceLutCS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

public:
	const static uint32 GroupSize = 8;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FAtmosphereUniformShaderParameters, Atmosphere)
		SHADER_PARAMETER_STRUCT_REF(FSkyAtmosphereInternalCommonParameters, SkyAtmosphere)
		SHADER_PARAMETER_UAV(RWTexture2D<float3>, MultiScatteredLuminanceLutUAV)
		SHADER_PARAMETER_TEXTURE(Texture2D<float3>, TransmittanceLutTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmittanceLutTextureSampler)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldPipelineCompileSkyAtmosphereShader(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GroupSize);
		OutEnvironment.SetDefine(TEXT("MULTISCATT_PASS"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FRenderMultiScatteredLuminanceLutCS, "/Engine/Private/SkyAtmosphere.usf", "RenderMultiScatteredLuminanceLutCS", SF_Compute);

//////////////////////////////////////////////////////////////////////////

class FRenderDistantSkyLightLutCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRenderDistantSkyLightLutCS);
	SHADER_USE_PARAMETER_STRUCT(FRenderDistantSkyLightLutCS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<FSkyPermutationMultiScatteringApprox, FSecondAtmosphereLight>;

public:
	const static uint32 GroupSize = 8;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FAtmosphereUniformShaderParameters, Atmosphere)
		SHADER_PARAMETER_STRUCT_REF(FSkyAtmosphereInternalCommonParameters, SkyAtmosphere)
		SHADER_PARAMETER_UAV(RWTexture2D<float3>, DistantSkyLightLutUAV)
		SHADER_PARAMETER_TEXTURE(Texture2D<float3>, TransmittanceLutTexture)
		SHADER_PARAMETER_TEXTURE(Texture2D<float3>, MultiScatteredLuminanceLutTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmittanceLutTextureSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, MultiScatteredLuminanceLutTextureSampler)
		SHADER_PARAMETER_SRV(Buffer<float4>, UniformSphereSamplesBuffer)
		SHADER_PARAMETER(FVector4, AtmosphereLightDirection0)
		SHADER_PARAMETER(FVector4, AtmosphereLightDirection1)
		SHADER_PARAMETER(FLinearColor, AtmosphereLightColor0)
		SHADER_PARAMETER(FLinearColor, AtmosphereLightColor1)
		SHADER_PARAMETER(float, DistantSkyLightSampleAltitude)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldPipelineCompileSkyAtmosphereShader(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GroupSize);
		OutEnvironment.SetDefine(TEXT("SKYLIGHT_PASS"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FRenderDistantSkyLightLutCS, "/Engine/Private/SkyAtmosphere.usf", "RenderDistantSkyLightLutCS", SF_Compute);

//////////////////////////////////////////////////////////////////////////

class FRenderSkyViewLutCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRenderSkyViewLutCS);
	SHADER_USE_PARAMETER_STRUCT(FRenderSkyViewLutCS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<FSkyPermutationMultiScatteringApprox, FSourceDiskEnabled, FSecondAtmosphereLight>;

public:
	const static uint32 GroupSize = 8;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FAtmosphereUniformShaderParameters, Atmosphere)
		SHADER_PARAMETER_STRUCT_REF(FSkyAtmosphereInternalCommonParameters, SkyAtmosphere)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_UAV(RWTexture2D<float3>, SkyViewLutUAV)
		SHADER_PARAMETER_TEXTURE(Texture2D<float3>, TransmittanceLutTexture)
		SHADER_PARAMETER_TEXTURE(Texture2D<float3>, MultiScatteredLuminanceLutTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmittanceLutTextureSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, MultiScatteredLuminanceLutTextureSampler)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldPipelineCompileSkyAtmosphereShader(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GroupSize);
	}
};
IMPLEMENT_GLOBAL_SHADER(FRenderSkyViewLutCS, "/Engine/Private/SkyAtmosphere.usf", "RenderSkyViewLutCS", SF_Compute);

//////////////////////////////////////////////////////////////////////////

class FRenderCameraAerialPerspectiveVolumeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRenderCameraAerialPerspectiveVolumeCS);
	SHADER_USE_PARAMETER_STRUCT(FRenderCameraAerialPerspectiveVolumeCS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<FSkyPermutationMultiScatteringApprox, FSecondAtmosphereLight>;

public:
	const static uint32 GroupSize = 4;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FAtmosphereUniformShaderParameters, Atmosphere)
		SHADER_PARAMETER_STRUCT_REF(FSkyAtmosphereInternalCommonParameters, SkyAtmosphere)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_UAV(RWTexture3D<float4>, CameraAerialPerspectiveVolumeUAV)
		SHADER_PARAMETER_TEXTURE(Texture2D<float3>, TransmittanceLutTexture)
		SHADER_PARAMETER_TEXTURE(Texture2D<float3>, MultiScatteredLuminanceLutTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmittanceLutTextureSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, MultiScatteredLuminanceLutTextureSampler)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldPipelineCompileSkyAtmosphereShader(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GroupSize);
	}
};
IMPLEMENT_GLOBAL_SHADER(FRenderCameraAerialPerspectiveVolumeCS, "/Engine/Private/SkyAtmosphere.usf", "RenderCameraAerialPerspectiveVolumeCS", SF_Compute);

//////////////////////////////////////////////////////////////////////////

class FRenderDebugSkyAtmospherePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRenderDebugSkyAtmospherePS);
	SHADER_USE_PARAMETER_STRUCT(FRenderDebugSkyAtmospherePS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<FSkyPermutationMultiScatteringApprox>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FAtmosphereUniformShaderParameters, Atmosphere)
		SHADER_PARAMETER_STRUCT_REF(FSkyAtmosphereInternalCommonParameters, SkyAtmosphere)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		RENDER_TARGET_BINDING_SLOTS()
		SHADER_PARAMETER_TEXTURE(Texture2D<float3>, TransmittanceLutTexture)
		SHADER_PARAMETER_TEXTURE(Texture2D<float3>, MultiScatteredLuminanceLutTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, TransmittanceLutTextureSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, MultiScatteredLuminanceLutTextureSampler)
		SHADER_PARAMETER(float, ViewPortWidth)
		SHADER_PARAMETER(float, ViewPortHeight)
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		// TODO: Exclude when shipping.
		return ShouldPipelineCompileSkyAtmosphereShader(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};
IMPLEMENT_GLOBAL_SHADER(FRenderDebugSkyAtmospherePS, "/Engine/Private/SkyAtmosphere.usf", "RenderSkyAtmosphereDebugPS", SF_Pixel);



/*=============================================================================
	FUniformSphereSamplesBuffer
=============================================================================*/

class FUniformSphereSamplesBuffer : public FRenderResource
{
public:
	FReadBuffer UniformSphereSamplesBuffer;

	virtual void InitRHI() override
	{
		const float GroupSize = float(FRenderDistantSkyLightLutCS::GroupSize);
		const float GroupSizeInv = 1.0f / GroupSize;

		UniformSphereSamplesBuffer.Initialize(sizeof(FVector4), FRenderDistantSkyLightLutCS::GroupSize * FRenderDistantSkyLightLutCS::GroupSize, EPixelFormat::PF_A32B32G32R32F, BUF_Static);
		FVector4* Dest = (FVector4*)RHILockVertexBuffer(UniformSphereSamplesBuffer.Buffer, 0, sizeof(FVector4)*FRenderDistantSkyLightLutCS::GroupSize*FRenderDistantSkyLightLutCS::GroupSize, RLM_WriteOnly);

		FMath::SRandInit(0xDE4DC0DE);
		for (int32 i = 0; i < FRenderDistantSkyLightLutCS::GroupSize; ++i)
		{
			for (int32 j = 0; j < FRenderDistantSkyLightLutCS::GroupSize; ++j)
			{
				const float u0 = (float(i) + FMath::SRand()) * GroupSizeInv;
				const float u1 = (float(j) + FMath::SRand()) * GroupSizeInv;

				const float a = 1.0f - 2.0f * u0;
				const float b = FMath::Sqrt(1.0f - a*a);
				const float phi = 2 * PI * u1;

				int32 idx = j * FRenderDistantSkyLightLutCS::GroupSize + i;
				Dest[idx].X = b * FMath::Cos(phi);
				Dest[idx].Y = b * FMath::Sin(phi);
				Dest[idx].Z = a;
				Dest[idx].W = 0.0f;
			}
		}

		RHIUnlockVertexBuffer(UniformSphereSamplesBuffer.Buffer);
	}

	virtual void ReleaseRHI()
	{
		UniformSphereSamplesBuffer.Release();
	}
};
TGlobalResource<FUniformSphereSamplesBuffer> GUniformSphereSamplesBuffer;



/*=============================================================================
	FSceneRenderer functions
=============================================================================*/

void FSceneRenderer::InitSkyAtmosphereForViews(FRHICommandListImmediate& RHICmdList)
{
	if (Scene)
	{
		GET_VALID_DATA_FROM_CVAR;

		check(ShouldRenderSkyAtmosphere(Scene->GetSkyAtmosphereSceneInfo(), Scene->GetShaderPlatform())); // This should not be called if we should not render SkyAtmosphere
		FPooledRenderTargetDesc Desc;
		check(Scene->GetSkyAtmosphereSceneInfo());
		FSkyAtmosphereRenderSceneInfo& SkyInfo = *Scene->GetSkyAtmosphereSceneInfo();

		EPixelFormat TextureLUTFormat = PF_FloatRGB;
		EPixelFormat TextureLUTSmallFormat = PF_R8G8B8A8;
		if (Scene->GetFeatureLevel() <= ERHIFeatureLevel::ES3_1)
		{
			// OpenGL ES3.1 does not support storing into 3-component images
			// TODO: check if need this for Metal, Vulkan
			TextureLUTFormat = PF_FloatRGBA;
		}  

		//
		// Initialise per scene/atmosphere resources
		//
		const bool TranstmittanceLUTUseSmallFormat = CVarSkyAtmosphereTransmittanceLUTUseSmallFormat.GetValueOnRenderThread() > 0;
		const bool MultiScatteringLUTUseSmallFormat = CVarSkyAtmosphereMultiScatteringLUTUseSmallFormat.GetValueOnRenderThread() > 0;

		TRefCountPtr<IPooledRenderTarget>& TransmittanceLutTexture = SkyInfo.GetTransmittanceLutTexture();
		Desc = FPooledRenderTargetDesc::Create2DDesc(
			FIntPoint(TransmittanceLutWidth, TransmittanceLutHeight), 
			TranstmittanceLUTUseSmallFormat ? TextureLUTSmallFormat : TextureLUTFormat, FClearValueBinding::None, TexCreate_HideInVisualizeTexture, TexCreate_ShaderResource | TexCreate_UAV, false);
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, TransmittanceLutTexture, TEXT("TransmittanceLutTexture"), true, ERenderTargetTransience::NonTransient);

		TRefCountPtr<IPooledRenderTarget>& MultiScatteredLuminanceLutTexture = SkyInfo.GetMultiScatteredLuminanceLutTexture();
		Desc = FPooledRenderTargetDesc::Create2DDesc(
			FIntPoint(MultiScatteredLuminanceLutWidth, MultiScatteredLuminanceLutHeight), 
			MultiScatteringLUTUseSmallFormat ? TextureLUTSmallFormat : TextureLUTFormat, FClearValueBinding::None, TexCreate_HideInVisualizeTexture, TexCreate_ShaderResource | TexCreate_UAV, false);
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, MultiScatteredLuminanceLutTexture, TEXT("MultiScatteredLuminanceLutTexture"), true, ERenderTargetTransience::NonTransient);

		if (CVarSkyAtmosphereDistantSkyLightLUT.GetValueOnRenderThread() > 0)
		{
			TRefCountPtr<IPooledRenderTarget>& DistantSkyLightLutTexture = SkyInfo.GetDistantSkyLightLutTexture();
			Desc = FPooledRenderTargetDesc::Create2DDesc(
				FIntPoint(1, 1),
				TextureLUTFormat, FClearValueBinding::None, TexCreate_HideInVisualizeTexture, TexCreate_ShaderResource | TexCreate_UAV, false);
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, DistantSkyLightLutTexture, TEXT("DistantSkyLightLutTexture"), true, ERenderTargetTransience::NonTransient);
		}

		//
		// Initialise transient per view resources.
		//

		FPooledRenderTargetDesc SkyAtmosphereViewLutTextureDesc = FPooledRenderTargetDesc::Create2DDesc(
			FIntPoint(SkyViewLutWidth, SkyViewLutHeight), 
			TextureLUTFormat, FClearValueBinding::None, TexCreate_HideInVisualizeTexture, TexCreate_ShaderResource | TexCreate_UAV, false);

		FPooledRenderTargetDesc SkyAtmosphereCameraAerialPerspectiveVolumeDesc = FPooledRenderTargetDesc::CreateVolumeDesc(
			CameraAerialPerspectiveVolumeScreenResolution, CameraAerialPerspectiveVolumeScreenResolution, CameraAerialPerspectiveVolumeDepthResolution, 
			PF_FloatRGBA, FClearValueBinding::None, TexCreate_HideInVisualizeTexture, TexCreate_ShaderResource | TexCreate_UAV, false);

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			FViewInfo& View = Views[ViewIndex];
			// Set textures and data that will be needed later on the view.
			View.SkyAtmosphereUniformShaderParameters = SkyInfo.GetAtmosphereShaderParameters();
			GRenderTargetPool.FindFreeElement(RHICmdList, SkyAtmosphereViewLutTextureDesc, View.SkyAtmosphereViewLutTexture, TEXT("View.SkyAtmosphereViewLutTexture"), true, ERenderTargetTransience::Transient);
			GRenderTargetPool.FindFreeElement(RHICmdList, SkyAtmosphereCameraAerialPerspectiveVolumeDesc, View.SkyAtmosphereCameraAerialPerspectiveVolume, TEXT("View.SkyAtmosphereCameraAerialPerspectiveVolume"), true, ERenderTargetTransience::Transient);
		}
	}
}

static void SetupSkyAtmosphereInternalCommonParameters(
	FSkyAtmosphereInternalCommonParameters& InternalCommonParameters, 
	const FScene& Scene,
	const FSkyAtmosphereRenderSceneInfo& SkyInfo)
{
	GET_VALID_DATA_FROM_CVAR;

	auto GetSizeAndInvSize = [](int32 Width, int32 Height)
	{
		float FWidth = float(Width);
		float FHeight = float(Height);
		return FVector4(FWidth, FHeight, 1.0f / FWidth, 1.0f / FHeight);
	};
	InternalCommonParameters.TransmittanceLutSizeAndInvSize = GetSizeAndInvSize(TransmittanceLutWidth, TransmittanceLutHeight);
	InternalCommonParameters.MultiScatteredLuminanceLutSizeAndInvSize = GetSizeAndInvSize(MultiScatteredLuminanceLutWidth, MultiScatteredLuminanceLutHeight);
	InternalCommonParameters.SkyViewLutSizeAndInvSize = GetSizeAndInvSize(SkyViewLutWidth, SkyViewLutHeight);

	InternalCommonParameters.SampleCountMin = CVarSkyAtmosphereSampleCountMin.GetValueOnRenderThread();
	InternalCommonParameters.SampleCountMin = CVarSkyAtmosphereSampleCountMin.GetValueOnRenderThread();
	InternalCommonParameters.SampleCountMax = CVarSkyAtmosphereSampleCountMax.GetValueOnRenderThread();
	float DistanceToSampleCountMaxInv = CVarSkyAtmosphereDistanceToSampleCountMax.GetValueOnRenderThread();

	InternalCommonParameters.FastSkySampleCountMin = CVarSkyAtmosphereFastSkyLUTSampleCountMin.GetValueOnRenderThread();
	InternalCommonParameters.FastSkySampleCountMax = CVarSkyAtmosphereFastSkyLUTSampleCountMax.GetValueOnRenderThread();
	float FastSkyDistanceToSampleCountMaxInv = CVarSkyAtmosphereFastSkyLUTDistanceToSampleCountMax.GetValueOnRenderThread();

	InternalCommonParameters.AerialPerspectiveStartDepth = AerialPerspectiveStartDepth;
	InternalCommonParameters.CameraAerialPerspectiveVolumeDepthResolution = float(CameraAerialPerspectiveVolumeDepthResolution);
	InternalCommonParameters.CameraAerialPerspectiveVolumeDepthResolutionInv = 1.0f / InternalCommonParameters.CameraAerialPerspectiveVolumeDepthResolution;
	InternalCommonParameters.CameraAerialPerspectiveVolumeDepthSliceLength = CameraAerialPerspectiveVolumeDepthSliceLength;
	InternalCommonParameters.CameraAerialPerspectiveVolumeDepthSliceLengthInv = 1.0f / CameraAerialPerspectiveVolumeDepthSliceLength;
	InternalCommonParameters.CameraAerialPerspectiveSampleCountPerSlice = CVarSkyAtmosphereAerialPerspectiveLUTSampleCountPerSlice.GetValueOnRenderThread();

	InternalCommonParameters.TransmittanceSampleCount = CVarSkyAtmosphereTranstmittanceLUTSampleCount.GetValueOnRenderThread();
	InternalCommonParameters.MultiScatteringSampleCount = CVarSkyAtmosphereMultiScatteringLUTSampleCount.GetValueOnRenderThread();

	InternalCommonParameters.SkyLuminanceFactor = FVector(SkyInfo.GetSkyLuminanceFactor());
	InternalCommonParameters.AerialPespectiveViewDistanceScale = SkyInfo.GetAerialPespectiveViewDistanceScale();

	auto ValidateDistanceValue = [](float& Value)
	{
		Value = Value < KINDA_SMALL_NUMBER ? KINDA_SMALL_NUMBER : Value;
	};
	auto ValidateSampleCountValue = [](float& Value)
	{
		Value = Value < 1.0f ? 1.0f : Value;
	};
	auto ValidateMaxSampleCountValue = [](float& Value, float& MinValue)
	{
		Value = Value < MinValue ? MinValue : Value;
	};
	ValidateSampleCountValue(InternalCommonParameters.SampleCountMin);
	ValidateMaxSampleCountValue(InternalCommonParameters.SampleCountMax, InternalCommonParameters.SampleCountMin);
	ValidateSampleCountValue(InternalCommonParameters.FastSkySampleCountMin);
	ValidateMaxSampleCountValue(InternalCommonParameters.FastSkySampleCountMax, InternalCommonParameters.FastSkySampleCountMin);
	ValidateSampleCountValue(InternalCommonParameters.CameraAerialPerspectiveSampleCountPerSlice);
	ValidateSampleCountValue(InternalCommonParameters.TransmittanceSampleCount);
	ValidateSampleCountValue(InternalCommonParameters.MultiScatteringSampleCount);
	ValidateDistanceValue(DistanceToSampleCountMaxInv);
	ValidateDistanceValue(FastSkyDistanceToSampleCountMaxInv);

	// Derived values post validation
	InternalCommonParameters.DistanceToSampleCountMaxInv = 1.0f / DistanceToSampleCountMaxInv;
	InternalCommonParameters.FastSkyDistanceToSampleCountMaxInv = 1.0f / FastSkyDistanceToSampleCountMaxInv;
	InternalCommonParameters.CameraAerialPerspectiveVolumeSizeAndInvSize = GetSizeAndInvSize(CameraAerialPerspectiveVolumeScreenResolution, CameraAerialPerspectiveVolumeScreenResolution);
}

static bool IsSecondAtmosphereLightEnabled(FScene* Scene)
{
	// If the second light is not null then we enable the second light.
	// We do not do any light1 to light0 remapping is light0 is null.
	return Scene->AtmosphereLights[1] != nullptr;
}

void FSceneRenderer::RenderSkyAtmosphereLookUpTables(FRHICommandListImmediate& RHICmdList)
{
	check(ShouldRenderSkyAtmosphere(Scene->GetSkyAtmosphereSceneInfo(), Scene->GetShaderPlatform())); // This should not be called if we should not render SkyAtmosphere

	DECLARE_GPU_STAT(SkyAtmosphereLUTs);
	SCOPED_DRAW_EVENT(RHICmdList, SkyAtmosphereLUTs);
	SCOPED_GPU_STAT(RHICmdList, SkyAtmosphereLUTs);

	FSkyAtmosphereRenderSceneInfo& SkyInfo = *Scene->GetSkyAtmosphereSceneInfo();
	const bool bMultiScattering = SkyInfo.IsMultiScatteringEnabled();
	const bool bFastSky = CVarSkyAtmosphereFastSkyLUT.GetValueOnRenderThread() > 0;
	const bool bFastAerialPespective = CVarSkyAtmosphereAerialPerspectiveApplyOnOpaque.GetValueOnRenderThread() > 0;
	const bool bSecondAtmosphereLightEnabled = IsSecondAtmosphereLightEnabled(Scene);

	FRHISamplerState* SamplerLinearClamp = TStaticSamplerState<SF_Trilinear>::GetRHI();

	// Initialise common internal parameters
	FSkyAtmosphereInternalCommonParameters InternalCommonParameters;
	SetupSkyAtmosphereInternalCommonParameters(InternalCommonParameters, *Scene, SkyInfo);
	TUniformBufferRef<FSkyAtmosphereInternalCommonParameters> InternalCommonParametersRef = TUniformBufferRef<FSkyAtmosphereInternalCommonParameters>::CreateUniformBufferImmediate(InternalCommonParameters, UniformBuffer_SingleFrame);

	UnbindRenderTargets(RHICmdList);
	FRDGBuilder GraphBuilder(RHICmdList);

	// Transmittance LUT
	TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
	{
		TShaderMapRef<FRenderTransmittanceLutCS> ComputeShader(GlobalShaderMap);

		FRenderTransmittanceLutCS::FParameters * PassParameters = GraphBuilder.AllocParameters<FRenderTransmittanceLutCS::FParameters>();
		PassParameters->Atmosphere = Scene->GetSkyAtmosphereSceneInfo()->GetAtmosphereUniformBuffer();
		PassParameters->SkyAtmosphere = InternalCommonParametersRef;
		PassParameters->TransmittanceLutUAV = SkyInfo.GetTransmittanceLutTexture()->GetRenderTargetItem().UAV;

		FIntVector TextureSize = SkyInfo.GetTransmittanceLutTexture()->GetDesc().GetSize();
		TextureSize.Z = 1;
		const FIntVector NumGroups = FIntVector::DivideAndRoundUp(TextureSize, FRenderTransmittanceLutCS::GroupSize);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TransmittanceLut"), *ComputeShader, PassParameters, NumGroups);
	}

	// Mean Illuminance LUT
	{
		TShaderMapRef<FRenderMultiScatteredLuminanceLutCS> ComputeShader(GlobalShaderMap);

		FRenderMultiScatteredLuminanceLutCS::FParameters * PassParameters = GraphBuilder.AllocParameters<FRenderMultiScatteredLuminanceLutCS::FParameters>();
		PassParameters->Atmosphere = Scene->GetSkyAtmosphereSceneInfo()->GetAtmosphereUniformBuffer();
		PassParameters->SkyAtmosphere = InternalCommonParametersRef;
		PassParameters->TransmittanceLutTextureSampler = SamplerLinearClamp;
		PassParameters->TransmittanceLutTexture = SkyInfo.GetTransmittanceLutTexture()->GetRenderTargetItem().ShaderResourceTexture;
		PassParameters->MultiScatteredLuminanceLutUAV = SkyInfo.GetMultiScatteredLuminanceLutTexture()->GetRenderTargetItem().UAV;

		FIntVector TextureSize = SkyInfo.GetMultiScatteredLuminanceLutTexture()->GetDesc().GetSize();
		TextureSize.Z = 1;
		const FIntVector NumGroups = FIntVector::DivideAndRoundUp(TextureSize, FRenderMultiScatteredLuminanceLutCS::GroupSize);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("MeanIllumLut"), *ComputeShader, PassParameters, NumGroups);
	}

	// Distant Sky Light LUT
	if(CVarSkyAtmosphereDistantSkyLightLUT.GetValueOnRenderThread() > 0)
	{
		FRenderDistantSkyLightLutCS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FSkyPermutationMultiScatteringApprox>(bMultiScattering);
		PermutationVector.Set<FSecondAtmosphereLight>(bSecondAtmosphereLightEnabled);
		TShaderMapRef<FRenderDistantSkyLightLutCS> ComputeShader(GlobalShaderMap, PermutationVector);

		using FPermutationDomain = TShaderPermutationDomain<FSkyPermutationMultiScatteringApprox, FSecondAtmosphereLight>;

		FRenderDistantSkyLightLutCS::FParameters * PassParameters = GraphBuilder.AllocParameters<FRenderDistantSkyLightLutCS::FParameters>();
		PassParameters->Atmosphere = Scene->GetSkyAtmosphereSceneInfo()->GetAtmosphereUniformBuffer();
		PassParameters->SkyAtmosphere = InternalCommonParametersRef;
		PassParameters->TransmittanceLutTextureSampler = SamplerLinearClamp;
		PassParameters->MultiScatteredLuminanceLutTextureSampler = SamplerLinearClamp;
		PassParameters->TransmittanceLutTexture = SkyInfo.GetTransmittanceLutTexture()->GetRenderTargetItem().ShaderResourceTexture;
		PassParameters->MultiScatteredLuminanceLutTexture = SkyInfo.GetMultiScatteredLuminanceLutTexture()->GetRenderTargetItem().ShaderResourceTexture;
		PassParameters->UniformSphereSamplesBuffer = GUniformSphereSamplesBuffer.UniformSphereSamplesBuffer.SRV;
		PassParameters->DistantSkyLightLutUAV = SkyInfo.GetDistantSkyLightLutTexture()->GetRenderTargetItem().UAV;

		FLightSceneInfo* Light0 = Scene->AtmosphereLights[0];
		FLightSceneInfo* Light1 = Scene->AtmosphereLights[1];
		if (Light0)
		{
			PassParameters->AtmosphereLightDirection0 = -Light0->Proxy->GetDirection();
			PassParameters->AtmosphereLightColor0 = Light0->Proxy->GetColor();
		}
		if (Light1)
		{
			PassParameters->AtmosphereLightDirection1 = -Light1->Proxy->GetDirection();
			PassParameters->AtmosphereLightColor1 = Light1->Proxy->GetColor();
		}
		PassParameters->DistantSkyLightSampleAltitude = CVarSkyAtmosphereDistantSkyLightLUTAltitude.GetValueOnAnyThread();

		FIntVector TextureSize = FIntVector(1, 1, 1);
		const FIntVector NumGroups = FIntVector::DivideAndRoundUp(TextureSize, FRenderDistantSkyLightLutCS::GroupSize);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("DistantSkyLightLut"), *ComputeShader, PassParameters, NumGroups);
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];

		const bool bLightDiskEnabled = !View.bIsReflectionCapture;

		UnbindRenderTargets(RHICmdList);

		// Sky View LUT
		{
			FRenderSkyViewLutCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FSkyPermutationMultiScatteringApprox>(bMultiScattering);
			PermutationVector.Set<FSourceDiskEnabled>(bLightDiskEnabled);
			PermutationVector.Set<FSecondAtmosphereLight>(bSecondAtmosphereLightEnabled);
			TShaderMapRef<FRenderSkyViewLutCS> ComputeShader(View.ShaderMap, PermutationVector);

			FRenderSkyViewLutCS::FParameters * PassParameters = GraphBuilder.AllocParameters<FRenderSkyViewLutCS::FParameters>();
			PassParameters->Atmosphere = Scene->GetSkyAtmosphereSceneInfo()->GetAtmosphereUniformBuffer();
			PassParameters->SkyAtmosphere = InternalCommonParametersRef;
			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
			PassParameters->TransmittanceLutTextureSampler = SamplerLinearClamp;
			PassParameters->MultiScatteredLuminanceLutTextureSampler = SamplerLinearClamp;
			PassParameters->TransmittanceLutTexture = SkyInfo.GetTransmittanceLutTexture()->GetRenderTargetItem().ShaderResourceTexture;
			PassParameters->MultiScatteredLuminanceLutTexture = SkyInfo.GetMultiScatteredLuminanceLutTexture()->GetRenderTargetItem().ShaderResourceTexture;
			PassParameters->SkyViewLutUAV = View.SkyAtmosphereViewLutTexture->GetRenderTargetItem().UAV;

			FIntVector TextureSize = View.SkyAtmosphereViewLutTexture->GetDesc().GetSize();
			TextureSize.Z = 1;
			const FIntVector NumGroups = FIntVector::DivideAndRoundUp(TextureSize, FRenderSkyViewLutCS::GroupSize);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SkyViewLut"), *ComputeShader, PassParameters, NumGroups);
		}

		// Camera Atmosphere Volume
		{
			FRenderCameraAerialPerspectiveVolumeCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FSkyPermutationMultiScatteringApprox>(bMultiScattering);
			PermutationVector.Set<FSecondAtmosphereLight>(bSecondAtmosphereLightEnabled);
			TShaderMapRef<FRenderCameraAerialPerspectiveVolumeCS> ComputeShader(View.ShaderMap, PermutationVector);

			FRenderCameraAerialPerspectiveVolumeCS::FParameters * PassParameters = GraphBuilder.AllocParameters<FRenderCameraAerialPerspectiveVolumeCS::FParameters>();
			PassParameters->Atmosphere = Scene->GetSkyAtmosphereSceneInfo()->GetAtmosphereUniformBuffer();
			PassParameters->SkyAtmosphere = InternalCommonParametersRef;
			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
			PassParameters->TransmittanceLutTextureSampler = SamplerLinearClamp;
			PassParameters->MultiScatteredLuminanceLutTextureSampler = SamplerLinearClamp;
			PassParameters->TransmittanceLutTexture = SkyInfo.GetTransmittanceLutTexture()->GetRenderTargetItem().ShaderResourceTexture;
			PassParameters->MultiScatteredLuminanceLutTexture = SkyInfo.GetMultiScatteredLuminanceLutTexture()->GetRenderTargetItem().ShaderResourceTexture;
			PassParameters->CameraAerialPerspectiveVolumeUAV = View.SkyAtmosphereCameraAerialPerspectiveVolume->GetRenderTargetItem().UAV;

			FIntVector TextureSize = View.SkyAtmosphereCameraAerialPerspectiveVolume->GetDesc().GetSize();
			const FIntVector NumGroups = FIntVector::DivideAndRoundUp(TextureSize, FRenderCameraAerialPerspectiveVolumeCS::GroupSize);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("CameraVolumeLut"), *ComputeShader, PassParameters, NumGroups);
		}
	}

	GraphBuilder.Execute();
	// TODO have RDG execute those above passes with compute overlap similarly to using AutomaticCacheFlushAfterComputeShader(true);
}

void FSceneRenderer::RenderSkyAtmosphere(FRHICommandListImmediate& RHICmdList)
{
	// We never render the sky pass on such platform as it is too expensive. 
	// In this case, the sky must be rendered as an opaque mesh using the shader graph as a composition tool.
	check(!IsMobilePlatform(Scene->GetShaderPlatform()));

	check(ShouldRenderSkyAtmosphere(Scene->GetSkyAtmosphereSceneInfo(), Scene->GetShaderPlatform())); // This should not be called if we should not render SkyAtmosphere

	DECLARE_GPU_STAT(SkyAtmosphere);
	SCOPED_DRAW_EVENT(RHICmdList, SkyAtmosphere);
	SCOPED_GPU_STAT(RHICmdList, SkyAtmosphere);

	FSkyAtmosphereRenderSceneInfo& SkyInfo = *Scene->GetSkyAtmosphereSceneInfo();
	const FAtmosphereSetup& Atmosphere = SkyInfo.GetAtmosphereSetup();
	const bool bMultiScattering = SkyInfo.IsMultiScatteringEnabled();
	const bool bFastSky = CVarSkyAtmosphereFastSkyLUT.GetValueOnRenderThread() > 0;
	const bool bFastAerialPerspective = CVarSkyAtmosphereAerialPerspectiveApplyOnOpaque.GetValueOnRenderThread() > 0;
	const bool bFastAerialPerspectiveDepthTest = CVarSkyAtmosphereAerialPerspectiveDepthTest.GetValueOnRenderThread() > 0;
	const bool bSecondAtmosphereLightEnabled = IsSecondAtmosphereLightEnabled(Scene);

	FRHISamplerState* SamplerLinearClamp = TStaticSamplerState<SF_Trilinear>::GetRHI();

	// Initialise common internal parameters
	FSkyAtmosphereInternalCommonParameters InternalCommonParameters;
	SetupSkyAtmosphereInternalCommonParameters(InternalCommonParameters, *Scene, SkyInfo);
	TUniformBufferRef<FSkyAtmosphereInternalCommonParameters> InternalCommonParametersRef = TUniformBufferRef<FSkyAtmosphereInternalCommonParameters>::CreateUniformBufferImmediate(InternalCommonParameters, UniformBuffer_SingleFrame);

	FRDGBuilder GraphBuilder(RHICmdList);
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	FRDGTextureRef SceneColor = GraphBuilder.RegisterExternalTexture(SceneContext.GetSceneColor(), TEXT("SceneColor"));
	FRDGTextureRef SceneDepth = GraphBuilder.RegisterExternalTexture(SceneContext.SceneDepthZ, TEXT("SceneDepth"));

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];

		const bool bLightDiskEnabled = !View.bIsReflectionCapture;

		// If the scene contains Sky material then it is likely rendering the sky using a sky dome mesh.
		// In this case we can use a simpler shader during this pass to only render aerial perspective
		// and sky pixels can likely be optimised out.
		const bool bRenderSkyPixel = !View.bSceneHasSkyMaterial;

		const FVector ViewOrigin = View.ViewMatrices.GetViewOrigin();
		const float KmToCm = 100000.0f;
		const FVector PlanetOrigin = FVector(0.0f, 0.0f, -Atmosphere.BottomRadius * KmToCm);
		const float TopOfAtmosphere = Atmosphere.TopRadius * KmToCm;
		const float SafeEdge = 1000.0f;	// 10 meters
		const bool ForceRayMarching = (FVector::Distance(ViewOrigin, PlanetOrigin) - TopOfAtmosphere - SafeEdge) > 0.0f;

		// Render the sky, and optionally the atmosphere aerial perspective, on the scene luminance buffer
		{
			FRenderSkyAtmospherePS::FPermutationDomain PsPermutationVector;
			PsPermutationVector.Set<FSkyPermutationMultiScatteringApprox>(bMultiScattering);
			PsPermutationVector.Set<FFastSky>(bFastSky && !ForceRayMarching);
			PsPermutationVector.Set<FFastAerialPespective>(bFastAerialPerspective && !ForceRayMarching);
			PsPermutationVector.Set<FSourceDiskEnabled>(bLightDiskEnabled);
			PsPermutationVector.Set<FSecondAtmosphereLight>(bSecondAtmosphereLightEnabled);
			PsPermutationVector.Set<FRenderSky>(bRenderSkyPixel);
			PsPermutationVector = FRenderSkyAtmospherePS::RemapPermutation(PsPermutationVector);
			TShaderMapRef<FRenderSkyAtmospherePS> PixelShader(View.ShaderMap, PsPermutationVector);

			FRenderSkyAtmosphereVS::FPermutationDomain VsPermutationVector;
			TShaderMapRef<FRenderSkyAtmosphereVS> VertexShader(View.ShaderMap, VsPermutationVector);

			FRenderTargetBindingSlots RenderTargets;
			RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad, ERenderTargetStoreAction::EStore);
			RenderTargets.DepthStencil = FDepthStencilBinding(SceneDepth, ERenderTargetLoadAction::ELoad, ERenderTargetStoreAction::ENoAction,
				ERenderTargetLoadAction::ENoAction, ERenderTargetStoreAction::ENoAction, FExclusiveDepthStencil::DepthRead_StencilNop);

			FRenderSkyAtmospherePS::FParameters* PsPassParameters = GraphBuilder.AllocParameters<FRenderSkyAtmospherePS::FParameters>();
			PsPassParameters->Atmosphere = Scene->GetSkyAtmosphereSceneInfo()->GetAtmosphereUniformBuffer();
			PsPassParameters->SkyAtmosphere = InternalCommonParametersRef;
			PsPassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
			PsPassParameters->RenderTargets = RenderTargets;
			PsPassParameters->SceneTextures = CreateSceneTextureUniformBufferSingleDraw(RHICmdList, ESceneTextureSetupMode::All, View.FeatureLevel);
			PsPassParameters->TransmittanceLutTextureSampler = SamplerLinearClamp;
			PsPassParameters->MultiScatteredLuminanceLutTextureSampler = SamplerLinearClamp;
			PsPassParameters->SkyViewLutTextureSampler = SamplerLinearClamp;
			PsPassParameters->CameraAerialPerspectiveVolumeTextureSampler = SamplerLinearClamp;
			PsPassParameters->TransmittanceLutTexture = SkyInfo.GetTransmittanceLutTexture()->GetRenderTargetItem().ShaderResourceTexture;
			PsPassParameters->MultiScatteredLuminanceLutTexture = SkyInfo.GetMultiScatteredLuminanceLutTexture()->GetRenderTargetItem().ShaderResourceTexture;
			PsPassParameters->SkyViewLutTexture = View.SkyAtmosphereViewLutTexture->GetRenderTargetItem().ShaderResourceTexture;
			PsPassParameters->CameraAerialPerspectiveVolumeTexture = View.SkyAtmosphereCameraAerialPerspectiveVolume->GetRenderTargetItem().ShaderResourceTexture;
			ClearUnusedGraphResources(*PixelShader, PsPassParameters);

			float StartDepthZ = 0.1f;
			if (bFastAerialPerspectiveDepthTest)
			{
				const FMatrix ViewProjectionMatrix = View.ViewMatrices.GetProjectionMatrix();
				float HalfHorizontalFOV = FMath::Atan(1.0f / ViewProjectionMatrix.M[0][0]);
				float HalfVerticalFOV = FMath::Atan(1.0f / ViewProjectionMatrix.M[1][1]);
				// For sky reflection capture, the start depth can be super large. Se we max it to make sure the triangle is never in front the NearClippingDistance.
				float StartDepthInKm = FMath::Max(InternalCommonParameters.AerialPerspectiveStartDepth, View.NearClippingDistance) * KmToCm;
				const float StartDepthViewKm = FMath::Cos(FMath::Max(HalfHorizontalFOV, HalfVerticalFOV)) * StartDepthInKm;
				const FVector4 Projected = ViewProjectionMatrix.TransformFVector4(FVector4(0.0f, 0.0f, StartDepthViewKm, 1.0f));
				StartDepthZ = Projected.Z / Projected.W;
			}

			FIntRect Viewport = View.ViewRect;
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("SkyAtmosphereDraw"),
				PsPassParameters,
				ERDGPassFlags::Raster,
				[PsPassParameters, VertexShader, PixelShader, Viewport, bFastAerialPerspectiveDepthTest, bRenderSkyPixel, StartDepthZ](FRHICommandList& RHICmdListLambda)
			{
				RHICmdListLambda.SetViewport(Viewport.Min.X, Viewport.Min.Y, 0.0f, Viewport.Max.X, Viewport.Max.Y, 1.0f);

				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdListLambda.ApplyCachedRenderTargets(GraphicsPSOInit);

				GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
				if (bFastAerialPerspectiveDepthTest)
				{
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI();
				}
				else
				{
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
				}

				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader->GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader->GetPixelShader();
				GraphicsPSOInit.PrimitiveType = PT_TriangleList;

				if (!bRenderSkyPixel && GSupportsDepthBoundsTest)
				{
					// When we do not render the sky in the sky pass and depth bound test is supported, we take advantage of it in order to skip the processing of sky pixels.
					GraphicsPSOInit.bDepthBounds = true;
					if (bool(ERHIZBuffer::IsInverted))
					{
						//const float SmallestFloatAbove0 = 1.1754943508e-38;		// 32bit float depth
						const float SmallestFloatAbove0 = 1.0f / 16777215.0f;		// 24bit norm depth
						RHICmdListLambda.SetDepthBounds(SmallestFloatAbove0, 1.0f);		// Tested on dx12 PC
					}
					else
					{
						//const float SmallestFloatBelow1 = 0.9999999404;			// 32bit float depth
						const float SmallestFloatBelow1 = 16777214.0f / 16777215.0f;// 24bit norm depth
						RHICmdListLambda.SetDepthBounds(0.0f, SmallestFloatBelow1);		// Untested
					}
				}

				SetGraphicsPipelineState(RHICmdListLambda, GraphicsPSOInit);

				SetShaderParameters(RHICmdListLambda, *PixelShader, PixelShader->GetPixelShader(), *PsPassParameters);

				FRenderSkyAtmosphereVS::FParameters VsPassParameters;
				VsPassParameters.StartDepthZ = StartDepthZ;
				SetShaderParameters(RHICmdListLambda, *VertexShader, VertexShader->GetVertexShader(), VsPassParameters);

				RHICmdListLambda.DrawPrimitive(0, 1, 1);
			});
		}
	}
	GraphBuilder.Execute();


#if WITH_EDITOR
	if (CVarSkyAtmosphereFastSkyLUT.GetValueOnAnyThread() == 0 && CVarSkyAtmosphereAerialPerspectiveApplyOnOpaque.GetValueOnAnyThread() > 0)
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			FViewInfo& View = Views[ViewIndex];
			const float ViewPortWidth = float(View.ViewRect.Width());
			const float ViewPortHeight = float(View.ViewRect.Height());

			FRenderTargetTemp TempRenderTarget(View, (const FTexture2DRHIRef&)SceneContext.GetSceneColor()->GetRenderTargetItem().TargetableTexture);
			FCanvas Canvas(&TempRenderTarget, NULL, View.Family->CurrentRealTime, ViewFamily.CurrentWorldTime, ViewFamily.DeltaWorldTime, View.GetFeatureLevel());

			FLinearColor TextColor(1.0f, 0.5f, 0.0f);
			FString Text = TEXT("You are using a FastAerialPespective without FastSky, visuals might look wrong.");
			Canvas.DrawShadowedString(ViewPortWidth*0.5 - Text.Len()*7, ViewPortHeight*0.5, *Text, GetStatsFont(), TextColor);

			Canvas.Flush_RenderThread(RHICmdList);
		}
	}
#endif
}

bool FSceneRenderer::ShouldRenderSkyAtmosphereEditorNotifications()
{
#if WITH_EDITOR
	bool bAnyViewHasSkyMaterial = false;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		bAnyViewHasSkyMaterial |= Views[ViewIndex].bSceneHasSkyMaterial;
	}
	return bAnyViewHasSkyMaterial;
#endif
	return false;
}

void FSceneRenderer::RenderSkyAtmosphereEditorNotifications(FRHICommandListImmediate& RHICmdList)
{
#if WITH_EDITOR
	DECLARE_GPU_STAT(SkyAtmosphereEditor);
	SCOPED_DRAW_EVENT(RHICmdList, SkyAtmosphereEditor);
	SCOPED_GPU_STAT(RHICmdList, SkyAtmosphereEditor);

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];

		if (View.bSceneHasSkyMaterial)
		{
			const float ViewPortWidth = float(View.ViewRect.Width());
			const float ViewPortHeight = float(View.ViewRect.Height());

			FRenderTargetTemp TempRenderTarget(View, (const FTexture2DRHIRef&)SceneContext.GetSceneColor()->GetRenderTargetItem().TargetableTexture);
			FCanvas Canvas(&TempRenderTarget, NULL, View.Family->CurrentRealTime, ViewFamily.CurrentWorldTime, ViewFamily.DeltaWorldTime, View.GetFeatureLevel());

			FLinearColor TextColor(1.0f, 0.5f, 0.0f);
			FString Text  = TEXT("Your scene contains a mesh with a sky material assigned to it.");
			FString Text2 = TEXT("So we optimize by not rendering the sky as a full-screen pass.");
			FString Text3 = TEXT("However, your sky mesh does not cover that part of the screen!");
			const int32 TextWidth = Text.Len() * 7;
			for (int i = 32; i < ViewPortWidth; i += (TextWidth + 32))
			{
				for (int j = 32; j < ViewPortHeight; j += 128)
				{
					Canvas.DrawShadowedString(i, j     , *Text , GetStatsFont(), TextColor);
					Canvas.DrawShadowedString(i, j + 16, *Text2, GetStatsFont(), TextColor);
					Canvas.DrawShadowedString(i, j + 32, *Text3, GetStatsFont(), TextColor);
				}
			}

			// Flush immediately to output into the scene color right away
			Canvas.Flush_RenderThread(RHICmdList);
		}
	}
#endif
}



/*=============================================================================
	FDeferredShadingSceneRenderer functions
=============================================================================*/



void FDeferredShadingSceneRenderer::RenderDebugSkyAtmosphere(FRHICommandListImmediate& RHICmdList)
{
#if WITH_EDITOR
	check(ShouldRenderSkyAtmosphere(Scene->GetSkyAtmosphereSceneInfo(), Scene->GetShaderPlatform())); // This should not be called if we should not render SkyAtmosphere

	//if (!RHISupportsComputeShaders()) return;	// TODO cannot render, add a ShouldRender function. Also should PipelineShouldCook ?

	DECLARE_GPU_STAT(SkyAtmosphereDebugViz);
	SCOPED_DRAW_EVENT(RHICmdList, SkyAtmosphereDebugViz);
	SCOPED_GPU_STAT(RHICmdList, SkyAtmosphereDebugViz);

	const int32 SkyAtmosphereVisualize = CVarSkyAtmosphereVisualize.GetValueOnRenderThread();
	FSkyAtmosphereRenderSceneInfo& SkyInfo = *Scene->GetSkyAtmosphereSceneInfo();
	const FAtmosphereSetup& Atmosphere = SkyInfo.GetAtmosphereSetup();
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	if (SkyAtmosphereVisualize)
	{
		const bool bMultiScattering = SkyInfo.IsMultiScatteringEnabled();
		const bool bFastSky = CVarSkyAtmosphereFastSkyLUT.GetValueOnRenderThread() > 0;
		const bool bFastAerialPespective = CVarSkyAtmosphereAerialPerspectiveApplyOnOpaque.GetValueOnRenderThread() > 0;

		FRDGBuilder GraphBuilder(RHICmdList);
		FRDGTextureRef SceneColor = GraphBuilder.RegisterExternalTexture(SceneContext.GetSceneColor(), TEXT("SceneColor"));
		FRDGTextureRef SceneDepth = GraphBuilder.RegisterExternalTexture(SceneContext.SceneDepthZ, TEXT("SceneDepth"));

		FRHIBlendState* PreMultipliedColorTransmittanceBlend = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();
		FRHIDepthStencilState* DepthStencilStateWrite = TStaticDepthStencilState<true, CF_Always>::GetRHI();
		FRHISamplerState* SamplerLinearClamp = TStaticSamplerState<SF_Trilinear>::GetRHI();

		// Initialise common internal parameters
		FSkyAtmosphereInternalCommonParameters InternalCommonParameters;
		SetupSkyAtmosphereInternalCommonParameters(InternalCommonParameters, *Scene, SkyInfo);
		TUniformBufferRef<FSkyAtmosphereInternalCommonParameters> InternalCommonParametersRef = TUniformBufferRef<FSkyAtmosphereInternalCommonParameters>::CreateUniformBufferImmediate(InternalCommonParameters, UniformBuffer_SingleFrame);

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			FViewInfo& View = Views[ViewIndex];

			// Render the sky and atmosphere on the scene luminance buffer
			{
				FRenderDebugSkyAtmospherePS::FPermutationDomain PermutationVector;
				PermutationVector.Set<FSkyPermutationMultiScatteringApprox>(bMultiScattering);
				TShaderMapRef<FRenderDebugSkyAtmospherePS> PixelShader(View.ShaderMap, PermutationVector);

				FRenderDebugSkyAtmospherePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRenderDebugSkyAtmospherePS::FParameters>();
				PassParameters->Atmosphere = Scene->GetSkyAtmosphereSceneInfo()->GetAtmosphereUniformBuffer();
				PassParameters->SkyAtmosphere = InternalCommonParametersRef;
				PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
				PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad, ERenderTargetStoreAction::EStore);
				PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(SceneDepth, ERenderTargetLoadAction::ELoad, ERenderTargetStoreAction::ENoAction,
					ERenderTargetLoadAction::ENoAction, ERenderTargetStoreAction::ENoAction, FExclusiveDepthStencil::DepthRead_StencilNop);
				PassParameters->TransmittanceLutTextureSampler = SamplerLinearClamp;
				PassParameters->MultiScatteredLuminanceLutTextureSampler = SamplerLinearClamp;
				PassParameters->TransmittanceLutTexture = SkyInfo.GetTransmittanceLutTexture()->GetRenderTargetItem().ShaderResourceTexture;
				PassParameters->MultiScatteredLuminanceLutTexture = SkyInfo.GetMultiScatteredLuminanceLutTexture()->GetRenderTargetItem().ShaderResourceTexture;
				PassParameters->ViewPortWidth = float(View.ViewRect.Width());
				PassParameters->ViewPortHeight = float(View.ViewRect.Height());

				FPixelShaderUtils::AddFullscreenPass<FRenderDebugSkyAtmospherePS>(GraphBuilder, View.ShaderMap, RDG_EVENT_NAME("SkyAtmosphere"), *PixelShader, PassParameters,
					View.ViewRect, PreMultipliedColorTransmittanceBlend, nullptr, DepthStencilStateWrite);
			}
		}
		GraphBuilder.Execute();
	}

	// Now debug print
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];
		const float ViewPortWidth = float(View.ViewRect.Width());
		const float ViewPortHeight = float(View.ViewRect.Height());

		FRenderTargetTemp TempRenderTarget(View, (const FTexture2DRHIRef&)SceneContext.GetSceneColor()->GetRenderTargetItem().TargetableTexture);
		FCanvas Canvas(&TempRenderTarget, NULL, View.Family->CurrentRealTime, ViewFamily.CurrentWorldTime, ViewFamily.DeltaWorldTime, View.GetFeatureLevel());

		FLinearColor TextColor(FLinearColor::White);
		FLinearColor GrayTextColor(FLinearColor::Gray);
		FLinearColor WarningColor(1.0f, 0.5f, 0.0f);
		FString Text;

		const float ViewPlanetAltitude = (View.ViewLocation*0.00001f - FVector(0.0f, 0.0f, -Atmosphere.BottomRadius)).Size() - Atmosphere.BottomRadius;
		const bool ViewUnderGroundLevel = ViewPlanetAltitude < 0.0f;
		if (ViewUnderGroundLevel)
		{
			Text = FString::Printf(TEXT("SkyAtmosphere: View is %.3f km under the planet ground level!"), -ViewPlanetAltitude);
			Canvas.DrawShadowedString(ViewPortWidth*0.5 - 250.0f, ViewPortHeight*0.5f, *Text, GetStatsFont(), WarningColor);
		}

		if (SkyAtmosphereVisualize)
		{
			// This needs to stay in sync with RenderSkyAtmosphereDebugPS.
			const float DensityViewTop = ViewPortHeight * 0.1f;
			const float DensityViewBottom = ViewPortHeight * 0.8f;
			const float DensityViewLeft = ViewPortWidth * 0.8f;
			const float Margin = 2.0f;
			const float TimeOfDayViewHeight = 64.0f;
			const float TimeOfDayViewTop = ViewPortHeight - (TimeOfDayViewHeight + Margin * 2.0);
			const float HemiViewHeight = ViewPortWidth * 0.25f;
			const float HemiViewTop = ViewPortHeight - HemiViewHeight - TimeOfDayViewHeight - Margin * 2.0;

			Text = FString::Printf(TEXT("Atmosphere top = %.1f km"), Atmosphere.TopRadius - Atmosphere.BottomRadius);
			Canvas.DrawShadowedString(DensityViewLeft, DensityViewTop, *Text, GetStatsFont(), TextColor);
			Text = FString::Printf(TEXT("Rayleigh extinction"));
			Canvas.DrawShadowedString(DensityViewLeft + 60.0f, DensityViewTop + 30.0f, *Text, GetStatsFont(), FLinearColor(FLinearColor::Red));
			Text = FString::Printf(TEXT("Mie extinction"));
			Canvas.DrawShadowedString(DensityViewLeft + 60.0f, DensityViewTop + 45.0f, *Text, GetStatsFont(), FLinearColor(FLinearColor::Green));
			Text = FString::Printf(TEXT("Absorption"));
			Canvas.DrawShadowedString(DensityViewLeft + 60.0f, DensityViewTop + 60.0f, *Text, GetStatsFont(), FLinearColor(FLinearColor::Blue));
			Text = FString::Printf(TEXT("<=== Low visual contribution"));
			Canvas.DrawShadowedString(DensityViewLeft + 2.0, DensityViewTop + 150.0f, *Text, GetStatsFont(), GrayTextColor);
			Text = FString::Printf(TEXT("High visual contribution ===>"));
			Canvas.DrawShadowedString(ViewPortWidth - 170.0f, DensityViewTop + 166.0f, *Text, GetStatsFont(), GrayTextColor);
			Text = FString::Printf(TEXT("Ground level"));
			Canvas.DrawShadowedString(DensityViewLeft, DensityViewBottom, *Text, GetStatsFont(), TextColor);

			Text = FString::Printf(TEXT("Time-of-day preview"));
			Canvas.DrawShadowedString(ViewPortWidth*0.5f - 80.0f, TimeOfDayViewTop, *Text, GetStatsFont(), TextColor);

			Text = FString::Printf(TEXT("Hemisphere view"));
			Canvas.DrawShadowedString(Margin, HemiViewTop, *Text, GetStatsFont(), TextColor);
		}

		if (ViewUnderGroundLevel || SkyAtmosphereVisualize)
		{
			Canvas.Flush_RenderThread(RHICmdList);
		}
	}

#endif
}


