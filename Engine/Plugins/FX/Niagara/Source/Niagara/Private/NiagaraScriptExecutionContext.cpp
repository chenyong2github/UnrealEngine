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
#include "NiagaraDataInterfaceRW.h"

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

	HasInterpolationParameters = Script && Script->GetComputedVMCompilationId().HasInterpolatedParameters();

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
			const FNiagaraVMExecutableData& ScriptExecutableData = Script->GetVMExecutableData();
			const TArray<UNiagaraDataInterface*>& DataInterfaces = GetDataInterfaces();

			SCOPE_CYCLE_COUNTER(STAT_NiagaraRebindDataInterfaceFunctionTable);
			// UE_LOG(LogNiagara, Log, TEXT("Updating data interfaces for script %s"), *Script->GetFullName());

			// We must make sure that the data interfaces match up between the original script values and our overrides...
			if (ScriptExecutableData.DataInterfaceInfo.Num() != DataInterfaces.Num())
			{
				UE_LOG(LogNiagara, Warning, TEXT("Mismatch between Niagara Exectuion Context data interfaces and those in it's script!"));
				return false;
			}

			//Fill the instance data table.
			if (ParentSystemInstance)
			{
				DataInterfaceInstDataTable.SetNumZeroed(ScriptExecutableData.NumUserPtrs, false);
				for (int32 i = 0; i < DataInterfaces.Num(); i++)
				{
					UNiagaraDataInterface* Interface = DataInterfaces[i];

					int32 UserPtrIdx = ScriptExecutableData.DataInterfaceInfo[i].UserPtrIdx;
					if (UserPtrIdx != INDEX_NONE)
					{
						void* InstData = ParentSystemInstance->FindDataInterfaceInstanceData(Interface);
						DataInterfaceInstDataTable[UserPtrIdx] = InstData;
					}
				}
			}
			else
			{
				check(ScriptExecutableData.NumUserPtrs == 0);//Can't have user ptrs if we have no parent instance.
			}

			const int32 FunctionCount = ScriptExecutableData.CalledVMExternalFunctions.Num();
			FunctionTable.Reset(FunctionCount);
			FunctionTable.AddZeroed(FunctionCount);
			TArray<int32> LocalFunctionTableIndices;
			LocalFunctionTableIndices.Reserve(FunctionCount);

			const auto& ScriptDataInterfaces = Script->GetExecutionReadyParameterStore(SimTarget)->GetDataInterfaces();

			bool bSuccessfullyMapped = true;

			for (int32 FunctionIt = 0; FunctionIt < FunctionCount; ++FunctionIt)
			{
				const FVMExternalFunctionBindingInfo& BindingInfo = ScriptExecutableData.CalledVMExternalFunctions[FunctionIt];

				// First check to see if we can pull from the fast path library..
				FVMExternalFunction FuncBind;
				if (UNiagaraFunctionLibrary::GetVectorVMFastPathExternalFunction(BindingInfo, FuncBind) && FuncBind.IsBound())
				{
					LocalFunctionTable.Add(FuncBind);
					LocalFunctionTableIndices.Add(FunctionIt);
					continue;
				}

				for (int32 i = 0; i < ScriptExecutableData.DataInterfaceInfo.Num(); i++)
				{
					const FNiagaraScriptDataInterfaceCompileInfo& ScriptInfo = ScriptExecutableData.DataInterfaceInfo[i];
					UNiagaraDataInterface* ExternalInterface = DataInterfaces[i];
					if (ScriptInfo.Name == BindingInfo.OwnerName)
					{
						// first check to see if we should just use the one from the script
						if (ScriptExecutableData.CalledVMExternalFunctionBindings.IsValidIndex(FunctionIt)
							&& ExternalInterface == ScriptDataInterfaces[i])
						{
							const FVMExternalFunction& ScriptFuncBind = ScriptExecutableData.CalledVMExternalFunctionBindings[FunctionIt];
							if (ScriptFuncBind.IsBound())
							{
								FunctionTable[FunctionIt] = &ScriptFuncBind;

								check(ScriptInfo.UserPtrIdx == INDEX_NONE);
								break;
							}
						}

						void* InstData = ScriptInfo.UserPtrIdx == INDEX_NONE ? nullptr : DataInterfaceInstDataTable[ScriptInfo.UserPtrIdx];
						FVMExternalFunction& LocalFunction = LocalFunctionTable.AddDefaulted_GetRef();
						LocalFunctionTableIndices.Add(FunctionIt);

						if (ExternalInterface != nullptr)
						{
							ExternalInterface->GetVMExternalFunction(BindingInfo, InstData, LocalFunction);
						}

						if (!LocalFunction.IsBound())
						{
							UE_LOG(LogNiagara, Error, TEXT("Could not Get VMExternalFunction '%s'.. emitter will not run!"), *BindingInfo.Name.ToString());
							bSuccessfullyMapped = false;
						}
						break;
					}
				}
			}

			const int32 LocalFunctionCount = LocalFunctionTableIndices.Num();
			for (int32 LocalFunctionIt = 0; LocalFunctionIt < LocalFunctionCount; ++LocalFunctionIt)
			{
				FunctionTable[LocalFunctionTableIndices[LocalFunctionIt]] = &LocalFunctionTable[LocalFunctionIt];
			}

