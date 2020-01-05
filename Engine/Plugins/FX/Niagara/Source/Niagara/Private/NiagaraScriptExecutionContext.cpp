// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraScriptExecutionContext.h"
#include "NiagaraStats.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraDataInterface.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraWorldManager.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraGPUInstanceCountManager.h"

DECLARE_CYCLE_STAT(TEXT("Register Setup"), STAT_NiagaraSimRegisterSetup, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Context Ticking"), STAT_NiagaraScriptExecContextTick, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Rebind DInterface Func Table"), STAT_NiagaraRebindDataInterfaceFunctionTable, STATGROUP_Niagara);
	//Add previous frame values if we're interpolated spawn.
	
	//Internal constants - only needed for non-GPU sim

uint32 FNiagaraScriptExecutionContext::TickCounter = 0;

static int32 GbExecVMScripts = 1;
static FAutoConsoleVariableRef CVarNiagaraExecVMScripts(
	TEXT("fx.ExecVMScripts"),
	GbExecVMScripts,
	TEXT("If > 0 VM scripts will be executed, otherwise they won't, useful for looking at the bytecode for a crashing compiled script. \n"),
	ECVF_Default
);

FNiagaraScriptExecutionContext::FNiagaraScriptExecutionContext()
	: Script(nullptr)
{

}

FNiagaraScriptExecutionContext::~FNiagaraScriptExecutionContext()
{
}

bool FNiagaraScriptExecutionContext::Init(UNiagaraScript* InScript, ENiagaraSimTarget InTarget)
{
	Script = InScript;

	Parameters.InitFromOwningContext(Script, InTarget, true);

	return true;//TODO: Error cases?
}

bool FNiagaraScriptExecutionContext::Tick(FNiagaraSystemInstance* ParentSystemInstance, ENiagaraSimTarget SimTarget)
{
	//Bind data interfaces if needed.
	if (Parameters.GetInterfacesDirty())
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraScriptExecContextTick);
		if (Script && Script->IsReadyToRun(ENiagaraSimTarget::CPUSim))//TODO: Remove. Script can only be null for system instances that currently don't have their script exec context set up correctly.
		{
			const TArray<UNiagaraDataInterface*>& DataInterfaces = GetDataInterfaces();

			SCOPE_CYCLE_COUNTER(STAT_NiagaraRebindDataInterfaceFunctionTable);
			// UE_LOG(LogNiagara, Log, TEXT("Updating data interfaces for script %s"), *Script->GetFullName());

			// We must make sure that the data interfaces match up between the original script values and our overrides...
			if (Script->GetVMExecutableData().DataInterfaceInfo.Num() != DataInterfaces.Num())
			{
				UE_LOG(LogNiagara, Warning, TEXT("Mismatch between Niagara Exectuion Context data interfaces and those in it's script!"));
				return false;
			}

			//Fill the instance data table.
			if (ParentSystemInstance)
			{
				DataInterfaceInstDataTable.SetNumZeroed(Script->GetVMExecutableData().NumUserPtrs, false);
				for (int32 i = 0; i < DataInterfaces.Num(); i++)
				{
					UNiagaraDataInterface* Interface = DataInterfaces[i];

					int32 UserPtrIdx = Script->GetVMExecutableData().DataInterfaceInfo[i].UserPtrIdx;
					if (UserPtrIdx != INDEX_NONE)
					{
						void* InstData = ParentSystemInstance->FindDataInterfaceInstanceData(Interface);
						DataInterfaceInstDataTable[UserPtrIdx] = InstData;
					}
				}
			}
			else
			{
				check(Script->GetVMExecutableData().NumUserPtrs == 0);//Can't have user ptrs if we have no parent instance.
			}

			FunctionTable.Reset(Script->GetVMExecutableData().CalledVMExternalFunctions.Num());

			bool bSuccessfullyMapped = true;
			for (FVMExternalFunctionBindingInfo& BindingInfo : Script->GetVMExecutableData().CalledVMExternalFunctions)
			{
				// First check to see if we can pull from the fast path library..
				FVMExternalFunction FuncBind;
				if (UNiagaraFunctionLibrary::GetVectorVMFastPathExternalFunction(BindingInfo, FuncBind) && FuncBind.IsBound())
				{
					FunctionTable.Add(FuncBind);
					continue;
				}

				for (int32 i = 0; i < Script->GetVMExecutableData().DataInterfaceInfo.Num(); i++)
				{
					FNiagaraScriptDataInterfaceCompileInfo& ScriptInfo = Script->GetVMExecutableData().DataInterfaceInfo[i];
					UNiagaraDataInterface* ExternalInterface = DataInterfaces[i];
					if (ScriptInfo.Name == BindingInfo.OwnerName)
					{
						void* InstData = ScriptInfo.UserPtrIdx == INDEX_NONE ? nullptr : DataInterfaceInstDataTable[ScriptInfo.UserPtrIdx];
						int32 AddedIdx = FunctionTable.Add(FVMExternalFunction());
						if (ExternalInterface != nullptr)
						{
							ExternalInterface->GetVMExternalFunction(BindingInfo, InstData, FunctionTable[AddedIdx]);
						}

						if (AddedIdx != INDEX_NONE && !FunctionTable[AddedIdx].IsBound())
						{
							UE_LOG(LogNiagara, Error, TEXT("Could not Get VMExternalFunction '%s'.. emitter will not run!"), *BindingInfo.Name.ToString());
							bSuccessfullyMapped = false;
						}
					}
				}
			}

			if (!bSuccessfullyMapped)
			{
				UE_LOG(LogNiagara, Warning, TEXT("Error building data interface function table!"));
				FunctionTable.Empty();
				return false;
			}
		}
	}

	Parameters.Tick();

	return true;
}

