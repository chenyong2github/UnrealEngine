// Copyright Epic Games, Inc. All Rights Reserved.

#include "RHIDefinitions.h"

#if RHI_RAYTRACING

#include "RenderCore.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RayTracing/RayTracingScene.h"
#include "RenderGraphUtils.h"
#include "DeferredShadingRenderer.h"
#include "PipelineStateCache.h"
#include "ScenePrivate.h"
#include "ShaderCompilerCore.h"
#include "Lumen/LumenHardwareRayTracingCommon.h"

static TAutoConsoleVariable<float> CVarLumenHardwareRayTracingSkipBackFaceHitDistance(
	TEXT("r.Lumen.HardwareRayTracing.SkipBackFaceHitDistance"),
	5.0f,
	TEXT("Distance to trace with backface culling enabled, useful when the Ray Tracing geometry doesn't match the GBuffer (Nanite Proxy geometry)."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

static TAutoConsoleVariable<float> CVarLumenHardwareRayTracingSkipTwoSidedHitDistance(
	TEXT("r.Lumen.HardwareRayTracing.SkipTwoSidedHitDistance"),
	1.0f,
	TEXT("When the SkipBackFaceHitDistance is enabled, the first two-sided material hit within this distance will be skipped. This is useful for avoiding self-intersections with the Nanite fallback mesh on foliage, as SkipBackFaceHitDistance doesn't work on two sided materials."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

namespace LumenHardwareRayTracing
{
	// 0 - hit group with AVOID_SELF_INTERSECTIONS=0
	// 1 - hit group with AVOID_SELF_INTERSECTIONS=1
	constexpr uint32 NumHitGroups = 2;
};

IMPLEMENT_RT_PAYLOAD_TYPE(ERayTracingPayloadType::LumenMinimal, 20);

IMPLEMENT_UNIFORM_BUFFER_STRUCT(FLumenHardwareRayTracingUniformBufferParameters, "LumenHardwareRayTracingUniformBuffer");

class FLumenHardwareRayTracingMaterialHitGroup : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenHardwareRayTracingMaterialHitGroup)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FLumenHardwareRayTracingMaterialHitGroup, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FLumenHardwareRayTracingUniformBufferParameters, LumenHardwareRayTracingUniformBuffer)
	END_SHADER_PARAMETER_STRUCT()

	class FAvoidSelfIntersections : SHADER_PERMUTATION_BOOL("AVOID_SELF_INTERSECTIONS");
	using FPermutationDomain = TShaderPermutationDomain<FAvoidSelfIntersections>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform) && DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static ERayTracingPayloadType GetRayTracingPayloadType(const int32 PermutationId)
	{
		return ERayTracingPayloadType::LumenMinimal;
	}
};

IMPLEMENT_GLOBAL_SHADER(FLumenHardwareRayTracingMaterialHitGroup, "/Engine/Private/Lumen/LumenHardwareRayTracingMaterials.usf", "closesthit=LumenHardwareRayTracingMaterialCHS anyhit=LumenHardwareRayTracingMaterialAHS", SF_RayHitGroup);

class FLumenHardwareRayTracingMaterialMS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLumenHardwareRayTracingMaterialMS)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FLumenHardwareRayTracingMaterialMS, FGlobalShader)

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform) && DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
	}

	static ERayTracingPayloadType GetRayTracingPayloadType(const int32 PermutationId)
	{
		return ERayTracingPayloadType::LumenMinimal;
	}

	using FParameters = FEmptyShaderParameters;
};

IMPLEMENT_GLOBAL_SHADER(FLumenHardwareRayTracingMaterialMS, "/Engine/Private/Lumen/LumenHardwareRayTracingMaterials.usf", "LumenHardwareRayTracingMaterialMS", SF_RayMiss);

// TODO: This should be moved into FRayTracingScene and used as a base for other effects. There is not need for it to be Lumen specific.
void BuildHardwareRayTracingHitGroupData(FRHICommandList& RHICmdList, FRayTracingScene& RayTracingScene, TArrayView<FRayTracingLocalShaderBindings> Bindings, FRDGBufferRef DstBuffer)
{
	Lumen::FHitGroupRootConstants* DstBasePtr = (Lumen::FHitGroupRootConstants*)RHILockBuffer(DstBuffer->GetRHI(), 0, DstBuffer->GetSize(), RLM_WriteOnly);

	const FRayTracingSceneInitializer2& SceneInitializer = RayTracingScene.GetRHIRayTracingSceneChecked()->GetInitializer();

	for (const FRayTracingLocalShaderBindings& Binding : Bindings)
	{
		const uint32 InstanceIndex = Binding.InstanceIndex;
		const uint32 SegmentIndex = Binding.SegmentIndex;

		const uint32 HitGroupIndex = SceneInitializer.SegmentPrefixSum[InstanceIndex] + SegmentIndex;

		DstBasePtr[HitGroupIndex].BaseInstanceIndex = SceneInitializer.BaseInstancePrefixSum[InstanceIndex];
		DstBasePtr[HitGroupIndex].UserData = Binding.UserData;
	}

	RHIUnlockBuffer(DstBuffer->GetRHI());
}

