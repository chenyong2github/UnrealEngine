// Copyright Epic Games, Inc. All Rights Reserved.

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "VolumeLighting.h"
#include "LumenSceneUtils.h"
#include "DistanceFieldLightingShared.h"
#include "VirtualShadowMaps/VirtualShadowMapClipmap.h"
#include "VolumetricCloudRendering.h"

#if RHI_RAYTRACING

#include "RayTracing/RayTracingDeferredMaterials.h"
#include "RayTracing/RaytracingOptions.h"
#include "RayTracing/RayTracingLighting.h"
#include "LumenHardwareRayTracingCommon.h"

// Console variables
static TAutoConsoleVariable<int32> CVarLumenSceneDirectLightingHardwareRayTracing(
	TEXT("r.LumenScene.DirectLighting.HardwareRayTracing"),
	1,
	TEXT("Enables hardware ray tracing for Lumen direct lighting (Default = 1)"),
	ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<int> CVarLumenSceneDirectLightingHardwareRayTracingGroupCount(
	TEXT("r.LumenScene.DirectLighting.HardwareRayTracing.GroupCount"),
	8192,
	TEXT("Determines the dispatch group count\n"),
	ECVF_RenderThreadSafe
);

#endif // RHI_RAYTRACING

namespace Lumen
{
	bool UseHardwareRayTracedDirectLighting()
	{
#if RHI_RAYTRACING
		return IsRayTracingEnabled()
			&& Lumen::UseHardwareRayTracing()
			&& (CVarLumenSceneDirectLightingHardwareRayTracing.GetValueOnRenderThread() != 0);
#else
		return false;
#endif
	}

	static const uint32 ShadowTraceTileSize2D = Lumen::MinCardResolution;
	static const uint32 ShadowTraceTileSize1D = ShadowTraceTileSize2D * ShadowTraceTileSize2D;
} // namespace Lumen

#if RHI_RAYTRACING

struct FLightBatchCullingParameters
{
	FVector4 InfluenceSphere;
	FVector LightPosition;
	FVector LightDirection;
	float CosConeAngle;
	float SinConeAngle;
	float LightRadius;
	uint32 LightType;
	uint32 bCastsDynamicShadow;
};

struct FLightBatchCullingParametersPacked
{
	FVector4 InfluenceSphere;
	FVector4 LightPositionAndCosConeAngle;
	FVector4 LightDirectionAndSinConeAngle;
	float LightRadius;
	uint32 Flags;
	uint32 Padding[2];

	FLightBatchCullingParametersPacked() {}

	FLightBatchCullingParametersPacked(const FLightBatchCullingParameters& LightBatchCullingParameters)
	{
		InfluenceSphere = LightBatchCullingParameters.InfluenceSphere;
		LightPositionAndCosConeAngle = FVector4(LightBatchCullingParameters.LightPosition, LightBatchCullingParameters.CosConeAngle);
		LightDirectionAndSinConeAngle = FVector4(LightBatchCullingParameters.LightDirection, LightBatchCullingParameters.SinConeAngle);
		LightRadius = LightBatchCullingParameters.LightRadius;
		Flags = (LightBatchCullingParameters.bCastsDynamicShadow ? 0x1 : 0x0) | ((LightBatchCullingParameters.LightType & 0x03) << 1);
	}
};

class FClearShadowTraceTileAllocatorBatchedCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearShadowTraceTileAllocatorBatchedCS)
	SHADER_USE_PARAMETER_STRUCT(FClearShadowTraceTileAllocatorBatchedCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Output
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCardPageAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWShadowTraceTileAllocator)
	END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 16);
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearShadowTraceTileAllocatorBatchedCS, "/Engine/Private/Lumen/LumenSceneDirectLightingHardwareRayTracing.usf", "ClearShadowTraceTileAllocatorCS", SF_Compute);

class FCreateShadowTraceTilesBatchedCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCreateShadowTraceTilesBatchedCS)
	SHADER_USE_PARAMETER_STRUCT(FCreateShadowTraceTilesBatchedCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		// Input
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenCardScene, LumenCardScene)
		SHADER_PARAMETER(FIntPoint, AtlasSize)

		// Lights
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FLightBatchCullingParametersPacked>, LightBatchCullingParameters)
		SHADER_PARAMETER(int32, LightsNum)

		// Frequency update parameters
		SHADER_PARAMETER(uint32, NumCardPagesToRenderIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CardPagesToRenderIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CardPagesToRenderHashMap)
		SHADER_PARAMETER(uint32, OperateOnCardPagesToRender)

		SHADER_PARAMETER(uint32, FrameId)
		SHADER_PARAMETER(float, CardLightingUpdateFrequencyScale)
		SHADER_PARAMETER(uint32, CardLightingUpdateMinFrequency)

		// Output
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWShadowTraceTileAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint4>, RWShadowTraceTileData)

		// Dispatch arguments
		RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
		OutEnvironment.SetDefine(TEXT("NUM_CARD_TILES_TO_RENDER_HASH_MAP_BUCKET_UINT32"), FLumenCardRenderer::NumCardPagesToRenderHashMapBucketUInt32);
		OutEnvironment.SetDefine(TEXT("OPERATE_ON_CARD_TILES_MODE"), static_cast<int>(ECullCardsMode::OperateOnSceneForceUpdateForCardPagesToRender));
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}

	static int32 GetGroupSize() { return 64; }

	static uint32 GetRaysPerTile() { return Lumen::ShadowTraceTileSize1D; }
};

IMPLEMENT_GLOBAL_SHADER(FCreateShadowTraceTilesBatchedCS, "/Engine/Private/Lumen/LumenSceneDirectLightingHardwareRayTracing.usf", "CreateShadowTraceTilesCS", SF_Compute);

class FLumenDirectLightingHardwareRayTracingBatchedRGS : public FLumenHardwareRayTracingRGS
{
	DECLARE_GLOBAL_SHADER(FLumenDirectLightingHardwareRayTracingBatchedRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FLumenDirectLightingHardwareRayTracingBatchedRGS, FLumenHardwareRayTracingRGS)

