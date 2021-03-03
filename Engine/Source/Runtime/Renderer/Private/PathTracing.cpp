// Copyright Epic Games, Inc. All Rights Reserved.

#include "RHI.h"

#if RHI_RAYTRACING

#include "RendererPrivate.h"
#include "GlobalShader.h"
#include "DeferredShadingRenderer.h"
#include "PostProcess/PostProcessing.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PathTracingUniformBuffers.h"
#include "RHI/Public/PipelineStateCache.h"
#include "RayTracing/RayTracingSkyLight.h"
#include "RayTracing/RaytracingOptions.h"
#include "HAL/PlatformApplicationMisc.h"

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
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FPathTracingLightData, "SceneLightsData");

// This function prepares the portion of shader arguments that may involve invalidating the path traced state
static bool PrepareShaderArgs(const FViewInfo& View, FPathTracingData& PathTracingData) {
	int32 PathTracingMaxBounces = CVarPathTracingMaxBounces.GetValueOnRenderThread();
	if (PathTracingMaxBounces < 0)
		PathTracingMaxBounces = View.FinalPostProcessSettings.PathTracingMaxBounces;
	PathTracingData.MaxBounces = PathTracingMaxBounces;
	PathTracingData.MaxNormalBias = GetRaytracingMaxNormalBias();
	PathTracingData.MISMode = CVarPathTracingMISMode.GetValueOnRenderThread();
	PathTracingData.VisibleLights = CVarPathTracingVisibleLights.GetValueOnRenderThread();
	PathTracingData.MaxPathIntensity = CVarPathTracingMaxPathIntensity.GetValueOnRenderThread();
	PathTracingData.UseErrorDiffusion = CVarPathTracingUseErrorDiffusion.GetValueOnRenderThread();

	bool NeedInvalidation = false;

	// If any of the parameters above changed since last time -- reset the accumulation
	// FIXME: find something cleaner than just using static variables here. Should all
	// the state used for comparison go into ViewState?
	static uint32 PrevMaxBounces = PathTracingMaxBounces;
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

	// the rest of PathTracingData and AdaptiveSamplingData is filled in by SetParameters below
	return NeedInvalidation;
}

class FPathTracingRG : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FPathTracingRG)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FPathTracingRG, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RadianceTexture)
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_REF(FSkyLightData, SkyLightData)
		SHADER_PARAMETER_STRUCT_REF(FPathTracingLightData, SceneLightsData)
		SHADER_PARAMETER_STRUCT_REF(FPathTracingData, PathTracingData)
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
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
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

