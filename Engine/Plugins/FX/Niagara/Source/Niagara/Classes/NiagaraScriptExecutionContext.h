// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
NiagaraEmitterInstance.h: Niagara emitter simulation class
==============================================================================*/
#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"
#include "NiagaraDataSet.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraEmitter.h"
#include "NiagaraScriptExecutionParameterStore.h"
#include "NiagaraTypes.h"
#include "RHIGPUReadback.h"

struct FNiagaraDataInterfaceProxy;
class FNiagaraGPUInstanceCountManager;
class NiagaraEmitterInstanceBatcher;

/** All scripts that will use the system script execution context. */
enum class ENiagaraSystemSimulationScript : uint8
{
	Spawn,
	Update,
	Num,
	//TODO: Maybe add emitter spawn and update here if we split those scripts out.
};

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

struct FScriptExecutionConstantBufferTable
{
	TArray<const uint8*, TInlineAllocator<12>> Buffers;
	TArray<int32, TInlineAllocator<12>> BufferSizes;

	void Reset(int32 ResetSize)
	{
		Buffers.Reset(ResetSize);
		BufferSizes.Reset(ResetSize);
	}

	template<typename T>
	void AddTypedBuffer(const T& Buffer)
	{
		Buffers.Add(reinterpret_cast<const uint8*>(&Buffer));
		BufferSizes.Add(sizeof(T));
	}

	void AddRawBuffer(const uint8* BufferData, int32 BufferSize)
	{
		Buffers.Add(BufferData);
		BufferSizes.Add(BufferSize);
	}
};

struct FNiagaraScriptExecutionContextBase
{
	UNiagaraScript* Script;

	/** Table of external function delegate handles called from the VM. */
	TArray<const FVMExternalFunction*> FunctionTable;

	/**
	Table of user ptrs to pass to the VM.
	*/
	TArray<void*> UserPtrTable;

	/** Parameter store. Contains all data interfaces and a parameter buffer that can be used directly by the VM or GPU. */
	FNiagaraScriptInstanceParameterStore Parameters;

	TArray<FDataSetMeta, TInlineAllocator<2>> DataSetMetaTable;

	TArray<FNiagaraDataSetExecutionInfo, TInlineAllocator<2>> DataSetInfo;

	static uint32 TickCounter;

	int32 HasInterpolationParameters : 1;
	int32 bAllowParallel : 1;
#if STATS
	TArray<FStatScopeData> StatScopeData;
	TMap<TStatIdData const*, float> ExecutionTimings;
	void CreateStatScopeData();
	TMap<TStatIdData const*, float> ReportStats();
#endif
	
	FNiagaraScriptExecutionContextBase();
	virtual ~FNiagaraScriptExecutionContextBase();

	virtual bool Init(UNiagaraScript* InScript, ENiagaraSimTarget InTarget);
	virtual bool Tick(class FNiagaraSystemInstance* Instance, ENiagaraSimTarget SimTarget) = 0;

	void BindData(int32 Index, FNiagaraDataSet& DataSet, int32 StartInstance, bool bUpdateInstanceCounts);
	void BindData(int32 Index, FNiagaraDataBuffer* Input, int32 StartInstance, bool bUpdateInstanceCounts);
	bool Execute(uint32 NumInstances, const FScriptExecutionConstantBufferTable& ConstantBufferTable);

	const TArray<UNiagaraDataInterface*>& GetDataInterfaces()const { return Parameters.GetDataInterfaces(); }

	bool CanExecute()const;

	TArrayView<const uint8> GetScriptLiterals() const;

	void DirtyDataInterfaces();
	void PostTick();

	//Unused. These are only useful in the new SystemScript context.
	virtual void BindSystemInstances(TArray<FNiagaraSystemInstance*>& InSystemInstances) {}
	virtual bool GeneratePerInstanceDIFunctionTable(FNiagaraSystemInstance* Inst, TArray<struct FNiagaraPerInstanceDIFuncInfo>& OutFunctions) {return true;}
};

struct FNiagaraScriptExecutionContext : public FNiagaraScriptExecutionContextBase
{
protected:
	/**
	Table of external function delegates unique to the instance.
	*/
	TArray<FVMExternalFunction> LocalFunctionTable;

public:
	virtual bool Tick(class FNiagaraSystemInstance* Instance, ENiagaraSimTarget SimTarget)override;
};

