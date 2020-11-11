// Copyright Epic Games, Inc. All Rights Reserved.

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "LumenSceneUtils.h"
#include "PixelShaderUtils.h"
#include "ReflectionEnvironment.h"
#include "DistanceFieldAmbientOcclusion.h"
#include "SceneTextureParameters.h"
#include "IndirectLightRendering.h"
#include "LumenRadianceCache.h"

int32 GLumenProbeHierarchyTraceCards = 1;
FAutoConsoleVariableRef GVarLumenProbeHierarchyTraceCards(
	TEXT("r.Lumen.ProbeHierarchy.TraceCards"),
	GLumenProbeHierarchyTraceCards,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

class FSetupLumenVoxelTraceProbeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSetupLumenVoxelTraceProbeCS)
	SHADER_USE_PARAMETER_STRUCT(FSetupLumenVoxelTraceProbeCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenProbeHierarchy::FHierarchyParameters, HierarchyParameters)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, DispatchParametersOutput)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), 8);
		OutEnvironment.SetDefine(TEXT("DIFFUSE_TRACE_CARDS"), 1);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
};

class FLumenCardTraceProbeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenCardTraceProbeCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenCardTraceProbeCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenMeshSDFGridParameters, MeshSDFGridParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenProbeHierarchy::FHierarchyParameters, HierarchyParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenProbeHierarchy::FHierarchyLevelParameters, LevelParameters)
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, DispatchParameters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, ProbeAtlasColorOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, ProbeAtlasSampleMaskOutput)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<LumenProbeHierarchy::FProbeTracingPermutationDim>;

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (RemapPermutation(PermutationVector) != PermutationVector)
		{
			return false;
		}

		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static int32 GetGroupSize()
	{
		return 8;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
		OutEnvironment.SetDefine(TEXT("DIFFUSE_TRACE_CARDS"), 1);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
	}
};

class FLumenVoxelTraceProbeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenVoxelTraceProbeCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenVoxelTraceProbeCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenProbeHierarchy::FHierarchyParameters, HierarchyParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenProbeHierarchy::FHierarchyLevelParameters, LevelParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheParameters, RadianceCacheParameters)
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, DispatchParameters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, ProbeAtlasColorOutput)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, ProbeAtlasSampleMaskOutput)
	END_SHADER_PARAMETER_STRUCT()

	class FDynamicSkyLight : SHADER_PERMUTATION_BOOL("ENABLE_DYNAMIC_SKY_LIGHT");
	class FTraceDistantScene : SHADER_PERMUTATION_BOOL("PROBE_HIERARCHY_TRACE_DISTANT_SCENE");
	class FTraceCards : SHADER_PERMUTATION_BOOL("DIFFUSE_TRACE_CARDS");
	class FRadianceCache : SHADER_PERMUTATION_BOOL("RADIANCE_CACHE");

	using FPermutationDomain = TShaderPermutationDomain<
		FDynamicSkyLight, FTraceDistantScene, FTraceCards, FRadianceCache,
		LumenProbeHierarchy::FProbeTracingPermutationDim>;

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (RemapPermutation(PermutationVector) != PermutationVector)
		{
			return false;
		}

		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static int32 GetGroupSize()
	{
		return 8;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);

		// Workaround for an internal PC FXC compiler crash when compiling with disabled optimizations
		if (Parameters.Platform == SP_PCD3D_SM5)
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_ForceOptimization);
		}
	}
};

class FSetupLumenTraceProbeOcclusionCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSetupLumenTraceProbeOcclusionCS);
	SHADER_USE_PARAMETER_STRUCT(FSetupLumenTraceProbeOcclusionCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(int32, MaxTilePerDispatch)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, GlobalClassificationCountersBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, DispatchParametersOutput)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("DIM_LUMEN_TRACING_PERMUTATION"), 0);
	}
};

class FLumenTraceProbeOcclusionCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenTraceProbeOcclusionCS)
	SHADER_USE_PARAMETER_STRUCT(FLumenTraceProbeOcclusionCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(HybridIndirectLighting::FCommonParameters, CommonIndirectParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenMeshSDFGridParameters, MeshSDFGridParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenProbeHierarchy::FIndirectLightingProbeOcclusionParameters, ProbeOcclusionParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenProbeHierarchy::FIndirectLightingProbeOcclusionOutputParameters, ProbeOcclusionOutputParameters)
		SHADER_PARAMETER(int32, DispatchOffset)
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, DispatchParameters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, DebugOutput)
	END_SHADER_PARAMETER_STRUCT()

	class FLumenTracingPermutationDim : SHADER_PERMUTATION_ENUM_CLASS("DIM_LUMEN_TRACING_PERMUTATION", Lumen::ETracingPermutation);
	class FTileClassificationDim : SHADER_PERMUTATION_ENUM_CLASS("DIM_PROBE_OCCLUSION_CLASSIFICATION", LumenProbeHierarchy::EProbeOcclusionClassification);
	using FPermutationDomain = TShaderPermutationDomain<FLumenTracingPermutationDim, FTileClassificationDim>;

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (RemapPermutation(PermutationVector) != PermutationVector)
		{
			return false;
		}

		if (PermutationVector.Get<FTileClassificationDim>() == LumenProbeHierarchy::EProbeOcclusionClassification::Unlit)
		{
			return false;
		}

		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
		OutEnvironment.CompilerFlags.Add(CFLAG_AllowTypedUAVLoads);
	}
};

IMPLEMENT_GLOBAL_SHADER(FSetupLumenVoxelTraceProbeCS, "/Engine/Private/Lumen/FinalGather/LumenProbeHierarchyTracing.usf", "SetupVoxelTraceProbeCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FLumenCardTraceProbeCS, "/Engine/Private/Lumen/FinalGather/LumenProbeHierarchyTracing.usf", "CardTraceProbeCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FLumenVoxelTraceProbeCS, "/Engine/Private/Lumen/FinalGather/LumenProbeHierarchyTracing.usf", "VoxelTraceProbeCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FSetupLumenTraceProbeOcclusionCS, "/Engine/Private/Lumen/FinalGather/LumenProbeOcclusionTracing.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FLumenTraceProbeOcclusionCS, "/Engine/Private/Lumen/FinalGather/LumenProbeOcclusionTracing.usf", "MainCS", SF_Compute);

