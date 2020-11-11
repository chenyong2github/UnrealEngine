// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenScreenProbeTracing.cpp
=============================================================================*/

#include "LumenScreenProbeGather.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "PixelShaderUtils.h"
#include "ReflectionEnvironment.h"
#include "DistanceFieldAmbientOcclusion.h"

int32 GLumenScreenProbeGatherHierarchicalScreenTraces = 1;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherHierarchicalScreenTraces(
	TEXT("r.Lumen.ScreenProbeGather.HierarchicalScreenTraces"),
	GLumenScreenProbeGatherHierarchicalScreenTraces,
	TEXT("Whether to use HZB tracing for SSGI instead of fixed step count intersection.  HZB tracing is much more accurate, in particular not missing thin features, but is about ~3x slower."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeGatherHierarchicalScreenTracesMaxIterations = 50;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherHierarchicalScreenTracesMaxIterations(
	TEXT("r.Lumen.ScreenProbeGather.HierarchicalScreenTraces.MaxIterations"),
	GLumenScreenProbeGatherHierarchicalScreenTracesMaxIterations,
	TEXT("Max iterations for HZB tracing."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GLumenScreenProbeGatherUncertainTraceRelativeDepthThreshold = .05f;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherUncertainTraceRelativeDepthThreshold(
	TEXT("r.Lumen.ScreenProbeGather.HierarchicalScreenTraces.UncertainTraceRelativeDepthThreshold"),
	GLumenScreenProbeGatherUncertainTraceRelativeDepthThreshold,
	TEXT("Determines depth thickness of objects hit by HZB tracing, as a relative depth threshold."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeGatherNumThicknessStepsToDetermineCertainty = 4;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherNumThicknessStepsToDetermineCertainty(
	TEXT("r.Lumen.ScreenProbeGather.HierarchicalScreenTraces.NumThicknessStepsToDetermineCertainty"),
	GLumenScreenProbeGatherNumThicknessStepsToDetermineCertainty,
	TEXT("Number of linear search steps to determine if a hit feature is thin and should be ignored."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeGatherVisualizeTraces = 0;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherVisualizeTraces(
	TEXT("r.Lumen.ScreenProbeGather.VisualizeTraces"),
	GLumenScreenProbeGatherVisualizeTraces,
	TEXT("Whether to visualize traces for the center screen probe, useful for debugging"),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenScreenProbeGatherVisualizeTracesFreeze = 0;
FAutoConsoleVariableRef GVarLumenScreenProbeGatherVisualizeTracesFreeze(
	TEXT("r.Lumen.ScreenProbeGather.VisualizeTracesFreeze"),
	GLumenScreenProbeGatherVisualizeTracesFreeze,
	TEXT("Whether to freeze updating the visualize trace data.  Note that no changes to cvars or shaders will propagate until unfrozen."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

class FClearTracesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearTracesCS)
	SHADER_USE_PARAMETER_STRUCT(FClearTracesCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearTracesCS, "/Engine/Private/Lumen/LumenScreenProbeTracing.usf", "ClearTracesCS", SF_Compute);


class FScreenProbeTraceScreenTexturesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeTraceScreenTexturesCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeTraceScreenTexturesCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FCommonScreenSpaceRayParameters, ScreenSpaceRayParameters)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, ClosestHZBTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SceneDepthTexture)
		SHADER_PARAMETER(FVector2D, HZBBaseTexelSize)
		SHADER_PARAMETER(FVector4, HZBUVToScreenUVScaleBias)
		SHADER_PARAMETER(float, MaxHierarchicalScreenTraceIterations)
		SHADER_PARAMETER(float, UncertainTraceRelativeDepthThreshold)
		SHADER_PARAMETER(float, NumThicknessStepsToDetermineCertainty)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheParameters, RadianceCacheParameters)
	END_SHADER_PARAMETER_STRUCT()

	class FRadianceCache : SHADER_PERMUTATION_BOOL("RADIANCE_CACHE");
	class FHierarchicalScreenTracing : SHADER_PERMUTATION_BOOL("HIERARCHICAL_SCREEN_TRACING");
	class FStructuredImportanceSampling : SHADER_PERMUTATION_BOOL("STRUCTURED_IMPORTANCE_SAMPLING");
	using FPermutationDomain = TShaderPermutationDomain<FStructuredImportanceSampling, FHierarchicalScreenTracing, FRadianceCache>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
};

IMPLEMENT_GLOBAL_SHADER(FScreenProbeTraceScreenTexturesCS, "/Engine/Private/Lumen/LumenScreenProbeTracing.usf", "ScreenProbeTraceScreenTexturesCS", SF_Compute);

BEGIN_SHADER_PARAMETER_STRUCT(FCompactedTraceParameters, )
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CompactedTraceTexelAllocator)
	SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint2>, CompactedTraceTexelData)
	SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, IndirectArgs)
END_SHADER_PARAMETER_STRUCT()

class FScreenProbeCompactTracesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeCompactTracesCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeCompactTracesCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
		SHADER_PARAMETER(float, CompactionTracingEndDistanceFromCamera)
		SHADER_PARAMETER(float, CompactionMaxTraceDistance)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCompactedTraceTexelAllocator)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCompactedTraceTexelData)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("DIFFUSE_TRACE_CARDS"), 1);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
};

