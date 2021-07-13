// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraScriptExecutionContext.h"
#include "RHIGPUReadback.h"

class FNiagaraGPUInstanceCountManager;
class NiagaraEmitterInstanceBatcher;
class FNiagaraGPUSystemTick;
struct FNiagaraComputeInstanceData;
struct FNiagaraComputeExecutionContext;

class FNiagaraRHIUniformBufferLayout : public FRHIResource
{
public:
	explicit FNiagaraRHIUniformBufferLayout(const TCHAR* LayoutName) : UBLayout(LayoutName) { }

	FRHIUniformBufferLayout UBLayout;
};

struct FNiagaraSimStageData
{
	FNiagaraSimStageData()
	{
		bFirstStage = false;
		bLastStage = false;
		bSetDataToRender = false;
	}

	uint32 bFirstStage : 1;
	uint32 bLastStage : 1;
	uint32 bSetDataToRender : 1;

	uint32 StageIndex = INDEX_NONE;

	FNiagaraDataBuffer* Source = nullptr;
	uint32 SourceCountOffset = INDEX_NONE;
	uint32 SourceNumInstances = 0;

	FNiagaraDataBuffer* Destination = nullptr;
	uint32 DestinationCountOffset = INDEX_NONE;
	uint32 DestinationNumInstances = 0;

	FNiagaraDataInterfaceProxyRW* AlternateIterationSource = nullptr;
	const FSimulationStageMetaData* StageMetaData = nullptr;
};

struct FNiagaraGpuDispatchInstance
{
	FNiagaraGpuDispatchInstance(const FNiagaraGPUSystemTick& InTick, const FNiagaraComputeInstanceData& InInstanceData)
		: Tick(InTick)
		, InstanceData(InInstanceData)
	{
	}

	const FNiagaraGPUSystemTick& Tick;
	const FNiagaraComputeInstanceData& InstanceData;
	FNiagaraSimStageData SimStageData;
};

struct FNiagaraGpuDispatchGroup
{
	TArray<FNiagaraGPUSystemTick*>				TicksWithPerInstanceData;
	TArray<FNiagaraGpuDispatchInstance>			DispatchInstances;
	TArray<FNiagaraComputeExecutionContext*>	FreeIDUpdates;
};

struct FNiagaraGpuDispatchList
{
	void PreAllocateGroups(int32 LastGroup)
	{
		const int32 GroupsToAllocate = LastGroup - DispatchGroups.Num();
		if (GroupsToAllocate > 0)
		{
			DispatchGroups.AddDefaulted(GroupsToAllocate);
		}
	}

	bool HasWork() const { return DispatchGroups.Num() > 0; }

	TArray<uint32>						CountsToRelease;
	TArray<FNiagaraGpuDispatchGroup>	DispatchGroups;
};

struct FNiagaraGpuSpawnInfoParams
{
	float IntervalDt;
	float InterpStartDt;
	int32 SpawnGroup;
	int32 GroupSpawnStartIndex;
};

struct FNiagaraGpuSpawnInfo
{
	uint32 EventSpawnTotal = 0;
	uint32 SpawnRateInstances = 0;
	uint32 MaxParticleCount = 0;
	int32 SpawnInfoStartOffsets[NIAGARA_MAX_GPU_SPAWN_INFOS];
	FNiagaraGpuSpawnInfoParams SpawnInfoParams[NIAGARA_MAX_GPU_SPAWN_INFOS];

	void Reset()
	{
		EventSpawnTotal = 0;
		SpawnRateInstances = 0;
		MaxParticleCount = 0;
		for (int32 i = 0; i < NIAGARA_MAX_GPU_SPAWN_INFOS; ++i)
		{
			SpawnInfoStartOffsets[i] = 0;

			SpawnInfoParams[i].IntervalDt = 0;
			SpawnInfoParams[i].InterpStartDt = 0;
			SpawnInfoParams[i].SpawnGroup = 0;
			SpawnInfoParams[i].GroupSpawnStartIndex = 0;
		}
	}
};

struct FNiagaraComputeExecutionContext
{
	FNiagaraComputeExecutionContext();
	~FNiagaraComputeExecutionContext();

	void Reset(NiagaraEmitterInstanceBatcher* Batcher);

	void InitParams(UNiagaraScript* InGPUComputeScript, ENiagaraSimTarget InSimTarget, const uint32 InDefaultSimulationStageIndex, int32 InMaxUpdateIterations, const TSet<uint32> InSpawnStages);
	void DirtyDataInterfaces();
	bool Tick(FNiagaraSystemInstance* ParentSystemInstance);

	bool OptionalContexInit(FNiagaraSystemInstance* ParentSystemInstance);

	void PostTick();

	void SetDataToRender(FNiagaraDataBuffer* InDataToRender);
	void SetTranslucentDataToRender(FNiagaraDataBuffer* InTranslucentDataToRender);
	FNiagaraDataBuffer* GetDataToRender(bool bIsLowLatencyTranslucent) const { return bIsLowLatencyTranslucent && TranslucentDataToRender ? TranslucentDataToRender : DataToRender; }

