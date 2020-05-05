// Copyright Epic Games, Inc. All Rights Reserved.

#include "RayTracingDeferredMaterials.h"
#include "RHIDefinitions.h"
#include "RenderCore.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "DeferredShadingRenderer.h"
#include "PipelineStateCache.h"

#if RHI_RAYTRACING

class FRayTracingDeferredMaterialCHS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRayTracingDeferredMaterialCHS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FRayTracingDeferredMaterialCHS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("UE_RAY_TRACING_LIGHTWEIGHT_CLOSEST_HIT_SHADER"), 1);
	}

	using FParameters = FEmptyShaderParameters;
};

IMPLEMENT_GLOBAL_SHADER(FRayTracingDeferredMaterialCHS, "/Engine/Private/RayTracing/RayTracingDeferredMaterials.usf", "DeferredMaterialCHS", SF_RayHitGroup);

FRayTracingPipelineState* FDeferredShadingSceneRenderer::BindRayTracingDeferredMaterialGatherPipeline(FRHICommandList& RHICmdList, const FViewInfo& View, FRHIRayTracingShader* RayGenShader)
{
	SCOPE_CYCLE_COUNTER(STAT_BindRayTracingPipeline);

	FRayTracingPipelineStateInitializer Initializer;

	FRHIRayTracingShader* RayGenShaderTable[] = { RayGenShader };
	Initializer.SetRayGenShaderTable(RayGenShaderTable);

	Initializer.MaxPayloadSizeInBytes = 12; // sizeof FDeferredMaterialPayload

	// Get the ray tracing materials
	auto ClosestHitShader = View.ShaderMap->GetShader<FRayTracingDeferredMaterialCHS>();
	FRHIRayTracingShader* HitShaderTable[] = { ClosestHitShader.GetRayTracingShader() };
	Initializer.SetHitGroupTable(HitShaderTable);

	FRayTracingPipelineState* PipelineState = PipelineStateCache::GetAndOrCreateRayTracingPipelineState(RHICmdList, Initializer);

	const FViewInfo& ReferenceView = Views[0];

	const int32 NumTotalBindings = ReferenceView.VisibleRayTracingMeshCommands.Num();

	const uint32 MergedBindingsSize = sizeof(FRayTracingLocalShaderBindings) * NumTotalBindings;
	FRayTracingLocalShaderBindings* Bindings = (FRayTracingLocalShaderBindings*)(RHICmdList.Bypass()
		? FMemStack::Get().Alloc(MergedBindingsSize, alignof(FRayTracingLocalShaderBindings))
		: RHICmdList.Alloc(MergedBindingsSize, alignof(FRayTracingLocalShaderBindings)));

	uint32 BindingIndex = 0;

	for (const FVisibleRayTracingMeshCommand VisibleMeshCommand : ReferenceView.VisibleRayTracingMeshCommands)
	{
		const FRayTracingMeshCommand& MeshCommand = *VisibleMeshCommand.RayTracingMeshCommand;

		FRayTracingLocalShaderBindings Binding = {};
		Binding.InstanceIndex = VisibleMeshCommand.InstanceIndex;
		Binding.SegmentIndex = MeshCommand.GeometrySegmentIndex;
		Binding.UserData = MeshCommand.MaterialShaderIndex;

		Bindings[BindingIndex] = Binding;
		BindingIndex++;
	}

	const bool bCopyDataToInlineStorage = false; // Storage is already allocated from RHICmdList, no extra copy necessary
	RHICmdList.SetRayTracingHitGroups(
		View.RayTracingScene.RayTracingSceneRHI,
		PipelineState,
		NumTotalBindings, Bindings,
		bCopyDataToInlineStorage);

	return PipelineState;
}

class FMaterialSortCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMaterialSortCS);
	SHADER_USE_PARAMETER_STRUCT(FMaterialSortCS, FGlobalShader);

	class FSortSize : SHADER_PERMUTATION_INT("DIM_SORT_SIZE", 5);
	class FWaveOps  : SHADER_PERMUTATION_BOOL("DIM_WAVE_OPS");

	using FPermutationDomain = TShaderPermutationDomain<FSortSize, FWaveOps>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(int, NumTotalEntries)
		SHADER_PARAMETER_RDG_BUFFER_UAV(StructuredBuffer<FDeferredMaterialPayload>, MaterialBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		if (!ShouldCompileRayTracingShadersForProject(Parameters.Platform))
		{
			return false;
		}

		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (PermutationVector.Get<FWaveOps>() && !RHISupportsWaveOperations(Parameters.Platform))
		{
			return false;
		}

		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		FPermutationDomain PermutationVector(Parameters.PermutationId);

		if (PermutationVector.Get<FWaveOps>())
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
		}
	}
};


IMPLEMENT_GLOBAL_SHADER(FMaterialSortCS, "/Engine/Private/RayTracing/MaterialSort.usf", "MaterialSortLocal", SF_Compute);

void SortDeferredMaterials(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	uint32 SortSize,
	uint32 NumElements,
	FRDGBufferRef MaterialBuffer)
{
	if (SortSize == 0)
	{
		return;
	}
	SortSize = FMath::Min(SortSize, 5u);

	// Setup shader and parameters
	FMaterialSortCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FMaterialSortCS::FParameters>();
	PassParameters->NumTotalEntries = NumElements;
	PassParameters->MaterialBuffer = GraphBuilder.CreateUAV(MaterialBuffer);

	FMaterialSortCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FMaterialSortCS::FSortSize>(SortSize - 1);
	PermutationVector.Set<FMaterialSortCS::FWaveOps>(GRHISupportsWaveOperations && GRHIMinimumWaveSize >= 32 && RHISupportsWaveOperations(View.GetShaderPlatform()));

	// Sort size represents an index into pow2 sizes, not an actual size, so convert to the actual number of elements being sorted
	const uint32 ElementBlockSize = 256 * (1 << (SortSize - 1));
	const uint32 DispatchWidth = FMath::DivideAndRoundUp(NumElements, ElementBlockSize);

	TShaderMapRef<FMaterialSortCS> SortShader(View.ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("MaterialSort SortSize=%d NumElements=%d", ElementBlockSize, NumElements),
		SortShader,
		PassParameters,
		FIntVector(DispatchWidth, 1, 1));
}

#else // RHI_RAYTRACING

void SortDeferredMaterials(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	uint32 SortSize,
	uint32 NumElements,
	FRDGBufferRef MaterialBuffer)
{
	checkNoEntry();
}

#endif // RHI_RAYTRACING