#if WITH_EDITOR	
			// We may now have new errors that we need to broadcast about, so flush the asset parameters delegate..
			if (ParentSystemInstance)
			{
				ParentSystemInstance->RaiseNeedsUIResync();
			}
#endif

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
	if (HasInterpolationParameters)
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
	TArrayView<uint8 const* RESTRICT const> InputRegisters = Input ? Input->GetRegisterTable() : TArrayView<uint8 const* RESTRICT const>();
	TArrayView<uint8 const* RESTRICT const> OutputRegisters = Output ? Output->GetRegisterTable() : TArrayView<uint8 const* RESTRICT const>();

	DataSetMetaTable.SetNum(FMath::Max(DataSetMetaTable.Num(), Index + 1));
	DataSetMetaTable[Index].Init(InputRegisters, OutputRegisters, StartInstance,
		Output ? &Output->GetIDTable() : nullptr, &DataSet.GetFreeIDTable(), &DataSet.GetNumFreeIDs(), &DataSet.GetMaxUsedID(), DataSet.GetIDAcquireTag(), &DataSet.GetSpawnedIDsTable());

	if (InputRegisters.Num() > 0)
	{
		static_assert(sizeof(DataSetMetaTable[Index].InputRegisterTypeOffsets) == sizeof(FNiagaraDataBuffer::RegisterTypeOffsetType), "ArraySizes do not match");
		memcpy(DataSetMetaTable[Index].InputRegisterTypeOffsets, Input->GetRegisterTypeOffsets(), sizeof(FNiagaraDataBuffer::RegisterTypeOffsetType));
	}

	if (OutputRegisters.Num() > 0)
	{
		static_assert(sizeof(DataSetMetaTable[Index].OutputRegisterTypeOffsets) == sizeof(FNiagaraDataBuffer::RegisterTypeOffsetType), "ArraySizes do not match");
		memcpy(DataSetMetaTable[Index].OutputRegisterTypeOffsets, Output->GetRegisterTypeOffsets(), sizeof(FNiagaraDataBuffer::RegisterTypeOffsetType));
	}
}

void FNiagaraScriptExecutionContext::BindData(int32 Index, FNiagaraDataBuffer* Input, int32 StartInstance, bool bUpdateInstanceCounts)
{
	check(Input && Input->GetOwner());
	DataSetInfo.SetNum(FMath::Max(DataSetInfo.Num(), Index + 1));
	FNiagaraDataSet* DataSet = Input->GetOwner();
	DataSetInfo[Index].Init(DataSet, Input, nullptr, StartInstance, bUpdateInstanceCounts);

	TArrayView<uint8 const* RESTRICT const> InputRegisters = Input->GetRegisterTable();

	DataSetMetaTable.SetNum(FMath::Max(DataSetMetaTable.Num(), Index + 1));
	DataSetMetaTable[Index].Init(InputRegisters, TArrayView<uint8 const* RESTRICT const>(), StartInstance, nullptr, nullptr, &DataSet->GetNumFreeIDs(), &DataSet->GetMaxUsedID(), DataSet->GetIDAcquireTag(), &DataSet->GetSpawnedIDsTable());

	if (InputRegisters.Num() > 0)
	{
		static_assert(sizeof(DataSetMetaTable[Index].InputRegisterTypeOffsets) == sizeof(FNiagaraDataBuffer::RegisterTypeOffsetType), "ArraySizes do not match");
		memcpy(DataSetMetaTable[Index].InputRegisterTypeOffsets, Input->GetRegisterTypeOffsets(), sizeof(FNiagaraDataBuffer::RegisterTypeOffsetType));
	}
}