void FDeferredShadingSceneRenderer::SetupLumenHardwareRayTracingHitGroupBuffer(FRDGBuilder& GraphBuilder, FViewInfo& View)
{
	const FRayTracingSceneInitializer2& SceneInitializer = Scene->RayTracingScene.GetRHIRayTracingSceneChecked()->GetInitializer();
	const uint32 NumTotalSegments = FMath::Max(SceneInitializer.NumTotalSegments, 1u);

	const uint32 ElementCount = NumTotalSegments;
	const uint32 ElementSize = sizeof(Lumen::FHitGroupRootConstants);
	
	View.LumenHardwareRayTracingHitDataBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredUploadDesc(ElementSize, ElementCount), TEXT("LumenHardwareRayTracingHitDataBuffer"));
}

void FDeferredShadingSceneRenderer::SetupLumenHardwareRayTracingUniformBuffer(FRDGBuilder& GraphBuilder, FViewInfo& View)
{
	FLumenHardwareRayTracingUniformBufferParameters* LumenHardwareRayTracingUniformBufferParameters = GraphBuilder.AllocParameters<FLumenHardwareRayTracingUniformBufferParameters>();
	LumenHardwareRayTracingUniformBufferParameters->SkipBackFaceHitDistance = CVarLumenHardwareRayTracingSkipBackFaceHitDistance.GetValueOnRenderThread();
	LumenHardwareRayTracingUniformBufferParameters->SkipTwoSidedHitDistance = CVarLumenHardwareRayTracingSkipTwoSidedHitDistance.GetValueOnRenderThread();;
	View.LumenHardwareRayTracingUniformBuffer = GraphBuilder.CreateUniformBuffer(LumenHardwareRayTracingUniformBufferParameters);
}

FRayTracingLocalShaderBindings* FDeferredShadingSceneRenderer::BuildLumenHardwareRayTracingMaterialBindings(FRHICommandList& RHICmdList, const FViewInfo& View, FRDGBufferRef OutHitGroupDataBuffer, bool bInlineOnly)
{
	const FViewInfo& ReferenceView = Views[0];
	const int32 NumTotalBindings = LumenHardwareRayTracing::NumHitGroups * ReferenceView.VisibleRayTracingMeshCommands.Num();

	auto Alloc = [&](uint32 Size, uint32 Align)
	{
		return RHICmdList.Bypass() || bInlineOnly
			? Allocator.Malloc(Size, Align)
			: RHICmdList.Alloc(Size, Align);
	};

	const uint32 MergedBindingsSize = sizeof(FRayTracingLocalShaderBindings) * NumTotalBindings;
	FRayTracingLocalShaderBindings* Bindings = (FRayTracingLocalShaderBindings*)Alloc(MergedBindingsSize, alignof(FRayTracingLocalShaderBindings));

	const uint32 NumUniformBuffers = 1;
	FRHIUniformBuffer** UniformBufferArray = (FRHIUniformBuffer**)Alloc(sizeof(FRHIUniformBuffer*) * NumUniformBuffers, alignof(FRHIUniformBuffer*));
	UniformBufferArray[0] = ReferenceView.LumenHardwareRayTracingUniformBuffer->GetRHI();

	int32 ShaderIndexInPipelinePerHitGroup[2] = { 0, 1 };

	if (GRHISupportsRayTracingShaders)
	{
		FLumenHardwareRayTracingMaterialHitGroup::FPermutationDomain PermutationVector;
		PermutationVector.Set<FLumenHardwareRayTracingMaterialHitGroup::FAvoidSelfIntersections>(false);
		auto HitGroupShader = View.ShaderMap->GetShader<FLumenHardwareRayTracingMaterialHitGroup>(PermutationVector).GetRayTracingShader();

		PermutationVector.Set<FLumenHardwareRayTracingMaterialHitGroup::FAvoidSelfIntersections>(true);
		auto HitGroupShaderWithAvoidSelfIntersections = View.ShaderMap->GetShader<FLumenHardwareRayTracingMaterialHitGroup>(PermutationVector).GetRayTracingShader();

		int32 ShaderIndexInPipelinePerHitGroup[2];
		ShaderIndexInPipelinePerHitGroup[0] = FindRayTracingHitGroupIndex(View.LumenHardwareRayTracingMaterialPipeline, HitGroupShader, true);
		ShaderIndexInPipelinePerHitGroup[1] = FindRayTracingHitGroupIndex(View.LumenHardwareRayTracingMaterialPipeline, HitGroupShaderWithAvoidSelfIntersections, true);
	}

	uint32 BindingIndex = 0;
	for (const FVisibleRayTracingMeshCommand VisibleMeshCommand : ReferenceView.VisibleRayTracingMeshCommands)
	{
		const FRayTracingMeshCommand& MeshCommand = *VisibleMeshCommand.RayTracingMeshCommand;

		for (uint32 HitGroupIndex = 0; HitGroupIndex < LumenHardwareRayTracing::NumHitGroups; ++HitGroupIndex)
		{
			FRayTracingLocalShaderBindings Binding = {};
			Binding.ShaderSlot = HitGroupIndex;
			Binding.ShaderIndexInPipeline = ShaderIndexInPipelinePerHitGroup[HitGroupIndex];
			Binding.InstanceIndex = VisibleMeshCommand.InstanceIndex;
			Binding.SegmentIndex = MeshCommand.GeometrySegmentIndex;
			uint32 PackedUserData = (MeshCommand.MaterialShaderIndex & 0x3FFFFFFF)
				| (((MeshCommand.bTwoSided != 0) & 0x01) << 30)
				| (((MeshCommand.bIsTranslucent != 0) & 0x01) << 31);
			Binding.UserData = PackedUserData;
			Binding.UniformBuffers = UniformBufferArray;
			Binding.NumUniformBuffers = NumUniformBuffers;

			Bindings[BindingIndex] = Binding;
			BindingIndex++;
		}
	}

	if (OutHitGroupDataBuffer)
	{
		BuildHardwareRayTracingHitGroupData(RHICmdList, Scene->RayTracingScene, MakeArrayView(Bindings, NumTotalBindings), OutHitGroupDataBuffer);
	}

	return Bindings;
}