void FNiagaraScriptExecutionContext::PostTick()
{
	//If we're for interpolated spawn, copy over the previous frame's parameters into the Prev parameters.
	if (Script && Script->GetComputedVMCompilationId().HasInterpolatedParameters())
	{
		Parameters.CopyCurrToPrev();
	}
}

void FNiagaraScriptExecutionContext::BindData(int32 Index, FNiagaraDataSet& DataSet, int32 StartInstance, bool bUpdateInstanceCounts)
{
	FNiagaraDataBuffer* Input = DataSet.GetCurrentData();
	FNiagaraDataBuffer* Output = DataSet.GetDestinationData();

	DataSetInfo.SetNum(FMath::Max(DataSetInfo.Num(), Index + 1));
	DataSetInfo[Index].Init(&DataSet, Input, Output, StartInstance, bUpdateInstanceCounts);

	//Would be nice to roll this and DataSetInfo into one but currently the VM being in it's own Engine module prevents this. Possibly should move the VM into Niagara itself.
	uint8** InputRegisters = Input ? Input->GetRegisterTable().GetData() : nullptr;
	uint8** OutputRegisters = Output ? Output->GetRegisterTable().GetData() : nullptr;
	DataSetMetaTable.SetNum(FMath::Max(DataSetMetaTable.Num(), Index + 1));
	DataSetMetaTable[Index].Init(InputRegisters, OutputRegisters, StartInstance,
		Output ? &Output->GetIDTable() : nullptr, &DataSet.GetFreeIDTable(), &DataSet.GetNumFreeIDs(), &DataSet.GetMaxUsedID(), DataSet.GetIDAcquireTag());
}

void FNiagaraScriptExecutionContext::BindData(int32 Index, FNiagaraDataBuffer* Input, int32 StartInstance, bool bUpdateInstanceCounts)
{
	check(Input && Input->GetOwner());
	DataSetInfo.SetNum(FMath::Max(DataSetInfo.Num(), Index + 1));
	FNiagaraDataSet* DataSet = Input->GetOwner();
	DataSetInfo[Index].Init(DataSet, Input, nullptr, StartInstance, bUpdateInstanceCounts);

	DataSetMetaTable.SetNum(FMath::Max(DataSetMetaTable.Num(), Index + 1));
	DataSetMetaTable[Index].Init(Input->GetRegisterTable().GetData(), nullptr, StartInstance, nullptr, nullptr, &DataSet->GetNumFreeIDs(), &DataSet->GetMaxUsedID(), DataSet->GetIDAcquireTag());
}

