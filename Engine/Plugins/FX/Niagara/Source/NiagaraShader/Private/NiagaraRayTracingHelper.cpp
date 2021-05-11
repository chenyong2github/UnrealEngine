// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraRayTracingHelper.h"

#if RHI_RAYTRACING

#include "GlobalShader.h"
#include "MeshPassProcessor.h"
#include "Renderer/Private/RayTracing/RayTracingScene.h"
#include "Renderer/Private/ScenePrivate.h"
#include "Renderer/Private/SceneRendering.h"
#include "ShaderParameterStruct.h"

/// TODO
///  -get geometry masking working when an environmental mask is implemented

class FNiagaraCollisionRayTraceRG : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FNiagaraCollisionRayTraceRG)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FNiagaraCollisionRayTraceRG, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FBasicRayData>, Rays)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<FNiagaraRayTracingPayload>, CollisionOutput)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<UINT>, RayTraceCounts)
	END_SHADER_PARAMETER_STRUCT()

	class FFakeIndirectDispatch : SHADER_PERMUTATION_BOOL("NIAGARA_RAYTRACE_FAKE_INDIRECT");
	using FPermutationDomain = TShaderPermutationDomain<FFakeIndirectDispatch>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("NIAGARA_SUPPORTS_RAY_TRACING"), 1);
	}

	static FRHIRayTracingShader* GetShader(FGlobalShaderMap* ShaderMap)
	{
		FPermutationDomain PermutationVector;
		PermutationVector.Set<FFakeIndirectDispatch>(!SupportsIndirectDispatch());

		return ShaderMap->GetShader<FNiagaraCollisionRayTraceRG>(PermutationVector).GetRayTracingShader();
	}

	static bool SupportsIndirectDispatch()
	{
		return GRHISupportsRayTracingDispatchIndirect;
	}
};

class FNiagaraCollisionRayTraceCH : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FNiagaraCollisionRayTraceCH, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("NIAGARA_SUPPORTS_RAY_TRACING"), 1);
	}

	FNiagaraCollisionRayTraceCH() = default;
	FNiagaraCollisionRayTraceCH(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}
};

class FNiagaraCollisionRayTraceMiss : public FGlobalShader
{
	DECLARE_SHADER_TYPE(FNiagaraCollisionRayTraceMiss, Global);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return ShouldCompileRayTracingShadersForProject(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("NIAGARA_SUPPORTS_RAY_TRACING"), 1);
	}

	FNiagaraCollisionRayTraceMiss() = default;
	FNiagaraCollisionRayTraceMiss(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}
};

IMPLEMENT_GLOBAL_SHADER(FNiagaraCollisionRayTraceRG, "/Plugin/FX/Niagara/Private/NiagaraRayTracingShaders.usf", "NiagaraCollisionRayTraceRG", SF_RayGen);
IMPLEMENT_GLOBAL_SHADER(FNiagaraCollisionRayTraceCH, "/Plugin/FX/Niagara/Private/NiagaraRayTracingShaders.usf", "NiagaraCollisionRayTraceCH", SF_RayHitGroup);
IMPLEMENT_GLOBAL_SHADER(FNiagaraCollisionRayTraceMiss, "/Plugin/FX/Niagara/Private/NiagaraRayTracingShaders.usf", "NiagaraCollisionRayTraceMiss", SF_RayMiss);

void FNiagaraRayTracingHelper::Reset()
{
	RayTracingPipelineState = nullptr;
	RayTracingScene = nullptr;
	RayTracingSceneView = nullptr;
	ViewUniformBuffer = nullptr;
}