IMPLEMENT_GLOBAL_SHADER(FScreenProbeCompactTracesCS, "/Engine/Private/Lumen/LumenScreenProbeTracing.usf", "ScreenProbeCompactTracesCS", SF_Compute);


class FSetupCompactedTracesIndirectArgsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FSetupCompactedTracesIndirectArgsCS)
	SHADER_USE_PARAMETER_STRUCT(FSetupCompactedTracesIndirectArgsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWScreenProbeCompactTracingIndirectArgs)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, CompactedTraceTexelAllocator)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}
};

IMPLEMENT_GLOBAL_SHADER(FSetupCompactedTracesIndirectArgsCS, "/Engine/Private/Lumen/LumenScreenProbeTracing.usf", "SetupCompactedTracesIndirectArgsCS", SF_Compute);

class FScreenProbeTraceCardsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeTraceCardsCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeTraceCardsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenMeshSDFGridParameters, MeshSDFGridParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_STRUCT_INCLUDE(FCompactedTraceParameters, CompactedTraceParameters)
	END_SHADER_PARAMETER_STRUCT()
		
	class FStructuredImportanceSampling : SHADER_PERMUTATION_BOOL("STRUCTURED_IMPORTANCE_SAMPLING");
	using FPermutationDomain = TShaderPermutationDomain<FStructuredImportanceSampling>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("DIFFUSE_TRACE_CARDS"), 1);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
};

IMPLEMENT_GLOBAL_SHADER(FScreenProbeTraceCardsCS, "/Engine/Private/Lumen/LumenScreenProbeTracing.usf", "ScreenProbeTraceCardsCS", SF_Compute);


class FScreenProbeTraceVoxelsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeTraceVoxelsCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeTraceVoxelsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenIndirectTracingParameters, IndirectTracingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(LumenRadianceCache::FRadianceCacheParameters, RadianceCacheParameters)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER_STRUCT_INCLUDE(FCompactedTraceParameters, CompactedTraceParameters)
	END_SHADER_PARAMETER_STRUCT()

	class FDynamicSkyLight : SHADER_PERMUTATION_BOOL("ENABLE_DYNAMIC_SKY_LIGHT");
	class FTraceDistantScene : SHADER_PERMUTATION_BOOL("TRACE_DISTANT_SCENE");
	class FRadianceCache : SHADER_PERMUTATION_BOOL("RADIANCE_CACHE");
	class FStructuredImportanceSampling : SHADER_PERMUTATION_BOOL("STRUCTURED_IMPORTANCE_SAMPLING");
	using FPermutationDomain = TShaderPermutationDomain<FDynamicSkyLight, FTraceDistantScene, FRadianceCache, FStructuredImportanceSampling>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);

		// Workaround for an internal PC FXC compiler crash when compiling with disabled optimizations
		if (Parameters.Platform == SP_PCD3D_SM5)
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_ForceOptimization);
		}
	}
};