bool FNiagaraScriptExecutionContext::Execute(uint32 NumInstances)
{
	if (NumInstances == 0)
	{
		DataSetInfo.Reset();
		return true;
	}

	++TickCounter;//Should this be per execution?

	if (GbExecVMScripts != 0)
	{
		const FNiagaraVMExecutableData& ExecData = Script->GetVMExecutableData();
		VectorVM::Exec(
			ExecData.ByteCode.GetData(),
			ExecData.OptimizedByteCode.Num() > 0 ? ExecData.OptimizedByteCode.GetData() : nullptr,
			ExecData.NumTempRegisters,
			Parameters.GetParameterDataArray().GetData(),
			DataSetMetaTable,
			FunctionTable.GetData(),
			DataInterfaceInstDataTable.GetData(),
			NumInstances
#if STATS
			, Script->GetStatScopeIDs()
#endif
		);
	}

	// Tell the datasets we wrote how many instances were actually written.
	for (int Idx = 0; Idx < DataSetInfo.Num(); Idx++)
	{
		FNiagaraDataSetExecutionInfo& Info = DataSetInfo[Idx];

#if NIAGARA_NAN_CHECKING
		Info.DataSet->CheckForNaNs();
#endif

		if (Info.bUpdateInstanceCount)
		{
			Info.Output->SetNumInstances(Info.StartInstance + DataSetMetaTable[Idx].DataSetAccessIndex + 1);
		}
	}

	//Can maybe do without resetting here. Just doing it for tidiness.
	for (int32 DataSetIdx = 0; DataSetIdx < DataSetInfo.Num(); ++DataSetIdx)
	{
		DataSetInfo[DataSetIdx].Reset();
		DataSetMetaTable[DataSetIdx].Reset();
	}

	return true;//TODO: Error cases?
}

void FNiagaraScriptExecutionContext::DirtyDataInterfaces()
{
	Parameters.MarkInterfacesDirty();
}

bool FNiagaraScriptExecutionContext::CanExecute()const
{
	return Script && Script->GetVMExecutableData().IsValid() && Script->GetVMExecutableData().ByteCode.Num() > 0;
}

struct H2
{
	FNiagaraDataInterfaceProxy* Proxy;
};