	class FUseVirtualShadowMapsDim : SHADER_PERMUTATION_BOOL("DIM_USE_VIRTUAL_SHADOW_MAPS");
	using FPermutationDomain = TShaderPermutationDomain<FUseVirtualShadowMapsDim>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenHardwareRayTracingRGS::FSharedParameters, SharedParameters)
		SHADER_PARAMETER_STRUCT(FLightShaderParameters, Light)
		SHADER_PARAMETER_STRUCT_REF(FDeferredLightUniformStruct, DeferredLightUniforms)

		// Lights
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FLightBatchCullingParametersPacked>, LightBatchCullingParameters)
		SHADER_PARAMETER(int32, LightsNum)

		// Constants
		SHADER_PARAMETER(float, PullbackBias)
		SHADER_PARAMETER(int, MaxTranslucentSkipCount)
		SHADER_PARAMETER(FIntPoint, AtlasSize)
		SHADER_PARAMETER(uint32, GroupCount)
		SHADER_PARAMETER(float, MaxTraceDistance)

		SHADER_PARAMETER(float, SurfaceBias)
		SHADER_PARAMETER(float, SlopeScaledSurfaceBias)

		// Shadow-specific bindings
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ShadowTraceTileAllocator)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint4>, ShadowTraceTileData)

		SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualShadowMapSamplingParameters, VirtualShadowMapSamplingParameters)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, VirtualShadowMapIds)

		// Output
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, RWShadowMask)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FLumenHardwareRayTracingRGS::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		FVirtualShadowMapArray::SetShaderDefines(OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), Lumen::ShadowTraceTileSize2D);
		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_DISPATCH_1D"), 1);
		OutEnvironment.SetDefine(TEXT("SURFACE_CACHE_FEEDBACK"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenDirectLightingHardwareRayTracingBatchedRGS, "/Engine/Private/Lumen/LumenSceneDirectLightingHardwareRayTracing.usf", "LumenSceneDirectLightingHardwareRayTracingRGS", SF_RayGen);

void FDeferredShadingSceneRenderer::PrepareLumenHardwareRayTracingDirectLightingLumenMaterial(const FViewInfo& View, TArray<FRHIRayTracingShader*>& OutRayGenShaders)
{
	if (Lumen::UseHardwareRayTracedDirectLighting())
	{
		FLumenDirectLightingHardwareRayTracingBatchedRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenDirectLightingHardwareRayTracingBatchedRGS::FUseVirtualShadowMapsDim>(false);
		TShaderRef<FLumenDirectLightingHardwareRayTracingBatchedRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenDirectLightingHardwareRayTracingBatchedRGS>(PermutationVector);

		OutRayGenShaders.Add(RayGenerationShader.GetRayTracingShader());
	}
}

void CreateLightBatchCullingParameters(
	const TArray<const FLightSceneInfo*, TInlineAllocator<Lumen::MaxShadowMaskChannels>>& Lights,
	TArray<FLightBatchCullingParametersPacked, TInlineAllocator<Lumen::MaxShadowMaskChannels>>& LightBatchCullingParameters
)
{
	for (int32 Index = 0; Index < Lights.Num(); ++Index)
	{
		FLightBatchCullingParameters Parameters;

		const FLightSceneInfo* LightSceneInfo = Lights[Index];
		const FSphere LightBounds = LightSceneInfo->Proxy->GetBoundingSphere();
		Parameters.bCastsDynamicShadow = LightSceneInfo->Proxy->CastsDynamicShadow() ? 1 : 0;
		Parameters.LightType = static_cast<uint32>(LightSceneInfo->Proxy->GetLightType());
		Parameters.InfluenceSphere = FVector4(LightBounds.Center, LightBounds.W);
		Parameters.LightPosition = LightSceneInfo->Proxy->GetPosition();
		Parameters.LightDirection = LightSceneInfo->Proxy->GetDirection();
		Parameters.LightRadius = LightSceneInfo->Proxy->GetRadius();
		Parameters.CosConeAngle = FMath::Cos(LightSceneInfo->Proxy->GetOuterConeAngle());
		Parameters.SinConeAngle = FMath::Sin(LightSceneInfo->Proxy->GetOuterConeAngle());

		LightBatchCullingParameters.Add(Parameters);
	}
}

bool GetVirtualShadowMapIds(
	const TArray<const FLightSceneInfo*, TInlineAllocator<Lumen::MaxShadowMaskChannels>>& Lights,
	TArray<FVisibleLightInfo, SceneRenderingAllocator>& VisibleLightInfos,
	const FVirtualShadowMapArray& VirtualShadowMapArray,
	TArray<int, TInlineAllocator<Lumen::MaxShadowMaskChannels>>& VirtualShadowMapIds
)
{
	bool bUseVirtualShadowMaps = false;

	for (int32 Index = 0; Index < Lights.Num(); ++Index)
	{
		int VirtualShadowMapId = -1;

		const FLightSceneInfo* LightSceneInfo = Lights[Index];
		FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightSceneInfo->Id];
		FLumenShadowSetup ShadowSetup = GetShadowForLumenDirectLighting(VisibleLightInfo);

		if (Lumen::UseVirtualShadowMaps()
			&& ShadowSetup.DenseShadowMap != nullptr
			&& VirtualShadowMapArray.IsAllocated())
		{
			bUseVirtualShadowMaps = true;

			if (LightSceneInfo->Proxy->GetLightType() == LightType_Directional)
			{
				VirtualShadowMapId = VisibleLightInfo.VirtualShadowMapClipmaps[0]->GetVirtualShadowMap()->ID;
			}
			else if (ShadowSetup.VirtualShadowMap)
			{
				VirtualShadowMapId = ShadowSetup.VirtualShadowMap->VirtualShadowMaps[0]->ID;
			}
		}

		VirtualShadowMapIds.Add(VirtualShadowMapId);
	}

	return bUseVirtualShadowMaps;
}