bool FNiagaraScriptExecutionContext::Execute(uint32 NumInstances, const FScriptExecutionConstantBufferTable& ConstantBufferTable)
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
			ConstantBufferTable.Buffers.Num(),
			ConstantBufferTable.Buffers.GetData(),
			ConstantBufferTable.BufferSizes.GetData(),
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

TArrayView<const uint8> FNiagaraScriptExecutionContext::GetScriptLiterals() const
{
#if WITH_EDITORONLY_DATA
	return Parameters.GetScriptLiterals();
#else
	return MakeArrayView(Script->GetVMExecutableData().ScriptLiterals);
#endif
}

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
	uint32 TotalParamSize = InSystemInstance->TotalGPUParamSize;

	uint32 TotalPackedBufferSize = PackedDispatchesSizeAligned + TotalParamSize;

	InstanceData_ParamData_Packed = (uint8*)FMemory::Malloc(TotalPackedBufferSize);

	FNiagaraComputeInstanceData* Instances = (FNiagaraComputeInstanceData*)(InstanceData_ParamData_Packed);
	uint8* ParamDataBufferPtr = InstanceData_ParamData_Packed + PackedDispatchesSizeAligned;

	int32 TickCount = InSystemInstance->GetTickCount();
	check(TickCount > 0);
	bNeedsReset = ( TickCount == 1);
	NumInstancesWithSimStages = 0;

	TotalDispatches = 0;

	// we want to include interpolation parameters (current and previous frame) if any of the emitters in the system
	// require it
	const bool IncludeInterpolationParameters = InSystemInstance->GPUParamIncludeInterpolation;
	const int32 InterpFactor = IncludeInterpolationParameters ? 2 : 1;

	GlobalParamData = ParamDataBufferPtr;
	SystemParamData = GlobalParamData + InterpFactor * sizeof(FNiagaraGlobalParameters);
	OwnerParamData = SystemParamData + InterpFactor * sizeof(FNiagaraSystemParameters);

	// actually copy all of the data over, for the system data we only need to do it once (rather than per-emitter)
	FMemory::Memcpy(GlobalParamData, &InSystemInstance->GetGlobalParameters(), sizeof(FNiagaraGlobalParameters));
	FMemory::Memcpy(SystemParamData, &InSystemInstance->GetSystemParameters(), sizeof(FNiagaraSystemParameters));
	FMemory::Memcpy(OwnerParamData, &InSystemInstance->GetOwnerParameters(), sizeof(FNiagaraOwnerParameters));

	if (IncludeInterpolationParameters)
	{
		FMemory::Memcpy(GlobalParamData + sizeof(FNiagaraGlobalParameters), &InSystemInstance->GetGlobalParameters(true), sizeof(FNiagaraGlobalParameters));
		FMemory::Memcpy(SystemParamData + sizeof(FNiagaraSystemParameters), &InSystemInstance->GetSystemParameters(true), sizeof(FNiagaraSystemParameters));
		FMemory::Memcpy(OwnerParamData + sizeof(FNiagaraOwnerParameters), &InSystemInstance->GetOwnerParameters(true), sizeof(FNiagaraOwnerParameters));
	}

	ParamDataBufferPtr = OwnerParamData + InterpFactor * sizeof(FNiagaraOwnerParameters);

	// Now we will generate instance data for every GPU simulation we want to run on the render thread.
	// This is spawn rate as well as DataInterface per instance data and the ParameterData for the emitter.
	// @todo Ideally we would only update DataInterface and ParameterData bits if they have changed.
	uint32 InstanceIndex = 0;
	for (int32 EmitterIdx : InSystemInstance->GetEmitterExecutionOrder())
	{
		FNiagaraEmitterInstance* Emitter = &InSystemInstance->GetEmitters()[EmitterIdx].Get();

		if (Emitter && Emitter->GetCachedEmitter()->SimTarget == ENiagaraSimTarget::GPUComputeSim && Emitter->GetGPUContext() != nullptr && !Emitter->IsComplete())
		{
			check(Emitter->HasTicked() == true);
			
			FNiagaraComputeExecutionContext* GPUContext = Emitter->GetGPUContext();

			FNiagaraComputeInstanceData* InstanceData = new (&Instances[InstanceIndex]) FNiagaraComputeInstanceData;
			InstanceIndex++;

			InstanceData->Context = GPUContext;
			check(GPUContext->MainDataSet);

			InstanceData->SpawnInfo = GPUContext->GpuSpawnInfo_GT;

			int32 ParmSize = GPUContext->CombinedParamStore.GetPaddedParameterSizeInBytes();

			InstanceData->EmitterParamData = ParamDataBufferPtr;
			ParamDataBufferPtr += InterpFactor * sizeof(FNiagaraEmitterParameters);

			InstanceData->ExternalParamData = ParamDataBufferPtr;
			ParamDataBufferPtr += ParmSize;

			// actually copy all of the data over
			FMemory::Memcpy(InstanceData->EmitterParamData, &InSystemInstance->GetEmitterParameters(EmitterIdx), sizeof(FNiagaraEmitterParameters));
			if (IncludeInterpolationParameters)
			{
				FMemory::Memcpy(InstanceData->EmitterParamData + sizeof(FNiagaraEmitterParameters), &InSystemInstance->GetEmitterParameters(EmitterIdx, true), sizeof(FNiagaraEmitterParameters));
			}

			GPUContext->CombinedParamStore.CopyParameterDataToPaddedBuffer(InstanceData->ExternalParamData, ParmSize);

			UNiagaraEmitter* EmitterRaw = Emitter->GetCachedEmitter();
			if (EmitterRaw)
			{
				InstanceData->bUsesSimStages = EmitterRaw->bSimulationStagesEnabled/* TODO limit to just with stages in the future! Leaving like this so what can convert! && EmitterRaw->GetSimulationStages().Num() > 0*/;
				InstanceData->bUsesOldShaderStages = EmitterRaw->bDeprecatedShaderStagesEnabled;
			}
			else
			{
				InstanceData->bUsesSimStages = false;
			}

			if (InstanceData->bUsesSimStages || InstanceData->bUsesOldShaderStages)
			{
				NumInstancesWithSimStages++;
			}

			check(GPUContext->MaxUpdateIterations > 0);
			InstanceData->SimStageData.AddZeroed(GPUContext->MaxUpdateIterations);
			TotalDispatches += FMath::Max<int32>(GPUContext->MaxUpdateIterations, 1);

			// @todo-threadsafety Think of a better way to do this!
			const TArray<UNiagaraDataInterface*>& DataInterfaces = GPUContext->CombinedParamStore.GetDataInterfaces();
			InstanceData->DataInterfaceProxies.Reserve(DataInterfaces.Num());
			//UE_LOG(LogNiagara, Log, TEXT("InitTick %s %d"), GPUContext->GetDebugSimName())
			for (UNiagaraDataInterface* DI : DataInterfaces)
			{
				check(DI->GetProxy());
				InstanceData->DataInterfaceProxies.Add(DI->GetProxy());
				//UE_LOG(LogNiagara, Log, TEXT("Proxy %p for DI %p"), DI->GetProxy(), DI);
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

FUniformBufferRHIRef FNiagaraGPUSystemTick::GetUniformBuffer(EUniformBufferType Type, const FNiagaraComputeInstanceData* Instance, bool Current) const
{
	const int32 InterpOffset = Current
		? 0
		: (UBT_NumSystemTypes + Count * UBT_NumInstanceTypes);

	if (Instance)
	{
		check(Type >= UBT_FirstInstanceType);
		check(Type < UBT_NumTypes);

		const int32 InstanceTypeIndex = Type - UBT_FirstInstanceType;

		const int32 InstanceIndex = (Instance - GetInstanceData());
		return UniformBuffers[InterpOffset + UBT_NumSystemTypes + Count * InstanceTypeIndex + InstanceIndex];
	}

	check(Type >= UBT_FirstSystemType);
	check(Type < UBT_FirstInstanceType);

	return UniformBuffers[InterpOffset + Type];
}

const uint8* FNiagaraGPUSystemTick::GetUniformBufferSource(EUniformBufferType Type, const FNiagaraComputeInstanceData* Instance, bool Current) const
{
	check(Type >= UBT_FirstSystemType);
	check(Type < UBT_NumTypes);

	switch (Type)
	{
		case UBT_Global:
			return GlobalParamData + (Current ? 0 : sizeof(FNiagaraGlobalParameters));
		case UBT_System:
			return SystemParamData + (Current ? 0 : sizeof(FNiagaraSystemParameters));
		case UBT_Owner:
			return OwnerParamData + (Current ? 0 : sizeof(FNiagaraOwnerParameters));
		case UBT_Emitter:
		{
			check(Instance);
			return Instance->EmitterParamData + (Current ? 0 : sizeof(FNiagaraEmitterParameters));
		}
		case UBT_External:
		{
			check(Instance);
			return Instance->ExternalParamData + (Current ? 0 : Instance->Context->ExternalCBufferLayout.ConstantBufferSize);
		}
	}

	return nullptr;
}

//////////////////////////////////////////////////////////////////////////

FNiagaraComputeExecutionContext::FNiagaraComputeExecutionContext()
	: MainDataSet(nullptr)
	, GPUScript(nullptr)
	, GPUScript_RT(nullptr)
	, ExternalCBufferLayout(TEXT("Niagara GPU External CBuffer"))
	, DataToRender(nullptr)

{
}

FNiagaraComputeExecutionContext::~FNiagaraComputeExecutionContext()
{
	// EmitterInstanceReadback.GPUCountOffset should be INDEX_NONE at this point to ensure the index is reused.
	// When the batcher is being destroyed though, we don't free the index, but this would not be leaking.
	// check(EmitterInstanceReadback.GPUCountOffset == INDEX_NONE);

#if WITH_EDITORONLY_DATA
	GPUDebugDataReadbackFloat.Reset();
	GPUDebugDataReadbackInt.Reset();
	GPUDebugDataReadbackHalf.Reset();
	GPUDebugDataReadbackCounts.Reset();
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

void FNiagaraComputeExecutionContext::InitParams(UNiagaraScript* InGPUComputeScript, ENiagaraSimTarget InSimTarget, const uint32 InDefaultSimulationStageIndex, const int32 InMaxUpdateIterations, const TSet<uint32> InSpawnStages)
{
	GPUScript = InGPUComputeScript;
	CombinedParamStore.InitFromOwningContext(InGPUComputeScript, InSimTarget, true);
	DefaultSimulationStageIndex = InDefaultSimulationStageIndex;
	MaxUpdateIterations = InMaxUpdateIterations;
	SpawnStages.Empty();

	SpawnStages.Append(InSpawnStages);
	
	HasInterpolationParameters = GPUScript && GPUScript->GetComputedVMCompilationId().HasInterpolatedParameters();

	if (InGPUComputeScript)
	{
		FNiagaraVMExecutableData& VMData = InGPUComputeScript->GetVMExecutableData();
		if (VMData.IsValid() && VMData.SimulationStageMetaData.Num() > 0)
		{

			SimStageInfo = VMData.SimulationStageMetaData;

			int32 FoundMaxUpdateIterations = SimStageInfo[SimStageInfo.Num() - 1].MaxStage;

			// Some useful debugging code should we need to look up differences between old and new
			const bool bDebugSimStages = false;
			if (bDebugSimStages)
			{
				UE_LOG(LogNiagara, Log, TEXT("Stored vs:"));
				bool bPass = FoundMaxUpdateIterations == MaxUpdateIterations;
				UE_LOG(LogNiagara, Log, TEXT("MaxUpdateIterations: %d vs %d %s"), FoundMaxUpdateIterations, MaxUpdateIterations, bPass ? TEXT("Pass") : TEXT("FAIL!!!!!!!!"));

				int32 NumSpawnFound = 0;
				bool bMatchesFound = true;
				for (int32 i = 0; i < SimStageInfo.Num(); i++)
				{
					if (SimStageInfo[i].bSpawnOnly)
					{
						NumSpawnFound++;

						if (!SpawnStages.Contains(SimStageInfo[i].MinStage))
						{
							bMatchesFound = false;
							UE_LOG(LogNiagara, Log, TEXT("Missing spawn stage: %d FAIL!!!!!!!!!"), SimStageInfo[i].MinStage);
						}
					}
				}

				bPass = SpawnStages.Num() == NumSpawnFound;
				UE_LOG(LogNiagara, Log, TEXT("SpawnStages.Num(): %d vs %d %s"), NumSpawnFound, SpawnStages.Num(), bPass ? TEXT("Pass") : TEXT("FAIL!!!!!!!!"));

				TArray<FNiagaraVariable> Params;
				CombinedParamStore.GetParameters(Params);
				for (FNiagaraVariable& Var : Params)
				{
					if (!Var.IsDataInterface())
						continue;

					UNiagaraDataInterface* DI = CombinedParamStore.GetDataInterface(Var);
					UNiagaraDataInterfaceRWBase* DIRW = Cast<UNiagaraDataInterfaceRWBase>(DI);
					if (DIRW)
					{
						for (int32 i = 0; i < SimStageInfo.Num(); i++)
						{
							if (SimStageInfo[i].IterationSource == Var.GetName())
							{
								if (!DIRW->IterationShaderStages.Contains(SimStageInfo[i].MinStage))
								{
									UE_LOG(LogNiagara, Log, TEXT("Missing iteration stage for %s: %d FAIL!!!!!!!!!"), *Var.GetName().ToString(), SimStageInfo[i].MinStage);
								}
							}

							if (SimStageInfo[i].OutputDestinations.Contains(Var.GetName()))
							{
								if (!DIRW->OutputShaderStages.Contains(SimStageInfo[i].MinStage))
								{
									UE_LOG(LogNiagara, Log, TEXT("Missing output stage for %s: %d FAIL!!!!!!!!!"), *Var.GetName().ToString(), SimStageInfo[i].MinStage);
								}
							}
						}
					}

				}
			}


			// Set the values that we are using from compiled data instead...
			MaxUpdateIterations = SimStageInfo[SimStageInfo.Num() - 1].MaxStage;
			SpawnStages.Empty();

			for (int32 i = 0; i < SimStageInfo.Num(); i++)
			{
				if (SimStageInfo[i].bSpawnOnly)
				{
					SpawnStages.Add(SimStageInfo[i].MinStage);
				}
			}

			// We need to store the name of each DI source variable here so that we can look it up later when looking for the 
			// iteration interface.
			TArray<FNiagaraVariable> Params;
			CombinedParamStore.GetParameters(Params);
			for (FNiagaraVariable& Var : Params)
			{
				if (!Var.IsDataInterface())
					continue;

				UNiagaraDataInterface* DI = CombinedParamStore.GetDataInterface(Var);
				if (DI)
				{
					FNiagaraDataInterfaceProxy* Proxy = DI->GetProxy();
					if (Proxy)
					{
						Proxy->SourceDIName = Var.GetName();
					}
				}
			}
		}
	}

	
#if DO_CHECK
	FNiagaraShaderRef Shader = InGPUComputeScript->GetRenderThreadScript()->GetShaderGameThread();
	if (Shader.IsValid())
	{
		DIClassNames.Empty(Shader->GetDIParameters().Num());
		for (const FNiagaraDataInterfaceParamRef& DIParams : Shader->GetDIParameters())
		{
			DIClassNames.Add(DIParams.DIType.Get(Shader.GetPointerTable().DITypes)->GetClass()->GetName());
		}
	}
	else
	{
		DIClassNames.Empty(InGPUComputeScript->GetRenderThreadScript()->GetDataInterfaceParamInfo().Num());
		for (const FNiagaraDataInterfaceGPUParamInfo& DIParams : InGPUComputeScript->GetRenderThreadScript()->GetDataInterfaceParamInfo())
		{
			DIClassNames.Add(DIParams.DIClassName);
		}
	}
#endif
}


const FSimulationStageMetaData* FNiagaraComputeExecutionContext::GetSimStageMetaData(uint32 SimulationStageIndex) const
{
	if (SimStageInfo.Num() > 0)
	{
		for (int32 i = 0; i < SimStageInfo.Num(); i++)
		{
			if (SimulationStageIndex >= (uint32)SimStageInfo[i].MinStage  && SimulationStageIndex < (uint32)SimStageInfo[i].MaxStage)
			{
				return &SimStageInfo[i];
			}
		}
	}
	return nullptr;
}

bool FNiagaraComputeExecutionContext::IsOutputStage(FNiagaraDataInterfaceProxy* DIProxy, uint32 CurrentStage) const
{
	const FSimulationStageMetaData* MetaData = GetSimStageMetaData(CurrentStage);
	if (MetaData && DIProxy && !DIProxy->SourceDIName.IsNone())
	{
		if (MetaData->OutputDestinations.Contains(DIProxy->SourceDIName))
			return true;
	}
	else if (DIProxy && SimStageInfo.Num() == 0)
	{
		return DIProxy->IsOutputStage_DEPRECATED(CurrentStage);
	}
	return false;
}

bool FNiagaraComputeExecutionContext::IsIterationStage(FNiagaraDataInterfaceProxy* DIProxy, uint32 CurrentStage) const
{
	const FSimulationStageMetaData* MetaData = GetSimStageMetaData(CurrentStage);
	if (MetaData && DIProxy && !DIProxy->SourceDIName.IsNone())
	{
		if (MetaData->IterationSource.IsNone()) // Per particle iteration...
			return false;

		if (MetaData->IterationSource == DIProxy->SourceDIName)
			return true;
	}
	else if (DIProxy && SimStageInfo.Num() == 0)
	{
		return DIProxy->IsIterationStage_DEPRECATED(CurrentStage);
	}
	return false;
}

FNiagaraDataInterfaceProxy* FNiagaraComputeExecutionContext::FindIterationInterface(const TArray<FNiagaraDataInterfaceProxy*>& InProxies, uint32 CurrentStage) const
{
	const FSimulationStageMetaData* MetaData = GetSimStageMetaData(CurrentStage);
	if (MetaData)
	{
		if (MetaData->IterationSource.IsNone()) // Per particle iteration...
			return nullptr;

		for (FNiagaraDataInterfaceProxy* Proxy : InProxies)
		{
			if (Proxy->SourceDIName == MetaData->IterationSource)
				return Proxy;
		}

		UE_LOG(LogNiagara, Verbose, TEXT("FNiagaraComputeExecutionContext::FindIterationInterface could not find IterationInterface %s"), *MetaData->IterationSource.ToString());

		return nullptr;
	}
	else if (SimStageInfo.Num() == 0)
	{

		// Fallback to old shader stages
		for (FNiagaraDataInterfaceProxy* Proxy : InProxies)
		{
			if (Proxy->IsIterationStage_DEPRECATED(CurrentStage))
				return Proxy;
		}
	}

	return nullptr;
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
		if (DIClassNames.Num() != DataInterfaces.Num())
		{
			UE_LOG(LogNiagara, Warning, TEXT("Mismatch between Niagara GPU Execution Context data interfaces and those in its script!"));
			return false;
		}

		for (int32 i = 0; i < DIClassNames.Num(); ++i)
		{
			FString UsedClassName = DataInterfaces[i]->GetClass()->GetName();
			if (DIClassNames[i] != UsedClassName)
			{
				UE_LOG(LogNiagara, Warning, TEXT("Mismatched class between Niagara GPU Execution Context data interfaces and those in its script!\nIndex:%d\nShader:%s\nScript:%s")
					, i, *DIClassNames[i], *UsedClassName);
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
	if (HasInterpolationParameters)
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
	GPUDebugDataReadbackFloat.Reset();
	GPUDebugDataReadbackInt.Reset();
	GPUDebugDataReadbackHalf.Reset();
	GPUDebugDataReadbackCounts.Reset();
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

bool FNiagaraComputeInstanceData::IsOutputStage(FNiagaraDataInterfaceProxy* DIProxy, uint32 CurrentStage) const
{
	if (bUsesOldShaderStages)
	{
		return DIProxy->IsOutputStage_DEPRECATED(CurrentStage);
	}
	else if (bUsesSimStages)
	{
		return Context->IsOutputStage(DIProxy, CurrentStage);
	}
	return false;
}

bool FNiagaraComputeInstanceData::IsIterationStage(FNiagaraDataInterfaceProxy* DIProxy, uint32 CurrentStage) const
{

	if (bUsesOldShaderStages)
	{
		return DIProxy->IsIterationStage_DEPRECATED(CurrentStage);
	}
	else if (bUsesSimStages)
	{
		return Context->IsIterationStage(DIProxy, CurrentStage);
	}
	return false;
}

FNiagaraDataInterfaceProxy* FNiagaraComputeInstanceData::FindIterationInterface(uint32 SimulationStageIndex) const
{

	if (bUsesOldShaderStages)
	{
		FNiagaraDataInterfaceProxy* IterationInterface = nullptr;

		for (FNiagaraDataInterfaceProxy* Interface : DataInterfaceProxies)
		{
			if (Interface->IsIterationStage_DEPRECATED(SimulationStageIndex))
			{
				if (IterationInterface)
				{
					UE_LOG(LogNiagara, Error, TEXT("Multiple output Data Interfaces found for current stage"));
				}
				else
				{
					IterationInterface = Interface;
				}
			}
		}
		return IterationInterface;
	}
	else if (bUsesSimStages)
	{
		return Context->FindIterationInterface(DataInterfaceProxies, SimulationStageIndex);
	}
	return nullptr;
}