void FNiagaraGPUSystemTick::Init(FNiagaraSystemInstance* InSystemInstance)
{
	check(IsInGameThread());

	ensure(InSystemInstance != nullptr);
	CA_ASSUME(InSystemInstance != nullptr);
	ensure(!InSystemInstance->IsComplete());
	SystemInstanceID = InSystemInstance->GetId();
	bRequiresDistanceFieldData = InSystemInstance->RequiresDistanceFieldData();
	bRequiresDepthBuffer = InSystemInstance->RequiresDepthBuffer();
	bRequiresEarlyViewData = InSystemInstance->RequiresEarlyViewData();
	uint32 DataSizeForGPU = InSystemInstance->GPUDataInterfaceInstanceDataSize;

	if (DataSizeForGPU > 0)
	{
		uint32 AllocationSize = DataSizeForGPU;

		DIInstanceData = new FNiagaraDataInterfaceInstanceData;
		DIInstanceData->PerInstanceDataSize = AllocationSize;
		DIInstanceData->PerInstanceDataForRT = FMemory::Malloc(AllocationSize);
		DIInstanceData->Instances = InSystemInstance->DataInterfaceInstanceDataOffsets.Num();

		uint8* InstanceDataBase = (uint8*) DIInstanceData->PerInstanceDataForRT;
		uint32 RunningOffset = 0;
		for (auto& Pair : InSystemInstance->DataInterfaceInstanceDataOffsets)
		{
			UNiagaraDataInterface* Interface = Pair.Key.Get();

			FNiagaraDataInterfaceProxy* Proxy = Interface->GetProxy();
			int32 Offset = Pair.Value;

			const int32 RTDataSize = Interface->PerInstanceDataPassedToRenderThreadSize();
			if (RTDataSize > 0)
			{
				check(Proxy);
				void* PerInstanceData = &InSystemInstance->DataInterfaceInstanceData[Offset];

				Interface->ProvidePerInstanceDataForRenderThread(InstanceDataBase, PerInstanceData, SystemInstanceID);

				// @todo rethink this. So ugly.
				DIInstanceData->InterfaceProxiesToOffsets.Add(Proxy, RunningOffset);

				InstanceDataBase += RTDataSize;
				RunningOffset += RTDataSize;
			}
		}
	}

	check(MAX_uint32 > InSystemInstance->ActiveGPUEmitterCount);

	// Layout our packet.
	const uint32 PackedDispatchesSize = InSystemInstance->ActiveGPUEmitterCount * sizeof(FNiagaraComputeInstanceData);
	// We want the Params after the instance data to be aligned so we can upload to the gpu.
	uint32 PackedDispatchesSizeAligned = Align(PackedDispatchesSize, SHADER_PARAMETER_STRUCT_ALIGNMENT);
	uint32 TotalParamSize = InSystemInstance->TotalParamSize;

	uint32 TotalPackedBufferSize = PackedDispatchesSizeAligned + TotalParamSize;

	InstanceData_ParamData_Packed = (uint8*)FMemory::Malloc(TotalPackedBufferSize);

	FNiagaraComputeInstanceData* Instances = (FNiagaraComputeInstanceData*)(InstanceData_ParamData_Packed);
	uint8* ParamDataBufferPtr = InstanceData_ParamData_Packed + PackedDispatchesSizeAligned;

	int32 TickCount = InSystemInstance->GetTickCount();
	check(TickCount > 0);
	bNeedsReset = ( TickCount == 1);


	// Now we will generate instance data for every GPU simulation we want to run on the render thread.
	// This is spawn rate as well as DataInterface per instance data and the ParameterData for the emitter.
	// @todo Ideally we would only update DataInterface and ParameterData bits if they have changed.
	uint32 InstanceIndex = 0;
	for (int32 i = 0; i < InSystemInstance->GetEmitters().Num(); i++)
	{
		FNiagaraEmitterInstance* Emitter = &InSystemInstance->GetEmitters()[i].Get();

		if (Emitter && Emitter->GetCachedEmitter()->SimTarget == ENiagaraSimTarget::GPUComputeSim && Emitter->GetGPUContext() != nullptr && Emitter->GetExecutionState() != ENiagaraExecutionState::Complete)
		{
			FNiagaraComputeInstanceData* InstanceData = new (&Instances[InstanceIndex]) FNiagaraComputeInstanceData;
			InstanceIndex++;

			InstanceData->Context = Emitter->GetGPUContext();
			check(InstanceData->Context->MainDataSet);

			InstanceData->SpawnInfo = Emitter->GetGPUContext()->GpuSpawnInfo_GT;

			int32 ParmSize = Emitter->GetGPUContext()->CombinedParamStore.GetPaddedParameterSizeInBytes();

			Emitter->GetGPUContext()->CombinedParamStore.CopyParameterDataToPaddedBuffer(ParamDataBufferPtr, ParmSize);

			InstanceData->ParamData = ParamDataBufferPtr;

			ParamDataBufferPtr += ParmSize;

			// @todo-threadsafety Think of a better way to do this!
			FNiagaraComputeExecutionContext* GPUContext = Emitter->GetGPUContext();
			const TArray<UNiagaraDataInterface*>& DataInterfaces = GPUContext->CombinedParamStore.GetDataInterfaces();
			InstanceData->DataInterfaceProxies.Reserve(DataInterfaces.Num());
			for (UNiagaraDataInterface* DI : DataInterfaces)
			{
				check(DI->GetProxy());
				InstanceData->DataInterfaceProxies.Add(DI->GetProxy());
			}			
		}
	}

	check(InSystemInstance->ActiveGPUEmitterCount == InstanceIndex);
	Count = InstanceIndex;
}

