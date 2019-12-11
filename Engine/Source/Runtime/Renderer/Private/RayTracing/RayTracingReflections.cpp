// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RendererPrivate.h"
#include "GlobalShader.h"
#include "DeferredShadingRenderer.h"

#if RHI_RAYTRACING

#include "ClearQuad.h"
#include "SceneRendering.h"
#include "SceneRenderTargets.h"
#include "RenderTargetPool.h"
#include "RHIResources.h"
#include "UniformBuffer.h"
#include "VisualizeTexture.h"
#include "LightRendering.h"
#include "SystemTextures.h"
#include "RayTracing/RaytracingOptions.h"
#include "RayTracing/RayTracingDeferredMaterials.h"
#include "RayTracing/RayTracingLighting.h"
#include "RenderGraph.h"
#include "RayTracing/RayTracingLighting.h"
#include "SceneTextureParameters.h"

static float GRayTracingReflectionsMaxRoughness = -1;
static FAutoConsoleVariableRef CVarRayTracingReflectionsMaxRoughness(
	TEXT("r.RayTracing.Reflections.MaxRoughness"),
	GRayTracingReflectionsMaxRoughness,
	TEXT("Sets the maximum roughness until which ray tracing reflections will be visible (default = -1 (max roughness driven by postprocessing volume))")
);

static int32 GRayTracingReflectionsMaxBounces = -1;
static FAutoConsoleVariableRef CVarRayTracingReflectionsMaxBounces(
	TEXT("r.RayTracing.Reflections.MaxBounces"),
	GRayTracingReflectionsMaxBounces,
	TEXT("Sets the maximum number of ray tracing reflection bounces (default = -1 (max bounces driven by postprocessing volume))")
);

static int32 GRayTracingReflectionsEmissiveAndIndirectLighting = 1;
static FAutoConsoleVariableRef CVarRayTracingReflectionsEmissiveAndIndirectLighting(
	TEXT("r.RayTracing.Reflections.EmissiveAndIndirectLighting"),
	GRayTracingReflectionsEmissiveAndIndirectLighting,
	TEXT("Enables ray tracing reflections emissive and indirect lighting (default = 1)")
);

static int32 GRayTracingReflectionsDirectLighting = 1;
static FAutoConsoleVariableRef CVarRayTracingReflectionsDirectLighting(
	TEXT("r.RayTracing.Reflections.DirectLighting"),
	GRayTracingReflectionsDirectLighting,
	TEXT("Enables ray tracing reflections direct lighting (default = 1)")
);

static int32 GRayTracingReflectionsShadows = -1;
static FAutoConsoleVariableRef CVarRayTracingReflectionsShadows(
	TEXT("r.RayTracing.Reflections.Shadows"),
	GRayTracingReflectionsShadows,
	TEXT("Enables shadows in ray tracing reflections)")
	TEXT(" -1: Shadows driven by postprocessing volume (default)")
	TEXT(" 0: Shadows disabled ")
	TEXT(" 1: Hard shadows")
	TEXT(" 2: Soft area shadows")
);

static int32 GRayTracingReflectionsTranslucency = -1;
static FAutoConsoleVariableRef CVarRayTracingReflectionsTranslucency(
	TEXT("r.RayTracing.Reflections.Translucency"),
	GRayTracingReflectionsTranslucency,
	TEXT("Translucent objects visible in ray tracing reflections)")
	TEXT(" -1: Driven by postprocessing volume (default)")
	TEXT(" 0: Translucent objects not visible")
	TEXT(" 1: Translucent objects visible")
);

static int32 GRayTracingReflectionsCaptures = 0;
static FAutoConsoleVariableRef CVarRayTracingReflectionsCaptures(
	TEXT("r.RayTracing.Reflections.ReflectionCaptures"),
	GRayTracingReflectionsCaptures,
	TEXT("Enables ray tracing reflections to use reflection captures as the last bounce reflection. Particularly usefull for metals in reflection. (default = 0)")
);

