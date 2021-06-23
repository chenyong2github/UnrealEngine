// Copyright Epic Games, Inc. All Rights Reserved.

#include "DeferredShadingRenderer.h"
#include "SceneRenderTargets.h"

#if RHI_RAYTRACING

#include "ClearQuad.h"
#include "SceneRendering.h"
#include "RenderGraphBuilder.h"
#include "RenderTargetPool.h"
#include "RHIResources.h"
#include "UniformBuffer.h"
#include "VisualizeTexture.h"
#include "RayGenShaderUtils.h"
#include "RaytracingOptions.h"
#include "BuiltInRayTracingShaders.h"
#include "RayTracing/RayTracingMaterialHitShaders.h"
#include "Containers/DynamicRHIResourceArray.h"
#include "SceneTextureParameters.h"

static float GRayTracingMaxNormalBias = 0.1f;
static FAutoConsoleVariableRef CVarRayTracingNormalBias(
	TEXT("r.RayTracing.NormalBias"),
	GRayTracingMaxNormalBias,
	TEXT("Sets the max. normal bias used for offseting the ray start position along the normal (default = 0.1, i.e., 1mm)")
);

static int32 GRayTracingShadowsEnableMaterials = 1;
static FAutoConsoleVariableRef CVarRayTracingShadowsEnableMaterials(
	TEXT("r.RayTracing.Shadows.EnableMaterials"),
	GRayTracingShadowsEnableMaterials,
	TEXT("Enables material shader binding for shadow rays. If this is disabled, then a default trivial shader is used. (default = 1)")
);

static TAutoConsoleVariable<int32> CVarRayTracingShadowsEnableTwoSidedGeometry(
	TEXT("r.RayTracing.Shadows.EnableTwoSidedGeometry"),
	1,
	TEXT("Enables two-sided geometry when tracing shadow rays (default = 1)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarRayTracingTransmissionSamplingDistanceCulling(
	TEXT("r.RayTracing.Transmission.TransmissionSamplingDistanceCulling"),
	1,
	TEXT("Enables visibility testing to cull transmission sampling distance (default = 1)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarRayTracingTransmissionSamplingTechnique(
	TEXT("r.RayTracing.Transmission.SamplingTechnique"),
	1,
	TEXT("0: Uses constant tracking of an infinite homogeneous medium\n")
	TEXT("1: Uses constant tracking of a finite homogeneous medium whose extent is determined by transmission sampling distance (default)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarRayTracingTransmissionRejectionSamplingTrials(
	TEXT("r.RayTracing.Transmission.RejectionSamplingTrials"),
	0,
	TEXT("Determines the number of rejection-sampling trials (default = 0)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarRayTracingShadowsEnableHairVoxel(
	TEXT("r.RayTracing.Shadows.EnableHairVoxel"),
	1,
	TEXT("Enables use of hair voxel data for tracing shadow (default = 1)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarRayTracingShadowsLODTransitionStart(
	TEXT("r.RayTracing.Shadows.LODTransitionStart"),
	4000.0, // 40 m
	TEXT("The start of an LOD transition range (default = 4000)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarRayTracingShadowsLODTransitionEnd(
	TEXT("r.RayTracing.Shadows.LODTransitionEnd"),
	5000.0f, // 50 m
	TEXT("The end of an LOD transition range (default = 5000)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int32> CVarRayTracingShadowsAcceptFirstHit(
	TEXT("r.RayTracing.Shadows.AcceptFirstHit"),
	0,
	TEXT("Whether to allow shadow rays to terminate early, on first intersected primitive. This may result in worse denoising quality in some cases. (default = 0)"),
	ECVF_RenderThreadSafe
);


bool EnableRayTracingShadowTwoSidedGeometry()
{
	return CVarRayTracingShadowsEnableTwoSidedGeometry.GetValueOnRenderThread() != 0;
}

class FOcclusionRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FOcclusionRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FOcclusionRGS, FGlobalShader)

	class FLightTypeDim : SHADER_PERMUTATION_INT("LIGHT_TYPE", LightType_MAX);
	class FDenoiserOutputDim : SHADER_PERMUTATION_INT("DIM_DENOISER_OUTPUT", 3);
	class FEnableTwoSidedGeometryDim : SHADER_PERMUTATION_BOOL("ENABLE_TWO_SIDED_GEOMETRY");
	class FEnableMultipleSamplesPerPixel : SHADER_PERMUTATION_BOOL("ENABLE_MULTIPLE_SAMPLES_PER_PIXEL");
	class FHairLighting : SHADER_PERMUTATION_INT("USE_HAIR_LIGHTING", 2);
	class FEnableTransmissionDim : SHADER_PERMUTATION_INT("ENABLE_TRANSMISSION", 2);

	using FPermutationDomain = TShaderPermutationDomain<FLightTypeDim, FDenoiserOutputDim, FEnableTwoSidedGeometryDim, FHairLighting, FEnableMultipleSamplesPerPixel, FEnableTransmissionDim>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_DYNAMIC_CLOSEST_HIT_SHADER"), 0);
		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_DYNAMIC_ANY_HIT_SHADER"),     1);
		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_DYNAMIC_MISS_SHADER"),        0);

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FLightTypeDim>() == LightType_Directional &&
			PermutationVector.Get<FHairLighting>() == 0)
		{
			OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_COHERENT_RAYS"), 1);
		}
		else
		{
			OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_COHERENT_RAYS"), 0);
		}
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)
		SHADER_PARAMETER(uint32, SamplesPerPixel)
		SHADER_PARAMETER(float, NormalBias)
		SHADER_PARAMETER(uint32, LightingChannelMask)
		SHADER_PARAMETER(FIntRect, LightScissor)
		SHADER_PARAMETER(FIntPoint, PixelOffset)
		SHADER_PARAMETER(uint32, bUseHairVoxel)
		SHADER_PARAMETER(float, TraceDistance)
		SHADER_PARAMETER(float, LODTransitionStart)
		SHADER_PARAMETER(float, LODTransitionEnd)
		SHADER_PARAMETER(uint32, bTransmissionSamplingDistanceCulling)
		SHADER_PARAMETER(uint32, TransmissionSamplingTechnique)
		SHADER_PARAMETER(uint32, RejectionSamplingTrials)
		SHADER_PARAMETER(uint32, bAcceptFirstHit)

		SHADER_PARAMETER_STRUCT(FLightShaderParameters, Light)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneLightingChannelParameters, SceneLightingChannels)

		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairCategorizationTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HairLightChannelMaskTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SSProfilesTexture)
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWOcclusionMaskUAV)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, RWRayDistanceUAV)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, RWSubPixelOcclusionMaskUAV)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FVirtualVoxelParameters, VirtualVoxel)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FOcclusionRGS, "/Engine/Private/RayTracing/RayTracingOcclusionRGS.usf", "OcclusionRGS", SF_RayGen);