IMPLEMENT_GLOBAL_SHADER(FScreenProbeTraceVoxelsCS, "/Engine/Private/Lumen/LumenScreenProbeTracing.usf", "ScreenProbeTraceVoxelsCS", SF_Compute);



class FScreenProbeSetupVisualizeTracesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FScreenProbeSetupVisualizeTracesCS)
	SHADER_USE_PARAMETER_STRUCT(FScreenProbeSetupVisualizeTracesCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float4>, RWVisualizeTracesData)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FScreenProbeParameters, ScreenProbeParameters)
	END_SHADER_PARAMETER_STRUCT()
		
	class FStructuredImportanceSampling : SHADER_PERMUTATION_BOOL("STRUCTURED_IMPORTANCE_SAMPLING");
	using FPermutationDomain = TShaderPermutationDomain<FStructuredImportanceSampling>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
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
	}
};

IMPLEMENT_GLOBAL_SHADER(FScreenProbeSetupVisualizeTracesCS, "/Engine/Private/Lumen/LumenScreenProbeTracing.usf", "ScreenProbeSetupVisualizeTraces", SF_Compute);


class FVisualizeTracesVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVisualizeTracesVS)
	SHADER_USE_PARAMETER_STRUCT(FVisualizeTracesVS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float3>, VisualizeTracesData)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FVisualizeTracesVS, "/Engine/Private/Lumen/LumenScreenProbeTracing.usf", "VisualizeTracesVS", SF_Vertex);


class FVisualizeTracesPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVisualizeTracesPS)
	SHADER_USE_PARAMETER_STRUCT(FVisualizeTracesPS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}
};

IMPLEMENT_GLOBAL_SHADER(FVisualizeTracesPS, "/Engine/Private/Lumen/LumenScreenProbeTracing.usf", "VisualizeTracesPS", SF_Pixel);

BEGIN_SHADER_PARAMETER_STRUCT(FVisualizeTraces, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FVisualizeTracesVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FVisualizeTracesPS::FParameters, PS)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()


class FVisualizeTracesVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;

	/** Destructor. */
	virtual ~FVisualizeTracesVertexDeclaration() {}

	virtual void InitRHI()
	{
		FVertexDeclarationElementList Elements;
		VertexDeclarationRHI = RHICreateVertexDeclaration(Elements);
	}

	virtual void ReleaseRHI()
	{
		VertexDeclarationRHI.SafeRelease();
	}
};

TGlobalResource<FVisualizeTracesVertexDeclaration> GVisualizeTracesVertexDeclaration;

TRefCountPtr<FRDGPooledBuffer> GVisualizeTracesData;

