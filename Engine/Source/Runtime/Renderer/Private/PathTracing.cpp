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

TAutoConsoleVariable<int32> CVarPathTracingAdaptiveSampling(
	TEXT("r.PathTracing.AdaptiveSampling"),
	0,
	TEXT("Toggles the use of adaptive sampling\n")
	TEXT("0: off (default)\n")
	TEXT("1: on\n"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarPathTracingAdaptiveSamplingMinimumSamplesPerPixel(
	TEXT("r.PathTracing.AdaptiveSampling.MinimumSamplesPerPixel"),
	16,
	TEXT("Changes the minimum samples-per-pixel before applying adaptive sampling (default=16)\n"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarPathTracingVarianceMapRebuildFrequency(
	TEXT("r.PathTracing.VarianceMapRebuildFrequency"),
	16,
	TEXT("Sets the variance map rebuild frequency (default = every 16 iterations)"),
	ECVF_RenderThreadSafe
);

TAutoConsoleVariable<int32> CVarPathTracingRayCountFrequency(
	TEXT("r.PathTracing.RayCountFrequency"),
	128,
	TEXT("Sets the ray count computation frequency (default = every 128 iterations)"),
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

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FPathTracingData, "PathTracingData");
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FPathTracingLightData, "SceneLightsData");
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FPathTracingAdaptiveSamplingData, "AdaptiveSamplingData");

// This function prepares the portion of shader arguments that may involve invalidating the path traced state
static bool PrepareShaderArgs(const FViewInfo& View, FPathTracingData& PathTracingData, FPathTracingAdaptiveSamplingData& AdaptiveSamplingData) {
	int32 PathTracingMaxBounces = CVarPathTracingMaxBounces.GetValueOnRenderThread();
	if (PathTracingMaxBounces < 0)
		PathTracingMaxBounces = View.FinalPostProcessSettings.PathTracingMaxBounces;
	PathTracingData.MaxBounces = PathTracingMaxBounces;
	PathTracingData.MaxNormalBias = GetRaytracingMaxNormalBias();
	PathTracingData.MISMode = CVarPathTracingMISMode.GetValueOnRenderThread();
	PathTracingData.VisibleLights = CVarPathTracingVisibleLights.GetValueOnRenderThread();
	PathTracingData.MaxPathIntensity = CVarPathTracingMaxPathIntensity.GetValueOnRenderThread();

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

	AdaptiveSamplingData.UseAdaptiveSampling = CVarPathTracingAdaptiveSampling.GetValueOnRenderThread();

	// Changing Adaptive sampling mode requires starting over
	static uint32 PreviousUseAdaptiveSampling = AdaptiveSamplingData.UseAdaptiveSampling;
	if (PreviousUseAdaptiveSampling != AdaptiveSamplingData.UseAdaptiveSampling)
	{
		NeedInvalidation = true;
		PreviousUseAdaptiveSampling = AdaptiveSamplingData.UseAdaptiveSampling;
	}

	// the rest of PathTracingData and AdaptiveSamplingData is filled in by SetParameters below
	return NeedInvalidation;
}

class FPathTracingRG : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPathTracingRG, Global);

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
	}

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	FPathTracingRG() {}
	~FPathTracingRG() {}

	FPathTracingRG(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		TLASParameter.Bind(Initializer.ParameterMap, TEXT("TLAS"));
		ViewParameter.Bind(Initializer.ParameterMap, TEXT("View"));
		SceneLightsParameters.Bind(Initializer.ParameterMap, TEXT("SceneLightsData"));
		PathTracingParameters.Bind(Initializer.ParameterMap, TEXT("PathTracingData"));
		SkyLightParameters.Bind(Initializer.ParameterMap, TEXT("SkyLight"));
		check(SkyLightParameters.IsBound());
		AdaptiveSamplingParameters.Bind(Initializer.ParameterMap, TEXT("AdaptiveSamplingData"));

		// Output
		RadianceRT.Bind(Initializer.ParameterMap, TEXT("RadianceRT"));
	}

	void SetParameters(
		FScene* Scene,
		const FViewInfo& View,
		FRayTracingShaderBindingsWriter& GlobalResources,
		const FRayTracingScene& RayTracingScene,
		FRHIUniformBuffer* ViewUniformBuffer,
		FRHIUniformBuffer* SceneTexturesUniformBuffer,
		// Shader arguments (expected to be filled in by PrepareShaderArgs ahead of time)
		FPathTracingData& PathTracingData,
		FPathTracingAdaptiveSamplingData& AdaptiveSamplingData,
		// Light buffer
		const TSparseArray<FLightSceneInfoCompact>& Lights,
		// Adaptive sampling
		uint32 Iteration,
		uint32 FrameIndependentTemporalSeed,
		FIntVector VarianceDimensions,
		const FRWBuffer& VarianceMipTree,
		const FIntVector& TileOffset,
		// Output
		FRHIUnorderedAccessView* RadianceUAV)
	{

		GlobalResources.Set(TLASParameter, RayTracingScene.RayTracingSceneRHI->GetShaderResourceView());
		GlobalResources.Set(ViewParameter, ViewUniformBuffer);

		// Path tracing data
		{
			PathTracingData.TileOffset = TileOffset;

			FUniformBufferRHIRef PathTracingDataUniformBuffer = RHICreateUniformBuffer(&PathTracingData, FPathTracingData::StaticStructMetadata.GetLayout(), EUniformBufferUsage::UniformBuffer_SingleDraw);
			GlobalResources.Set(PathTracingParameters, PathTracingDataUniformBuffer);
		}

		// Sky light
		FSkyLightData SkyLightData;
		bool IsSkyLightValid = false;
		{
			IsSkyLightValid = SetupSkyLightParameters(*Scene, &SkyLightData);

			FUniformBufferRHIRef SkyLightUniformBuffer = RHICreateUniformBuffer(&SkyLightData, FSkyLightData::StaticStructMetadata.GetLayout(), EUniformBufferUsage::UniformBuffer_SingleDraw);
			GlobalResources.Set(SkyLightParameters, SkyLightUniformBuffer);
		}

		// Lights
		{
			FPathTracingLightData LightData;
			LightData.Count = 0;

			// Prepend SkyLight to light buffer since it is not part of the regular light list
			if (IsSkyLightValid)
			{
				uint32 SkyLightIndex = 0;
				uint8 SkyLightLightingChannelMask = 0xFF;
				LightData.Type[SkyLightIndex] = 0;
				LightData.Color[SkyLightIndex] = FVector(SkyLightData.Color);
				LightData.Flags[SkyLightIndex] = SkyLightData.bTransmission & 0x01;
				LightData.Flags[SkyLightIndex] |= (SkyLightLightingChannelMask & 0x7) << 1;
				LightData.Count++;
			}

			for (auto Light : Lights)
			{
				if (LightData.Count >= RAY_TRACING_LIGHT_COUNT_MAXIMUM) break;

				if (Light.LightSceneInfo->Proxy->HasStaticLighting() && Light.LightSceneInfo->IsPrecomputedLightingValid()) continue;

				FLightShaderParameters LightParameters;
				Light.LightSceneInfo->Proxy->GetLightShaderParameters(LightParameters);
				uint32 Transmission = Light.LightSceneInfo->Proxy->Transmission();
				uint8 LightingChannelMask = Light.LightSceneInfo->Proxy->GetLightingChannelMask();
				LightData.Flags[LightData.Count] = Transmission & 0x01;
				LightData.Flags[LightData.Count] |= (LightingChannelMask & 0x7) << 1;

				ELightComponentType LightComponentType = (ELightComponentType)Light.LightSceneInfo->Proxy->GetLightType();
				switch (LightComponentType)
				{
					case LightType_Directional:
					{
						LightData.Type[LightData.Count] = 2;
						LightData.Normal[LightData.Count] = LightParameters.Direction;
						LightData.Color[LightData.Count] = LightParameters.Color;
						LightData.Dimensions[LightData.Count] = FVector(0.0f, 0.0f, LightParameters.SourceRadius);
						LightData.Attenuation[LightData.Count] = 1.0 / LightParameters.InvRadius;
						break;
					}
					case LightType_Rect:
					{
						LightData.Type[LightData.Count] = 3;
						LightData.Position[LightData.Count] = LightParameters.Position;
						LightData.Normal[LightData.Count] = -LightParameters.Direction;
						LightData.dPdu[LightData.Count] = FVector::CrossProduct(LightParameters.Tangent, LightParameters.Direction);
						LightData.dPdv[LightData.Count] = LightParameters.Tangent;
						LightData.Color[LightData.Count] = LightParameters.Color;
						LightData.Dimensions[LightData.Count] = FVector(2.0f * LightParameters.SourceRadius, 2.0f * LightParameters.SourceLength, 0.0f);
						LightData.Attenuation[LightData.Count] = 1.0 / LightParameters.InvRadius;
						LightData.RectLightBarnCosAngle[LightData.Count] = LightParameters.RectLightBarnCosAngle;
						LightData.RectLightBarnLength[LightData.Count] = LightParameters.RectLightBarnLength;
						break;
					}
					case LightType_Spot:
					{
						LightData.Type[LightData.Count] = 4;
						LightData.Position[LightData.Count] = LightParameters.Position;
						LightData.Normal[LightData.Count] = -LightParameters.Direction;
						LightData.Color[LightData.Count] = LightParameters.Color;
						LightData.Dimensions[LightData.Count] = FVector(LightParameters.SpotAngles, LightParameters.SourceRadius);
						LightData.Attenuation[LightData.Count] = 1.0 / LightParameters.InvRadius;
						break;
					}
					case LightType_Point:
					{
						LightData.Type[LightData.Count] = 1;
						LightData.Position[LightData.Count] = LightParameters.Position;
						LightData.Color[LightData.Count] = LightParameters.Color;
						LightData.Dimensions[LightData.Count] = FVector(0.0, 0.0, LightParameters.SourceRadius);
						LightData.Attenuation[LightData.Count] = 1.0 / LightParameters.InvRadius;
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

			FUniformBufferRHIRef SceneLightsUniformBuffer = RHICreateUniformBuffer(&LightData, FPathTracingLightData::StaticStructMetadata.GetLayout(), EUniformBufferUsage::UniformBuffer_SingleDraw);
			GlobalResources.Set(SceneLightsParameters, SceneLightsUniformBuffer);
		}

		// Adaptive sampling
		{
			if (CVarPathTracingFrameIndependentTemporalSeed.GetValueOnRenderThread() == 0)
			{
				// Count samples from 0 for deterministic results
				AdaptiveSamplingData.TemporalSeed = Iteration;
			}
			else
			{
				// Count samples from an ever-increasing counter to avoid screen-door effect
				AdaptiveSamplingData.TemporalSeed = FrameIndependentTemporalSeed;
			}

			AdaptiveSamplingData.Iteration = Iteration;

			if (VarianceMipTree.NumBytes > 0)
			{
				AdaptiveSamplingData.VarianceDimensions = VarianceDimensions;
				AdaptiveSamplingData.VarianceMipTree = VarianceMipTree.SRV;
				AdaptiveSamplingData.MinimumSamplesPerPixel = CVarPathTracingAdaptiveSamplingMinimumSamplesPerPixel.GetValueOnRenderThread();
			}
			else
			{
				AdaptiveSamplingData.UseAdaptiveSampling = 0;
				AdaptiveSamplingData.VarianceDimensions = FIntVector(1, 1, 1);
				AdaptiveSamplingData.VarianceMipTree = RHICreateShaderResourceView(GBlackTexture->TextureRHI->GetTexture2D(), 0);
				AdaptiveSamplingData.MinimumSamplesPerPixel = CVarPathTracingAdaptiveSamplingMinimumSamplesPerPixel.GetValueOnRenderThread();
			}

			FUniformBufferRHIRef AdaptiveSamplingDataUniformBuffer = RHICreateUniformBuffer(&AdaptiveSamplingData, FPathTracingAdaptiveSamplingData::StaticStructMetadata.GetLayout(), EUniformBufferUsage::UniformBuffer_SingleDraw);
			GlobalResources.Set(AdaptiveSamplingParameters, AdaptiveSamplingDataUniformBuffer);
		}

		// Accumulated output
		{
			GlobalResources.Set(RadianceRT, RadianceUAV);
		}
	}

	/*bool Serialize(FArchive& Ar)
	{
		bool bShaderHasOutdatedParameters = FGlobalShader::Serialize(Ar);
		Ar << TLASParameter;
		Ar << ViewParameter;
		Ar << PathTracingParameters;
		Ar << SceneLightsParameters;
		Ar << SkyLightParameters;
		Ar << AdaptiveSamplingParameters;
		// Output
		Ar << RadianceRT;
		Ar << SampleCountRT;
		Ar << PixelPositionRT;
		Ar << RayCountPerPixelRT;

		return bShaderHasOutdatedParameters;
	}*/
	
	LAYOUT_FIELD(FShaderResourceParameter, TLASParameter);   // RaytracingAccelerationStructure
	LAYOUT_FIELD(FShaderUniformBufferParameter, ViewParameter);
	LAYOUT_FIELD(FShaderUniformBufferParameter, PathTracingParameters);
	LAYOUT_FIELD(FShaderUniformBufferParameter, SceneLightsParameters);
	LAYOUT_FIELD(FShaderUniformBufferParameter, SkyLightParameters);
	LAYOUT_FIELD(FShaderUniformBufferParameter, AdaptiveSamplingParameters);

	// Output parameters
	LAYOUT_FIELD(FShaderResourceParameter, RadianceRT);
};
IMPLEMENT_SHADER_TYPE(, FPathTracingRG, TEXT("/Engine/Private/PathTracing/PathTracing.usf"), TEXT("PathTracingMainRG"), SF_RayGen);

DECLARE_GPU_STAT_NAMED(Stat_GPU_PathTracing, TEXT("Path Tracing"));
DECLARE_GPU_STAT_NAMED(Stat_GPU_PathTracingBuildSkyLightCDF, TEXT("Path Tracing: Build Sky Light CDF"));
DECLARE_GPU_STAT_NAMED(Stat_GPU_PathTracingBuildVarianceMipTree, TEXT("Path Tracing: Build Variance Map Tree"));

class FPathTracingCompositorPS : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FPathTracingCompositorPS, Global);

	static bool ShouldCache(EShaderPlatform Platform)
	{
		return IsFeatureLevelSupported(Platform, ERHIFeatureLevel::SM5);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	FPathTracingCompositorPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		Iteration.Bind(Initializer.ParameterMap, TEXT("Iteration"));
		RadianceTexture.Bind(Initializer.ParameterMap, TEXT("RadianceTexture"));
	}

	FPathTracingCompositorPS()
	{
	}

	template<typename TRHICommandList>
	void SetParameters(
		TRHICommandList& RHICmdList,
		const FViewInfo& View,
		uint32 IterationValue,
		FRHITexture* RadianceRT)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);
		SetShaderValue(RHICmdList, ShaderRHI, Iteration, IterationValue);
		SetTextureParameter(RHICmdList, ShaderRHI, RadianceTexture, RadianceRT);
	}

