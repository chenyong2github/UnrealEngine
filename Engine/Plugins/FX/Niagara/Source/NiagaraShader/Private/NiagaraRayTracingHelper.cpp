// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraRayTracingHelper.h"
#include "NiagaraShader.h"

#if RHI_RAYTRACING

#include "GlobalShader.h"
#include "MeshPassProcessor.h"
#include "Renderer/Private/RayTracing/RayTracingScene.h"
#include "Renderer/Private/ScenePrivate.h"
#include "Renderer/Private/SceneRendering.h"
#include "ShaderParameterStruct.h"
#include "NiagaraShaderParticleID.h"

//////////////////////////////////////////////////////////////////////////

class FNiagaraUpdateCollisionGroupMapCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FNiagaraUpdateCollisionGroupMapCS);
	SHADER_USE_PARAMETER_STRUCT(FNiagaraUpdateCollisionGroupMapCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_UAV(Buffer<UINT>, RWHashTable)		
		SHADER_PARAMETER(uint32, HashTableSize)
		SHADER_PARAMETER_UAV(Buffer<UINT>, RWHashToCollisionGroups)
		SHADER_PARAMETER_SRV(Buffer<UINT>, NewPrimIdCollisionGroupPairs)
		SHADER_PARAMETER(uint32, NumNewPrims)
	END_SHADER_PARAMETER_STRUCT()
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsComputeShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), THREAD_COUNT);
	}

	static constexpr uint32 THREAD_COUNT = 64;
};
IMPLEMENT_GLOBAL_SHADER(FNiagaraUpdateCollisionGroupMapCS, "/Plugin/FX/Niagara/Private/NiagaraRayTraceCollisionGroupShaders.usf", "UpdatePrimIdToCollisionGroupMap", SF_Compute);

//////////////////////////////////////////////////////////////////////////

/// TODO
///  -get geometry masking working when an environmental mask is implemented

BEGIN_SHADER_PARAMETER_STRUCT(FGPUSceneParameters, )
SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUSceneInstanceSceneData)
SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUScenePrimitiveSceneData)
SHADER_PARAMETER(uint32, GPUSceneFrameNumber)
END_SHADER_PARAMETER_STRUCT()