static FRayTracingPipelineState* CreateNiagaraRayTracingPipelineState(
	EShaderPlatform Platform,
	FRHICommandList& RHICmdList,
	FRHIRayTracingShader* RayGenShader,
	FRHIRayTracingShader* ClosestHitShader,
	FRHIRayTracingShader* MissShader)
{
	FRayTracingPipelineStateInitializer Initializer;
	Initializer.MaxPayloadSizeInBytes = sizeof(FNiagaraRayTracingPayload);

	FRHIRayTracingShader* RayGenShaderTable[] = { RayGenShader };
	Initializer.SetRayGenShaderTable(RayGenShaderTable);

	FRHIRayTracingShader* HitGroupTable[] = { ClosestHitShader };
	Initializer.SetHitGroupTable(HitGroupTable);

	FRHIRayTracingShader* MissTable[] = { MissShader };
	Initializer.SetMissShaderTable(MissTable);

	Initializer.bAllowHitGroupIndexing = true; // Use the same hit shader for all geometry in the scene by disabling SBT indexing.

	return PipelineStateCache::GetAndOrCreateRayTracingPipelineState(RHICmdList, Initializer);
}

static void BindNiagaraRayTracingMeshCommands(
	FRHICommandList& RHICmdList,
	FRayTracingSceneRHIRef RayTracingScene,
	FRHIUniformBuffer* ViewUniformBuffer,
	TConstArrayView<FVisibleRayTracingMeshCommand> RayTracingMeshCommands,
	FRayTracingPipelineState* Pipeline,
	TFunctionRef<uint32(const FRayTracingMeshCommand&)> PackUserData)
{
	const int32 NumTotalBindings = RayTracingMeshCommands.Num();

	const uint32 MergedBindingsSize = sizeof(FRayTracingLocalShaderBindings) * NumTotalBindings;

	const uint32 NumUniformBuffers = 1;
	const uint32 UniformBufferArraySize = sizeof(FRHIUniformBuffer*) * NumUniformBuffers;

	FRayTracingLocalShaderBindings* Bindings = nullptr;
	FRHIUniformBuffer** UniformBufferArray = nullptr;

	if (RHICmdList.Bypass())
	{
		FMemStack& MemStack = FMemStack::Get();

		Bindings = reinterpret_cast<FRayTracingLocalShaderBindings*>(MemStack.Alloc(MergedBindingsSize, alignof(FRayTracingLocalShaderBindings)));
		UniformBufferArray = reinterpret_cast<FRHIUniformBuffer**>(MemStack.Alloc(UniformBufferArraySize, alignof(FRHIUniformBuffer*)));
	}
	else
	{
		Bindings = reinterpret_cast<FRayTracingLocalShaderBindings*>(RHICmdList.Alloc(MergedBindingsSize, alignof(FRayTracingLocalShaderBindings)));
		UniformBufferArray = reinterpret_cast<FRHIUniformBuffer**>(RHICmdList.Alloc(UniformBufferArraySize, alignof(FRHIUniformBuffer*)));
	}

	UniformBufferArray[0] = ViewUniformBuffer;

	uint32 BindingIndex = 0;
	for (const FVisibleRayTracingMeshCommand VisibleMeshCommand : RayTracingMeshCommands)
	{
		const FRayTracingMeshCommand& MeshCommand = *VisibleMeshCommand.RayTracingMeshCommand;

		FRayTracingLocalShaderBindings Binding = {};
		Binding.InstanceIndex = VisibleMeshCommand.InstanceIndex;
		Binding.SegmentIndex = MeshCommand.GeometrySegmentIndex;
		Binding.UserData = PackUserData(MeshCommand);
		Binding.UniformBuffers = UniformBufferArray;
		Binding.NumUniformBuffers = NumUniformBuffers;

		Bindings[BindingIndex] = Binding;
		BindingIndex++;
	}

	const bool bCopyDataToInlineStorage = false; // Storage is already allocated from RHICmdList, no extra copy necessary
	RHICmdList.SetRayTracingHitGroups(
		RayTracingScene,
		Pipeline,
		NumTotalBindings,
		Bindings,
		bCopyDataToInlineStorage);
}

