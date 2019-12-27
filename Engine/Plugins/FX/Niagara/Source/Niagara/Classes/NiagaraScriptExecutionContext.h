// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraEmitterInstance.h: Niagara emitter simulation class
==============================================================================*/
#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "NiagaraCommon.h"
#include "NiagaraDataSet.h"
#include "NiagaraEvents.h"
#include "NiagaraCollision.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraEmitter.h"
#include "NiagaraScriptExecutionParameterStore.h"
#include "NiagaraTypes.h"
#include "RHIGPUReadback.h"

struct FNiagaraDataInterfaceProxy;
class FNiagaraGPUInstanceCountManager;
class NiagaraEmitterInstanceBatcher;

/** Container for data needed to process event data. */
struct FNiagaraEventHandlingInfo
{
	FNiagaraEventHandlingInfo()
		: TotalSpawnCount(0)
		, EventData(nullptr)
		, SourceEmitterName(NAME_None)
	{
	}

	~FNiagaraEventHandlingInfo()
	{
		SetEventData(nullptr);
	}

	void SetEventData(FNiagaraDataBuffer* InEventData)
	{
		if (EventData)
		{
			EventData->ReleaseReadRef();
		}
		EventData = InEventData;
		if (EventData)
		{
			EventData->AddReadRef();
		}
	}

	TArray<int32, TInlineAllocator<16>> SpawnCounts;
	int32 TotalSpawnCount;
	FNiagaraDataBuffer* EventData;
	FGuid SourceEmitterGuid;
	FName SourceEmitterName;
};


struct FNiagaraDataSetExecutionInfo
{
	FNiagaraDataSetExecutionInfo()
		: DataSet(nullptr)
		, Input(nullptr)
		, Output(nullptr)
		, StartInstance(0)
		, bUpdateInstanceCount(false)
	{
		Reset();
	}


	FORCEINLINE void Init(FNiagaraDataSet* InDataSet, FNiagaraDataBuffer* InInput, FNiagaraDataBuffer* InOutput, int32 InStartInstance, bool bInUpdateInstanceCount)
	{
		if (Input)
		{
			Input->ReleaseReadRef();
		}

		DataSet = InDataSet;
		Input = InInput;
		Output = InOutput;
		StartInstance = InStartInstance;
		bUpdateInstanceCount = bInUpdateInstanceCount;

		check(DataSet);
		check(Input == nullptr || DataSet == Input->GetOwner());
		check(Output == nullptr || DataSet == Output->GetOwner());

		if (Input)
		{
			Input->AddReadRef();
		}
		check(Output == nullptr || Output->IsBeingWritten());
	}
	
	~FNiagaraDataSetExecutionInfo()
	{
		Reset();
	}

	FORCEINLINE void Reset()
	{
		if (Input)
		{
			Input->ReleaseReadRef();
		}

		DataSet = nullptr;
		Input = nullptr;
		Output = nullptr;
		StartInstance = INDEX_NONE;
		bUpdateInstanceCount = false;
	}

	FNiagaraDataSet* DataSet;
	FNiagaraDataBuffer* Input;
	FNiagaraDataBuffer* Output;
	int32 StartInstance;
	bool bUpdateInstanceCount;
};

struct FNiagaraScriptExecutionContext
{
	UNiagaraScript* Script;

	/** Table of external function delegates called from the VM. */
	TArray<FVMExternalFunction> FunctionTable;

	/** Table of instance data for data interfaces that require it. */
	TArray<void*> DataInterfaceInstDataTable;

	/** Parameter store. Contains all data interfaces and a parameter buffer that can be used directly by the VM or GPU. */
	FNiagaraScriptExecutionParameterStore Parameters;

	TArray<FDataSetMeta, TInlineAllocator<4>> DataSetMetaTable;

	TArray<FNiagaraDataSetExecutionInfo, TInlineAllocator<4>> DataSetInfo;

	static uint32 TickCounter;