public:
	LAYOUT_FIELD(FShaderParameter, Iteration);
	LAYOUT_FIELD(FShaderResourceParameter, RadianceTexture);
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

BEGIN_SHADER_PARAMETER_STRUCT(FPathTracingPassParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

void FSceneViewState::PathTracingInvalidate()
{
	PathTracingRadianceRT.SafeRelease();
	VarianceMipTreeDimensions = FIntVector(0);
	TotalRayCount = 0;
	PathTracingSPP = 0;
}

void FDeferredShadingSceneRenderer::RenderPathTracing(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	FRDGTextureRef SceneColorOutputTexture)
{
	RDG_GPU_STAT_SCOPE(GraphBuilder, Stat_GPU_PathTracing);

	FPathTracingPassParameters* PassParameters = GraphBuilder.AllocParameters<FPathTracingPassParameters>();
	PassParameters->SceneTextures = SceneTexturesUniformBuffer;

	// NOTE: The SkipRenderPass flag means this doesn't get bound. It just ensures that it's put in the RTV state.
	PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColorOutputTexture, ERenderTargetLoadAction::ELoad);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("PathTracing"),
		PassParameters,
		ERDGPassFlags::Compute | ERDGPassFlags::Raster | ERDGPassFlags::SkipRenderPass | ERDGPassFlags::UntrackedAccess,
		[this, &View, SceneTexturesUniformBuffer, SceneColorOutputTexture](FRHICommandListImmediate& RHICmdList)
	{
		auto ViewSize = View.ViewRect.Size();
		FSceneViewState* ViewState = (FSceneViewState*)View.State;

		FPathTracingData PathTracingData;
		FPathTracingAdaptiveSamplingData AdaptiveSamplingData;

		bool bArgsChanged = PrepareShaderArgs(View, PathTracingData, AdaptiveSamplingData);

        // Get current value of MaxSPP and reset render if it has changed
		int32 SamplesPerPixelCVar = CVarPathTracingSamplesPerPixel.GetValueOnRenderThread();
		uint32 MaxSPP = SamplesPerPixelCVar > -1 ? SamplesPerPixelCVar : View.FinalPostProcessSettings.PathTracingSamplesPerPixel;
		static uint32 PreviousMaxSPP = MaxSPP;
		if (PreviousMaxSPP != MaxSPP)
		{
			PreviousMaxSPP = MaxSPP;
			bArgsChanged = true;
		}

		// If the scene has changed in some way (camera move, object movement, etc ...)
		// we must invalidate the viewstate to start over from scratch
		if (bArgsChanged || ViewState->PathTracingRect != View.ViewRect)
		{
			ViewState->PathTracingInvalidate();
			ViewState->PathTracingRect = View.ViewRect;
		}

		bool NeedsMoreRays = ViewState->PathTracingSPP < MaxSPP;
		
		// Construct render targets for compositing
		TRefCountPtr<IPooledRenderTarget> RadianceRT;

		if (ViewState->PathTracingRadianceRT)
		{
			// already got a buffer
			RadianceRT = ViewState->PathTracingRadianceRT;
		}
		else
		{
			FPooledRenderTargetDesc Desc = Translate(SceneColorOutputTexture->Desc);
			Desc.Flags &= ~(TexCreate_FastVRAM | TexCreate_Transient);
			Desc.Format = PF_FloatRGBA;
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, RadianceRT, TEXT("RadianceRT"));
			ViewState->PathTracingRadianceRT = RadianceRT;
		}

		auto RayGenShader = GetGlobalShaderMap(FeatureLevel)->GetShader<FPathTracingRG>();

		FRayTracingShaderBindingsWriter GlobalResources;

		FRHIUniformBuffer* SceneTexturesUniformBufferRHI = SceneTexturesUniformBuffer->GetRHI();

		FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;

		int32 GPUCount = CVarPathTracingGPUCount.GetValueOnRenderThread();
		uint32 MainGPUIndex = 0; // Default GPU for rendering

		float DPIScale = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(View.CursorPos.X, View.CursorPos.Y);
		const int32 bWiperMode = CVarPathTracingWiperMode.GetValueOnRenderThread();
		const int32 WipeOffsetX = bWiperMode ? View.CursorPos.X / DPIScale : 0;

		bool bDoMGPUPathTracing = GNumExplicitGPUsForRendering > 1 && GPUCount > 1;

		if (bDoMGPUPathTracing && NeedsMoreRays)
		{
			//#dxr-todo: Set minimum tile size for mGPU
			int32 TileSizeX = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), GPUCount).X;

			{
				// FIXME: cross-GPU fences aren't currently supported in the transition API, we need to figure it out.
#if 0
				FRHIUnorderedAccessView* UAVs[] = {
						RadianceRT->GetRenderTargetItem().UAV
				};

				// Begin mGPU fence
				FRHIGPUMask GPUMask = FRHIGPUMask::All();
				FComputeFenceRHIRef Fence = RHICmdList.CreateComputeFence(TEXT("PathTracingRayGen_Fence_Begin"));

				RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToGfx, UAVs, UE_ARRAY_COUNT(UAVs), Fence);

				for (uint32 GPUIndex : GPUMask)
				{
					if (GPUIndex == MainGPUIndex)
						continue;

					SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::FromIndex(GPUIndex));

					RHICmdList.WaitComputeFence(Fence);
				}
