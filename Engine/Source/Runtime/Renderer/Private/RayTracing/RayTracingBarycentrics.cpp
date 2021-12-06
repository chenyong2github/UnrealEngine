// Copyright Epic Games, Inc. All Rights Reserved.

#include "RHI.h"

#if RHI_RAYTRACING

#include "DeferredShadingRenderer.h"
#include "GlobalShader.h"
#include "SceneRenderTargets.h"
#include "RenderGraphBuilder.h"
#include "RHI/Public/PipelineStateCache.h"
#include "RayTracing/RaytracingOptions.h"

class FRayTracingBarycentricsRGS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRayTracingBarycentricsRGS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FRayTracingBarycentricsRGS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, Output)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}
};
IMPLEMENT_GLOBAL_SHADER(FRayTracingBarycentricsRGS, "/Engine/Private/RayTracing/RayTracingBarycentrics.usf", "RayTracingBarycentricsMainRGS", SF_RayGen);

// Example closest hit shader
class FRayTracingBarycentricsCHS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRayTracingBarycentricsCHS);

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	FRayTracingBarycentricsCHS() = default;
	FRayTracingBarycentricsCHS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}
};
IMPLEMENT_SHADER_TYPE(, FRayTracingBarycentricsCHS, TEXT("/Engine/Private/RayTracing/RayTracingBarycentrics.usf"), TEXT("RayTracingBarycentricsMainCHS"), SF_RayHitGroup);

class FRayTracingBarycentricsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRayTracingBarycentricsCS)
	SHADER_USE_PARAMETER_STRUCT(FRayTracingBarycentricsCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, Output)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
		OutEnvironment.CompilerFlags.Add(CFLAG_InlineRayTracing);

		OutEnvironment.SetDefine(TEXT("INLINE_RAY_TRACING_THREAD_GROUP_SIZE_X"), ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("INLINE_RAY_TRACING_THREAD_GROUP_SIZE_Y"), ThreadGroupSizeY);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsRayTracingEnabledForProject(Parameters.Platform) && RHISupportsRayTracing(Parameters.Platform) && RHISupportsInlineRayTracing(Parameters.Platform);
	}

	// Current inline ray tracing implementation requires 1:1 mapping between thread groups and waves and only supports wave32 mode.
	static constexpr uint32 ThreadGroupSizeX = 8;
	static constexpr uint32 ThreadGroupSizeY = 4;
};
IMPLEMENT_GLOBAL_SHADER(FRayTracingBarycentricsCS, "/Engine/Private/RayTracing/RayTracingBarycentrics.usf", "RayTracingBarycentricsMainCS", SF_Compute);

void RenderRayTracingBarycentricsCS(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneColor, FGlobalShaderMap* ShaderMap)
{
	FRayTracingBarycentricsCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FRayTracingBarycentricsCS::FParameters>();

	PassParameters->TLAS = View.GetRayTracingSceneViewChecked();
	PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	PassParameters->Output = GraphBuilder.CreateUAV(SceneColor);

	FIntRect ViewRect = View.ViewRect;	

	auto ComputeShader = ShaderMap->GetShader<FRayTracingBarycentricsCS>();
	ClearUnusedGraphResources(ComputeShader, PassParameters);
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("Barycentrics"),
		PassParameters,
		ERDGPassFlags::Compute,
		[PassParameters, ComputeShader, &View, ViewRect](FRHIRayTracingCommandList& RHICmdList)
		{
			FRHIComputeShader* ShaderRHI = ComputeShader.GetComputeShader();
			RHICmdList.SetComputeShader(ShaderRHI);
			SetShaderParameters(RHICmdList, ComputeShader, ShaderRHI, *PassParameters);

			const FIntPoint GroupSize(FRayTracingBarycentricsCS::ThreadGroupSizeX, FRayTracingBarycentricsCS::ThreadGroupSizeY);
			const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(ViewRect.Size(), GroupSize);
			DispatchComputeShader(RHICmdList, ComputeShader.GetShader(), GroupCount.X, GroupCount.Y, 1);

			UnsetShaderUAVs(RHICmdList, ComputeShader, ShaderRHI);
		});
}

void RenderRayTracingBarycentricsRGS(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneColor, FGlobalShaderMap* ShaderMap)
{
	auto RayGenShader = ShaderMap->GetShader<FRayTracingBarycentricsRGS>();
	auto ClosestHitShader = ShaderMap->GetShader<FRayTracingBarycentricsCHS>();

	FRayTracingPipelineStateInitializer Initializer;

	FRHIRayTracingShader* RayGenShaderTable[] = { RayGenShader.GetRayTracingShader() };
	Initializer.SetRayGenShaderTable(RayGenShaderTable);

	FRHIRayTracingShader* HitGroupTable[] = { ClosestHitShader.GetRayTracingShader() };
	Initializer.SetHitGroupTable(HitGroupTable);
	Initializer.bAllowHitGroupIndexing = false; // Use the same hit shader for all geometry in the scene by disabling SBT indexing.

	FRayTracingPipelineState* Pipeline = PipelineStateCache::GetAndOrCreateRayTracingPipelineState(GraphBuilder.RHICmdList, Initializer);

	FRayTracingBarycentricsRGS::FParameters* RayGenParameters = GraphBuilder.AllocParameters<FRayTracingBarycentricsRGS::FParameters>();

	RayGenParameters->TLAS = View.GetRayTracingSceneViewChecked();
	RayGenParameters->ViewUniformBuffer = View.ViewUniformBuffer;
	RayGenParameters->Output = GraphBuilder.CreateUAV(SceneColor);

	FIntRect ViewRect = View.ViewRect;

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("Barycentrics"),
		RayGenParameters,
		ERDGPassFlags::Compute,
		[RayGenParameters, RayGenShader, &View, Pipeline, ViewRect](FRHIRayTracingCommandList& RHICmdList)
	{
		FRayTracingShaderBindingsWriter GlobalResources;
		SetShaderParameters(GlobalResources, RayGenShader, *RayGenParameters);

		// Dispatch rays using default shader binding table
		RHICmdList.RayTraceDispatch(Pipeline, RayGenShader.GetRayTracingShader(), View.GetRayTracingSceneChecked(), GlobalResources, ViewRect.Size().X, ViewRect.Size().Y);
	});
}

void FDeferredShadingSceneRenderer::RenderRayTracingBarycentrics(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneColor)
{
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);

	const bool bRayTracingInline = ShouldRenderRayTracingEffect(ERayTracingPipelineCompatibilityFlags::Inline);
	const bool bRayTracingPipeline = ShouldRenderRayTracingEffect(ERayTracingPipelineCompatibilityFlags::FullPipeline);

	if (bRayTracingInline)
	{
		RenderRayTracingBarycentricsCS(GraphBuilder, View, SceneColor, ShaderMap);
	}
	else if (bRayTracingPipeline)
	{
		RenderRayTracingBarycentricsRGS(GraphBuilder, View, SceneColor, ShaderMap);
	}
}
#endif