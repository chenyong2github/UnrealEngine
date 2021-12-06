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
SHADER_PARAMETER_SRV(StructuredBuffer<float4>, GPUSceneInstancePayloadData)
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
		SHADER_PARAMETER(uint32, RaysOffset)
		SHADER_PARAMETER_UAV(Buffer<FNiagaraRayTracingResult>, CollisionOutput)
		SHADER_PARAMETER(uint32, CollisionOutputOffset)
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
	DECLARE_GLOBAL_SHADER(FNiagaraCollisionRayTraceCH);

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
	DECLARE_GLOBAL_SHADER(FNiagaraCollisionRayTraceMiss);

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


int32 GNiagaraHWRTScratchBucketSize = 1024;
static FAutoConsoleVariableRef CVarNiagaraHWRTScratchBucketSize(
	TEXT("fx.Niagara.RayTracing.ScratchPadBucketSize"),
	GNiagaraHWRTScratchBucketSize,
	TEXT("Size (in elements) for Niagara hardware ray tracing scratch buffer buckets. \n"),
	ECVF_Default
);

int32 GNiagaraHWRTCountsScratchBucketSize = 3 * 256;
static FAutoConsoleVariableRef CVarNiagaraHWRTCountsScratchBucketSize(
	TEXT("fx.Niagara.RayTracing.CountsScratchPadBucketSize"),
	GNiagaraHWRTCountsScratchBucketSize,
	TEXT("Scratch bucket size for the HWRT counts buffer. This buffer requires 4. \n"),
	ECVF_Default
);

FNiagaraRayTracingHelper::FNiagaraRayTracingHelper(EShaderPlatform InShaderPlatform)
	: ShaderPlatform(InShaderPlatform)
	, RayRequests(GNiagaraHWRTScratchBucketSize, TEXT("NiagaraRayRequests"))
	, RayTraceIntersections(GNiagaraHWRTScratchBucketSize, TEXT("NiagaraRayTraceIntersections"))
	, RayTraceCounts(GNiagaraHWRTCountsScratchBucketSize, TEXT("NiagaraRayTraceCounts"), BUF_Static | BUF_DrawIndirect)
{}

FNiagaraRayTracingHelper::~FNiagaraRayTracingHelper()
{
	Reset();
	Dispatches.Reset();

	//TODO: Move RT Helper out of NiagaraShader and into Niagara. Makes general sense plus it would allow use to hook into these memory stats.
// 	DEC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, RayRequests.AllocatedBytes());
// 	DEC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, RayTraceIntersections.AllocatedBytes());
// 	DEC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, RayTraceCounts.AllocatedBytes());

	RayRequests.Release();
	RayTraceIntersections.Release();
	RayTraceCounts.Release();
}

void FNiagaraRayTracingHelper::Reset()
{
	RayTracingPipelineState = nullptr;
	RayTracingScene = nullptr;
	RayTracingSceneView = nullptr;
	ViewUniformBuffer = nullptr;
}

void FNiagaraRayTracingHelper::BeginFrame(FRHICommandList& RHICmdList, bool HasRayTracingScene)
{
	//Store off last frames dispatches so we can still access the previous results buffers for reading in this frame's simulations.
	PreviousFrameDispatches = MoveTemp(Dispatches);
	Dispatches.Reset();

	//Ensure we're each buffer is definitely in the right access state.
	RayRequests.Transition(RHICmdList, ERHIAccess::Unknown, ERHIAccess::UAVCompute);//Simulations will write new ray trace requests.
	RayTraceIntersections.Transition(RHICmdList, ERHIAccess::Unknown, ERHIAccess::SRVCompute);//Simulations will read from the previous frames results.
	RayTraceCounts.Transition(RHICmdList, ERHIAccess::Unknown, ERHIAccess::UAVCompute);//Simulations will accumulate the number of traces requests here.

	//Reset the allocations.
	RayRequests.Reset();

	//Note this doesn't change any buffers or data themselves and the ray intersections buffer are still going to be read as SRVs in the up coming simulations dispatches.
	//We're just clearing existing allocations here.
	RayTraceIntersections.Reset();

	//We have to also clear the counts/indirect args buffer.
	RayTraceCounts.Reset(RHICmdList, true);

	// Clear the dummy buffer allocations.
	DummyDispatch.Reset();

	RayTracingPipelineState = nullptr;
	RayTracingScene = nullptr;
	RayTracingSceneView = nullptr;
	ViewUniformBuffer = nullptr;
}