FRayTracingPipelineState* FDeferredShadingSceneRenderer::CreateLumenHardwareRayTracingMaterialPipeline(FRHICommandList& RHICmdList, const FViewInfo& View, const TArrayView<FRHIRayTracingShader*>& RayGenShaderTable)
{
	SCOPE_CYCLE_COUNTER(STAT_BindRayTracingPipeline);
	
	FRayTracingPipelineStateInitializer Initializer;

	Initializer.SetRayGenShaderTable(RayGenShaderTable);

	Initializer.MaxPayloadSizeInBytes = GetRayTracingPayloadTypeMaxSize(ERayTracingPayloadType::LumenMinimal);

	FLumenHardwareRayTracingMaterialHitGroup::FPermutationDomain PermutationVector;
	PermutationVector.Set<FLumenHardwareRayTracingMaterialHitGroup::FAvoidSelfIntersections>(false);
	auto HitGroupShader = View.ShaderMap->GetShader<FLumenHardwareRayTracingMaterialHitGroup>(PermutationVector);

	PermutationVector.Set<FLumenHardwareRayTracingMaterialHitGroup::FAvoidSelfIntersections>(true);
	auto HitGroupShaderWithAvoidSelfIntersections = View.ShaderMap->GetShader<FLumenHardwareRayTracingMaterialHitGroup>(PermutationVector);

	FRHIRayTracingShader* HitShaderTable[] = { HitGroupShader.GetRayTracingShader(), HitGroupShaderWithAvoidSelfIntersections.GetRayTracingShader() };
	Initializer.SetHitGroupTable(HitShaderTable);
	Initializer.bAllowHitGroupIndexing = true;

	auto MissShader = View.ShaderMap->GetShader<FLumenHardwareRayTracingMaterialMS>();
	FRHIRayTracingShader* MissShaderTable[] = { MissShader.GetRayTracingShader() };
	Initializer.SetMissShaderTable(MissShaderTable);

	FRayTracingPipelineState* PipelineState = PipelineStateCache::GetAndOrCreateRayTracingPipelineState(RHICmdList, Initializer);

	return PipelineState;
}

void FDeferredShadingSceneRenderer::BindLumenHardwareRayTracingMaterialPipeline(FRHICommandListImmediate& RHICmdList, FRayTracingLocalShaderBindings* Bindings, const FViewInfo& View, FRayTracingPipelineState* PipelineState, FRDGBufferRef OutHitGroupDataBuffer)
{
	// If we haven't build bindings before we need to build them here
	if (Bindings == nullptr)
	{
		Bindings = BuildLumenHardwareRayTracingMaterialBindings(RHICmdList, View, OutHitGroupDataBuffer, false);
	}

	const int32 NumTotalBindings = LumenHardwareRayTracing::NumHitGroups * View.VisibleRayTracingMeshCommands.Num();	

	const bool bCopyDataToInlineStorage = false; // Storage is already allocated from RHICmdList, no extra copy necessary
	RHICmdList.SetRayTracingHitGroups(
		View.GetRayTracingSceneChecked(),
		PipelineState,
		NumTotalBindings,
		Bindings,
		bCopyDataToInlineStorage);
}

#endif // RHI_RAYTRACING