	FNiagaraScriptExecutionContext();
	~FNiagaraScriptExecutionContext();

	bool Init(UNiagaraScript* InScript, ENiagaraSimTarget InTarget);
	
	bool Tick(class FNiagaraSystemInstance* Instance, ENiagaraSimTarget SimTarget = ENiagaraSimTarget::CPUSim);
	void PostTick();

	void BindData(int32 Index, FNiagaraDataSet& DataSet, int32 StartInstance, bool bUpdateInstanceCounts);
	void BindData(int32 Index, FNiagaraDataBuffer* Input, int32 StartInstance, bool bUpdateInstanceCounts);
	bool Execute(uint32 NumInstances);

	const TArray<UNiagaraDataInterface*>& GetDataInterfaces()const { return Parameters.GetDataInterfaces(); }

	void DirtyDataInterfaces();

	bool CanExecute()const;
};

struct FNiagaraGpuSpawnInfo
{
	uint32		EventSpawnTotal = 0;
	uint32		SpawnRateInstances = 0;
	uint32		MaxParticleCount = 0;
	FVector4	SpawnInfoStartOffsets[NIAGARA_MAX_GPU_SPAWN_INFOS_V4];
	FVector4	SpawnInfoParams[NIAGARA_MAX_GPU_SPAWN_INFOS];
};

struct FNiagaraComputeExecutionContext
{
	FNiagaraComputeExecutionContext();
	~FNiagaraComputeExecutionContext();

	void Reset(NiagaraEmitterInstanceBatcher* Batcher);

	void InitParams(UNiagaraScript* InGPUComputeScript, ENiagaraSimTarget InSimTarget, const FString& InDebugSimName, const uint32 InDefaultShaderStageIndex, int32 InMaxUpdateIterations, const TSet<uint32> InSpawnStages);
	void DirtyDataInterfaces();
	bool Tick(FNiagaraSystemInstance* ParentSystemInstance);

	void PostTick();

	void SetDataToRender(FNiagaraDataBuffer* InDataToRender);
	FNiagaraDataBuffer* GetDataToRender()const { return DataToRender; }

	struct 
	{
		// The offset at which the GPU instance count (see FNiagaraGPUInstanceCountManager()).
		uint32 GPUCountOffset = INDEX_NONE;
		// The CPU instance count at the time the GPU count readback was issued. Always bigger or equal to the GPU count.
		uint32 CPUCount = 0;
	}  EmitterInstanceReadback;

private:
	void ResetInternal(NiagaraEmitterInstanceBatcher* Batcher);

public:
	static uint32 TickCounter;

#if !UE_BUILD_SHIPPING
	//Persistent state 
	FString DebugSimName;
	FORCEINLINE const TCHAR* GetDebugSimName() const { return *DebugSimName; }
#else
	FORCEINLINE const TCHAR* GetDebugSimName() const { return TEXT(""); }
#endif

	class FNiagaraDataSet *MainDataSet;
	UNiagaraScript* GPUScript;
	class FNiagaraShaderScript*  GPUScript_RT;
	FRHIUniformBufferLayout CBufferLayout; // Persistent layouts used to create Compute Sim CBuffer

	//Dynamic state updated either from GT via RT commands or from the RT side sim code itself.
	//TArray<uint8, TAlignedHeapAllocator<16>> ParamData_RT;		// RT side copy of the parameter data
	FNiagaraScriptExecutionParameterStore CombinedParamStore;
#if DO_CHECK
	TArray< FNiagaraDataInterfaceGPUParamInfo >  DIParamInfo;
#endif

	TArray<FNiagaraDataInterfaceProxy*> DataInterfaceProxies;

	//Most current buffer that can be used for rendering.
	FNiagaraDataBuffer* DataToRender;

	// Game thread spawn info will be sent to the render thread inside FNiagaraComputeInstanceData
	FNiagaraGpuSpawnInfo GpuSpawnInfo_GT;