void SetupVisualizeTraces(
	FRDGBuilder& GraphBuilder,
	const FScene* Scene,
	const FViewInfo& View,
	const FScreenProbeParameters& ScreenProbeParameters)
{
	FRDGBufferRef VisualizeTracesData = nullptr;

	if (GVisualizeTracesData.IsValid())
	{
		VisualizeTracesData = GraphBuilder.RegisterExternalBuffer(GVisualizeTracesData);
	}
	
	const int32 VisualizeBufferNumElements = ScreenProbeParameters.ScreenProbeTracingOctahedronResolution * ScreenProbeParameters.ScreenProbeTracingOctahedronResolution * 3;
	bool bShouldUpdate = !GLumenScreenProbeGatherVisualizeTracesFreeze;

	if (!VisualizeTracesData || VisualizeTracesData->Desc.NumElements != VisualizeBufferNumElements)
	{
		VisualizeTracesData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(FVector4), VisualizeBufferNumElements), TEXT("VisualizeTracesData"));
		bShouldUpdate = true;
	}

	if (bShouldUpdate)
	{
		FScreenProbeSetupVisualizeTracesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeSetupVisualizeTracesCS::FParameters>();
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->ScreenProbeParameters = ScreenProbeParameters;
		PassParameters->RWVisualizeTracesData = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(VisualizeTracesData, PF_A32B32G32R32F));

		FScreenProbeSetupVisualizeTracesCS::FPermutationDomain PermutationVector;
		PermutationVector.Set< FScreenProbeSetupVisualizeTracesCS::FStructuredImportanceSampling >(LumenScreenProbeGather::UseImportanceSampling());
		auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeSetupVisualizeTracesCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("SetupVisualizeTraces"),
			ComputeShader,
			PassParameters,
			FComputeShaderUtils::GetGroupCount(FIntPoint(ScreenProbeParameters.ScreenProbeTracingOctahedronResolution), FScreenProbeSetupVisualizeTracesCS::GetGroupSize()));

		ConvertToExternalBuffer(GraphBuilder, VisualizeTracesData, GVisualizeTracesData);
	}
}

void FDeferredShadingSceneRenderer::RenderScreenProbeGatherVisualizeTraces(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FRDGTextureRef SceneColor)
{
	if (GLumenScreenProbeGatherVisualizeTraces && GVisualizeTracesData.IsValid())
	{
		FRDGBufferRef VisualizeTracesData = GraphBuilder.RegisterExternalBuffer(GVisualizeTracesData);
		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get();

		FVisualizeTraces* PassParameters = GraphBuilder.AllocParameters<FVisualizeTraces>();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad);
		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(GraphBuilder.RegisterExternalTexture(SceneContext.SceneDepthZ), ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthRead_StencilNop);
		PassParameters->VS.View = View.ViewUniformBuffer;
		PassParameters->VS.VisualizeTracesData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(VisualizeTracesData, PF_A32B32G32R32F));

		auto VertexShader = View.ShaderMap->GetShader<FVisualizeTracesVS>();
		auto PixelShader = View.ShaderMap->GetShader<FVisualizeTracesPS>();

		const int32 NumPrimitives = LumenScreenProbeGather::GetTracingOctahedronResolution() * LumenScreenProbeGather::GetTracingOctahedronResolution();

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("VisualizeTraces"),
			PassParameters,
			ERDGPassFlags::Raster,
			[PassParameters, VertexShader, PixelShader, &View, NumPrimitives](FRHICommandListImmediate& RHICmdList)
			{
				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

				RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);

				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI();
				GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB>::GetRHI();

				GraphicsPSOInit.PrimitiveType = PT_LineList;

				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GVisualizeTracesVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

				SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), PassParameters->VS);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PassParameters->PS);
			
				RHICmdList.SetStreamSource(0, nullptr, 0);
				RHICmdList.DrawPrimitive(0, NumPrimitives, 1);
			});
	}
}

