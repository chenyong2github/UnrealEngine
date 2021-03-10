// Copyright Epic Games, Inc. All Rights Reserved.

#include "RHI.h"

#if RHI_RAYTRACING

#include "RendererPrivate.h"
#include "GlobalShader.h"
#include "DeferredShadingRenderer.h"
#include "PathTracingUniformBuffers.h"
#include "HAL/PlatformApplicationMisc.h"
#include "RayTracingTypes.h"
#include "RenderCore/Public/GenerateMips.h"

TAutoConsoleVariable<int32> CVarPathTracingMaxBounces(
	TEXT("r.PathTracing.MaxBounces"),
	-1,
	TEXT("Sets the maximum number of path tracing bounces (default = -1 (driven by postprocesing volume))"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarPathTracingSamplesPerPixel(
	TEXT("r.PathTracing.SamplesPerPixel"),
	-1,
	TEXT("Defines the samples per pixel before resetting the simulation (default = -1 (driven by postprocesing volume))"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<float> CVarPathTracingFilterWidth(
	TEXT("r.PathTracing.FilterWidth"),
	-1,
	TEXT("Define the anti-aliasing filter width (default = -1 (driven by postprocesing volume))"),
	ECVF_RenderThreadSafe
);


TAutoConsoleVariable<int32> CVarPathTracingUseErrorDiffusion(
	TEXT("r.PathTracing.UseErrorDiffusion"),
	0,
	TEXT("Enables an experimental sampler that diffuses visible error in screen space. This generally produces better results when the target sample count can be reached. (default = 0 (disabled))"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarPathTracingMISMode(
	TEXT("r.PathTracing.MISMode"),
	2,
	TEXT("Selects the sampling techniques (default = 2 (MIS enabled))\n")
	TEXT("0: Material sampling\n")
	TEXT("1: Light sampling\n")
	TEXT("2: MIS betwen material and light sampling (default)\n"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarPathTracingVisibleLights(
	TEXT("r.PathTracing.VisibleLights"),
	0,
	TEXT("Should light sources be visible to camera rays? (default = 0 (off))\n")
	TEXT("0: Hide lights from camera rays (default)\n")
	TEXT("1: Make lights visible to camera\n"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<float> CVarPathTracingMaxPathIntensity(
	TEXT("r.PathTracing.MaxPathIntensity"),
	-1,
	TEXT("When positive, light paths greater that this amount are clamped to prevent fireflies (default = -1 (off))"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarPathTracingApproximateCaustics(
	TEXT("r.PathTracing.ApproximateCaustics"),
	1,
	TEXT("When non-zero, the path tracer will approximate caustic paths to reduce noise. This reduces speckles and noise from low-roughness glass and metals. (default = 1 (enabled))"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarPathTracingEnableCameraBackfaceCulling(
	TEXT("r.PathTracing.EnableCameraBackfaceCulling"),
	1,
	TEXT("When non-zero, the path tracer will skip over backfacing triangles when tracing primary rays from the camera. (default = 1 (enabled))"),
	ECVF_RenderThreadSafe
);


TAutoConsoleVariable<int32> CVarPathTracingFrameIndependentTemporalSeed(
	TEXT("r.PathTracing.FrameIndependentTemporalSeed"),
	1,
	TEXT("Indicates to use different temporal seed for each sample across frames rather than resetting the sequence at the start of each frame\n")
	TEXT("0: off\n")
	TEXT("1: on (default)\n"),
	ECVF_RenderThreadSafe
);

// r.PathTracing.GPUCount is read only because ComputeViewGPUMasks results cannot change after UE has been launched
TAutoConsoleVariable<int32> CVarPathTracingGPUCount(
	TEXT("r.PathTracing.GPUCount"),
	1,
	TEXT("Sets the amount of GPUs used for computing the path tracing pass (default = 1 GPU)"),
	ECVF_RenderThreadSafe | ECVF_ReadOnly
);

TAutoConsoleVariable<int32> CVarPathTracingWiperMode(
	TEXT("r.PathTracing.WiperMode"),
	0,
	TEXT("Enables wiper mode to render using the path tracer only in a region of the screen for debugging purposes (default = 0, wiper mode disabled)"),
	ECVF_RenderThreadSafe 
);

TAutoConsoleVariable<int32> CVarPathTracingProgressDisplay(
	TEXT("r.PathTracing.ProgressDisplay"),
	0,
	TEXT("Enables an in-frame display of progress towards the defined sample per pixel limit. The indicator dissapears when the maximum is reached and sample accumulation has stopped (default = 0)\n")
	TEXT("0: off (default)\n")
	TEXT("1: on\n"),
	ECVF_RenderThreadSafe
);

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FPathTracingData, "PathTracingData");

// This function prepares the portion of shader arguments that may involve invalidating the path traced state
static bool PrepareShaderArgs(const FViewInfo& View, FPathTracingData& PathTracingData) {
	PathTracingData.SkipDirectLighting = false;
	int32 MaxBounces = CVarPathTracingMaxBounces.GetValueOnRenderThread();
	if (MaxBounces < 0)
	{
		MaxBounces = View.FinalPostProcessSettings.PathTracingMaxBounces;
	}
	if (View.Family->EngineShowFlags.DirectLighting)
	{
		if (!View.Family->EngineShowFlags.GlobalIllumination)
		{
			// direct lighting, but no GI
			MaxBounces = 1;
		}
	}
	else
	{
		if (View.Family->EngineShowFlags.GlobalIllumination)
		{
			// skip direct lighting, but still do the full bounces
			PathTracingData.SkipDirectLighting = true;
		}
		else
		{
			// neither direct, nor GI is on
			MaxBounces = 0;
		}
	}

	PathTracingData.MaxBounces = MaxBounces;
	PathTracingData.MaxNormalBias = GetRaytracingMaxNormalBias();
	PathTracingData.MISMode = CVarPathTracingMISMode.GetValueOnRenderThread();
	PathTracingData.VisibleLights = CVarPathTracingVisibleLights.GetValueOnRenderThread();
	PathTracingData.MaxPathIntensity = CVarPathTracingMaxPathIntensity.GetValueOnRenderThread();
	PathTracingData.UseErrorDiffusion = CVarPathTracingUseErrorDiffusion.GetValueOnRenderThread();
	PathTracingData.ApproximateCaustics = CVarPathTracingApproximateCaustics.GetValueOnRenderThread();
	PathTracingData.EnableCameraBackfaceCulling = CVarPathTracingEnableCameraBackfaceCulling.GetValueOnRenderThread();
	float FilterWidth = CVarPathTracingFilterWidth.GetValueOnRenderThread();
	if (FilterWidth < 0)
	{
		FilterWidth = View.FinalPostProcessSettings.PathTracingFilterWidth;
	}
	PathTracingData.FilterWidth = FilterWidth;

	bool NeedInvalidation = false;

	// If any of the parameters above changed since last time -- reset the accumulation
	// FIXME: find something cleaner than just using static variables here. Should all
	// the state used for comparison go into ViewState?
	static uint32 PrevMaxBounces = PathTracingData.MaxBounces;
	if (PathTracingData.MaxBounces != PrevMaxBounces)
	{
		NeedInvalidation = true;
		PrevMaxBounces = PathTracingData.MaxBounces;
	}

	// Changing MIS mode requires starting over
	static uint32 PreviousMISMode = PathTracingData.MISMode;
	if (PreviousMISMode != PathTracingData.MISMode)
	{
		NeedInvalidation = true;
		PreviousMISMode = PathTracingData.MISMode;
	}

	// Changing VisibleLights requires starting over
	static uint32 PreviousVisibleLights = PathTracingData.VisibleLights;
	if (PreviousVisibleLights != PathTracingData.VisibleLights)
	{
		NeedInvalidation = true;
		PreviousVisibleLights = PathTracingData.VisibleLights;
	}

	// Changing MaxPathIntensity requires starting over
	static float PreviousMaxPathIntensity = PathTracingData.MaxPathIntensity;
	if (PreviousMaxPathIntensity != PathTracingData.MaxPathIntensity)
	{
		NeedInvalidation = true;
		PreviousMaxPathIntensity = PathTracingData.MaxPathIntensity;
	}

	// Changing sampler requires starting over
	static uint32 PreviousUseErrorDiffusion = PathTracingData.UseErrorDiffusion;
	if (PreviousUseErrorDiffusion != PathTracingData.UseErrorDiffusion)
	{
		NeedInvalidation = true;
		PreviousUseErrorDiffusion = PathTracingData.UseErrorDiffusion;
	}

	// Changing approximate caustics requires starting over
	static uint32 PreviousApproximateCaustics = PathTracingData.ApproximateCaustics;
	if (PreviousApproximateCaustics != PathTracingData.ApproximateCaustics)
	{
		NeedInvalidation = true;
		PreviousApproximateCaustics = PathTracingData.ApproximateCaustics;
	}

	// Changing filter width requires starting over
	static float PreviousFilterWidth = PathTracingData.FilterWidth;
	if (PreviousFilterWidth != PathTracingData.FilterWidth)
	{
		NeedInvalidation = true;
		PreviousFilterWidth = PathTracingData.FilterWidth;
	}

	// Changing backface culling status requires starting over
	static uint32 PreviousBackfaceCulling = PathTracingData.EnableCameraBackfaceCulling;
	if (PreviousBackfaceCulling != PathTracingData.EnableCameraBackfaceCulling)
	{
		NeedInvalidation = true;
		PreviousBackfaceCulling = PathTracingData.EnableCameraBackfaceCulling;
	}

	// Changing direct lighting skipping requires starting over
	static uint32 PreviousSkipDirectLighting = PathTracingData.SkipDirectLighting;
	if (PreviousSkipDirectLighting != PathTracingData.SkipDirectLighting)
	{
		NeedInvalidation = true;
		PreviousSkipDirectLighting = PathTracingData.SkipDirectLighting;
	}

	// the rest of PathTracingData and AdaptiveSamplingData is filled in by SetParameters below
	return NeedInvalidation;
}

class FPathTracingSkylightPrepareCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPathTracingSkylightPrepareCS)
	SHADER_USE_PARAMETER_STRUCT(FPathTracingSkylightPrepareCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_X"), FComputeShaderUtils::kGolden2DGroupSize);
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_Y"), FComputeShaderUtils::kGolden2DGroupSize);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(TextureCube, SkyLightCubemap0)
		SHADER_PARAMETER_TEXTURE(TextureCube, SkyLightCubemap1)
		SHADER_PARAMETER_SAMPLER(SamplerState, SkyLightCubemapSampler0)
		SHADER_PARAMETER_SAMPLER(SamplerState, SkyLightCubemapSampler1)
		SHADER_PARAMETER(float, SkylightBlendFactor)
		SHADER_PARAMETER(float, SkylightInvResolution)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, SkylightTextureOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, SkylightTexturePdf)
		SHADER_PARAMETER(FVector, SkyColor)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_SHADER_TYPE(, FPathTracingSkylightPrepareCS, TEXT("/Engine/Private/PathTracing/PathTracingSkylightPrepare.usf"), TEXT("PathTracingSkylightPrepareCS"), SF_Compute);

class FPathTracingRG : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPathTracingRG)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FPathTracingRG, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("USE_NEW_SKYDOME"), 1);
	}


	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RadianceTexture)
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_REF(FPathTracingData, PathTracingData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FPathTracingLight>, SceneLights)
		SHADER_PARAMETER(uint32, SceneLightCount)
		// Skylight
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SkylightTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SkylightPdf)
		SHADER_PARAMETER_SAMPLER(SamplerState, SkylightTextureSampler)
		SHADER_PARAMETER(float, SkylightInvResolution)
		SHADER_PARAMETER(int32, SkylightMipCount)
		// IES Profiles
		SHADER_PARAMETER_RDG_TEXTURE(Texture2DArray, IESTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, IESTextureSampler)
		// Shared sampler for all IES profiles
		SHADER_PARAMETER(FIntVector, TileOffset)	// Used by multi-GPU rendering
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FPathTracingRG, "/Engine/Private/PathTracing/PathTracing.usf", "PathTracingMainRG", SF_RayGen);

class FPathTracingIESAtlasCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPathTracingIESAtlasCS)
	SHADER_USE_PARAMETER_STRUCT(FPathTracingIESAtlasCS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_X"), FComputeShaderUtils::kGolden2DGroupSize);
		OutEnvironment.SetDefine(TEXT("THREADGROUPSIZE_Y"), FComputeShaderUtils::kGolden2DGroupSize);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_TEXTURE(Texture2D, IESTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, IESSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray, IESAtlas)
		SHADER_PARAMETER(int32, IESAtlasSlice)
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_SHADER_TYPE(, FPathTracingIESAtlasCS, TEXT("/Engine/Private/PathTracing/PathTracingIESAtlas.usf"), TEXT("PathTracingIESAtlasCS"), SF_Compute);

bool PrepareSkyTexture(FRDGBuilder& GraphBuilder, FScene* Scene, const FViewInfo& View, FPathTracingRG::FParameters* PathTracingParameters)
{
	PathTracingParameters->SkylightTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	FReflectionUniformParameters Parameters;
	SetupReflectionUniformParameters(View, Parameters);
	if (!(Parameters.SkyLightParameters.Y > 0))
	{
		// textures not ready, or skylight not active
		// just put in a placeholder
		PathTracingParameters->SkylightTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
		PathTracingParameters->SkylightPdf = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
		PathTracingParameters->SkylightInvResolution = 0;
		PathTracingParameters->SkylightMipCount = 0;
		return false;
	}

	// TODO: this current runs every frame, but should only run when something has been updated
	// This pass is not too expensive (at least compared to the cost of the path tracer) but this should
	// be cleaned up.
	RDG_EVENT_SCOPE(GraphBuilder, "Path Tracing SkylightPrepare");

	FLinearColor SkyColor = Scene->SkyLight->GetEffectiveLightColor();
	// since we are resampled into an octahedral layout, we multiply the cubemap resolution by 2 to get roughly the same number of texels
	uint32 Size = FMath::RoundUpToPowerOfTwo(2 * Scene->SkyLight->CaptureCubeMapResolution);

	FRDGTextureDesc SkylightTextureDesc = FRDGTextureDesc::Create2D(
		FIntPoint(Size, Size),
		PF_A32B32G32R32F, // half precision might be ok?
		FClearValueBinding::None,
		TexCreate_ShaderResource | TexCreate_UAV);
	PathTracingParameters->SkylightTexture = GraphBuilder.CreateTexture(SkylightTextureDesc, TEXT("PathTracer.Skylight"), ERDGTextureFlags::None);
	FRDGTextureDesc SkylightPdfDesc = FRDGTextureDesc::Create2D(
		FIntPoint(Size, Size),
		PF_R32_FLOAT, // half precision might be ok?
		FClearValueBinding::None,
		TexCreate_ShaderResource | TexCreate_UAV,
		FMath::CeilLogTwo(Size) + 1);
	PathTracingParameters->SkylightPdf = GraphBuilder.CreateTexture(SkylightPdfDesc, TEXT("PathTracer.SkylightPdf"), ERDGTextureFlags::None);

	PathTracingParameters->SkylightInvResolution = 1.0f / Size;
	PathTracingParameters->SkylightMipCount = SkylightPdfDesc.NumMips;

	// run a simple compute shader to sample the cubemap and prep the top level of the mipmap hierarchy
	{
		TShaderMapRef<FPathTracingSkylightPrepareCS> ComputeShader(View.ShaderMap);
		FPathTracingSkylightPrepareCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FPathTracingSkylightPrepareCS::FParameters>();
		PassParameters->SkyColor = FVector(SkyColor.R, SkyColor.G, SkyColor.B);
		PassParameters->SkyLightCubemap0 = Parameters.SkyLightCubemap;
		PassParameters->SkyLightCubemap1 = Parameters.SkyLightBlendDestinationCubemap;
		PassParameters->SkyLightCubemapSampler0 = Parameters.SkyLightCubemapSampler;
		PassParameters->SkyLightCubemapSampler1 = Parameters.SkyLightBlendDestinationCubemapSampler;
		PassParameters->SkylightBlendFactor = Parameters.SkyLightParameters.W;
		PassParameters->SkylightInvResolution = PathTracingParameters->SkylightInvResolution;
		PassParameters->SkylightTextureOutput = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(PathTracingParameters->SkylightTexture, 0));
		PassParameters->SkylightTexturePdf = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(PathTracingParameters->SkylightPdf, 0));
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("SkylightPrepare"),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(FIntPoint(Size, Size), FComputeShaderUtils::kGolden2DGroupSize));
	}

	FGenerateMips::ExecuteCompute(GraphBuilder, PathTracingParameters->SkylightPdf, TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI());

	return true;
}

void SetLightParameters(FRDGBuilder& GraphBuilder, FPathTracingRG::FParameters* PassParameters, FScene* Scene, const FViewInfo& View)
{
	// Lights
	FPathTracingLight Lights[RAY_TRACING_LIGHT_COUNT_MAXIMUM]; // Keep this on the stack for now -- eventually will need to make this dynamic to lift size limit (and also avoid uploading per frame ...)
	unsigned LightCount = 0;

	// Prepend SkyLight to light buffer since it is not part of the regular light list
	if (PrepareSkyTexture(GraphBuilder, Scene, View, PassParameters))
	{
		check(Scene->SkyLight != nullptr);
		FPathTracingLight& DestLight = Lights[LightCount];
		DestLight.Color = FVector(1, 1, 1); // not used
		DestLight.Flags = Scene->SkyLight->bTransmission ? PATHTRACER_FLAG_TRANSMISSION_MASK : 0;
		DestLight.Flags |= PATHTRACER_FLAG_LIGHTING_CHANNEL_MASK;
		DestLight.Flags |= PATHTRACING_LIGHT_SKY;
		DestLight.Flags |= Scene->SkyLight->bCastShadows ? PATHTRACER_FLAG_CAST_SHADOW_MASK : 0;
		DestLight.IESTextureSlice = -1;
		LightCount++;
	}

	TMap<FTexture*, int> IESLightProfilesMap;
	for (auto Light : Scene->Lights)
	{
		if (LightCount >= RAY_TRACING_LIGHT_COUNT_MAXIMUM) break;

		ELightComponentType LightComponentType = (ELightComponentType)Light.LightSceneInfo->Proxy->GetLightType();

		if (((LightComponentType == LightType_Directional) && !View.Family->EngineShowFlags.DirectionalLights) ||
			((LightComponentType == LightType_Rect       ) && !View.Family->EngineShowFlags.RectLights       ) ||
			((LightComponentType == LightType_Spot       ) && !View.Family->EngineShowFlags.SpotLights       ) ||
			((LightComponentType == LightType_Point      ) && !View.Family->EngineShowFlags.PointLights      ))
		{
			// This light type is not currently enabled
			continue;
		}

		FPathTracingLight& DestLight = Lights[LightCount];

		FLightShaderParameters LightParameters;
		Light.LightSceneInfo->Proxy->GetLightShaderParameters(LightParameters);
		uint32 Transmission = Light.LightSceneInfo->Proxy->Transmission();
		uint8 LightingChannelMask = Light.LightSceneInfo->Proxy->GetLightingChannelMask();

		DestLight.Flags = Transmission ? PATHTRACER_FLAG_TRANSMISSION_MASK : 0;
		DestLight.Flags |= LightingChannelMask & PATHTRACER_FLAG_LIGHTING_CHANNEL_MASK;
		DestLight.Flags |= Light.LightSceneInfo->Proxy->CastsDynamicShadow() ? PATHTRACER_FLAG_CAST_SHADOW_MASK : 0;
		DestLight.IESTextureSlice = -1;

		if (View.Family->EngineShowFlags.TexturedLightProfiles)
		{
			FTexture* IESTexture = Light.LightSceneInfo->Proxy->GetIESTextureResource();
			if (IESTexture != nullptr)
			{
				// Only add a given texture once
				DestLight.IESTextureSlice = IESLightProfilesMap.FindOrAdd(IESTexture, IESLightProfilesMap.Num());
			}
		}

		// these mean roughly the same thing across all light types
		DestLight.Color = LightParameters.Color;
		DestLight.Position = LightParameters.Position;
		DestLight.Normal = -LightParameters.Direction;
		DestLight.dPdu = FVector::CrossProduct(LightParameters.Tangent, LightParameters.Direction);
		DestLight.dPdv = LightParameters.Tangent;
		DestLight.Attenuation = LightParameters.InvRadius;
		DestLight.FalloffExponent = 0;

		switch (LightComponentType)
		{
			case LightType_Directional:
			{
				DestLight.Normal = LightParameters.Direction;
				DestLight.Dimensions = FVector(LightParameters.SourceRadius, LightParameters.SoftSourceRadius, 0.0f);
				DestLight.Flags |= PATHTRACING_LIGHT_DIRECTIONAL;
				break;
			}
			case LightType_Rect:
			{
				DestLight.Dimensions = FVector(2.0f * LightParameters.SourceRadius, 2.0f * LightParameters.SourceLength, 0.0f);
				DestLight.Shaping = FVector2D(LightParameters.RectLightBarnCosAngle, LightParameters.RectLightBarnLength);
				DestLight.FalloffExponent = LightParameters.FalloffExponent;
				DestLight.Flags |= Light.LightSceneInfo->Proxy->IsInverseSquared() ? 0 : PATHTRACER_FLAG_NON_INVERSE_SQUARE_FALLOFF_MASK;
				DestLight.Flags |= PATHTRACING_LIGHT_RECT;
				break;
			}
			case LightType_Spot:
			{
				DestLight.Dimensions = FVector(LightParameters.SourceRadius, LightParameters.SoftSourceRadius, LightParameters.SourceLength);
				DestLight.Shaping = LightParameters.SpotAngles;
				DestLight.FalloffExponent = LightParameters.FalloffExponent;
				DestLight.Flags |= Light.LightSceneInfo->Proxy->IsInverseSquared() ? 0 : PATHTRACER_FLAG_NON_INVERSE_SQUARE_FALLOFF_MASK;
				DestLight.Flags |= PATHTRACING_LIGHT_SPOT;
				break;
			}
			case LightType_Point:
			{
				DestLight.Dimensions = FVector(LightParameters.SourceRadius, LightParameters.SoftSourceRadius, LightParameters.SourceLength);
				DestLight.FalloffExponent = LightParameters.FalloffExponent;
				DestLight.Flags |= Light.LightSceneInfo->Proxy->IsInverseSquared() ? 0 : PATHTRACER_FLAG_NON_INVERSE_SQUARE_FALLOFF_MASK;
				DestLight.Flags |= PATHTRACING_LIGHT_POINT;
				break;
			}
			default:
			{
				// Just in case someone adds a new light type one day ...
				checkNoEntry();
				break;
			}
		}

		LightCount++;
	}

	{
		// Upload the buffer of lights to the GPU
		size_t DataSize = sizeof(FPathTracingLight) * FMath::Max(LightCount, 1u);
		PassParameters->SceneLights = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CreateStructuredBuffer(GraphBuilder, TEXT("PathTracer.LightsBuffer"), sizeof(FPathTracingLight), FMath::Max(LightCount, 1u), Lights, DataSize)));
		PassParameters->SceneLightCount = LightCount;
	}

	if (IESLightProfilesMap.Num() > 0)
	{
		// We found some IES profiles to use -- upload them into a single atlas so we can access them easily in HLSL

		// FIXME: This is redundant because all the IES textures are already on the GPU, we just don't have the ability to use
		// an array of texture handles on the HLSL side.

		// FIXME: This is also redundant with the logic in RayTracingLighting.cpp, but the latter is limitted to 1D profiles and 
		// does not consider the same set of lights as the path tracer. Longer term we should aim to unify the representation of lights
		// across both passes

		// FIXME: This process is repeated every frame! would be nicer to cache the data somehow. Perhaps just do this step for
		// Iteration == 0 since we can assume that any changes in IES profiles will invalidate the path tracer anyway?

		// This size matches the import resolution of light profiles (see FIESLoader::GetWidth)
		const int kIESAtlasSize = 256;
		const int NumSlices = IESLightProfilesMap.Num();
		FRDGTextureDesc IESTextureDesc = FRDGTextureDesc::Create2DArray(
			FIntPoint(kIESAtlasSize, kIESAtlasSize),
			PF_R32_FLOAT,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV,
			NumSlices);
		FRDGTexture* IESTexture = GraphBuilder.CreateTexture(IESTextureDesc, TEXT("PathTracer.IESAtlas"), ERDGTextureFlags::None);

		for (auto&& Entry : IESLightProfilesMap)
		{
			FPathTracingIESAtlasCS::FParameters* AtlasPassParameters = GraphBuilder.AllocParameters<FPathTracingIESAtlasCS::FParameters>();
			const int Slice = Entry.Value;
			AtlasPassParameters->IESTexture = Entry.Key->TextureRHI;
			AtlasPassParameters->IESSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			AtlasPassParameters->IESAtlas = GraphBuilder.CreateUAV(IESTexture);
			AtlasPassParameters->IESAtlasSlice = Slice;
			TShaderMapRef<FPathTracingIESAtlasCS> ComputeShader(View.ShaderMap);
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Path Tracing IES Atlas (Slice=%d)", Slice),
				ComputeShader,
				AtlasPassParameters,
				FComputeShaderUtils::GetGroupCount(FIntPoint(kIESAtlasSize, kIESAtlasSize), FComputeShaderUtils::kGolden2DGroupSize));
		}

		PassParameters->IESTexture = IESTexture;
	}
	else
	{
		PassParameters->IESTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.WhiteDummy);
	}
}

class FPathTracingCompositorPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPathTracingCompositorPS)
	SHADER_USE_PARAMETER_STRUCT(FPathTracingCompositorPS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<float4>, RadianceTexture)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(uint32, Iteration)
		SHADER_PARAMETER(uint32, MaxSamples)
		SHADER_PARAMETER(int, ProgressDisplayEnabled)

		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_SHADER_TYPE(, FPathTracingCompositorPS, TEXT("/Engine/Private/PathTracing/PathTracingCompositingPixelShader.usf"), TEXT("CompositeMain"), SF_Pixel);

void FDeferredShadingSceneRenderer::PreparePathTracing(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	if (View.RayTracingRenderMode == ERayTracingRenderMode::PathTracing)
	{
		// Declare all RayGen shaders that require material closest hit shaders to be bound
		auto RayGenShader = View.ShaderMap->GetShader<FPathTracingRG>();
		OutRayGenShaders.Add(RayGenShader.GetRayTracingShader());
	}
}

void FSceneViewState::PathTracingInvalidate()
{
	PathTracingRadianceRT.SafeRelease();
	PathTracingSampleIndex = 0;
}

DECLARE_GPU_STAT_NAMED(Stat_GPU_PathTracing, TEXT("Path Tracing"));
void FDeferredShadingSceneRenderer::RenderPathTracing(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	FRDGTextureRef SceneColorOutputTexture)
{
	RDG_GPU_STAT_SCOPE(GraphBuilder, Stat_GPU_PathTracing);
	RDG_EVENT_SCOPE(GraphBuilder, "Path Tracing");


	bool bArgsChanged = false;

	// Get current value of MaxSPP and reset render if it has changed
	// NOTE: we ignore the CVar when using offline rendering
	int32 SamplesPerPixelCVar = View.bIsOfflineRender ? -1 : CVarPathTracingSamplesPerPixel.GetValueOnRenderThread();
	uint32 MaxSPP = SamplesPerPixelCVar > -1 ? SamplesPerPixelCVar : View.FinalPostProcessSettings.PathTracingSamplesPerPixel;
	MaxSPP = FMath::Max(MaxSPP, 1u);
	if (View.ViewState->PathTracingTargetSPP != MaxSPP)
	{
		// Store MaxSPP in the view state because we may have multiple views, each targetting a different sample count
		View.ViewState->PathTracingTargetSPP = MaxSPP;
		bArgsChanged = true;
	}
	// Changing FrameIndependentTemporalSeed requires starting over
	bool LockedSamplingPattern = CVarPathTracingFrameIndependentTemporalSeed.GetValueOnRenderThread() == 0;
	static bool PreviousLockedSamplingPattern = LockedSamplingPattern;
	if (PreviousLockedSamplingPattern != LockedSamplingPattern)
	{
		PreviousLockedSamplingPattern = LockedSamplingPattern;
		bArgsChanged = true;
	}

	// compute an integer code of what show flags related to lights are currently enabled so we can detect changes
	int CurrentLightShowFlags = 0;
	CurrentLightShowFlags |= View.Family->EngineShowFlags.SkyLighting           ? 1 << 0 : 0;
	CurrentLightShowFlags |= View.Family->EngineShowFlags.DirectionalLights     ? 1 << 1 : 0;
	CurrentLightShowFlags |= View.Family->EngineShowFlags.RectLights            ? 1 << 2 : 0;
	CurrentLightShowFlags |= View.Family->EngineShowFlags.SpotLights            ? 1 << 3 : 0;
	CurrentLightShowFlags |= View.Family->EngineShowFlags.PointLights           ? 1 << 4 : 0;
	CurrentLightShowFlags |= View.Family->EngineShowFlags.TexturedLightProfiles ? 1 << 5 : 0;

	static int PreviousLightShowFlags = CurrentLightShowFlags;
	if (PreviousLightShowFlags != CurrentLightShowFlags)
	{
		PreviousLightShowFlags = CurrentLightShowFlags;
		bArgsChanged = true;
	}

	// Get other basic path tracing settings and see if we need to invalidate the current state
	FPathTracingData PathTracingData;
	bArgsChanged |= PrepareShaderArgs(View, PathTracingData);

	// If the scene has changed in some way (camera move, object movement, etc ...)
	// we must invalidate the ViewState to start over from scratch
	if (bArgsChanged || View.ViewState->PathTracingRect != View.ViewRect)
	{
		View.ViewState->PathTracingInvalidate();
		View.ViewState->PathTracingRect = View.ViewRect;
	}

	// Setup temporal seed _after_ invalidation in case we got reset
	if (LockedSamplingPattern)
	{
		// Count samples from 0 for deterministic results
		PathTracingData.TemporalSeed = View.ViewState->PathTracingSampleIndex;
	}
	else
	{
		// Count samples from an ever-increasing counter to avoid screen-door effect
		PathTracingData.TemporalSeed = View.ViewState->PathTracingFrameIndex;
	}
	PathTracingData.Iteration = View.ViewState->PathTracingSampleIndex;
	PathTracingData.MaxSamples = MaxSPP;

	// Prepare radiance buffer (will be shared with display pass)
	FRDGTexture* RadianceTexture = nullptr;
	if (View.ViewState->PathTracingRadianceRT)
	{
		// we already have a valid radiance texture, re-use it
		RadianceTexture = GraphBuilder.RegisterExternalTexture(View.ViewState->PathTracingRadianceRT, TEXT("PathTracer.Radiance"));
	}
	else
	{
		// First time through, need to make a new texture
		FRDGTextureDesc RadianceTextureDesc = FRDGTextureDesc::Create2D(
			View.ViewRect.Size(),
			PF_A32B32G32R32F,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);
		RadianceTexture = GraphBuilder.CreateTexture(RadianceTextureDesc, TEXT("PathTracer.Radiance"), ERDGTextureFlags::MultiFrame);
	}
	bool NeedsMoreRays = PathTracingData.Iteration < MaxSPP;

	if (NeedsMoreRays)
	{
		FPathTracingRG::FParameters* PassParameters = GraphBuilder.AllocParameters<FPathTracingRG::FParameters>();
		PassParameters->TLAS = View.RayTracingScene.RayTracingSceneRHI->GetShaderResourceView();
		PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
		PassParameters->PathTracingData = CreateUniformBufferImmediate(PathTracingData, EUniformBufferUsage::UniformBuffer_SingleFrame);
		// upload sky/lights data
		SetLightParameters(GraphBuilder, PassParameters, Scene, View);
		PassParameters->IESTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->RadianceTexture = GraphBuilder.CreateUAV(RadianceTexture);

		// TODO: in multi-gpu case, split image into tiles
		PassParameters->TileOffset.X = 0;
		PassParameters->TileOffset.Y = 0;

		TShaderMapRef<FPathTracingRG> RayGenShader(View.ShaderMap);
		ClearUnusedGraphResources(RayGenShader, PassParameters);
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("Path Tracer Compute (%d x %d) Sample=%d/%d", View.ViewRect.Size().X, View.ViewRect.Size().Y, View.ViewState->PathTracingSampleIndex, MaxSPP),
			PassParameters,
			ERDGPassFlags::Compute,
			[PassParameters, RayGenShader, &View](FRHICommandListImmediate& RHICmdList)
		{
			FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;
			
			int32 DispatchSizeX = View.ViewRect.Size().X;
			int32 DispatchSizeY = View.ViewRect.Size().Y;

			FRayTracingShaderBindingsWriter GlobalResources;
			SetShaderParameters(GlobalResources, RayGenShader, *PassParameters);

			RHICmdList.RayTraceDispatch(
				View.RayTracingMaterialPipeline,
				RayGenShader.GetRayTracingShader(),
				RayTracingSceneRHI, GlobalResources,
				DispatchSizeX, DispatchSizeY
			);
		});

		// After we are done, make sure we remember our texture for next time so that we can accumulate samples across frames
		GraphBuilder.QueueTextureExtraction(RadianceTexture, &View.ViewState->PathTracingRadianceRT);
	}

	// now add a pixel shader pass to display our Radiance buffer

	FPathTracingCompositorPS::FParameters* DisplayParameters = GraphBuilder.AllocParameters<FPathTracingCompositorPS::FParameters>();
	DisplayParameters->Iteration = PathTracingData.Iteration;
	DisplayParameters->MaxSamples = MaxSPP;
	DisplayParameters->ProgressDisplayEnabled = CVarPathTracingProgressDisplay.GetValueOnRenderThread();
	DisplayParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	DisplayParameters->RadianceTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(RadianceTexture));
	DisplayParameters->RenderTargets[0] = FRenderTargetBinding(SceneColorOutputTexture, ERenderTargetLoadAction::ELoad);

	FScreenPassTextureViewport Viewport(SceneColorOutputTexture, View.ViewRect);

	// wiper mode - reveals the render below the path tracing display
	// NOTE: we still path trace the full resolution even while wiping the cursor so that rendering does not get out of sync
	if (CVarPathTracingWiperMode.GetValueOnRenderThread() != 0)
	{
		float DPIScale = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(View.CursorPos.X, View.CursorPos.Y);
		Viewport.Rect.Min.X = View.CursorPos.X / DPIScale;
	}

	TShaderMapRef<FPathTracingCompositorPS> PixelShader(View.ShaderMap);
	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("Path Tracer Display (%d x %d)", View.ViewRect.Size().X, View.ViewRect.Size().Y),
		View,
		Viewport,
		Viewport,
		PixelShader,
		DisplayParameters
	);

	// Bump counters for next frame
	++View.ViewState->PathTracingSampleIndex;
	++View.ViewState->PathTracingFrameIndex;
}

#endif