#endif
			}

			for (int32 GPUIndex = 0; GPUIndex < GPUCount; ++GPUIndex)
			{
				SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::FromIndex(GPUIndex));

				FIntVector TileOffset;
				TileOffset.X = GPUIndex * TileSizeX;
				TileOffset.Y = 0; //vertical tiles only 

				RayGenShader->SetParameters(
					Scene,
					View,
					GlobalResources,
					View.RayTracingScene,
					View.ViewUniformBuffer,
					SceneTexturesUniformBufferRHI,
					PathTracingData,
					AdaptiveSamplingData,
					Scene->Lights,
					ViewState->PathTracingSPP,
					ViewState->PathTracingFrameIndependentTemporalSeed,
					ViewState->VarianceMipTreeDimensions,
					*ViewState->VarianceMipTree,
					TileOffset,
					RadianceRT->GetRenderTargetItem().UAV
				);

				int32 DispatchSizeX = FMath::Min<int32>(TileSizeX, View.ViewRect.Size().X - TileOffset.X);
				int32 DispatchSizeY = View.ViewRect.Size().Y;

				RHICmdList.RayTraceDispatch(
					View.RayTracingMaterialPipeline,
					RayGenShader.GetRayTracingShader(),
					RayTracingSceneRHI, GlobalResources,
					DispatchSizeX, DispatchSizeY
				);

				FIntRect GPURect;
				GPURect.Min.X = TileOffset.X;
				GPURect.Min.Y = TileOffset.Y;
				GPURect.Max.X = TileOffset.X + DispatchSizeX;
				GPURect.Max.Y = TileOffset.Y + DispatchSizeY;

				if (GPUIndex > 0)
				{
					RHICmdList.TransferTexture(RadianceRT->GetRenderTargetItem().TargetableTexture->GetTexture2D(), GPURect, GPUIndex, 0, true);
				}
			}

			// FIXME: cross-GPU fences aren't currently supported in the transition API, we need to figure it out.