FCompactedTraceParameters CompactTraces(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View, 
	const FScreenProbeParameters& ScreenProbeParameters,
	float CompactionTracingEndDistanceFromCamera,
	float CompactionMaxTraceDistance)
{
	FCompactedTraceParameters CompactedTraceParameters;
	FRDGBufferRef CompactedTraceTexelAllocator = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1), TEXT("CompactedTraceTexelAllocator"));
	const int32 NumCompactedTraceTexelDataElements = ScreenProbeParameters.ScreenProbeTraceBufferSize.X * ScreenProbeParameters.ScreenProbeTraceBufferSize.Y;
	FRDGBufferRef CompactedTraceTexelData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32) * 2, NumCompactedTraceTexelDataElements), TEXT("CompactedTraceTexelData"));

	CompactedTraceParameters.IndirectArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("CompactTracingIndirectArgs"));

	AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(CompactedTraceTexelAllocator, PF_R32_UINT), 0);

	{
		FScreenProbeCompactTracesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeCompactTracesCS::FParameters>();
		PassParameters->ScreenProbeParameters = ScreenProbeParameters;
		PassParameters->RWCompactedTraceTexelAllocator = GraphBuilder.CreateUAV(CompactedTraceTexelAllocator, PF_R32_UINT);
		PassParameters->RWCompactedTraceTexelData = GraphBuilder.CreateUAV(CompactedTraceTexelData, PF_R32G32_UINT);
		PassParameters->CompactionTracingEndDistanceFromCamera = CompactionTracingEndDistanceFromCamera;
		PassParameters->CompactionMaxTraceDistance = CompactionMaxTraceDistance;

		auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeCompactTracesCS>(0);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("CompactTraces"),
			ComputeShader,
			PassParameters,
			ScreenProbeParameters.ProbeIndirectArgs,
			(uint32)EScreenProbeIndirectArgs::ThreadPerTrace * sizeof(FRHIDispatchIndirectParameters));
	}

	{
		FSetupCompactedTracesIndirectArgsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSetupCompactedTracesIndirectArgsCS::FParameters>();
		PassParameters->ScreenProbeParameters = ScreenProbeParameters;
		PassParameters->RWScreenProbeCompactTracingIndirectArgs = GraphBuilder.CreateUAV(CompactedTraceParameters.IndirectArgs, PF_R32_UINT);
		PassParameters->CompactedTraceTexelAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CompactedTraceTexelAllocator, PF_R32_UINT));

		auto ComputeShader = View.ShaderMap->GetShader<FSetupCompactedTracesIndirectArgsCS>(0);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("SetupCompactedTracesIndirectArgs"),
			ComputeShader,
			PassParameters,
			FIntVector(1, 1, 1));
	}

	CompactedTraceParameters.CompactedTraceTexelAllocator = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CompactedTraceTexelAllocator, PF_R32_UINT));
	CompactedTraceParameters.CompactedTraceTexelData = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(CompactedTraceTexelData, PF_R32G32_UINT));

	return CompactedTraceParameters;
}