class FNiagaraCollisionRayTraceRG : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FNiagaraCollisionRayTraceRG)
	SHADER_USE_ROOT_PARAMETER_STRUCT(FNiagaraCollisionRayTraceRG, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FGPUSceneParameters, GPUSceneParameters)
		SHADER_PARAMETER_SRV(Buffer<UINT>, HashTable)
		SHADER_PARAMETER_UAV(Buffer<UINT>, RWHashTable)
		SHADER_PARAMETER(uint32, HashTableSize)
		SHADER_PARAMETER_SRV(RaytracingAccelerationStructure, TLAS)
		SHADER_PARAMETER_SRV(Buffer<FNiagaraRayData>, Rays)
		SHADER_PARAMETER_UAV(Buffer<FNiagaraRayTracingResult>, CollisionOutput)
		SHADER_PARAMETER_SRV(Buffer<UINT>, RayTraceCounts)
		SHADER_PARAMETER_SRV(Buffer<UINT>, HashToCollisionGroups)
		SHADER_PARAMETER(uint32, MaxRetraces)
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
		OutEnvironment.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
		OutEnvironment.SetDefine(TEXT("USE_GLOBAL_GPU_SCENE_DATA"), 1);
	}

	static TShaderRef< FNiagaraCollisionRayTraceRG> GetShader(FGlobalShaderMap* ShaderMap)
	{
		FPermutationDomain PermutationVector;
		PermutationVector.Set<FFakeIndirectDispatch>(!SupportsIndirectDispatch());

		return ShaderMap->GetShader<FNiagaraCollisionRayTraceRG>(PermutationVector);
	}

	static FRHIRayTracingShader* GetRayTracingShader(FGlobalShaderMap* ShaderMap)
	{
		return GetShader(ShaderMap).GetRayTracingShader();
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

void FNiagaraRayTracingHelper::SetPrimitiveCollisionGroup(FPrimitiveSceneInfo& Primitive, uint32 CollisionGroup)
{
	CollisionGroupMap.FindOrAdd(Primitive.PrimitiveComponentId) = CollisionGroup;
	bCollisionGroupMapDirty = true;
}

void FNiagaraRayTracingHelper::RefreshPrimitiveInstanceData()
{
	bCollisionGroupMapDirty = true;
}

void FNiagaraRayTracingHelper::UpdateCollisionGroupMap(FRHICommandList& RHICmdList, FScene* Scene, ERHIFeatureLevel::Type FeatureLevel)
{
	if (bCollisionGroupMapDirty)
	{
		SCOPED_DRAW_EVENT(RHICmdList, NiagaraUpdateCollisionGroupsMap);
		bCollisionGroupMapDirty = false;

		uint32 MinBufferInstances = FNiagaraUpdateCollisionGroupMapCS::THREAD_COUNT;
		uint32 NeededInstances = FMath::Max((uint32)CollisionGroupMap.Num(), MinBufferInstances);
		uint32 AllocInstances = Align(NeededInstances, FNiagaraUpdateCollisionGroupMapCS::THREAD_COUNT);

		//We can probably be smarter here but for now just push the whole map over to the GPU each time it's dirty.
		//Ideally this should be a small amount of data and updated infrequently.
		FReadBuffer NewPrimIdCollisionGroupPairs;		
		NewPrimIdCollisionGroupPairs.Initialize(TEXT("NewPrimIdCollisionGroupPairs"), sizeof(uint32) * 2, AllocInstances, EPixelFormat::PF_R32G32_UINT, BUF_Volatile);

		uint32* PrimIdCollisionGroupPairPtr = (uint32*)RHILockBuffer(NewPrimIdCollisionGroupPairs.Buffer, 0, AllocInstances * sizeof(uint32) * 2, RLM_WriteOnly);
		FMemory::Memset(PrimIdCollisionGroupPairPtr, 0, AllocInstances * sizeof(uint32) * 2);

		for (auto Entry : CollisionGroupMap)
		{
			FPrimitiveComponentId PrimId = Entry.Key;
			uint32 CollisionGroup = Entry.Value;
			
			uint32 GPUSceneInstanceIndex = INDEX_NONE;
			//Ugh this is a bit pants. Maybe try to rework things so I can use the direct prim index.
			int32 PrimIndex = Scene->PrimitiveComponentIds.Find(PrimId);
			if (PrimIndex != INDEX_NONE)
			{
				GPUSceneInstanceIndex = Scene->Primitives[PrimIndex]->GetInstanceSceneDataOffset();
			}

			PrimIdCollisionGroupPairPtr[0] = GPUSceneInstanceIndex;
			PrimIdCollisionGroupPairPtr[1] = CollisionGroup;
			PrimIdCollisionGroupPairPtr += 2;
		}
		RHIUnlockBuffer(NewPrimIdCollisionGroupPairs.Buffer);

		//Init the hash table if needed
		if (HashTableSize < AllocInstances)
		{
			HashTableSize = AllocInstances;

			PrimIdHashTable.Release();
			PrimIdHashTable.Initialize(TEXT("NiagaraPrimIdHashTable"), sizeof(uint32), AllocInstances, EPixelFormat::PF_R32_UINT, BUF_Static);

			HashToCollisionGroups.Release();
			HashToCollisionGroups.Initialize(TEXT("NiagaraPrimIdHashToCollisionGroups"), sizeof(uint32), AllocInstances, EPixelFormat::PF_R32_UINT, BUF_Static);
		}

		RHICmdList.Transition(FRHITransitionInfo(PrimIdHashTable.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
		RHICmdList.Transition(FRHITransitionInfo(HashToCollisionGroups.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));

 		//First we have to clear the buffers. Can probably do this better.
 		NiagaraFillGPUIntBuffer(RHICmdList, FeatureLevel, PrimIdHashTable, 0);
 		RHICmdList.Transition(FRHITransitionInfo(PrimIdHashTable.UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));
 		NiagaraFillGPUIntBuffer(RHICmdList, FeatureLevel, HashToCollisionGroups, INDEX_NONE);
 		RHICmdList.Transition(FRHITransitionInfo(HashToCollisionGroups.UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));

		TShaderMapRef<FNiagaraUpdateCollisionGroupMapCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		FRHIComputeShader* ShaderRHI = ComputeShader.GetComputeShader();

		FNiagaraUpdateCollisionGroupMapCS::FParameters Params;
		Params.RWHashTable = PrimIdHashTable.UAV;
		Params.HashTableSize = HashTableSize;
		Params.RWHashToCollisionGroups = HashToCollisionGroups.UAV;
		Params.NewPrimIdCollisionGroupPairs = NewPrimIdCollisionGroupPairs.SRV;
		Params.NumNewPrims = CollisionGroupMap.Num();

		// To simplify the shader code, the size of the table must be a multiple of the thread count.
		check(HashTableSize % FNiagaraUpdateCollisionGroupMapCS::THREAD_COUNT == 0);

		RHICmdList.SetComputeShader(ShaderRHI);
		SetShaderParameters(RHICmdList, ComputeShader, ShaderRHI, Params);
		RHICmdList.DispatchComputeShader(FMath::DivideAndRoundUp(CollisionGroupMap.Num(), (int32)FNiagaraUpdateCollisionGroupMapCS::THREAD_COUNT), 1, 1);
		UnsetShaderUAVs(RHICmdList, ComputeShader, ShaderRHI);

		RHICmdList.Transition(FRHITransitionInfo(PrimIdHashTable.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVCompute));
		RHICmdList.Transition(FRHITransitionInfo(HashToCollisionGroups.UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVCompute));
	}
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
		auto RayGenShader = FNiagaraCollisionRayTraceRG::GetRayTracingShader(ShaderMap);
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

void FNiagaraRayTracingHelper::IssueRayTraces(FRHICommandList& RHICmdList, FScene* Scene, const FIntPoint& RayTraceCounts, uint32 MaxRetraces, FRHIShaderResourceView* RayTraceRequests, FRWBuffer* IndirectArgsBuffer, uint32 IndirectArgsOffset, FRHIUnorderedAccessView* RayTraceResults) const
{
	SCOPED_DRAW_EVENT(RHICmdList, NiagaraIssueCollisionRayTraces);
	check(RayTracingPipelineState);
	check(RayTracingScene);
	check(RayTracingSceneView);
	check(Scene);

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(ShaderPlatform);

	TShaderRef<FNiagaraCollisionRayTraceRG> RGShader = FNiagaraCollisionRayTraceRG::GetShader(ShaderMap);

	FNiagaraCollisionRayTraceRG::FParameters Params;

	Params.GPUSceneParameters.GPUSceneInstanceSceneData = Scene->GPUScene.InstanceSceneDataBuffer.SRV;
	Params.GPUSceneParameters.GPUScenePrimitiveSceneData = Scene->GPUScene.PrimitiveBuffer.SRV;
	Params.GPUSceneParameters.GPUSceneFrameNumber = Scene->GPUScene.GetSceneFrameNumber();

	Params.HashTable = PrimIdHashTable.SRV;
	Params.HashTableSize = HashTableSize;
	Params.TLAS = RayTracingSceneView;
	Params.Rays = RayTraceRequests;
	Params.CollisionOutput = RayTraceResults;
	Params.HashToCollisionGroups = HashToCollisionGroups.SRV;
	Params.MaxRetraces = MaxRetraces;
	FRayTracingShaderBindingsWriter GlobalResources;
	SetShaderParameters(GlobalResources, RGShader, Params);

	if (FNiagaraCollisionRayTraceRG::SupportsIndirectDispatch())
	{
		RHICmdList.Transition(FRHITransitionInfo(IndirectArgsBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::IndirectArgs | ERHIAccess::SRVCompute));

		RHICmdList.RayTraceDispatchIndirect(
			RayTracingPipelineState,
			RGShader.GetRayTracingShader(),
			RayTracingScene,
			GlobalResources,
			IndirectArgsBuffer->Buffer,
			IndirectArgsOffset);

		RHICmdList.Transition(FRHITransitionInfo(IndirectArgsBuffer->UAV, ERHIAccess::IndirectArgs | ERHIAccess::SRVCompute, ERHIAccess::UAVCompute));
	}
	else
	{
		RHICmdList.Transition(FRHITransitionInfo(IndirectArgsBuffer->UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVCompute));
		GlobalResources.SetSRV(2, IndirectArgsBuffer->SRV);

		RHICmdList.RayTraceDispatch(
			RayTracingPipelineState,
			RGShader.GetRayTracingShader(),
			RayTracingScene,
			GlobalResources,
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