static float GRayTracingReflectionsMinRayDistance = -1;
static FAutoConsoleVariableRef CVarRayTracingReflectionsMinRayDistance(
	TEXT("r.RayTracing.Reflections.MinRayDistance"),
	GRayTracingReflectionsMinRayDistance,
	TEXT("Sets the minimum ray distance for ray traced reflection rays. Actual reflection ray length is computed as Lerp(MaxRayDistance, MinRayDistance, Roughness), i.e. reflection rays become shorter when traced from rougher surfaces. (default = -1 (infinite rays))")
);

static float GRayTracingReflectionsMaxRayDistance = -1;
static FAutoConsoleVariableRef CVarRayTracingReflectionsMaxRayDistance(
	TEXT("r.RayTracing.Reflections.MaxRayDistance"),
	GRayTracingReflectionsMaxRayDistance,
	TEXT("Sets the maximum ray distance for ray traced reflection rays. When ray shortening is used, skybox will not be sampled in RT reflection pass and will be composited later, together with local reflection captures. Negative values turn off this optimization. (default = -1 (infinite rays))")
);

static TAutoConsoleVariable<int32> CVarRayTracingReflectionsSortMaterials(
	TEXT("r.RayTracing.Reflections.SortMaterials"),
	1,
	TEXT("Sets whether refected materials will be sorted before shading\n")
	TEXT("0: Disabled\n ")
	TEXT("1: Enabled, using Trace->Sort->Trace (Default)\n"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRayTracingReflectionsHybrid(
	TEXT("r.RayTracing.Reflections.Hybrid"),
	0,
	TEXT("Sets whether screen space reflections should be used when possible (experimental).\n")
	TEXT("Forces material sorting and single ray bounce.\n")
	TEXT("0: Disabled (Default)\n ")
	TEXT("1: Enabled\n"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRayTracingReflectionsSortTileSize(
	TEXT("r.RayTracing.Reflections.SortTileSize"),
	64,
	TEXT("Size of pixel tiles for sorted reflections\n")
	TEXT("  Default 64\n"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRayTracingReflectionsRenderTileSize(
	TEXT("r.RayTracing.Reflections.RenderTileSize"),
	0,
	TEXT("Render ray traced reflections in NxN pixel tiles, where each tile is submitted as separate GPU command buffer, allowing high quality rendering without triggering timeout detection (default = 0, tiling disabled)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRayTracingReflectionsSortSize(
	TEXT("r.RayTracing.Reflections.SortSize"),
	5,
	TEXT("Size of horizon for material ID sort\n")
	TEXT("0: Disabled\n")
	TEXT("1: 256 Elements\n")
	TEXT("2: 512 Elements\n")
	TEXT("3: 1024 Elements\n")
	TEXT("4: 2048 Elements\n")
	TEXT("5: 4096 Elements (Default)\n"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRayTracingReflectionsTestPathRoughness(
	TEXT("r.RayTracing.Reflections.TestPathRoughness"),
	1,
	TEXT("Accumulate roughness along path and test accumulated roughness against MaxRoughness before launching the next bounce (default 1)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarRayTracingReflectionsMinClearCoatLevel(
	TEXT("r.RayTracing.Reflections.MinClearCoatLevel"),
	0.01f,
	TEXT("Minimum level at which to apply clear coat shading (default 0.01)\n")
	TEXT(" Note: causes some variation in height fog due to using the bottom layer path"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRayTracingReflectionsMaxUnderCoatBounces(
	TEXT("r.RayTracing.Reflections.MaxUnderCoatBounces"),
	0,
	TEXT("How many bounces to apply ray traced reflections to the undercoat layer. Extra bounces will use reflection probes. (default 0, always use probes)"),
	ECVF_RenderThreadSafe);



// ESamplePhase
enum class ESamplePhase
{
	Monlithic = 0, //single pass for all samples
	Init = 1,      // First sample of the set initialize the accumulators
	Accum = 2,     // Intermediate sample, accumulate results
	Resolve = 3    // Final sample, apply weighting
};

static const int32 GReflectionLightCountMaximum = 64;

class FRayTracingReflectionsRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRayTracingReflectionsRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FRayTracingReflectionsRGS, FGlobalShader)

	class FDenoiserOutput : SHADER_PERMUTATION_BOOL("DIM_DENOISER_OUTPUT");

	class FDeferredMaterialMode : SHADER_PERMUTATION_ENUM_CLASS("DIM_DEFERRED_MATERIAL_MODE", EDeferredMaterialMode);
	class FHybrid : SHADER_PERMUTATION_BOOL("DIM_HYBRID");
	class FEnableTwoSidedGeometryForShadowDim : SHADER_PERMUTATION_BOOL("ENABLE_TWO_SIDED_GEOMETRY");

	using FPermutationDomain = TShaderPermutationDomain<FDenoiserOutput, FDeferredMaterialMode, FHybrid, FEnableTwoSidedGeometryForShadowDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(int32, SamplesPerPixel)
		SHADER_PARAMETER(int32, MaxBounces)
		SHADER_PARAMETER(int32, HeightFog)
		SHADER_PARAMETER(int32, UseReflectionCaptures)
		SHADER_PARAMETER(int32, ShouldDoDirectLighting)
		SHADER_PARAMETER(int32, ReflectedShadowsType)
		SHADER_PARAMETER(int32, ShouldDoEmissiveAndIndirectLighting)
		SHADER_PARAMETER(int32, UpscaleFactor)
		SHADER_PARAMETER(int32, SortTileSize)
		SHADER_PARAMETER(FIntPoint, RayTracingResolution)
		SHADER_PARAMETER(FIntPoint, TileAlignedResolution)
		SHADER_PARAMETER(float, ReflectionMinRayDistance)
		SHADER_PARAMETER(float, ReflectionMaxRayDistance)
		SHADER_PARAMETER(float, ReflectionMaxRoughness)
		SHADER_PARAMETER(float, ReflectionMaxNormalBias)
		SHADER_PARAMETER(int32, TestPathRoughness)
		SHADER_PARAMETER(float, MinClearCoatLevel)
		SHADER_PARAMETER(int32, MaxUnderCoatBounces)
		SHADER_PARAMETER(uint32, RenderTileOffsetX)
		SHADER_PARAMETER(uint32, RenderTileOffsetY)
		SHADER_PARAMETER(uint32, EnableTranslucency)
		SHADER_PARAMETER(int32, SampleMode)
		SHADER_PARAMETER(int32, SampleOffset)

		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColor)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SSProfilesTexture)
		SHADER_PARAMETER_SRV(StructuredBuffer<FRTLightingData>, LightDataBuffer)

		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureSamplerParameters, SceneTextureSamplers)

		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_STRUCT_REF(FRaytracingLightDataPacked, LightDataPacked)
		SHADER_PARAMETER_STRUCT_REF(FReflectionUniformParameters, ReflectionStruct)
		SHADER_PARAMETER_STRUCT_REF(FFogUniformParameters, FogUniformParameters)
		SHADER_PARAMETER_STRUCT_REF(FReflectionCaptureShaderData, ReflectionCapture)
		SHADER_PARAMETER_STRUCT_REF(FForwardLightData, Forward)

		// Optional indirection buffer used for sorted materials
		SHADER_PARAMETER_RDG_BUFFER_UAV(StructuredBuffer<FDeferredMaterialPayload>, MaterialBuffer)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, ColorOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RayHitDistanceOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RayImaginaryDepthOutput)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FRayTracingReflectionsRGS, "/Engine/Private/RayTracing/RayTracingReflections.usf", "RayTracingReflectionsRGS", SF_RayGen);

void FDeferredShadingSceneRenderer::PrepareRayTracingReflections(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	// Declare all RayGen shaders that require material closest hit shaders to be bound

	const bool bHybridReflections = CVarRayTracingReflectionsHybrid.GetValueOnRenderThread() != 0;
	const bool bIsMultiviewSecondary = View.ViewRect.Min.X > 0 || View.ViewRect.Min.Y > 0;
	const bool bSortMaterials = (bHybridReflections || CVarRayTracingReflectionsSortMaterials.GetValueOnRenderThread() != 0) && !bIsMultiviewSecondary;

	if (bSortMaterials)
	{
		{
			FRayTracingReflectionsRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FRayTracingReflectionsRGS::FEnableTwoSidedGeometryForShadowDim>(EnableRayTracingShadowTwoSidedGeometry());
			PermutationVector.Set<FRayTracingReflectionsRGS::FDeferredMaterialMode>(EDeferredMaterialMode::Gather);
			PermutationVector.Set<FRayTracingReflectionsRGS::FHybrid>(bHybridReflections);
			auto RayGenShader = View.ShaderMap->GetShader<FRayTracingReflectionsRGS>(PermutationVector);
			OutRayGenShaders.Add(RayGenShader->GetRayTracingShader());
		}
		
		{
			FRayTracingReflectionsRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FRayTracingReflectionsRGS::FEnableTwoSidedGeometryForShadowDim>(EnableRayTracingShadowTwoSidedGeometry());
			PermutationVector.Set<FRayTracingReflectionsRGS::FDeferredMaterialMode>(EDeferredMaterialMode::Shade);
			PermutationVector.Set<FRayTracingReflectionsRGS::FHybrid>(bHybridReflections);
			auto RayGenShader = View.ShaderMap->GetShader<FRayTracingReflectionsRGS>(PermutationVector);
			OutRayGenShaders.Add(RayGenShader->GetRayTracingShader());
		}
	}
	else
	{
		{
			FRayTracingReflectionsRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FRayTracingReflectionsRGS::FEnableTwoSidedGeometryForShadowDim>(EnableRayTracingShadowTwoSidedGeometry());
			PermutationVector.Set<FRayTracingReflectionsRGS::FDeferredMaterialMode>(EDeferredMaterialMode::None);
			auto RayGenShader = View.ShaderMap->GetShader<FRayTracingReflectionsRGS>(PermutationVector);
			OutRayGenShaders.Add(RayGenShader->GetRayTracingShader());
		}
	}
}

#endif // RHI_RAYTRACING

void FDeferredShadingSceneRenderer::RenderRayTracingReflections(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	int32 SamplePerPixel,
	int32 HeightFog,
	float ResolutionFraction,
	IScreenSpaceDenoiser::FReflectionsInputs* OutDenoiserInputs)
#if RHI_RAYTRACING
{
	const uint32 SortTileSize = CVarRayTracingReflectionsSortTileSize.GetValueOnRenderThread();

	const bool bHybridReflections = CVarRayTracingReflectionsHybrid.GetValueOnRenderThread() != 0;
	const bool bIsMultiviewSecondary = View.ViewRect.Min.X > 0 || View.ViewRect.Min.Y > 0;
	const bool bSortMaterials = (bHybridReflections || CVarRayTracingReflectionsSortMaterials.GetValueOnRenderThread() != 0) && !bIsMultiviewSecondary;
	const uint32 EnableTranslucency = GRayTracingReflectionsTranslucency > -1 ? (uint32)GRayTracingReflectionsTranslucency : (uint32) View.FinalPostProcessSettings.RayTracingReflectionsTranslucency;

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);

	int32 UpscaleFactor = int32(1.0f / ResolutionFraction);
	ensure(ResolutionFraction == 1.0 / UpscaleFactor);
	ensureMsgf(FComputeShaderUtils::kGolden2DGroupSize % UpscaleFactor == 0, TEXT("Reflection ray tracing will have uv misalignement."));
	FIntPoint RayTracingResolution = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), UpscaleFactor);

	{
		FRDGTextureDesc Desc = FRDGTextureDesc::Create2DDesc(
			SceneTextures.SceneDepthBuffer->Desc.Extent / UpscaleFactor,
			PF_FloatRGBA,
			FClearValueBinding::None,
			/* InFlags = */ TexCreate_None,
			/* InTargetableFlags = */ TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV,
			/* bInForceSeparateTargetAndShaderResource = */ false);

		OutDenoiserInputs->Color = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingReflections"));

		Desc.Format = PF_R16F;
		OutDenoiserInputs->RayHitDistance = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingReflectionsHitDistance"));
		OutDenoiserInputs->RayImaginaryDepth = GraphBuilder.CreateTexture(Desc, TEXT("RayTracingReflectionsImaginaryDepth"));
	}

	// When deferred materials are used, we need to dispatch the reflection shader twice:
	// - First pass gathers reflected ray hit data and sorts it by hit shader ID.
	// - Second pass re-traces the reflected ray and performs full shading.
	// When deferred materials are not used, everything is done in a single pass.
	const uint32 NumPasses = bSortMaterials ? 2 : 1;
	const EDeferredMaterialMode DeferredMaterialModes[2] = 
	{
		bSortMaterials ? EDeferredMaterialMode::Gather : EDeferredMaterialMode::None,
		bSortMaterials ? EDeferredMaterialMode::Shade : EDeferredMaterialMode::None,
	};

	FRDGBufferRef DeferredMaterialBuffer = nullptr;

	FIntPoint TileAlignedResolution = RayTracingResolution;
	if (SortTileSize)
	{
		TileAlignedResolution = FIntPoint::DivideAndRoundUp(RayTracingResolution, SortTileSize) * SortTileSize;
	}

	const uint32 DeferredMaterialBufferNumElements = TileAlignedResolution.X * TileAlignedResolution.Y;

	FRayTracingReflectionsRGS::FParameters CommonParameters;

	CommonParameters.SamplesPerPixel = SamplePerPixel;
	CommonParameters.MaxBounces = GRayTracingReflectionsMaxBounces > -1? GRayTracingReflectionsMaxBounces : View.FinalPostProcessSettings.RayTracingReflectionsMaxBounces;
	CommonParameters.HeightFog = HeightFog;
	CommonParameters.UseReflectionCaptures = GRayTracingReflectionsCaptures;
	CommonParameters.ShouldDoDirectLighting = GRayTracingReflectionsDirectLighting;
	CommonParameters.ReflectedShadowsType = GRayTracingReflectionsShadows > -1 ? GRayTracingReflectionsShadows : (int32)View.FinalPostProcessSettings.RayTracingReflectionsShadows;
	CommonParameters.ShouldDoEmissiveAndIndirectLighting = GRayTracingReflectionsEmissiveAndIndirectLighting;
	CommonParameters.UpscaleFactor = UpscaleFactor;
	CommonParameters.ReflectionMinRayDistance = FMath::Min(GRayTracingReflectionsMinRayDistance, GRayTracingReflectionsMaxRayDistance);
	CommonParameters.ReflectionMaxRayDistance = GRayTracingReflectionsMaxRayDistance;
	CommonParameters.ReflectionMaxRoughness = FMath::Clamp(GRayTracingReflectionsMaxRoughness >= 0 ? GRayTracingReflectionsMaxRoughness : View.FinalPostProcessSettings.RayTracingReflectionsMaxRoughness, 0.01f, 1.0f);
	CommonParameters.ReflectionMaxNormalBias = GetRaytracingMaxNormalBias();
	CommonParameters.RayTracingResolution = RayTracingResolution;
	CommonParameters.TileAlignedResolution = TileAlignedResolution;
	CommonParameters.TestPathRoughness = CVarRayTracingReflectionsTestPathRoughness.GetValueOnRenderThread();
	CommonParameters.MinClearCoatLevel = CVarRayTracingReflectionsMinClearCoatLevel.GetValueOnRenderThread();
	CommonParameters.MaxUnderCoatBounces = CVarRayTracingReflectionsMaxUnderCoatBounces.GetValueOnRenderThread();
	CommonParameters.RenderTileOffsetX = 0;
	CommonParameters.RenderTileOffsetY = 0;
	CommonParameters.EnableTranslucency = EnableTranslucency; 
	CommonParameters.SampleMode = (int32)ESamplePhase::Monlithic;

	CommonParameters.TLAS = View.RayTracingScene.RayTracingSceneRHI->GetShaderResourceView();
	CommonParameters.ViewUniformBuffer = View.ViewUniformBuffer;

	FStructuredBufferRHIRef LightingDataBuffer;
	CommonParameters.LightDataPacked = CreateLightDataPackedUniformBuffer(Scene->Lights, View, EUniformBufferUsage::UniformBuffer_SingleFrame, LightingDataBuffer);
	CommonParameters.LightDataBuffer = RHICreateShaderResourceView(LightingDataBuffer);

	CommonParameters.SceneTextures = SceneTextures;
	SetupSceneTextureSamplers(&CommonParameters.SceneTextureSamplers);

	// Hybrid reflection path samples lit scene color texture instead of performing a ray trace.
	CommonParameters.SceneColor = GraphBuilder.RegisterExternalTexture(bHybridReflections ? SceneContext.GetSceneColor() : GSystemTextures.BlackDummy);

	// TODO: should be converted to RDG
	TRefCountPtr<IPooledRenderTarget> SubsurfaceProfileRT((IPooledRenderTarget*)GetSubsufaceProfileTexture_RT(GraphBuilder.RHICmdList));
	if (!SubsurfaceProfileRT)
	{
		SubsurfaceProfileRT = GSystemTextures.BlackDummy;
	}
	CommonParameters.SSProfilesTexture = GraphBuilder.RegisterExternalTexture(SubsurfaceProfileRT);

	CommonParameters.ReflectionStruct = CreateReflectionUniformBuffer(View, EUniformBufferUsage::UniformBuffer_SingleFrame);
	CommonParameters.FogUniformParameters = CreateFogUniformBuffer(View, EUniformBufferUsage::UniformBuffer_SingleFrame);
	CommonParameters.ColorOutput = GraphBuilder.CreateUAV(OutDenoiserInputs->Color);
	CommonParameters.RayHitDistanceOutput = GraphBuilder.CreateUAV(OutDenoiserInputs->RayHitDistance);
	CommonParameters.RayImaginaryDepthOutput = GraphBuilder.CreateUAV(OutDenoiserInputs->RayImaginaryDepth);
	CommonParameters.SortTileSize = SortTileSize;
	CommonParameters.ReflectionCapture = View.ReflectionCaptureUniformBuffer;
	CommonParameters.Forward = View.ForwardLightingResources->ForwardLightDataUniformBuffer;

	for (int32 SamplePassIndex = 0; SamplePassIndex < SamplePerPixel; SamplePassIndex++)
	{
		if (SamplePerPixel > 1)
		{
			CommonParameters.SampleMode = (int32)ESamplePhase::Accum;
			CommonParameters.SampleMode = SamplePassIndex == 0 ? (int32)ESamplePhase::Init : CommonParameters.SampleMode;
			CommonParameters.SampleMode = SamplePassIndex == (SamplePerPixel - 1) ? (int32)ESamplePhase::Resolve : CommonParameters.SampleMode;
		}
		CommonParameters.SampleOffset = SamplePassIndex;

		for (uint32 PassIndex = 0; PassIndex < NumPasses; ++PassIndex)
		{
			FRayTracingReflectionsRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRayTracingReflectionsRGS::FParameters>();
			*PassParameters = CommonParameters;

			const EDeferredMaterialMode DeferredMaterialMode = DeferredMaterialModes[PassIndex];

			if (DeferredMaterialMode != EDeferredMaterialMode::None)
			{
				if (DeferredMaterialMode == EDeferredMaterialMode::Gather)
				{
					FRDGBufferDesc Desc = FRDGBufferDesc::CreateStructuredDesc(sizeof(FDeferredMaterialPayload), DeferredMaterialBufferNumElements);
					DeferredMaterialBuffer = GraphBuilder.CreateBuffer(Desc, TEXT("RayTracingReflectionsMaterialBuffer"));
				}

				PassParameters->MaterialBuffer = GraphBuilder.CreateUAV(DeferredMaterialBuffer);
			}

			FRayTracingReflectionsRGS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FRayTracingReflectionsRGS::FDeferredMaterialMode>(DeferredMaterialMode);
			PermutationVector.Set<FRayTracingReflectionsRGS::FHybrid>(bHybridReflections);
			PermutationVector.Set<FRayTracingReflectionsRGS::FEnableTwoSidedGeometryForShadowDim>(EnableRayTracingShadowTwoSidedGeometry());
			auto RayGenShader = View.ShaderMap->GetShader<FRayTracingReflectionsRGS>(PermutationVector);

			ClearUnusedGraphResources(RayGenShader, PassParameters);

			if (DeferredMaterialMode == EDeferredMaterialMode::Gather)
			{
				GraphBuilder.AddPass(
					RDG_EVENT_NAME("ReflectionRayTracingGatherMaterials %dx%d", TileAlignedResolution.X, TileAlignedResolution.Y),
					PassParameters,
					ERDGPassFlags::Compute,
					[PassParameters, this, &View, RayGenShader, TileAlignedResolution](FRHICommandList& RHICmdList)
				{
					FRayTracingPipelineState* Pipeline = BindRayTracingDeferredMaterialGatherPipeline(RHICmdList, View, RayGenShader->GetRayTracingShader());

					FRayTracingShaderBindingsWriter GlobalResources;
					SetShaderParameters(GlobalResources, RayGenShader, *PassParameters);

					FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;
					RHICmdList.RayTraceDispatch(Pipeline, RayGenShader->GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, TileAlignedResolution.X, TileAlignedResolution.Y);
				});

				// A material sorting pass
				const uint32 SortSize = CVarRayTracingReflectionsSortSize.GetValueOnRenderThread();
				if (SortSize)
				{
					SortDeferredMaterials(GraphBuilder, View, SortSize, DeferredMaterialBufferNumElements, DeferredMaterialBuffer);
				}
			}
			else
			{
				// Add optional tiling behavior to avoid TDR events in expensive passes
				int32 RenderTileSize = CVarRayTracingReflectionsRenderTileSize.GetValueOnRenderThread();
				if (NumPasses > 1 || RenderTileSize <= 0)
				{
					GraphBuilder.AddPass(
						RDG_EVENT_NAME("ReflectionRayTracing(spp=%d) %dx%d", SamplePerPixel, RayTracingResolution.X, RayTracingResolution.Y),
						PassParameters,
						ERDGPassFlags::Compute,
						[PassParameters, this, &View, RayGenShader, RayTracingResolution, DeferredMaterialBufferNumElements, DeferredMaterialMode](FRHICommandList& RHICmdList)
					{
						FRayTracingShaderBindingsWriter GlobalResources;
						SetShaderParameters(GlobalResources, RayGenShader, *PassParameters);

						FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;

						if (DeferredMaterialMode == EDeferredMaterialMode::Shade)
						{
							// Shading pass for sorted materials uses 1D dispatch over all elements in the material buffer.
							// This can be reduced to the number of output pixels if sorting pass guarantees that all invalid entries are moved to the end.
							RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenShader->GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, DeferredMaterialBufferNumElements, 1);
						}
						else // EDeferredMaterialMode::None
						{
							RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenShader->GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, RayTracingResolution.X, RayTracingResolution.Y);
						}
					});
				}
				else
				{
					RenderTileSize = FMath::Max(RenderTileSize, 32);
					int32 NumTilesX = FMath::DivideAndRoundUp(RayTracingResolution.X, RenderTileSize);
					int32 NumTilesY = FMath::DivideAndRoundUp(RayTracingResolution.Y, RenderTileSize);
					for (int32 Y = 0; Y < NumTilesY; ++Y)
					{
						FRayTracingReflectionsRGS::FParameters* TilePassParameters = PassParameters;
						for (int32 X = 0; X < NumTilesX; ++X)
						{
							if (X > 0 || Y > 0)
							{
								TilePassParameters = GraphBuilder.AllocParameters<FRayTracingReflectionsRGS::FParameters>();
								*TilePassParameters = *PassParameters;
								TilePassParameters->RenderTileOffsetX = X * RenderTileSize;
								TilePassParameters->RenderTileOffsetY = Y * RenderTileSize;
							}

							int32 DispatchSizeX = FMath::Min<int32>(RenderTileSize, RayTracingResolution.X - TilePassParameters->RenderTileOffsetX);
							int32 DispatchSizeY = FMath::Min<int32>(RenderTileSize, RayTracingResolution.Y - TilePassParameters->RenderTileOffsetY);
							GraphBuilder.AddPass(
								RDG_EVENT_NAME("ReflectionRayTracing(spp=%d) %dx%d", SamplePerPixel, DispatchSizeX, DispatchSizeY),
								TilePassParameters,
								ERDGPassFlags::Compute,
								[TilePassParameters, this, &View, RayGenShader, DispatchSizeX, DispatchSizeY, DeferredMaterialBufferNumElements, DeferredMaterialMode](FRHICommandList& RHICmdList)
							{
								FRayTracingShaderBindingsWriter GlobalResources;
								SetShaderParameters(GlobalResources, RayGenShader, *TilePassParameters);

								FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;
								RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenShader->GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, DispatchSizeX, DispatchSizeY);
							});
						}
					}
				}
			}
		}
	}

}
#else // !RHI_RAYTRACING
{
	check(0);
}
#endif