void FDeferredShadingSceneRenderer::RenderLumenProbe(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const LumenProbeHierarchy::FHierarchyParameters& HierarchyParameters,
	const LumenProbeHierarchy::FIndirectLightingAtlasParameters& IndirectLightingAtlasParameters,
	const LumenProbeHierarchy::FEmitProbeParameters& EmitProbeParameters,
	const LumenRadianceCache::FRadianceCacheParameters& RadianceCacheParameters,
	bool bUseRadianceCache)
{
	LLM_SCOPE_BYTAG(Lumen);

	FLumenCardTracingInputs TracingInputs(GraphBuilder, Scene, View);

	FRDGBufferRef DispatchParameters = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(LumenProbeHierarchy::kProbeMaxHierarchyDepth),
		TEXT("LumenVoxelTraceProbeDispatch"));

	{
		FSetupLumenVoxelTraceProbeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSetupLumenVoxelTraceProbeCS::FParameters>();
		PassParameters->HierarchyParameters = HierarchyParameters;
		PassParameters->DispatchParametersOutput = GraphBuilder.CreateUAV(DispatchParameters);

		auto ComputeShader = View.ShaderMap->GetShader<FSetupLumenVoxelTraceProbeCS>();
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("SetupVoxelTraceProbe"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1));
	}

	const bool bTraceCards = GLumenProbeHierarchyTraceCards != 0;

	if (bTraceCards)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Card ProbeTracing");

		FLumenMeshSDFGridParameters MeshSDFGridParameters;
		FLumenDiffuseTracingParameters DiffuseTracingParametersForCulling;
		SetupLumenDiffuseTracingParameters(/* out */ DiffuseTracingParametersForCulling.IndirectTracingParameters);

		CullMeshSDFObjectsToProbes(
			GraphBuilder,
			Scene,
			View,
			DiffuseTracingParametersForCulling.IndirectTracingParameters.MaxCardTraceDistance,
			DiffuseTracingParametersForCulling.IndirectTracingParameters.CardTraceEndDistanceFromCamera,
			HierarchyParameters,
			EmitProbeParameters,
			/* out */ MeshSDFGridParameters);

		FRDGTextureUAVRef ProbeAtlasColorOutput = GraphBuilder.CreateUAV(IndirectLightingAtlasParameters.ProbeAtlasColor, ERDGUnorderedAccessViewFlags::SkipBarrier);
		FRDGTextureUAVRef ProbeAtlasSampleMaskOutput = GraphBuilder.CreateUAV(IndirectLightingAtlasParameters.ProbeAtlasSampleMask, ERDGUnorderedAccessViewFlags::SkipBarrier);

		for (int32 HierarchyLevelId = 0; HierarchyLevelId < HierarchyParameters.HierarchyDepth; HierarchyLevelId++)
		{
			FLumenCardTraceProbeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenCardTraceProbeCS::FParameters>();
			GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters);
			PassParameters->MeshSDFGridParameters = MeshSDFGridParameters;
			PassParameters->HierarchyParameters = HierarchyParameters;
			PassParameters->LevelParameters = LumenProbeHierarchy::GetLevelParameters(HierarchyParameters, HierarchyLevelId);
			SetupLumenDiffuseTracingParametersForProbe(PassParameters->IndirectTracingParameters, LumenProbeHierarchy::ComputeHierarchyLevelConeAngle(PassParameters->LevelParameters));
			PassParameters->DispatchParameters = DispatchParameters;

			PassParameters->ProbeAtlasColorOutput = ProbeAtlasColorOutput;
			PassParameters->ProbeAtlasSampleMaskOutput = ProbeAtlasSampleMaskOutput;

			FLumenCardTraceProbeCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<LumenProbeHierarchy::FProbeTracingPermutationDim>(
				LumenProbeHierarchy::GetProbeTracingPermutation(PassParameters->LevelParameters));
			PermutationVector = FLumenCardTraceProbeCS::RemapPermutation(PermutationVector);

			auto ComputeShader = View.ShaderMap->GetShader<FLumenCardTraceProbeCS>(PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("CardTraceProbe(Level=%d Res=%d SuperSample=%d)",
					PassParameters->LevelParameters.LevelId,
					PassParameters->LevelParameters.LevelResolution,
					PassParameters->LevelParameters.LevelSuperSampling),
				ComputeShader,
				PassParameters,
				DispatchParameters,
				/* IndirectArgOffset = */ sizeof(FRHIDispatchIndirectParameters) * HierarchyLevelId);
		}
	}

	{
		RDG_EVENT_SCOPE(GraphBuilder, "Voxel ProbeTracing");

		FRDGTextureUAVRef ProbeAtlasColorOutput = GraphBuilder.CreateUAV(IndirectLightingAtlasParameters.ProbeAtlasColor, ERDGUnorderedAccessViewFlags::SkipBarrier);
		FRDGTextureUAVRef ProbeAtlasSampleMaskOutput = GraphBuilder.CreateUAV(IndirectLightingAtlasParameters.ProbeAtlasSampleMask, ERDGUnorderedAccessViewFlags::SkipBarrier);

		for (int32 HierarchyLevelId = 0; HierarchyLevelId < HierarchyParameters.HierarchyDepth; HierarchyLevelId++)
		{
			FLumenVoxelTraceProbeCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenVoxelTraceProbeCS::FParameters>();
			GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters);
			PassParameters->RadianceCacheParameters = RadianceCacheParameters;
			PassParameters->HierarchyParameters = HierarchyParameters;
			PassParameters->LevelParameters = LumenProbeHierarchy::GetLevelParameters(HierarchyParameters, HierarchyLevelId);
			SetupLumenDiffuseTracingParametersForProbe(PassParameters->IndirectTracingParameters, LumenProbeHierarchy::ComputeHierarchyLevelConeAngle(PassParameters->LevelParameters));
			PassParameters->DispatchParameters = DispatchParameters;

			PassParameters->ProbeAtlasColorOutput = ProbeAtlasColorOutput;
			PassParameters->ProbeAtlasSampleMaskOutput = ProbeAtlasSampleMaskOutput;

			const bool bLastLevel = (HierarchyLevelId + 1) == HierarchyParameters.HierarchyDepth;
			const bool bRadianceCache = bUseRadianceCache && bLastLevel;

			FLumenVoxelTraceProbeCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenVoxelTraceProbeCS::FDynamicSkyLight>(ShouldRenderDynamicSkyLight(Scene, ViewFamily) && bLastLevel);
			PermutationVector.Set<FLumenVoxelTraceProbeCS::FTraceCards>(bTraceCards);
			PermutationVector.Set<FLumenVoxelTraceProbeCS::FRadianceCache>(bRadianceCache);
			PermutationVector.Set<FLumenVoxelTraceProbeCS::FTraceDistantScene >(Scene->LumenSceneData->DistantCardIndices.Num() > 0);
			PermutationVector.Set<LumenProbeHierarchy::FProbeTracingPermutationDim>(
				LumenProbeHierarchy::GetProbeTracingPermutation(PassParameters->LevelParameters));
			PermutationVector = FLumenVoxelTraceProbeCS::RemapPermutation(PermutationVector);
			auto ComputeShader = View.ShaderMap->GetShader<FLumenVoxelTraceProbeCS>(PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("VoxelTrace Probe(Level=%d Res=%d SuperSample=%d%s)",
					PassParameters->LevelParameters.LevelId,
					PassParameters->LevelParameters.LevelResolution,
					PassParameters->LevelParameters.LevelSuperSampling,
					PermutationVector.Get<FLumenVoxelTraceProbeCS::FDynamicSkyLight>() ? TEXT(" SkyLight") : TEXT("")),
				ComputeShader,
				PassParameters,
				DispatchParameters,
				/* IndirectArgOffset = */ sizeof(FRHIDispatchIndirectParameters) * HierarchyLevelId);
		}
	}
} // RenderLumenProbe()