#if 0
			{
				// End mGPU fence
				FRHIGPUMask GPUMask = FRHIGPUMask::All();
				FComputeFenceRHIRef Fence = RHICmdList.CreateComputeFence(TEXT("PathTracingRayGen_Fence_End"));

				RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToGfx, UAVs, UE_ARRAY_COUNT(UAVs), Fence);

				for (uint32 GPUIndex : GPUMask)
				{
					if (GPUIndex == MainGPUIndex)
						continue;

					SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::FromIndex(MainGPUIndex));

					RHICmdList.WaitComputeFence(Fence);
				}
			}
#endif
		}
		else if (NeedsMoreRays)
		{
			FIntVector TileOffset;
			TileOffset.X = bWiperMode > 0 ? WipeOffsetX : 0;
			TileOffset.Y = 0;

			RayGenShader->SetParameters(
				Scene,
				View,
				GlobalResources,
				View.RayTracingScene,
				View.ViewUniformBuffer,
				SceneTexturesUniformBufferRHI,
				PathTracingData,
				AdaptiveSamplingData,
				Scene->Lights,
				ViewState->PathTracingSPP,
				ViewState->PathTracingFrameIndependentTemporalSeed,
				ViewState->VarianceMipTreeDimensions,
				*ViewState->VarianceMipTree,
				TileOffset,
				RadianceRT->GetRenderTargetItem().UAV
			);

			int32 ViewWidth = View.ViewRect.Size().X;

			int32 DispatchSizeX = bWiperMode > 0 ? ViewWidth - WipeOffsetX : ViewWidth;
			int32 DispatchSizeY = View.ViewRect.Size().Y;

			RHICmdList.RayTraceDispatch(
				View.RayTracingMaterialPipeline,
				RayGenShader.GetRayTracingShader(),
				RayTracingSceneRHI, GlobalResources,
				DispatchSizeX, DispatchSizeY
			);
		}

		// Save RayTracingIndirect for compositing
		RHICmdList.CopyToResolveTarget(RadianceRT->GetRenderTargetItem().TargetableTexture, RadianceRT->GetRenderTargetItem().ShaderResourceTexture, FResolveParams());

		// Single GPU for launching compute shaders
		SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::FromIndex(MainGPUIndex));