void FNiagaraRayTracingHelper::BuildRayTracingSceneInfo(FRHICommandList& RHICmdList, TConstArrayView<FViewInfo> Views)
{
	check(Views.Num() > 0);

	const FViewInfo& ReferenceView = Views[0];
	const FScene* RenderScene = (ReferenceView.Family && ReferenceView.Family->Scene) ? ReferenceView.Family->Scene->GetRenderScene() : nullptr;

	if (RenderScene && RenderScene->RayTracingScene.IsCreated())
	{
		RayTracingScene = RenderScene->RayTracingScene.GetRHIRayTracingSceneChecked();
		RayTracingSceneView = RenderScene->RayTracingScene.GetShaderResourceViewChecked();
		ViewUniformBuffer = ReferenceView.ViewUniformBuffer;

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(ShaderPlatform);
		auto RayGenShader = FNiagaraCollisionRayTraceRG::GetShader(ShaderMap);
		auto ClosestHitShader = ShaderMap->GetShader<FNiagaraCollisionRayTraceCH>().GetRayTracingShader();
		auto MissShader = ShaderMap->GetShader<FNiagaraCollisionRayTraceMiss>().GetRayTracingShader();

		RayTracingPipelineState = CreateNiagaraRayTracingPipelineState(
			ShaderPlatform,
			RHICmdList,
			RayGenShader,
			ClosestHitShader,
			MissShader);

		// some options for what we want with our per MeshCommand user data.  For now we'll ignore it, but possibly
		// something we'd want to incorporate.  Some examples could be if the material is translucent, or possibly the physical material?
		auto BakeTranslucent = [&](const FRayTracingMeshCommand& MeshCommand) {	return (MeshCommand.bIsTranslucent != 0) & 0x1;	};
		auto BakeDefault = [&](const FRayTracingMeshCommand& MeshCommand) { return 0; };

		BindNiagaraRayTracingMeshCommands(
			RHICmdList,
			RayTracingScene,
			ViewUniformBuffer,
			ReferenceView.VisibleRayTracingMeshCommands,
			RayTracingPipelineState,
			BakeDefault);
	}
	else
	{
		Reset();
	}
}

void FNiagaraRayTracingHelper::IssueRayTraces(FRHICommandList& RHICmdList, const FIntPoint& RayTraceCounts, FRHIShaderResourceView* RayTraceRequests, FRWBuffer* IndirectArgsBuffer, uint32 IndirectArgsOffset, FRHIUnorderedAccessView* RayTraceResults) const
{
	check(RayTracingPipelineState);
	check(RayTracingScene);
	check(RayTracingSceneView);

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(ShaderPlatform);

	FRayTracingShaderBindings GlobalBindings;
	GlobalBindings.SRVs[0] = RayTracingSceneView;
	GlobalBindings.SRVs[1] = RayTraceRequests;
	GlobalBindings.UAVs[0] = RayTraceResults;

	if (FNiagaraCollisionRayTraceRG::SupportsIndirectDispatch())
	{
		RHICmdList.Transition(FRHITransitionInfo(IndirectArgsBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::IndirectArgs | ERHIAccess::SRVCompute));

		RHICmdList.RayTraceDispatchIndirect(
			RayTracingPipelineState,
			FNiagaraCollisionRayTraceRG::GetShader(ShaderMap),
			RayTracingScene,
			GlobalBindings,
			IndirectArgsBuffer->Buffer,
			IndirectArgsOffset);

		RHICmdList.Transition(FRHITransitionInfo(IndirectArgsBuffer->UAV, ERHIAccess::IndirectArgs | ERHIAccess::SRVCompute, ERHIAccess::UAVCompute));
	}
	else
	{
		RHICmdList.Transition(FRHITransitionInfo(IndirectArgsBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVCompute));
		GlobalBindings.SRVs[2] = IndirectArgsBuffer->SRV;

		RHICmdList.RayTraceDispatch(
			RayTracingPipelineState,
			FNiagaraCollisionRayTraceRG::GetShader(ShaderMap),
			RayTracingScene,
			GlobalBindings,
			RayTraceCounts.X,
			RayTraceCounts.Y
		);

		RHICmdList.Transition(FRHITransitionInfo(IndirectArgsBuffer->UAV, ERHIAccess::SRVCompute, ERHIAccess::UAVCompute));
	}
}

bool FNiagaraRayTracingHelper::IsValid() const
{
	return RayTracingPipelineState
		&& RayTracingScene
		&& RayTracingSceneView
		&& ViewUniformBuffer;
}

#endif