/**
For function calls from system scripts on User DIs or those with per instance data, we build a per instance binding table that is called from a helper function in the exec context.
TODO: We can embed the instance data in the lambda capture for reduced complexity here. No need for the user ptr table.
We have to rebind if the instance data is recreated anyway.
*/
struct FNiagaraPerInstanceDIFuncInfo
{
	FVMExternalFunction Function;
	void* InstData;
};

/** Specialized exec context for system scripts. Allows us to better handle the added complication of Data Interfaces across different system instances. */
struct FNiagaraSystemScriptExecutionContext : public FNiagaraScriptExecutionContextBase
{
protected:

	struct FExternalFuncInfo
	{
		FVMExternalFunction Function;
	};

	TArray<FExternalFuncInfo> ExtFunctionInfo;

	/**
	Array of system instances the context is currently operating on.
	We need this to allow us to call into per instance DI functions.
	*/
	TArray<FNiagaraSystemInstance*>* SystemInstances;

	/** The script type this context is for. Allows us to access the correct per instance function table on the system instance. */
	ENiagaraSystemSimulationScript ScriptType;

	/** Helper function that handles calling into per instance DI calls and massages the VM context appropriately. */
	void PerInstanceFunctionHook(FVectorVMContext& Context, int32 PerInstFunctionIndex, int32 UserPtrIndex);

public:
	FNiagaraSystemScriptExecutionContext(ENiagaraSystemSimulationScript InScriptType) : SystemInstances(nullptr), ScriptType(InScriptType){}
	
	virtual bool Init(UNiagaraScript* InScript, ENiagaraSimTarget InTarget)override;
	virtual bool Tick(class FNiagaraSystemInstance* Instance, ENiagaraSimTarget SimTarget);

	void BindSystemInstances(TArray<FNiagaraSystemInstance*>& InSystemInstances) { SystemInstances = &InSystemInstances; }

	/** Generates a table of DI calls unique to the passed system instance. These are then accesss inside the PerInstanceFunctionHook. */
	virtual bool GeneratePerInstanceDIFunctionTable(FNiagaraSystemInstance* Inst, TArray<FNiagaraPerInstanceDIFuncInfo>& OutFunctions);
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

class FNiagaraRHIUniformBufferLayout : public FRHIResource
{
public:
	explicit FNiagaraRHIUniformBufferLayout(const TCHAR* LayoutName) : UBLayout(LayoutName) { }