void FNiagaraGPUSystemTick::Destroy()
{
	FNiagaraComputeInstanceData* Instances = GetInstanceData();
	for (uint32 i = 0; i < Count; i++)
	{
		FNiagaraComputeInstanceData& Instance = Instances[i];
		Instance.~FNiagaraComputeInstanceData();
	}

	FMemory::Free(InstanceData_ParamData_Packed);
	if (DIInstanceData)
	{
		FMemory::Free(DIInstanceData->PerInstanceDataForRT);
		delete DIInstanceData;
	}
}

//////////////////////////////////////////////////////////////////////////

FNiagaraComputeExecutionContext::FNiagaraComputeExecutionContext()
	: MainDataSet(nullptr)
	, GPUScript(nullptr)
	, GPUScript_RT(nullptr)
	, CBufferLayout(TEXT("Niagara Compute Sim CBuffer"))
	, DataToRender(nullptr)
#if WITH_EDITORONLY_DATA
	, GPUDebugDataReadbackFloat(nullptr)
	, GPUDebugDataReadbackInt(nullptr)
	, GPUDebugDataReadbackCounts(nullptr)
	, GPUDebugDataFloatSize(0)
	, GPUDebugDataIntSize(0)
	, GPUDebugDataFloatStride(0)
	, GPUDebugDataIntStride(0)
	, GPUDebugDataCountOffset(INDEX_NONE)
#endif	  
{
}

FNiagaraComputeExecutionContext::~FNiagaraComputeExecutionContext()
{
	// EmitterInstanceReadback.GPUCountOffset should be INDEX_NONE at this point to ensure the index is reused.
	// When the batcher is being destroyed though, we don't free the index, but this would not be leaking.
	// check(EmitterInstanceReadback.GPUCountOffset == INDEX_NONE);

#if WITH_EDITORONLY_DATA
	if (GPUDebugDataReadbackFloat)
	{
		delete GPUDebugDataReadbackFloat;
		GPUDebugDataReadbackFloat = nullptr;
	}
	if (GPUDebugDataReadbackInt)
	{
		delete GPUDebugDataReadbackInt;
		GPUDebugDataReadbackInt = nullptr;
	}
	if (GPUDebugDataReadbackCounts)
	{
		delete GPUDebugDataReadbackCounts;
		GPUDebugDataReadbackCounts = nullptr;
	}
#endif

	SetDataToRender(nullptr);
}

void FNiagaraComputeExecutionContext::Reset(NiagaraEmitterInstanceBatcher* Batcher)
{
	FNiagaraComputeExecutionContext* Context = this;
	NiagaraEmitterInstanceBatcher* B = Batcher && !Batcher->IsPendingKill() ? Batcher : nullptr;
	ENQUEUE_RENDER_COMMAND(ResetRT)(
		[B, Context](FRHICommandListImmediate& RHICmdList)
	{
		Context->ResetInternal(B);
	}
	);
}