void TraceScreenProbes(
	FRDGBuilder& GraphBuilder, 
	const FScene* Scene,
	const FViewInfo& View, 
	bool bEnableSSGI,
	bool bTraceCards,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	const ScreenSpaceRayTracing::FPrevSceneColorMip& PrevSceneColor,
	const FLumenCardTracingInputs& TracingInputs,
	const LumenRadianceCache::FRadianceCacheParameters& RadianceCacheParameters,
	FScreenProbeParameters& ScreenProbeParameters,
	FLumenMeshSDFGridParameters& MeshSDFGridParameters)
{
	const FSceneTextureParameters SceneTextures = GetSceneTextureParameters(GraphBuilder, SceneTexturesUniformBuffer);

	{
		FClearTracesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FClearTracesCS::FParameters>();
		PassParameters->ScreenProbeParameters = ScreenProbeParameters;

		auto ComputeShader = View.ShaderMap->GetShader<FClearTracesCS>(0);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("ClearTraces"),
			ComputeShader,
			PassParameters,
			ScreenProbeParameters.ProbeIndirectArgs,
			(uint32)EScreenProbeIndirectArgs::ThreadPerTrace * sizeof(FRHIDispatchIndirectParameters));
	}

	FLumenIndirectTracingParameters IndirectTracingParameters;
	SetupLumenDiffuseTracingParameters(IndirectTracingParameters);

	if (bEnableSSGI)
	{
		FScreenProbeTraceScreenTexturesCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeTraceScreenTexturesCS::FParameters>();

		ScreenSpaceRayTracing::SetupCommonScreenSpaceRayParameters(GraphBuilder, SceneTextures, PrevSceneColor, View, /* out */ &PassParameters->ScreenSpaceRayParameters);

		PassParameters->ScreenSpaceRayParameters.CommonDiffuseParameters.SceneTextures = SceneTextures;

		{
			const FVector2D HZBUvFactor(
				float(View.ViewRect.Width()) / float(2 * View.HZBMipmap0Size.X),
				float(View.ViewRect.Height()) / float(2 * View.HZBMipmap0Size.Y));

			const FVector4 ScreenPositionScaleBias = View.GetScreenPositionScaleBias(SceneTextures.SceneDepthTexture->Desc.Extent, View.ViewRect);
			const FVector2D HZBUVToScreenUVScale = FVector2D(1.0f / HZBUvFactor.X, 1.0f / HZBUvFactor.Y) * FVector2D(2.0f, -2.0f) * FVector2D(ScreenPositionScaleBias.X, ScreenPositionScaleBias.Y);
			const FVector2D HZBUVToScreenUVBias = FVector2D(-1.0f, 1.0f) * FVector2D(ScreenPositionScaleBias.X, ScreenPositionScaleBias.Y) + FVector2D(ScreenPositionScaleBias.W, ScreenPositionScaleBias.Z);
			PassParameters->HZBUVToScreenUVScaleBias = FVector4(HZBUVToScreenUVScale, HZBUVToScreenUVBias);
		}

		checkf(View.ClosestHZB, TEXT("Lumen screen tracing: ClosestHZB was not setup, should have been setup by FDeferredShadingSceneRenderer::RenderHzb"));
		PassParameters->ClosestHZBTexture = View.ClosestHZB;
		PassParameters->SceneDepthTexture = SceneTextures.SceneDepthTexture;
		PassParameters->HZBBaseTexelSize = FVector2D(1.0f / View.ClosestHZB->Desc.Extent.X, 1.0f / View.ClosestHZB->Desc.Extent.Y);
		PassParameters->MaxHierarchicalScreenTraceIterations = GLumenScreenProbeGatherHierarchicalScreenTracesMaxIterations;
		PassParameters->UncertainTraceRelativeDepthThreshold = GLumenScreenProbeGatherUncertainTraceRelativeDepthThreshold;
		PassParameters->NumThicknessStepsToDetermineCertainty = GLumenScreenProbeGatherNumThicknessStepsToDetermineCertainty;

		PassParameters->ScreenProbeParameters = ScreenProbeParameters;
		PassParameters->IndirectTracingParameters = IndirectTracingParameters;
		PassParameters->RadianceCacheParameters = RadianceCacheParameters;

		FScreenProbeTraceScreenTexturesCS::FPermutationDomain PermutationVector;
		PermutationVector.Set< FScreenProbeTraceScreenTexturesCS::FRadianceCache >(LumenScreenProbeGather::UseRadianceCache(View));
		PermutationVector.Set< FScreenProbeTraceScreenTexturesCS::FHierarchicalScreenTracing >(GLumenScreenProbeGatherHierarchicalScreenTraces != 0);
		PermutationVector.Set< FScreenProbeTraceScreenTexturesCS::FStructuredImportanceSampling >(LumenScreenProbeGather::UseImportanceSampling());
		auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeTraceScreenTexturesCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TraceScreen"),
			ComputeShader,
			PassParameters,
			ScreenProbeParameters.ProbeIndirectArgs,
			(uint32)EScreenProbeIndirectArgs::ThreadPerTrace * sizeof(FRHIDispatchIndirectParameters));
	}

	
	if (bTraceCards)
	{
		CullForCardTracing(
			GraphBuilder,
			Scene, View,
			TracingInputs,
			ScreenProbeParameters.DownsampledDepth,
			ScreenProbeParameters.ScreenProbeDownsampleFactor,
			IndirectTracingParameters,
			/* out */ MeshSDFGridParameters);

		extern int32 GLumenDiffuseCubeMapTree;
		ensureMsgf(GLumenDiffuseCubeMapTree != 0, TEXT("Only CubeMapTree currently supported"));

		if (Lumen::UseHardwareRayTracedScreenProbeGather())
		{
			FRDGTextureRef NewTraceHit = GraphBuilder.CreateTexture(ScreenProbeParameters.TraceHit->Desc, TEXT("NewTraceHit"));
			ScreenProbeParameters.RWTraceHit = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(NewTraceHit));

			RenderHardwareRayTracingScreenProbe(GraphBuilder,
				Scene,
				SceneTextures,
				ScreenProbeParameters,
				View,
				TracingInputs,
				MeshSDFGridParameters,
				IndirectTracingParameters,
				RadianceCacheParameters);

			ScreenProbeParameters.TraceHit = NewTraceHit;
		}
		else if (MeshSDFGridParameters.TracingParameters.NumSceneObjects > 0)
		{
			FCompactedTraceParameters CompactedTraceParameters = CompactTraces(
				GraphBuilder,
				View,
				ScreenProbeParameters,
				IndirectTracingParameters.CardTraceEndDistanceFromCamera,
				IndirectTracingParameters.MaxCardTraceDistance);

			{
				FScreenProbeTraceCardsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeTraceCardsCS::FParameters>();
				GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters);
				PassParameters->MeshSDFGridParameters = MeshSDFGridParameters;
				PassParameters->ScreenProbeParameters = ScreenProbeParameters;
				PassParameters->IndirectTracingParameters = IndirectTracingParameters;
				PassParameters->SceneTexturesStruct = SceneTexturesUniformBuffer;
				PassParameters->CompactedTraceParameters = CompactedTraceParameters;

				FScreenProbeTraceCardsCS::FPermutationDomain PermutationVector;
				PermutationVector.Set< FScreenProbeTraceCardsCS::FStructuredImportanceSampling >(LumenScreenProbeGather::UseImportanceSampling());
				auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeTraceCardsCS>(PermutationVector);

				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("TraceCards"),
					ComputeShader,
					PassParameters,
					CompactedTraceParameters.IndirectArgs,
					0);
			}
		}
	}

	FCompactedTraceParameters CompactedTraceParameters = CompactTraces(
		GraphBuilder,
		View,
		ScreenProbeParameters,
		WORLD_MAX,
		// Make sure the shader runs on all misses to apply radiance cache + skylight
		IndirectTracingParameters.MaxTraceDistance + 1);

	{
		FScreenProbeTraceVoxelsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FScreenProbeTraceVoxelsCS::FParameters>();
		PassParameters->RadianceCacheParameters = RadianceCacheParameters;
		GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters);
		PassParameters->ScreenProbeParameters = ScreenProbeParameters;
		PassParameters->IndirectTracingParameters = IndirectTracingParameters;
		PassParameters->SceneTexturesStruct = SceneTexturesUniformBuffer;
		PassParameters->CompactedTraceParameters = CompactedTraceParameters;

		const bool bRadianceCache = LumenScreenProbeGather::UseRadianceCache(View);

		FScreenProbeTraceVoxelsCS::FPermutationDomain PermutationVector;
		PermutationVector.Set< FScreenProbeTraceVoxelsCS::FDynamicSkyLight >(ShouldRenderDynamicSkyLight(Scene, *View.Family));
		PermutationVector.Set< FScreenProbeTraceVoxelsCS::FTraceDistantScene >(Scene->LumenSceneData->DistantCardIndices.Num() > 0);
		PermutationVector.Set< FScreenProbeTraceVoxelsCS::FRadianceCache >(bRadianceCache);
		PermutationVector.Set< FScreenProbeTraceVoxelsCS::FStructuredImportanceSampling >(LumenScreenProbeGather::UseImportanceSampling());
		auto ComputeShader = View.ShaderMap->GetShader<FScreenProbeTraceVoxelsCS>(PermutationVector);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("TraceVoxels"),
			ComputeShader,
			PassParameters,
			CompactedTraceParameters.IndirectArgs,
			0);
	}

	if (GLumenScreenProbeGatherVisualizeTraces)
	{
		SetupVisualizeTraces(GraphBuilder, Scene, View, ScreenProbeParameters);
	}
}