#if 0
		if ((ViewState->PathTracingSPP % CVarPathTracingRayCountFrequency.GetValueOnRenderThread() == 0))
		{
			ComputeRayCount(RHICmdList, View, RayCountPerPixelRT->GetRenderTargetItem().ShaderResourceTexture);
		}
#endif
		// Run compositing engine
		const auto ShaderMap = GetGlobalShaderMap(FeatureLevel);

		TShaderMapRef<FPostProcessVS> VertexShader(ShaderMap);
		TShaderMapRef<FPathTracingCompositorPS> PixelShader(ShaderMap);
		FRHITexture* RenderTargets[1] =
		{
			SceneColorOutputTexture->GetRHI()
		};
		FRHIRenderPassInfo RenderPassInfo(UE_ARRAY_COUNT(RenderTargets), RenderTargets, ERenderTargetActions::Load_Store);
		RHICmdList.BeginRenderPass(RenderPassInfo, TEXT("PathTracing"));

		// DEBUG: Inspect render target in isolation
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		{
			RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

			PixelShader->SetParameters(RHICmdList, View, AdaptiveSamplingData.Iteration, RadianceRT->GetRenderTargetItem().ShaderResourceTexture);

			int32 DispatchSizeX = View.ViewRect.Size().X;

			DrawRectangle(
				RHICmdList,
				WipeOffsetX, 0,
				DispatchSizeX, View.ViewRect.Height(),
				WipeOffsetX, View.ViewRect.Min.Y,
				DispatchSizeX, View.ViewRect.Height(),
				FIntPoint(View.ViewRect.Width(), View.ViewRect.Height()),
				SceneColorOutputTexture->Desc.Extent,
				VertexShader);
		}
		RHICmdList.EndRenderPass();

		// Bump counters for next frame
		++ViewState->PathTracingSPP;
		++ViewState->PathTracingFrameIndependentTemporalSeed;
	});
}
#endif