	struct 
	{
		// The offset at which the GPU instance count (see FNiagaraGPUInstanceCountManager()).
		uint32 GPUCountOffset = INDEX_NONE;
		// The CPU instance count at the time the GPU count readback was issued. Always bigger or equal to the GPU count.
		uint32 CPUCount = 0;
	}  EmitterInstanceReadback;
	void ReleaseReadbackCounter(FNiagaraGPUInstanceCountManager& GPUInstanceCountManager);
	
#if !UE_BUILD_SHIPPING
	const TCHAR* GetDebugSimName() const { return *DebugSimName; }
	void SetDebugSimName(const TCHAR* InDebugSimName) { DebugSimName = InDebugSimName; }
#else
	const TCHAR* GetDebugSimName() const { return TEXT(""); }
	void SetDebugSimName(const TCHAR*) { }
#endif

//-TOOD:private:
	void ResetInternal(NiagaraEmitterInstanceBatcher* Batcher);

public:
	static uint32 TickCounter;

#if !UE_BUILD_SHIPPING
	FString DebugSimName;
#endif
#if STATS
	TWeakObjectPtr<UNiagaraEmitter> EmitterPtr; // emitter pointer used to report captured gpu stats
#endif

	const TArray<UNiagaraDataInterface*>& GetDataInterfaces()const { return CombinedParamStore.GetDataInterfaces(); }

	class FNiagaraDataSet *MainDataSet;
	UNiagaraScript* GPUScript;
	class FNiagaraShaderScript*  GPUScript_RT;

	// persistent layouts used to create the constant buffers for the compute sim shader
	TRefCountPtr<FNiagaraRHIUniformBufferLayout> ExternalCBufferLayout;

	//Dynamic state updated either from GT via RT commands or from the RT side sim code itself.
	//TArray<uint8, TAlignedHeapAllocator<16>> ParamData_RT;		// RT side copy of the parameter data
	FNiagaraScriptInstanceParameterStore CombinedParamStore;
#if DO_CHECK
	TArray< FString >  DIClassNames;
#endif

	TArray<FNiagaraDataInterfaceProxy*> DataInterfaceProxies;

	// Most current buffer that can be used for rendering.
	FNiagaraDataBuffer* DataToRender = nullptr;

	// Optional buffer which can be used to render translucent data with no latency (i.e. this frames data)
	FNiagaraDataBuffer* TranslucentDataToRender = nullptr;

	// Game thread spawn info will be sent to the render thread inside FNiagaraComputeInstanceData
	FNiagaraGpuSpawnInfo GpuSpawnInfo_GT;

	uint32 DefaultSimulationStageIndex = 0;
	uint32 MaxUpdateIterations = 0;
	TSet<uint32> SpawnStages;

	bool HasInterpolationParameters = false;

	// Do we have a reset pending, controlled by the game thread and passed to instance data
	bool bResetPending_GT = true;

	// Particle count read fence, used to allow us to snoop the count written by the render thread but also avoid racing on a reset value
	uint32 ParticleCountReadFence = 1;
	uint32 ParticleCountWriteFence = 0;

	// Render thread data
	FNiagaraDataBuffer* GetPrevDataBuffer() { check(IsInRenderingThread() && (BufferSwapsThisFrame_RT > 0)); return DataBuffers_RT[(BufferSwapsThisFrame_RT & 1) ^ 1]; }
	FNiagaraDataBuffer* GetNextDataBuffer() { check(IsInRenderingThread()); return DataBuffers_RT[(BufferSwapsThisFrame_RT & 1)]; }
	void AdvanceDataBuffer() { ++BufferSwapsThisFrame_RT; }

	FNiagaraDataBuffer* DataBuffers_RT[2] = { nullptr, nullptr };
	uint32 BufferSwapsThisFrame_RT = 0;
	uint32 CountOffset_RT = INDEX_NONE;

	// Used only when we multi-tick and need to keep track of pointing back to the correct FNiagaraDataBuffer
	FNiagaraDataBuffer* DataSetOriginalBuffer_RT = nullptr;

	// Used to track if we have processed any ticks for this context this frame
	bool bHasTickedThisFrame_RT = false;

	// The current number of instances on the RT
	// Before ticks are processed on the RT this will be CurrentData's NumInstances
	// As ticks are processed (or we generated the tick batches) this will change and won't be accurate until dispatches are executed
	uint32 CurrentNumInstances_RT = 0;
	// The current maximum of instances on the RT
	uint32 CurrentMaxInstances_RT = 0;
	// The current maximum instances we should allocate on the RT
	uint32 CurrentMaxAllocateInstances_RT = 0;

	TArray<FSimulationStageMetaData> SimStageInfo;

	bool IsOutputStage(FNiagaraDataInterfaceProxy* DIProxy, uint32 CurrentStage) const;
	bool IsIterationStage(FNiagaraDataInterfaceProxy* DIProxy, uint32 CurrentStage) const;
	FNiagaraDataInterfaceProxyRW* FindIterationInterface(const TArray<FNiagaraDataInterfaceProxyRW*>& InProxies, uint32 SimulationStageIndex) const;
	const FSimulationStageMetaData* GetSimStageMetaData(uint32 SimulationStageIndex) const;
};