void RenderLumenHardwareRayTracingDirectLighting(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FSceneTextureParameters& SceneTextures,
	const FViewInfo& View,
	const TArray<const FLightSceneInfo*, TInlineAllocator<Lumen::MaxShadowMaskChannels>>& Lights,
	TArray<FVisibleLightInfo, SceneRenderingAllocator>& VisibleLightInfos,
	const FVirtualShadowMapArray& VirtualShadowMapArray,
	const FLumenCardTracingInputs& TracingInputs,
	FRDGTextureRef OpacityAtlas,
	const FLumenCardRenderer& LumenCardRenderer,
	const FLumenCardScatterContext& CardScatterContext,
	FRDGTextureRef& ShadowMaskTexture,
	uint32 ShadowMaskIndex
)
{
	if (ShadowMaskIndex != 0) return;

	FLumenSceneData& LumenSceneData = *Scene->LumenSceneData;
	const FRDGTextureDesc ShadowMaskAtlasTextureDescriptor = FRDGTextureDesc::Create2D(
		LumenSceneData.GetPhysicalAtlasSize(),
		PF_R32_UINT,
		FClearValueBinding(),
		TexCreate_ShaderResource | TexCreate_UAV
	);
	ShadowMaskTexture = GraphBuilder.CreateTexture(ShadowMaskAtlasTextureDescriptor, TEXT("ShadowMaskAtlas"));

	// Allocate tiles for tracing
	FRDGBufferRef ShadowTraceTileAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("ShadowTraceTileAllocator"));
	{
		FClearShadowTraceTileAllocatorBatchedCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearShadowTraceTileAllocatorBatchedCS::FParameters>();
		PassParameters->RWShadowTraceTileAllocator = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ShadowTraceTileAllocator, PF_R32_UINT));

		TShaderRef<FClearShadowTraceTileAllocatorBatchedCS> ComputeShader = View.ShaderMap->GetShader<FClearShadowTraceTileAllocatorBatchedCS>();

		const FIntVector GroupSize(1, 1, 1);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ClearShadowTraceTileAllocatorBatchedCS"),
			ComputeShader,
			PassParameters,
			GroupSize);
	}

	// Create light culling struct
	TArray<FLightBatchCullingParametersPacked, TInlineAllocator<Lumen::MaxShadowMaskChannels>> LightBatchCullingParameters;
	CreateLightBatchCullingParameters(Lights, LightBatchCullingParameters);

	// Create virtual shadow map ids
	TArray<int, TInlineAllocator<Lumen::MaxShadowMaskChannels>> VirtualShadowMapIds;
	bool bUseVirtualShadowMaps = GetVirtualShadowMapIds(Lights, VisibleLightInfos, VirtualShadowMapArray, VirtualShadowMapIds);
	
	FRDGBufferRef LightBatchCullingParametersPackedBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("Lumen.DirectLighting.LightBatchCullingParameters"), LightBatchCullingParameters);
	FRDGBufferRef VirtualShadowMapIdsBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("Lumen.DirectLighting.VirtualShadowMapIds"), VirtualShadowMapIds);

	// Create shadow trace tiles
	const uint32 NumTraceTilesToAllocate = (LumenSceneData.GetPhysicalAtlasSize().X * LumenSceneData.GetPhysicalAtlasSize().Y * Lights.Num()) / FCreateShadowTraceTilesBatchedCS::GetRaysPerTile();
	FRDGBufferRef ShadowTraceTileData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(FIntPoint), NumTraceTilesToAllocate), TEXT("ShadowTraceTileData"));
	{
		FCreateShadowTraceTilesBatchedCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCreateShadowTraceTilesBatchedCS::FParameters>();

		// Input
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->LumenCardScene = TracingInputs.LumenCardSceneUniformBuffer;
		PassParameters->AtlasSize = LumenSceneData.GetPhysicalAtlasSize();

		// Lights
		PassParameters->LightBatchCullingParameters = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(LightBatchCullingParametersPackedBuffer));
		PassParameters->LightsNum = LightBatchCullingParameters.Num();

		// Frequency update parameters
		PassParameters->OperateOnCardPagesToRender = 0;
		PassParameters->NumCardPagesToRenderIndices = LumenCardRenderer.CardPagesToRender.Num();
		PassParameters->CardPagesToRenderIndices = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(LumenCardRenderer.CardPagesToRenderIndexBuffer, PF_R32_UINT));
		PassParameters->CardPagesToRenderHashMap = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(LumenCardRenderer.CardPagesToRenderHashMapBuffer, PF_R32_UINT));;

		extern float GLumenSceneCardDirectLightingUpdateFrequencyScale;
		extern int32 GLumenSceneLightingMinUpdateFrequency;
		PassParameters->CardLightingUpdateFrequencyScale = Lumen::UseLumenSceneLightingForceFullUpdate() ? 0.0 : GLumenSceneCardDirectLightingUpdateFrequencyScale;
		PassParameters->CardLightingUpdateMinFrequency = Lumen::UseLumenSceneLightingForceFullUpdate() ? 1 : GLumenSceneLightingMinUpdateFrequency;
		PassParameters->FrameId = View.ViewState->GetFrameIndex();

		// Output
		PassParameters->RWShadowTraceTileAllocator = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ShadowTraceTileAllocator, PF_R32_UINT));
		PassParameters->RWShadowTraceTileData = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(ShadowTraceTileData, PF_G32R32F));

		TShaderRef<FCreateShadowTraceTilesBatchedCS> ComputeShader = View.ShaderMap->GetShader<FCreateShadowTraceTilesBatchedCS>();
		//ClearUnusedGraphResources(ComputeShader, PassParameters);

		int NumCardPages = LumenSceneData.GetNumCardPages();
		const FIntVector GroupSize(FMath::DivideAndRoundUp<int32>(NumCardPages, FCreateShadowTraceTilesBatchedCS::GetGroupSize()), 1, 1);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("CreateShadowTraceTilesBatchedCS"),
			ComputeShader,
			PassParameters,
			GroupSize);
	}

	// Trace shadow tiles
	FRDGTextureUAVRef ShadowMaskTextureUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(ShadowMaskTexture));
	{
		FLumenDirectLightingHardwareRayTracingBatchedRGS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenDirectLightingHardwareRayTracingBatchedRGS::FParameters>();
		SetLumenHardwareRayTracingSharedParameters(
			GraphBuilder,
			SceneTextures,
			View,
			TracingInputs,
			&PassParameters->SharedParameters
		);

		// Lights
		PassParameters->LightBatchCullingParameters = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(LightBatchCullingParametersPackedBuffer));
		PassParameters->LightsNum = LightBatchCullingParameters.Num();

		PassParameters->ShadowTraceTileAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ShadowTraceTileAllocator, PF_R32_UINT));
		PassParameters->ShadowTraceTileData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(ShadowTraceTileData, PF_G32R32F));
		PassParameters->PullbackBias = Lumen::GetHardwareRayTracingPullbackBias();
		PassParameters->MaxTranslucentSkipCount = 1; // TODO: CVarLumenReflectionsHardwareRayTracingMaxTranslucentSkipCount.GetValueOnRenderThread();
		PassParameters->AtlasSize = LumenSceneData.GetPhysicalAtlasSize();
		PassParameters->GroupCount = FMath::Max(CVarLumenSceneDirectLightingHardwareRayTracingGroupCount.GetValueOnRenderThread(), 1);
		PassParameters->MaxTraceDistance = 15000; // TODO: Align with global off-screen tracing distance?

		PassParameters->VirtualShadowMapSamplingParameters = VirtualShadowMapArray.GetSamplingParameters(GraphBuilder);
		PassParameters->VirtualShadowMapIds = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(VirtualShadowMapIdsBuffer));

		PassParameters->SurfaceBias = 1.0;
		PassParameters->SlopeScaledSurfaceBias = 1.0;

		// Output
		PassParameters->RWShadowMask = ShadowMaskTextureUAV;

		FLumenDirectLightingHardwareRayTracingBatchedRGS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenDirectLightingHardwareRayTracingBatchedRGS::FUseVirtualShadowMapsDim>(false);
		TShaderRef<FLumenDirectLightingHardwareRayTracingBatchedRGS> RayGenerationShader = View.ShaderMap->GetShader<FLumenDirectLightingHardwareRayTracingBatchedRGS>(PermutationVector);
		//ClearUnusedGraphResources(RayGenerationShader, PassParameters);

		FIntPoint DispatchResolution = FIntPoint(FCreateShadowTraceTilesBatchedCS::GetRaysPerTile(), PassParameters->GroupCount);
		FString LightName;
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("LumenDirectLightingHardwareRayTracingRGS (%d) %ux%u ", Lights.Num(), DispatchResolution.X, DispatchResolution.Y),
			PassParameters,
			ERDGPassFlags::Compute,
			[PassParameters, &View, RayGenerationShader, DispatchResolution](FRHICommandList& RHICmdList)
			{
				FRayTracingShaderBindingsWriter GlobalResources;
				SetShaderParameters(GlobalResources, RayGenerationShader, *PassParameters);

				FRHIRayTracingScene* RayTracingSceneRHI = View.GetRayTracingSceneChecked();
				FRayTracingPipelineState* RayTracingPipeline = View.LumenHardwareRayTracingMaterialPipeline;

				RHICmdList.RayTraceDispatch(RayTracingPipeline, RayGenerationShader.GetRayTracingShader(), RayTracingSceneRHI, GlobalResources,
					DispatchResolution.X, DispatchResolution.Y);
			}
		);
	}
}

#endif // RHI_RAYTRACING