void FDeferredShadingSceneRenderer::RenderLumenProbeOcclusion(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const HybridIndirectLighting::FCommonParameters& CommonParameters,
	const LumenProbeHierarchy::FIndirectLightingProbeOcclusionParameters& ProbeOcclusionParameters)
{
	using namespace LumenProbeHierarchy;

	check(CommonParameters.RayCountPerPixel == 8); // TODO

	RDG_EVENT_SCOPE(GraphBuilder, "WorldTrace ProbeOcclusion %dx%d",
		CommonParameters.TracingViewportSize.X,
		CommonParameters.TracingViewportSize.Y);

	FRDGBufferRef DispatchParameters;
	{
		DispatchParameters = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(int32(EProbeOcclusionClassification::MAX) * ProbeOcclusionParameters.DispatchCount),
			TEXT("ProbeHierarchy.Occlusion.VoxelDispatchParameters"));

		FSetupLumenTraceProbeOcclusionCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSetupLumenTraceProbeOcclusionCS::FParameters>();
		PassParameters->MaxTilePerDispatch = ProbeOcclusionParameters.MaxTilePerDispatch;
		PassParameters->GlobalClassificationCountersBuffer = ProbeOcclusionParameters.GlobalClassificationCountersBuffer;
		PassParameters->DispatchParametersOutput = GraphBuilder.CreateUAV(DispatchParameters);

		TShaderMapRef<FSetupLumenTraceProbeOcclusionCS> ComputeShader(View.ShaderMap);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("SetupLumenTraceProbeOcclusion"),
			ComputeShader,
			PassParameters,
			FIntVector(ProbeOcclusionParameters.DispatchCount, 1, 1));
	}

	const bool bTraceCards = GLumenProbeHierarchyTraceCards != 0;

	FLumenCardTracingInputs TracingInputs(GraphBuilder, Scene, View);

	FLumenTraceProbeOcclusionCS::FParameters ReferencePassParameters;
	{
		GetLumenCardTracingParameters(View, TracingInputs, /* out */ ReferencePassParameters.TracingParameters);
		ReferencePassParameters.CommonIndirectParameters = CommonParameters;
		ReferencePassParameters.ProbeOcclusionParameters = ProbeOcclusionParameters;
		ReferencePassParameters.ProbeOcclusionOutputParameters = CreateProbeOcclusionOutputParameters(
			GraphBuilder, ProbeOcclusionParameters, ERDGUnorderedAccessViewFlags::SkipBarrier);
		ReferencePassParameters.DispatchParameters = DispatchParameters;

		{
			FRDGTextureDesc DebugDesc = FRDGTextureDesc::Create2D(
				ProbeOcclusionParameters.CompressedDepthTexture->Desc.Extent,
				PF_FloatRGBA,
				FClearValueBinding::None,
				TexCreate_ShaderResource | TexCreate_UAV);

			FRDGTextureRef DebugOutputTexture = GraphBuilder.CreateTexture(DebugDesc, TEXT("Debug.ProbeHierarchy.VoxelProbeOcclusion"));

			ReferencePassParameters.DebugOutput = GraphBuilder.CreateUAV(DebugOutputTexture);
		}

		FLumenDiffuseTracingParameters DiffuseTracingParameters;
		SetupLumenDiffuseTracingParameters(/* out */ DiffuseTracingParameters.IndirectTracingParameters);
		DiffuseTracingParameters.CommonDiffuseParameters = CommonParameters;
		DiffuseTracingParameters.SampleWeight = (2.0f * PI) / float(CommonParameters.RayCountPerPixel);
		DiffuseTracingParameters.DownsampledNormal = nullptr;
		DiffuseTracingParameters.DownsampledDepth = nullptr;

		if (bTraceCards)
		{
			CullForCardTracing(
				GraphBuilder,
				Scene, View,
				TracingInputs,
				DiffuseTracingParameters.DownsampledDepth,
				DiffuseTracingParameters.CommonDiffuseParameters.DownscaleFactor,
				DiffuseTracingParameters.IndirectTracingParameters,
				/* out */ ReferencePassParameters.MeshSDFGridParameters);
		}
	}

	for (int32 i = 0; i < int32(EProbeOcclusionClassification::MAX); i++)
	{
		EProbeOcclusionClassification TileClassification = EProbeOcclusionClassification(i);

		if (TileClassification == LumenProbeHierarchy::EProbeOcclusionClassification::Unlit)
		{
			continue;
		}

		// Trace cards
		if (bTraceCards)
		{
			FLumenTraceProbeOcclusionCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenTraceProbeOcclusionCS::FParameters>();
			*PassParameters = ReferencePassParameters;

			SetupLumenDiffuseTracingParameters(/* out */ PassParameters->IndirectTracingParameters);

			FLumenTraceProbeOcclusionCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenTraceProbeOcclusionCS::FLumenTracingPermutationDim>(Lumen::ETracingPermutation::Cards);
			PermutationVector.Set<FLumenTraceProbeOcclusionCS::FTileClassificationDim>(TileClassification);
			PermutationVector = FLumenTraceProbeOcclusionCS::RemapPermutation(PermutationVector);

			TShaderMapRef<FLumenTraceProbeOcclusionCS> ComputeShader(View.ShaderMap, PermutationVector);
			ClearUnusedGraphResources(ComputeShader, PassParameters);
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("CardTrace ProbeOcclusion(%s)", GetEventName(TileClassification)),
				PassParameters,
				ERDGPassFlags::Compute,
				[PassParameters, ComputeShader, i](FRHIComputeCommandList& RHICmdList)
				{
					FLumenTraceProbeOcclusionCS::FParameters ShaderParameters = *PassParameters;
					ShaderParameters.DispatchParameters->MarkResourceAsUsed();

					for (int32 DispatchId = 0; DispatchId < ShaderParameters.ProbeOcclusionParameters.DispatchCount; DispatchId++)
					{
						ShaderParameters.DispatchOffset = DispatchId * ShaderParameters.ProbeOcclusionParameters.MaxTilePerDispatch;

						int32 IndirectArgsOffset = sizeof(FRHIDispatchIndirectParameters) * (i + DispatchId * int32(EProbeOcclusionClassification::MAX));
						FComputeShaderUtils::DispatchIndirect(
							RHICmdList,
							ComputeShader,
							ShaderParameters,
							ShaderParameters.DispatchParameters->GetIndirectRHICallBuffer(),
							IndirectArgsOffset);
					}
				});
		}

		// Trace voxels
		{
			FLumenTraceProbeOcclusionCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FLumenTraceProbeOcclusionCS::FParameters>();
			*PassParameters = ReferencePassParameters;

			SetupLumenDiffuseTracingParameters(/* out */ PassParameters->IndirectTracingParameters);

			FLumenTraceProbeOcclusionCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FLumenTraceProbeOcclusionCS::FLumenTracingPermutationDim>(bTraceCards ? Lumen::ETracingPermutation::VoxelsAfterCards : Lumen::ETracingPermutation::Voxels);
			PermutationVector.Set<FLumenTraceProbeOcclusionCS::FTileClassificationDim>(TileClassification);
			PermutationVector = FLumenTraceProbeOcclusionCS::RemapPermutation(PermutationVector);

			TShaderMapRef<FLumenTraceProbeOcclusionCS> ComputeShader(View.ShaderMap, PermutationVector);
			ClearUnusedGraphResources(ComputeShader, PassParameters);
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("VoxelTrace ProbeOcclusion(%s)", GetEventName(TileClassification)),
				PassParameters,
				ERDGPassFlags::Compute,
				[PassParameters, ComputeShader, i](FRHIComputeCommandList& RHICmdList)
			{
					FLumenTraceProbeOcclusionCS::FParameters ShaderParameters = *PassParameters;
				ShaderParameters.DispatchParameters->MarkResourceAsUsed();

				for (int32 DispatchId = 0; DispatchId < ShaderParameters.ProbeOcclusionParameters.DispatchCount; DispatchId++)
				{
					ShaderParameters.DispatchOffset = DispatchId * ShaderParameters.ProbeOcclusionParameters.MaxTilePerDispatch;

					int32 IndirectArgsOffset = sizeof(FRHIDispatchIndirectParameters) * (i + DispatchId * int32(EProbeOcclusionClassification::MAX));
					FComputeShaderUtils::DispatchIndirect(
						RHICmdList, 
						ComputeShader,
						ShaderParameters,
						ShaderParameters.DispatchParameters->GetIndirectRHICallBuffer(),
						IndirectArgsOffset);
				}
			});
		}
	}
}