	FRHIUniformBufferLayout UBLayout;
};

struct FNiagaraComputeSharedContext
{
	int32 ScratchIndex = INDEX_NONE;
	int32 ScratchTickStage = INDEX_NONE;
};

struct FNiagaraComputeSharedContextDeleter
{
	void operator()(FNiagaraComputeSharedContext* Ptr) const
	{
		if (Ptr)
		{
			ENQUEUE_RENDER_COMMAND(NiagaraDeleteSharedContext)([RT_Ptr=Ptr](FRHICommandListImmediate& RHICmdList) { delete RT_Ptr; });
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
	
#if !UE_BUILD_SHIPPING
	const TCHAR* GetDebugSimName() const { return *DebugSimName; }
	void SetDebugSimName(const TCHAR* InDebugSimName) { DebugSimName = InDebugSimName; }
#else
	const TCHAR* GetDebugSimName() const { return TEXT(""); }
	void SetDebugSimName(const TCHAR*) { }
#endif

private:
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

	uint32 DefaultSimulationStageIndex;
	uint32 MaxUpdateIterations;
	TSet<uint32> SpawnStages;

	bool HasInterpolationParameters;

	/** Temp data used in NiagaraEmitterInstanceBatcher::ExecuteAll() to avoid creating a map per FNiagaraComputeExecutionContext */
	mutable uint32 ScratchNumInstances = 0;
	mutable uint32 ScratchMaxInstances = 0;

	TArray<FSimulationStageMetaData> SimStageInfo;

	bool IsOutputStage(FNiagaraDataInterfaceProxy* DIProxy, uint32 CurrentStage) const;
	bool IsIterationStage(FNiagaraDataInterfaceProxy* DIProxy, uint32 CurrentStage) const;
	FNiagaraDataInterfaceProxyRW* FindIterationInterface(const TArray<FNiagaraDataInterfaceProxyRW*>& InProxies, uint32 SimulationStageIndex) const;
	const FSimulationStageMetaData* GetSimStageMetaData(uint32 SimulationStageIndex) const;
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


struct FNiagaraSimStageData
{
	FNiagaraDataBuffer* Source = nullptr;
	FNiagaraDataBuffer* Destination = nullptr;
	FNiagaraDataInterfaceProxyRW* AlternateIterationSource = nullptr;
	uint32 SourceCountOffset = 0;
	uint32 DestinationCountOffset = 0;
	uint32 SourceNumInstances = 0;
	uint32 DestinationNumInstances = 0;
	const FSimulationStageMetaData* StageMetaData = nullptr;
};

struct FNiagaraComputeInstanceData
{
	FNiagaraGpuSpawnInfo SpawnInfo;
	uint8* EmitterParamData = nullptr;
	uint8* ExternalParamData = nullptr;
	FNiagaraComputeExecutionContext* Context = nullptr;
	TArray<FNiagaraDataInterfaceProxy*> DataInterfaceProxies;
	TArray<FNiagaraDataInterfaceProxyRW*> IterationDataInterfaceProxies;
	bool bStartNewOverlapGroup = false;
	bool bUsesSimStages = false;
	bool bUsesOldShaderStages = false;
	TArray<FNiagaraSimStageData, TInlineAllocator<1>> SimStageData;

	bool IsOutputStage(FNiagaraDataInterfaceProxy* DIProxy, uint32 CurrentStage) const;
	bool IsIterationStage(FNiagaraDataInterfaceProxy* DIProxy, uint32 CurrentStage) const;
	FNiagaraDataInterfaceProxyRW* FindIterationInterface(uint32 SimulationStageIndex) const;
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
	void Init(FNiagaraSystemInstance* InSystemInstance);
	void Destroy();

	FORCEINLINE bool IsValid()const{ return InstanceData_ParamData_Packed != nullptr; }
	FORCEINLINE FNiagaraComputeInstanceData* GetInstanceData()const{ return reinterpret_cast<FNiagaraComputeInstanceData*>(InstanceData_ParamData_Packed); }

	enum EUniformBufferType
	{
		UBT_FirstSystemType = 0,
		UBT_Global = UBT_FirstSystemType,
		UBT_System,
		UBT_Owner,
		UBT_NumSystemTypes,

		UBT_FirstInstanceType = UBT_NumSystemTypes,
		UBT_Emitter = UBT_FirstInstanceType,
		UBT_External,

		UBT_NumTypes,

		UBT_NumInstanceTypes = UBT_NumTypes - UBT_NumSystemTypes,
	};

	FUniformBufferRHIRef GetUniformBuffer(EUniformBufferType Type, const FNiagaraComputeInstanceData* InstanceData, bool Previous) const;
	const uint8* GetUniformBufferSource(EUniformBufferType Type, const FNiagaraComputeInstanceData* InstanceData, bool Previous) const;

public:
	// Transient data used by the RT
	TArray<FUniformBufferRHIRef> UniformBuffers;

	// data assigned by GT
	FNiagaraSystemInstanceID SystemInstanceID = 0LL;
	FNiagaraComputeSharedContext* SharedContext = nullptr;
	FNiagaraDataInterfaceInstanceData* DIInstanceData = nullptr;
	uint8* InstanceData_ParamData_Packed = nullptr;
	uint8* GlobalParamData = nullptr;
	uint8* SystemParamData = nullptr;
	uint8* OwnerParamData = nullptr;
	uint32 Count = 0;
	uint32 TotalDispatches = 0;
	uint32 NumInstancesWithSimStages = 0;
	bool bRequiresDistanceFieldData = false;
	bool bRequiresDepthBuffer = false;
	bool bRequiresEarlyViewData = false;
	bool bRequiresViewUniformBuffer = false;
	bool bNeedsReset = false;
	bool bIsFinalTick = false;
};