float GetRaytracingMaxNormalBias()
{
	return FMath::Max(0.01f, GRayTracingMaxNormalBias);
}

void FDeferredShadingSceneRenderer::PrepareRayTracingShadows(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	// Declare all RayGen shaders that require material closest hit shaders to be bound

	static auto CVarRayTracingShadows = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.Shadows"));

	const bool bRayTracingShadows = ShouldRenderRayTracingEffect(CVarRayTracingShadows != nullptr && CVarRayTracingShadows->GetInt() > 0);

	if (!bRayTracingShadows)
	{
		return;
	}

	const IScreenSpaceDenoiser::EShadowRequirements DenoiserRequirements[] =
	{
		IScreenSpaceDenoiser::EShadowRequirements::Bailout,
		IScreenSpaceDenoiser::EShadowRequirements::PenumbraAndAvgOccluder,
		IScreenSpaceDenoiser::EShadowRequirements::PenumbraAndClosestOccluder,
	};

	for (int32 MultiSPP = 0; MultiSPP < 2; ++MultiSPP)
	{
		for (int32 EnableTransmissionDim = 0; EnableTransmissionDim < 2; ++EnableTransmissionDim)
		{
			for (int32 HairLighting = 0; HairLighting < 2; ++HairLighting)
			{
				for (int32 LightType = 0; LightType < LightType_MAX; ++LightType)
				{
					for (IScreenSpaceDenoiser::EShadowRequirements DenoiserRequirement : DenoiserRequirements)
					{
						FOcclusionRGS::FPermutationDomain PermutationVector;
						PermutationVector.Set<FOcclusionRGS::FLightTypeDim>(LightType);
						PermutationVector.Set<FOcclusionRGS::FDenoiserOutputDim>((int32)DenoiserRequirement);
						PermutationVector.Set<FOcclusionRGS::FEnableTwoSidedGeometryDim>(CVarRayTracingShadowsEnableTwoSidedGeometry.GetValueOnRenderThread() != 0);
						PermutationVector.Set<FOcclusionRGS::FHairLighting>(HairLighting);
						PermutationVector.Set<FOcclusionRGS::FEnableMultipleSamplesPerPixel>(MultiSPP != 0);
						PermutationVector.Set<FOcclusionRGS::FEnableTransmissionDim>(EnableTransmissionDim);

						TShaderMapRef<FOcclusionRGS> RayGenerationShader(View.ShaderMap, PermutationVector);
						OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
					}
				}
			}
		}
	}
}