void FNiagaraRayTracingHelper::EndFrame(FRHICommandList& RHICmdList, bool HasRayTracingScene, FScene* Scene)
{
	// skip everything if we don't have a ray tracing scene
	if (HasRayTracingScene)
	{
		RayRequests.Transition(RHICmdList, ERHIAccess::UAVCompute, ERHIAccess::SRVCompute);//Ray trace dispatches will read the ray requests.
		RayTraceIntersections.Transition(RHICmdList, ERHIAccess::SRVCompute, ERHIAccess::UAVCompute);//Ray trace dispatches will write new results.
		RayTraceCounts.Transition(RHICmdList, ERHIAccess::UAVCompute, ERHIAccess::IndirectArgs | ERHIAccess::SRVCompute);//Ray trace dispatches read these counts and use them for indirect dispatches.
	
		IssueRayTraces(RHICmdList, Scene);

		RayRequests.Transition(RHICmdList, ERHIAccess::SRVCompute, ERHIAccess::UAVCompute);//Next frame, simulations will write new ray trace requests.
		RayTraceIntersections.Transition(RHICmdList, ERHIAccess::UAVCompute, ERHIAccess::SRVCompute);//Next frame these results will be read by simulations shaders.
		RayTraceCounts.Transition(RHICmdList, ERHIAccess::IndirectArgs | ERHIAccess::SRVCompute, ERHIAccess::UAVCompute);//Next frame these counts will be written by simulation shaders.
	}
}

void FNiagaraRayTracingHelper::AddToDispatch(FNiagaraDataInterfaceProxy* DispatchKey, uint32 MaxRays, int32 MaxRetraces)
{
	FNiagaraRayTraceDispatchInfo& Dispatch = Dispatches.FindOrAdd(DispatchKey);
	Dispatch.MaxRays += MaxRays;
	Dispatch.MaxRetraces = MaxRetraces;
}

void FNiagaraRayTracingHelper::BuildDispatch(FRHICommandList& RHICmdList, FNiagaraDataInterfaceProxy* DispatchKey)
{
	FNiagaraRayTraceDispatchInfo& Dispatch = Dispatches.FindChecked(DispatchKey);

	//Finalize allocations first time we access the dispatch.
	if (Dispatch.RayRequests.IsValid() == false)
	{
		//TODO: Move RT Helper out of NiagaraShader and into Niagara. Makes general sense plus it would allow use to hook into these memory stats.
// 		DEC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, RayRequests.AllocatedBytes());
// 		DEC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, RayTraceIntersections.AllocatedBytes());
// 		DEC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, RayTraceCounts.AllocatedBytes());

		Dispatch.RayRequests = RayRequests.Alloc(Dispatch.MaxRays);
		Dispatch.RayTraceIntersections = RayTraceIntersections.Alloc(Dispatch.MaxRays);
		Dispatch.RayCounts = RayTraceCounts.Alloc<uint32>(3, RHICmdList, true);

		//TODO: Could add a delegate to call from the scratch when new buffers are allocated to reduce stat spam here?
// 		INC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, RayRequests.AllocatedBytes());
// 		INC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, RayTraceIntersections.AllocatedBytes());
// 		INC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, RayTraceCounts.AllocatedBytes());

		//Find last frame's results buffer if there was one. Ideally we can do this without the map but it will need a bit of wrangling.
		FNiagaraRayTraceDispatchInfo* PrevDispatch = PreviousFrameDispatches.Find(DispatchKey);
		if (PrevDispatch && PrevDispatch->RayTraceIntersections.IsValid())
		{
			Dispatch.LastFrameRayTraceIntersections = PrevDispatch->RayTraceIntersections;
		}
		else
		{
			//Set the current frame results just so this is a valid buffer.
			//If we didn't have a results buffer last frame then this won't actually be read.
			Dispatch.LastFrameRayTraceIntersections = Dispatch.RayTraceIntersections;
		}
	}
}

void FNiagaraRayTracingHelper::BuildDummyDispatch(FRHICommandList& RHICmdList)
{
	if (DummyDispatch.IsValid() == false)
	{
		//TODO: Move RT Helper out of NiagaraShader and into Niagara. Makes general sense plus it would allow use to hook into these memory stats.
// 		DEC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, RayRequests.AllocatedBytes());
// 		DEC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, RayTraceIntersections.AllocatedBytes());
// 		DEC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, RayTraceCounts.AllocatedBytes());

		DummyDispatch.RayRequests = RayRequests.Alloc(1);
		DummyDispatch.RayTraceIntersections = RayTraceIntersections.Alloc(1);
		DummyDispatch.LastFrameRayTraceIntersections = DummyDispatch.RayTraceIntersections;
		DummyDispatch.RayCounts = RayTraceCounts.Alloc<uint32>(3, RHICmdList, true);
		
		//TODO: Move RT Helper out of NiagaraShader and into Niagara. Makes general sense plus it would allow use to hook into these memory stats.
// 		INC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, RayRequests.AllocatedBytes());
// 		INC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, RayTraceIntersections.AllocatedBytes());
// 		INC_MEMORY_STAT_BY(STAT_NiagaraGPUDataInterfaceMemory, RayTraceCounts.AllocatedBytes());

		DummyDispatch.MaxRays = 0;
		DummyDispatch.MaxRetraces = 0;
	}
}