void SetLightParameters(FRDGBuilder& GraphBuilder, FPathTracingRG::FParameters* PassParameters, FPathTracingLightData& LightData, FSkyLightData& SkyLightData, FScene* Scene, const FViewInfo& View, bool UseLightProfiles)
{
	// Sky light
	bool IsSkyLightValid = SetupSkyLightParameters(*Scene, &SkyLightData);

	// Lights
	LightData.Count = 0;

	// Prepend SkyLight to light buffer since it is not part of the regular light list
	if (IsSkyLightValid)
	{
		uint32 SkyLightIndex = 0;
		uint8 SkyLightLightingChannelMask = 0xFF;
		LightData.Color[SkyLightIndex] = FVector(SkyLightData.Color);
		LightData.Flags[SkyLightIndex] = SkyLightData.bTransmission ? PATHTRACER_FLAG_TRANSMISSION_MASK : 0;
		LightData.Flags[SkyLightIndex] |= PATHTRACER_FLAG_LIGHTING_CHANNEL_MASK;
		LightData.Flags[SkyLightIndex] |= PATHTRACING_LIGHT_SKY;
		LightData.IESTextureSlice[SkyLightIndex] = -1;
		LightData.FalloffExponent[LightData.Count] = 0;
		LightData.Count++;
	}

	TMap<FTexture*, int> IESLightProfilesMap;
	for (auto Light : Scene->Lights)
	{
		if (LightData.Count >= RAY_TRACING_LIGHT_COUNT_MAXIMUM) break;

		FLightShaderParameters LightParameters;
		Light.LightSceneInfo->Proxy->GetLightShaderParameters(LightParameters);
		uint32 Transmission = Light.LightSceneInfo->Proxy->Transmission();
		uint8 LightingChannelMask = Light.LightSceneInfo->Proxy->GetLightingChannelMask();
		LightData.Flags[LightData.Count] = Transmission ? PATHTRACER_FLAG_TRANSMISSION_MASK : 0;
		LightData.Flags[LightData.Count] |= LightingChannelMask & PATHTRACER_FLAG_LIGHTING_CHANNEL_MASK;
		LightData.IESTextureSlice[LightData.Count] = -1;

		if (UseLightProfiles)
		{
			FTexture* IESTexture = Light.LightSceneInfo->Proxy->GetIESTextureResource();
			if (IESTexture != nullptr)
			{
				// Only add a given texture once
				LightData.IESTextureSlice[LightData.Count] = IESLightProfilesMap.FindOrAdd(IESTexture, IESLightProfilesMap.Num());
			}
		}

		// these mean roughly the same thing across all light types
		LightData.Color[LightData.Count] = LightParameters.Color;
		LightData.Position[LightData.Count] = LightParameters.Position;
		LightData.Normal[LightData.Count] = -LightParameters.Direction;
		LightData.dPdu[LightData.Count] = FVector::CrossProduct(LightParameters.Tangent, LightParameters.Direction);
		LightData.dPdv[LightData.Count] = LightParameters.Tangent;
		LightData.Attenuation[LightData.Count] = LightParameters.InvRadius;
		LightData.FalloffExponent[LightData.Count] = 0;

		ELightComponentType LightComponentType = (ELightComponentType)Light.LightSceneInfo->Proxy->GetLightType();
		switch (LightComponentType)
		{
			case LightType_Directional:
			{
                LightData.Normal[LightData.Count] = LightParameters.Direction;
				LightData.Dimensions[LightData.Count] = FVector(0.0f, 0.0f, LightParameters.SourceRadius);
				LightData.Flags[LightData.Count] |= PATHTRACING_LIGHT_DIRECTIONAL;
				break;
			}
			case LightType_Rect:
			{
				LightData.Dimensions[LightData.Count] = FVector(2.0f * LightParameters.SourceRadius, 2.0f * LightParameters.SourceLength, 0.0f);
				LightData.RectLightBarnCosAngle[LightData.Count] = LightParameters.RectLightBarnCosAngle;
				LightData.RectLightBarnLength[LightData.Count] = LightParameters.RectLightBarnLength;
				LightData.FalloffExponent[LightData.Count] = LightParameters.FalloffExponent;
				LightData.Flags[LightData.Count] |= Light.LightSceneInfo->Proxy->IsInverseSquared() ? 0 : PATHTRACER_FLAG_NON_INVERSE_SQUARE_FALLOFF_MASK;
				LightData.Flags[LightData.Count] |= PATHTRACING_LIGHT_RECT;
				break;
			}
			case LightType_Spot:
			{
				LightData.Dimensions[LightData.Count] = FVector(LightParameters.SpotAngles, LightParameters.SourceRadius);
				LightData.FalloffExponent[LightData.Count] = LightParameters.FalloffExponent;
				LightData.Flags[LightData.Count] |= Light.LightSceneInfo->Proxy->IsInverseSquared() ? 0 : PATHTRACER_FLAG_NON_INVERSE_SQUARE_FALLOFF_MASK;
				LightData.Flags[LightData.Count] |= PATHTRACING_LIGHT_SPOT;
				break;
			}
			case LightType_Point:
			{
				LightData.Dimensions[LightData.Count] = FVector(0.0, 0.0, LightParameters.SourceRadius);
				LightData.FalloffExponent[LightData.Count] = LightParameters.FalloffExponent;
				LightData.Flags[LightData.Count] |= Light.LightSceneInfo->Proxy->IsInverseSquared() ? 0 : PATHTRACER_FLAG_NON_INVERSE_SQUARE_FALLOFF_MASK;
				LightData.Flags[LightData.Count] |= PATHTRACING_LIGHT_POINT;
				break;
			}
			default:
			{
				// Just in case someone adds a new light type one day ...
				checkNoEntry();
				break;
			}
		};

		LightData.Count++;
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
		FRDGTexture* IESTexture = GraphBuilder.CreateTexture(IESTextureDesc, TEXT("PathTracerIESAtlas"), ERDGTextureFlags::None);

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
		SHADER_PARAMETER(unsigned, Iteration)
		SHADER_PARAMETER(unsigned, MaxSamples)
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
	VarianceMipTreeDimensions = FIntVector(0);
	TotalRayCount = 0;
	PathTracingSPP = 0;
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
	int32 SamplesPerPixelCVar = CVarPathTracingSamplesPerPixel.GetValueOnRenderThread();
	uint32 MaxSPP = SamplesPerPixelCVar > -1 ? SamplesPerPixelCVar : View.FinalPostProcessSettings.PathTracingSamplesPerPixel;
	MaxSPP = FMath::Max(MaxSPP, 1u);
	static uint32 PreviousMaxSPP = MaxSPP;
	if (PreviousMaxSPP != MaxSPP)
	{
		PreviousMaxSPP = MaxSPP;
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

	bool UseLightProfiles = View.Family->EngineShowFlags.TexturedLightProfiles;
	static bool PreviousUseLightProfiles = UseLightProfiles;
	if (PreviousUseLightProfiles != UseLightProfiles)
	{
		PreviousUseLightProfiles = UseLightProfiles;
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
		PathTracingData.TemporalSeed = View.ViewState->PathTracingSPP;
	}
	else
	{
		// Count samples from an ever-increasing counter to avoid screen-door effect
		PathTracingData.TemporalSeed = View.ViewState->PathTracingFrameIndependentTemporalSeed;
	}
	PathTracingData.Iteration = View.ViewState->PathTracingSPP;
	PathTracingData.MaxSamples = MaxSPP;

	// Prepare radiance buffer (will be shared with display pass)
	FRDGTexture* RadianceTexture = nullptr;
	if (View.ViewState->PathTracingRadianceRT)
	{
		// we already have a valid radiance texture, re-use it
		RadianceTexture = GraphBuilder.RegisterExternalTexture(View.ViewState->PathTracingRadianceRT, TEXT("PathTracerRadiance"));
	}
	else
	{
		// First time through, need to make a new texture
		FRDGTextureDesc RadianceTextureDesc = FRDGTextureDesc::Create2D(
			View.ViewRect.Size(),
			PF_A32B32G32R32F,
			FClearValueBinding::None,
			TexCreate_ShaderResource | TexCreate_UAV);
		RadianceTexture = GraphBuilder.CreateTexture(RadianceTextureDesc, TEXT("PathTracerRadiance"), ERDGTextureFlags::MultiFrame);
	}
	bool NeedsMoreRays = PathTracingData.Iteration < MaxSPP;

	if (NeedsMoreRays)
	{
		FPathTracingRG::FParameters* PassParameters = GraphBuilder.AllocParameters<FPathTracingRG::FParameters>();
		PassParameters->TLAS = View.RayTracingScene.RayTracingSceneRHI->GetShaderResourceView();
		PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
		PassParameters->PathTracingData = CreateUniformBufferImmediate(PathTracingData, EUniformBufferUsage::UniformBuffer_SingleFrame);
		{
			// upload sky/lights data
			FSkyLightData SkyLightData;
			FPathTracingLightData LightData;
			SetLightParameters(GraphBuilder, PassParameters, LightData, SkyLightData, Scene, View, UseLightProfiles);
			PassParameters->SkyLightData = CreateUniformBufferImmediate(SkyLightData, EUniformBufferUsage::UniformBuffer_SingleFrame);
			PassParameters->SceneLightsData = CreateUniformBufferImmediate(LightData, EUniformBufferUsage::UniformBuffer_SingleFrame);
		}
		PassParameters->IESTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->RadianceTexture = GraphBuilder.CreateUAV(RadianceTexture);

		// TODO: in multi-gpu case, split image into tiles
		PassParameters->TileOffset.X = 0;
		PassParameters->TileOffset.Y = 0;

		TShaderMapRef<FPathTracingRG> RayGenShader(View.ShaderMap);
		ClearUnusedGraphResources(RayGenShader, PassParameters);
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("Path Tracer Compute (%d x %d) Sample=%d/%d", View.ViewRect.Size().X, View.ViewRect.Size().Y, View.ViewState->PathTracingSPP, MaxSPP),
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
	DisplayParameters->RenderTargets[0] = FRenderTargetBinding(SceneColorOutputTexture, ERenderTargetLoadAction::ENoAction);

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
	++View.ViewState->PathTracingSPP;
	++View.ViewState->PathTracingFrameIndependentTemporalSeed;
}

#endif