	uint32 DefaultShaderStageIndex;
	uint32 MaxUpdateIterations;
	TSet<uint32> SpawnStages;

	/** Temp data used in NiagaraEmitterInstanceBatcher::ExecuteAll() to avoid creating a map per FNiagaraComputeExecutionContext */
	mutable int32 ScratchIndex = INDEX_NONE;

#if WITH_EDITORONLY_DATA
	mutable FRHIGPUMemoryReadback *GPUDebugDataReadbackFloat;
	mutable FRHIGPUMemoryReadback *GPUDebugDataReadbackInt;
	mutable FRHIGPUMemoryReadback *GPUDebugDataReadbackCounts;
	mutable uint32 GPUDebugDataFloatSize;
	mutable uint32 GPUDebugDataIntSize;
	mutable uint32 GPUDebugDataFloatStride;
	mutable uint32 GPUDebugDataIntStride;
	mutable uint32 GPUDebugDataCountOffset;
	mutable TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe> DebugInfo;
#endif

};

struct FNiagaraDataInterfaceInstanceData
{
	void* PerInstanceDataForRT;
	TMap<FNiagaraDataInterfaceProxy*, int32> InterfaceProxiesToOffsets;
	uint32 PerInstanceDataSize;
	uint32 Instances;

	~FNiagaraDataInterfaceInstanceData()
	{}
};

//TODO: Rename FNiagaraGPUEmitterTick?

struct FNiagaraComputeInstanceData
{
	FNiagaraGpuSpawnInfo SpawnInfo;
	uint8* ParamData;
	FNiagaraComputeExecutionContext* Context;
	TArray<FNiagaraDataInterfaceProxy*> DataInterfaceProxies;

	//Buffer containing current state that this tick will read from. Initialized at the start of processing this tick on the RT.
	FNiagaraDataBuffer* CurrentData;
	//Buffer into which we'll write the new simulation state. Initialized at the start of processing this tick on the RT.
	FNiagaraDataBuffer* DestinationData;

	FNiagaraComputeInstanceData()
		: ParamData(nullptr)
		, Context(nullptr)
		, CurrentData(nullptr)
		, DestinationData(nullptr)
	{}
};

/*
	Represents all the information needed to dispatch a single tick of a FNiagaraSystemInstance.
	This object will be created on the game thread and passed to the renderthread.

	It contains the PerInstance data buffer for every DataInterface referenced by the system as well
	as the Data required to dispatch updates for each Emitter in the system.

	DataInterface data is packed tightly. It includes a TMap that associates the data interface with
	the offset into the packed buffer. At that offset is the Per-Instance data for this System.

	InstanceData_ParamData_Packed packs FNiagaraComputeInstanceData and ParamData into one buffer.
	There is padding after the array of FNiagaraComputeInstanceData so we can upload ParamData directly into a UniformBuffer
	(it is 16 byte aligned).

*/
class FNiagaraGPUSystemTick
{
public:
	FNiagaraGPUSystemTick()
		: Count(0)
		, DIInstanceData(nullptr)
		, InstanceData_ParamData_Packed(nullptr)
	{}

	void Init(FNiagaraSystemInstance* InSystemInstance);
	void Destroy();

	FORCEINLINE bool IsValid()const{ return InstanceData_ParamData_Packed != nullptr; }
	FORCEINLINE FNiagaraComputeInstanceData* GetInstanceData()const{ return reinterpret_cast<FNiagaraComputeInstanceData*>(InstanceData_ParamData_Packed); }

	uint32 Count;
	FNiagaraSystemInstanceID SystemInstanceID;
	FNiagaraDataInterfaceInstanceData* DIInstanceData;
	uint8* InstanceData_ParamData_Packed;
	bool bRequiresDistanceFieldData = false;
	bool bRequiresDepthBuffer = false;
	bool bRequiresEarlyViewData = false;
	bool bNeedsReset = false;
	bool bIsFinalTick = false;
};