const FNiagaraRayTraceDispatchInfo& FNiagaraRayTracingHelper::GetDispatch(FNiagaraDataInterfaceProxy* DispatchKey) const
{
	const FNiagaraRayTraceDispatchInfo& Dispatch = Dispatches.FindChecked(DispatchKey);

	// make sure that the buffer is valid (that BuildDispatch has been called)
	check(Dispatch.RayRequests.IsValid());

	return Dispatch;
}

const FNiagaraRayTraceDispatchInfo& FNiagaraRayTracingHelper::GetDummyDispatch() const
{
	// make sure that the buffers are valid (that BuildDummyDispatch has been called)
	check(DummyDispatch.RayRequests.IsValid());

	return DummyDispatch;
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

void FNiagaraRayTracingHelper::IssueRayTraces(FRHICommandList& RHICmdList, FScene* Scene)
{
	SCOPED_DRAW_EVENT(RHICmdList, NiagaraIssueCollisionRayTraces);
	check(RayTracingPipelineState);
	check(RayTracingScene);
	check(RayTracingSceneView);
	check(Scene);

	for (TPair<FNiagaraDataInterfaceProxy*, FNiagaraRayTraceDispatchInfo>& Pair : Dispatches)
	{
		FNiagaraRayTraceDispatchInfo& DispatchInfo = Pair.Value;
		
		if (DispatchInfo.MaxRays == 0)
		{
			continue;
		}

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(ShaderPlatform);

		TShaderRef<FNiagaraCollisionRayTraceRG> RGShader = FNiagaraCollisionRayTraceRG::GetShader(ShaderMap);

		FNiagaraCollisionRayTraceRG::FParameters Params;

		Params.GPUSceneParameters.GPUSceneInstanceSceneData = Scene->GPUScene.InstanceSceneDataBuffer.SRV;
		Params.GPUSceneParameters.GPUSceneInstancePayloadData = Scene->GPUScene.InstancePayloadDataBuffer.SRV;
		Params.GPUSceneParameters.GPUScenePrimitiveSceneData = Scene->GPUScene.PrimitiveBuffer.SRV;
		Params.GPUSceneParameters.GPUSceneFrameNumber = Scene->GPUScene.GetSceneFrameNumber();

		Params.HashTable = PrimIdHashTable.SRV;
		Params.HashTableSize = HashTableSize;
		Params.TLAS = RayTracingSceneView;
		Params.Rays = DispatchInfo.RayRequests.Buffer->SRV;
		Params.RaysOffset = DispatchInfo.RayRequests.Offset;
		Params.CollisionOutput = DispatchInfo.RayTraceIntersections.Buffer->UAV;
		Params.CollisionOutputOffset = DispatchInfo.RayTraceIntersections.Offset;
		Params.HashToCollisionGroups = HashToCollisionGroups.SRV;
		Params.MaxRetraces = DispatchInfo.MaxRetraces;

		if (FNiagaraCollisionRayTraceRG::SupportsIndirectDispatch())
		{
			FRayTracingShaderBindingsWriter GlobalResources;
			SetShaderParameters(GlobalResources, RGShader, Params);

			//Can we wrangle things so we can have one indirect dispatch with each internal dispatch pointing to potentially different Ray and Results buffers?
			//For now have a each as a unique dispatch.
			RHICmdList.RayTraceDispatchIndirect(
				RayTracingPipelineState,
				RGShader.GetRayTracingShader(),
				RayTracingScene,
				GlobalResources,
				DispatchInfo.RayCounts.Buffer->Buffer,
				DispatchInfo.RayCounts.Offset * sizeof(uint32));
		}
		else
		{
			Params.RayTraceCounts = DispatchInfo.RayCounts.Buffer->SRV;

			FRayTracingShaderBindingsWriter GlobalResources;
			SetShaderParameters(GlobalResources, RGShader, Params);

			RHICmdList.RayTraceDispatch(
				RayTracingPipelineState,
				RGShader.GetRayTracingShader(),
				RayTracingScene,
				GlobalResources,
				DispatchInfo.MaxRays,
				1
			);
		}
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