void FNiagaraComputeExecutionContext::InitParams(UNiagaraScript* InGPUComputeScript, ENiagaraSimTarget InSimTarget, const FString& InDebugSimName, const uint32 InDefaultShaderStageIndex, const int32 InMaxUpdateIterations, const TSet<uint32> InSpawnStages)
{
#if !UE_BUILD_SHIPPING
	DebugSimName = InDebugSimName;
#endif
	GPUScript = InGPUComputeScript;
	CombinedParamStore.InitFromOwningContext(InGPUComputeScript, InSimTarget, true);
	DefaultShaderStageIndex = InDefaultShaderStageIndex;
	MaxUpdateIterations = InMaxUpdateIterations;
	SpawnStages.Empty();

	SpawnStages.Append(InSpawnStages);
	
	
#if DO_CHECK
	FNiagaraShader *Shader = InGPUComputeScript->GetRenderThreadScript()->GetShaderGameThread();
	DIParamInfo.Empty();
	if (Shader)
	{
		for (FNiagaraDataInterfaceParamRef& DIParams : Shader->GetDIParameters())
		{
			DIParamInfo.Add(DIParams.ParameterInfo);
		}
	}
	else
	{
		DIParamInfo = InGPUComputeScript->GetRenderThreadScript()->GetDataInterfaceParamInfo();
	}
#endif
}

void FNiagaraComputeExecutionContext::DirtyDataInterfaces()
{
	CombinedParamStore.MarkInterfacesDirty();
}

bool FNiagaraComputeExecutionContext::Tick(FNiagaraSystemInstance* ParentSystemInstance)
{
	if (CombinedParamStore.GetInterfacesDirty())
	{
#if DO_CHECK
		const TArray<UNiagaraDataInterface*> &DataInterfaces = CombinedParamStore.GetDataInterfaces();
		// We must make sure that the data interfaces match up between the original script values and our overrides...
		if (DIParamInfo.Num() != DataInterfaces.Num())
		{
			UE_LOG(LogNiagara, Warning, TEXT("Mismatch between Niagara GPU Execution Context data interfaces and those in its script!"));
			return false;
		}

		for (int32 i = 0; i < DIParamInfo.Num(); ++i)
		{
			FString UsedClassName = DataInterfaces[i]->GetClass()->GetName();
			if (DIParamInfo[i].DIClassName != UsedClassName)
			{
				UE_LOG(LogNiagara, Warning, TEXT("Mismatched class between Niagara GPU Execution Context data interfaces and those in its script!\nIndex:%d\nShader:%s\nScript:%s")
					, i, *DIParamInfo[i].DIClassName, *UsedClassName);
			}
		}
#endif

		CombinedParamStore.Tick();
	}

	return true;
}

void FNiagaraComputeExecutionContext::PostTick()
{
	//If we're for interpolated spawn, copy over the previous frame's parameters into the Prev parameters.
	if (GPUScript && GPUScript->GetComputedVMCompilationId().HasInterpolatedParameters())
	{
		CombinedParamStore.CopyCurrToPrev();
	}
}

void FNiagaraComputeExecutionContext::ResetInternal(NiagaraEmitterInstanceBatcher* Batcher)
{
	checkf(IsInRenderingThread(), TEXT("Can only reset the gpu context from the render thread"));

	// Release and reset readback data.
	if (Batcher)
	{
		Batcher->GetGPUInstanceCounterManager().FreeEntry(EmitterInstanceReadback.GPUCountOffset);
	}
	else // In this case the batcher is pending kill so no need to putback entry in the pool.
	{
		EmitterInstanceReadback.GPUCountOffset = INDEX_NONE;
	}

#if WITH_EDITORONLY_DATA
	if (GPUDebugDataReadbackFloat)
	{
		delete GPUDebugDataReadbackFloat;
		GPUDebugDataReadbackFloat = nullptr;
	}
	if (GPUDebugDataReadbackInt)
	{
		delete GPUDebugDataReadbackInt;
		GPUDebugDataReadbackInt = nullptr;
	}
	if (GPUDebugDataReadbackCounts)
	{
		delete GPUDebugDataReadbackCounts;
		GPUDebugDataReadbackCounts = nullptr;
	}
#endif

	SetDataToRender(nullptr);
}

void FNiagaraComputeExecutionContext::SetDataToRender(FNiagaraDataBuffer* InDataToRender)
{
	if (DataToRender)
	{
		DataToRender->ReleaseReadRef();
	}

	DataToRender = InDataToRender;

	if (DataToRender)
	{
		DataToRender->AddReadRef();
	}
}