#endif // RHI_RAYTRACING

void FDeferredShadingSceneRenderer::RenderRayTracingShadows(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const FLightSceneInfo& LightSceneInfo,
	const IScreenSpaceDenoiser::FShadowRayTracingConfig& RayTracingConfig,
	const IScreenSpaceDenoiser::EShadowRequirements DenoiserRequirements,
	const FHairStrandsOcclusionResources* HairResources,
	FRDGTextureRef LightingChannelsTexture,
	FRDGTextureUAV* OutShadowMaskUAV,
	FRDGTextureUAV* OutRayHitDistanceUAV,
	FRDGTextureUAV* SubPixelRayTracingShadowMaskUAV)
#if RHI_RAYTRACING
{
	FLightSceneProxy* LightSceneProxy = LightSceneInfo.Proxy;
	check(LightSceneProxy);

	FIntRect ScissorRect = View.ViewRect;
	FIntPoint PixelOffset = { 0, 0 };

	//#UE-95409: Implement support for scissor in multi-view 
	const bool bClipDispatch = View.Family->Views.Num() == 1;

	if (LightSceneProxy->GetScissorRect(ScissorRect, View, View.ViewRect))
	{
		// Account for scissor being defined on the whole frame viewport while the trace is only on the view subrect
		// ScissorRect.Min = ScissorRect.Min;
		// ScissorRect.Max = ScissorRect.Max;
	}
	else
	{
		ScissorRect = View.ViewRect;
	}

	if (bClipDispatch)
	{
		PixelOffset = ScissorRect.Min;
	}

	// Ray generation pass for shadow occlusion.
	{
		const bool bUseHairLighting =
			HairResources != nullptr &&
			HairResources->CategorizationTexture != nullptr &&
			HairResources->LightChannelMaskTexture != nullptr &&
			HairResources->VoxelResources != nullptr;

		FOcclusionRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FOcclusionRGS::FParameters>();
		PassParameters->RWOcclusionMaskUAV = OutShadowMaskUAV;
		PassParameters->RWRayDistanceUAV = OutRayHitDistanceUAV;
		PassParameters->RWSubPixelOcclusionMaskUAV = SubPixelRayTracingShadowMaskUAV;
		PassParameters->SamplesPerPixel = RayTracingConfig.RayCountPerPixel;
		PassParameters->NormalBias = GetRaytracingMaxNormalBias();
		PassParameters->LightingChannelMask = LightSceneProxy->GetLightingChannelMask();
		LightSceneProxy->GetLightShaderParameters(PassParameters->Light);
		PassParameters->Light.SourceRadius *= LightSceneProxy->GetShadowSourceAngleFactor();

		PassParameters->TraceDistance = LightSceneProxy->GetTraceDistance();
		PassParameters->LODTransitionStart = CVarRayTracingShadowsLODTransitionStart.GetValueOnRenderThread();
		PassParameters->LODTransitionEnd = CVarRayTracingShadowsLODTransitionEnd.GetValueOnRenderThread();
		PassParameters->bAcceptFirstHit = CVarRayTracingShadowsAcceptFirstHit.GetValueOnRenderThread();
		PassParameters->TLAS = View.RayTracingScene.RayTracingSceneRHI->GetShaderResourceView();
		PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
		PassParameters->SceneTextures = SceneTextures;
		PassParameters->SceneLightingChannels = GetSceneLightingChannelParameters(GraphBuilder, LightingChannelsTexture);
		PassParameters->LightScissor = ScissorRect;
		PassParameters->PixelOffset = PixelOffset;
		PassParameters->SSProfilesTexture = GraphBuilder.RegisterExternalTexture(View.RayTracingSubSurfaceProfileTexture);
		PassParameters->bTransmissionSamplingDistanceCulling = CVarRayTracingTransmissionSamplingDistanceCulling.GetValueOnRenderThread();
		PassParameters->TransmissionSamplingTechnique = CVarRayTracingTransmissionSamplingTechnique.GetValueOnRenderThread();
		PassParameters->RejectionSamplingTrials = CVarRayTracingTransmissionRejectionSamplingTrials.GetValueOnRenderThread();
		if (bUseHairLighting)
		{
			const bool bUseHairVoxel = CVarRayTracingShadowsEnableHairVoxel.GetValueOnRenderThread() > 0;
			PassParameters->bUseHairVoxel = (HairResources->bUseHairVoxel && bUseHairVoxel) ? 1 : 0;
			PassParameters->HairCategorizationTexture = HairResources->CategorizationTexture;
			PassParameters->HairLightChannelMaskTexture = HairResources->LightChannelMaskTexture;
			PassParameters->VirtualVoxel = HairResources->VoxelResources->UniformBuffer;

			if (ShaderDrawDebug::IsShaderDrawDebugEnabled(View))
			{
				ShaderDrawDebug::SetParameters(GraphBuilder, View.ShaderDrawData, PassParameters->ShaderDrawParameters);
			}
		}
		FOcclusionRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FOcclusionRGS::FLightTypeDim>(LightSceneProxy->GetLightType());
		if (DenoiserRequirements == IScreenSpaceDenoiser::EShadowRequirements::PenumbraAndAvgOccluder)
		{
			PermutationVector.Set<FOcclusionRGS::FDenoiserOutputDim>(1);
		}
		else if (DenoiserRequirements == IScreenSpaceDenoiser::EShadowRequirements::PenumbraAndClosestOccluder)
		{
			PermutationVector.Set<FOcclusionRGS::FDenoiserOutputDim>(2);
		}
		else
		{
			PermutationVector.Set<FOcclusionRGS::FDenoiserOutputDim>(0);
		}
		PermutationVector.Set<FOcclusionRGS::FEnableTwoSidedGeometryDim>(CVarRayTracingShadowsEnableTwoSidedGeometry.GetValueOnRenderThread() != 0);
		PermutationVector.Set<FOcclusionRGS::FHairLighting>(bUseHairLighting? 1 : 0);

		PermutationVector.Set<FOcclusionRGS::FEnableMultipleSamplesPerPixel>(RayTracingConfig.RayCountPerPixel > 1);
		PermutationVector.Set<FOcclusionRGS::FEnableTransmissionDim>(LightSceneProxy->Transmission() ? 1 : 0);

		TShaderMapRef<FOcclusionRGS> RayGenerationShader(GetGlobalShaderMap(FeatureLevel), PermutationVector);

		ClearUnusedGraphResources(RayGenerationShader, PassParameters);

		FIntPoint Resolution(View.ViewRect.Width(), View.ViewRect.Height());

		if (bClipDispatch)
		{
			Resolution = ScissorRect.Size();
		}

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("RayTracedShadow (spp=%d) %dx%d", RayTracingConfig.RayCountPerPixel, Resolution.X, Resolution.Y),
			PassParameters,
			ERDGPassFlags::Compute,
			[this, &View, RayGenerationShader, PassParameters, Resolution](FRHICommandList& RHICmdList)
		{
			FRayTracingShaderBindingsWriter GlobalResources;
			SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);

			FRHIRayTracingScene* RayTracingSceneRHI = View.RayTracingScene.RayTracingSceneRHI;

			if (GRayTracingShadowsEnableMaterials)
			{
				RHICmdList.RayTraceDispatch(View.RayTracingMaterialPipeline, RayGenerationShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, Resolution.X, Resolution.Y);
			}
			else
			{
				FRayTracingPipelineStateInitializer Initializer;

				Initializer.MaxPayloadSizeInBytes = 64; // sizeof(FPackedMaterialClosestHitPayload)

				FRHIRayTracingShader* RayGenShaderTable[] = { RayGenerationShader.GetRayTracingShader() };
				Initializer.SetRayGenShaderTable(RayGenShaderTable);

				FRHIRayTracingShader* HitGroupTable[] = { View.ShaderMap->GetShader<FOpaqueShadowHitGroup>().GetRayTracingShader() };
				Initializer.SetHitGroupTable(HitGroupTable);
				Initializer.bAllowHitGroupIndexing = false; // Use the same hit shader for all geometry in the scene by disabling SBT indexing.

				FRayTracingPipelineState* Pipeline = PipelineStateCache::GetAndOrCreateRayTracingPipelineState(RHICmdList, Initializer);

				RHICmdList.RayTraceDispatch(Pipeline, RayGenerationShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources, Resolution.X, Resolution.Y);
			}
		});
	}
}
#else // !RHI_RAYTRACING
{
	unimplemented();
}
#endif

void FDeferredShadingSceneRenderer::RenderDitheredLODFadingOutMask(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneDepthTexture)
{
	auto* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
	PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(SceneDepthTexture, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilWrite);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("DitheredLODFadingOutMask"),
		PassParameters,
		ERDGPassFlags::Raster,
		[this, &View](FRHICommandListImmediate& RHICmdList)
	{
		RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
		RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
		View.ParallelMeshDrawCommandPasses[EMeshPass::DitheredLODFadingOutMaskPass].DispatchDraw(nullptr, RHICmdList);
	});
}
