// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraScriptExecutionContext.h"
#include "NiagaraStats.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraDataInterface.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemGpuComputeProxy.h"
#include "NiagaraWorldManager.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraGPUInstanceCountManager.h"
#include "NiagaraDataInterfaceRW.h"

DECLARE_CYCLE_STAT(TEXT("Register Setup"), STAT_NiagaraSimRegisterSetup, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Context Ticking"), STAT_NiagaraScriptExecContextTick, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Rebind DInterface Func Table"), STAT_NiagaraRebindDataInterfaceFunctionTable, STATGROUP_Niagara);
	//Add previous frame values if we're interpolated spawn.
	
	//Internal constants - only needed for non-GPU sim

uint32 FNiagaraScriptExecutionContextBase::TickCounter = 0;

static int32 GbExecVMScripts = 1;
static FAutoConsoleVariableRef CVarNiagaraExecVMScripts(
	TEXT("fx.ExecVMScripts"),
	GbExecVMScripts,
	TEXT("If > 0 VM scripts will be executed, otherwise they won't, useful for looking at the bytecode for a crashing compiled script. \n"),
	ECVF_Default
);

FNiagaraScriptExecutionContextBase::FNiagaraScriptExecutionContextBase()
	: Script(nullptr)
	, bAllowParallel(true)
{

}

FNiagaraScriptExecutionContextBase::~FNiagaraScriptExecutionContextBase()
{
}

bool FNiagaraScriptExecutionContextBase::Init(UNiagaraScript* InScript, ENiagaraSimTarget InTarget)
{
	Script = InScript;

	Parameters.InitFromOwningContext(Script, InTarget, true);

	HasInterpolationParameters = Script && Script->GetComputedVMCompilationId().HasInterpolatedParameters();

	return true;
}

void FNiagaraScriptExecutionContextBase::BindData(int32 Index, FNiagaraDataSet& DataSet, int32 StartInstance, bool bUpdateInstanceCounts)
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

void FNiagaraScriptExecutionContextBase::BindData(int32 Index, FNiagaraDataBuffer* Input, int32 StartInstance, bool bUpdateInstanceCounts)
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

#if STATS
void FNiagaraScriptExecutionContextBase::CreateStatScopeData()
{
	StatScopeData.Empty();
	for (const TStatId& StatId : Script->GetStatScopeIDs())
	{
		StatScopeData.Add(FStatScopeData(StatId));
	}
}

TMap<TStatIdData const*, float> FNiagaraScriptExecutionContextBase::ReportStats()
{
	// Process recorded times
	for (FStatScopeData& ScopeData : StatScopeData)
	{
		uint64 ExecCycles = ScopeData.ExecutionCycleCount.exchange(0);
		if (ExecCycles > 0)
		{
			ExecutionTimings.FindOrAdd(ScopeData.StatId.GetRawPointer()) = ExecCycles;
		}
	}
	return ExecutionTimings;
}
#endif

bool FNiagaraScriptExecutionContextBase::Execute(uint32 NumInstances, const FScriptExecutionConstantBufferTable& ConstantBufferTable)
{
	if (NumInstances == 0)
	{
		DataSetInfo.Reset();
		return true;
	}

	++TickCounter;//Should this be per execution?

	if (GbExecVMScripts != 0)
	{
#if STATS
		CreateStatScopeData();
#endif
		const FNiagaraVMExecutableData& ExecData = Script->GetVMExecutableData();

		VectorVM::FVectorVMExecArgs ExecArgs;
		ExecArgs.ByteCode = ExecData.ByteCode.GetData();
		ExecArgs.OptimizedByteCode = ExecData.OptimizedByteCode.Num() > 0 ? ExecData.OptimizedByteCode.GetData() : nullptr;
		ExecArgs.NumTempRegisters = ExecData.NumTempRegisters;
		ExecArgs.ConstantTableCount = ConstantBufferTable.Buffers.Num();
		ExecArgs.ConstantTable = ConstantBufferTable.Buffers.GetData();
		ExecArgs.ConstantTableSizes = ConstantBufferTable.BufferSizes.GetData();
		ExecArgs.DataSetMetaTable = DataSetMetaTable;
		ExecArgs.ExternalFunctionTable = FunctionTable.GetData();
		ExecArgs.UserPtrTable = UserPtrTable.GetData();
		ExecArgs.NumInstances = NumInstances;
#if STATS
		ExecArgs.StatScopes = MakeArrayView(StatScopeData);
#elif ENABLE_STATNAMEDEVENTS
		ExecArgs.StatNamedEventsScopes = Script->GetStatNamedEvents();
#endif
		
		ExecArgs.bAllowParallel = bAllowParallel;
		VectorVM::Exec(ExecArgs);
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

bool FNiagaraScriptExecutionContextBase::CanExecute()const
{
	return Script && Script->GetVMExecutableData().IsValid() && Script->GetVMExecutableData().ByteCode.Num() > 0;
}

TArrayView<const uint8> FNiagaraScriptExecutionContextBase::GetScriptLiterals() const
{
#if WITH_EDITORONLY_DATA
	if (!Script->IsScriptCooked())
	{
		return Parameters.GetScriptLiterals();
	}
#endif
	return MakeArrayView(Script->GetVMExecutableData().ScriptLiterals);
}

//////////////////////////////////////////////////////////////////////////

void FNiagaraScriptExecutionContextBase::DirtyDataInterfaces()
{
	Parameters.MarkInterfacesDirty();
}

bool FNiagaraScriptExecutionContext::Tick(FNiagaraSystemInstance* ParentSystemInstance, ENiagaraSimTarget SimTarget)
{
	//Bind data interfaces if needed.
	if (Parameters.GetInterfacesDirty())
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraScriptExecContextTick);
		if (Script && Script->IsReadyToRun(ENiagaraSimTarget::CPUSim) && SimTarget == ENiagaraSimTarget::CPUSim)//TODO: Remove. Script can only be null for system instances that currently don't have their script exec context set up correctly.
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

			const FNiagaraScriptExecutionParameterStore* ScriptParameterStore = Script->GetExecutionReadyParameterStore(SimTarget);
			check(ScriptParameterStore != nullptr);

			//Fill the instance data table.
			if (ParentSystemInstance)
			{
				UserPtrTable.SetNumZeroed(ScriptExecutableData.NumUserPtrs, false);
				for (int32 i = 0; i < DataInterfaces.Num(); i++)
				{
					UNiagaraDataInterface* Interface = DataInterfaces[i];

					int32 UserPtrIdx = ScriptExecutableData.DataInterfaceInfo[i].UserPtrIdx;
					if (UserPtrIdx != INDEX_NONE)
					{
						void* InstData = ParentSystemInstance->FindDataInterfaceInstanceData(Interface);
						UserPtrTable[UserPtrIdx] = InstData;
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
			LocalFunctionTable.Reset();
			TArray<int32> LocalFunctionTableIndices;
			LocalFunctionTableIndices.Reserve(FunctionCount);

			const auto& ScriptDataInterfaces = ScriptParameterStore->GetDataInterfaces();

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
							&& ScriptDataInterfaces.IsValidIndex(i)
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

						void* InstData = ScriptInfo.UserPtrIdx == INDEX_NONE ? nullptr : UserPtrTable[ScriptInfo.UserPtrIdx];
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

void FNiagaraScriptExecutionContextBase::PostTick()
{
	//If we're for interpolated spawn, copy over the previous frame's parameters into the Prev parameters.
	if (HasInterpolationParameters)
	{
		Parameters.CopyCurrToPrev();
	}
}

//////////////////////////////////////////////////////////////////////////

void FNiagaraSystemScriptExecutionContext::PerInstanceFunctionHook(FVectorVMContext& Context, int32 PerInstFunctionIndex, int32 UserPtrIndex)
{
	check(SystemInstances);
	
	//This is a bit of a hack. We grab the base offset into the instance data from the primary dataset.
	//TODO: Find a cleaner way to do this.
	int32 InstanceOffset = Context.GetDataSetMeta(0).InstanceOffset;

	//Cache context state.
	int32 CachedContextStartInstance = Context.StartInstance;
	int32 CachedContextNumInstances = Context.NumInstances;
	uint8 const* CachedCodeLocation = Context.Code;

	//Hack context so we can run the DI calls one by one.
	Context.NumInstances = 1;

	for (int32 i = 0; i < CachedContextNumInstances; ++i)
	{
		//Reset the code each iteration.
		Context.Code = CachedCodeLocation;
		//Offset buffer I/O to the correct instance's data.
		Context.ExternalFunctionInstanceOffset = i;

		int32 InstanceIndex = InstanceOffset + CachedContextStartInstance + i;
		FNiagaraSystemInstance* Instance = (*SystemInstances)[InstanceIndex];
		const FNiagaraPerInstanceDIFuncInfo& FuncInfo = Instance->GetPerInstanceDIFunction(ScriptType, PerInstFunctionIndex);
		
		//TODO: We can embed the instance data inside the function lambda. No need for the user ptr table at all.
		//Do this way for now to reduce overall complexity of the initial change. Doing this needs extensive boiler plate changes to most DI classes and a script recompile.
		if(UserPtrIndex != INDEX_NONE)
		{
			Context.UserPtrTable[UserPtrIndex] = FuncInfo.InstData;
		}

		Context.StartInstance = InstanceIndex;

		//TODO: In future for DIs where more perf is needed here we could split the DI func into an args gen and a execution.
		//The this path could gen args from the bytecode once and just run the execution func per instance.
		//I wonder if we could auto generate the args gen in a template func and just pass them into the DI for perf and reduced end user/author complexity.
		FuncInfo.Function.Execute(Context);
	}

	//Restore the context state.
	Context.ExternalFunctionInstanceOffset = 0;
	Context.StartInstance = CachedContextStartInstance;
	Context.NumInstances = CachedContextNumInstances;
}

bool FNiagaraSystemScriptExecutionContext::Init(UNiagaraScript* InScript, ENiagaraSimTarget InTarget)
{
	//FORT - 314222 - There is a bug currently when system scripts execute in parallel.
	//This is unlikely for these scripts but we're explicitly disallowing it for safety.
	bAllowParallel = false;

	return FNiagaraScriptExecutionContextBase::Init(InScript, InTarget);
}

bool FNiagaraSystemScriptExecutionContext::Tick(class FNiagaraSystemInstance* Instance, ENiagaraSimTarget SimTarget)
{
	//Bind data interfaces if needed.
	if (Parameters.GetInterfacesDirty())
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraScriptExecContextTick);
		if (Script && Script->IsReadyToRun(ENiagaraSimTarget::CPUSim))//TODO: Remove. Script can only be null for system instances that currently don't have their script exec context set up correctly.
		{
			const FNiagaraVMExecutableData& ScriptExecutableData = Script->GetVMExecutableData();
			const TArray<UNiagaraDataInterface*>& DataInterfaces = GetDataInterfaces();

			const int32 FunctionCount = ScriptExecutableData.CalledVMExternalFunctions.Num();
			FunctionTable.Reset();
			FunctionTable.SetNum(FunctionCount);
			ExtFunctionInfo.AddDefaulted(FunctionCount);

			const FNiagaraScriptExecutionParameterStore* ScriptParameterStore = Script->GetExecutionReadyParameterStore(ENiagaraSimTarget::CPUSim);
			check(ScriptParameterStore != nullptr);
			const auto& ScriptDataInterfaces = ScriptParameterStore->GetDataInterfaces();
			int32 NumPerInstanceFunctions = 0;
			for (int32 FunctionIndex = 0; FunctionIndex < FunctionCount; ++FunctionIndex)
			{
				const FVMExternalFunctionBindingInfo& BindingInfo = ScriptExecutableData.CalledVMExternalFunctions[FunctionIndex];

				FExternalFuncInfo& FuncInfo = ExtFunctionInfo[FunctionIndex];

				// First check to see if we can pull from the fast path library..		
				if (UNiagaraFunctionLibrary::GetVectorVMFastPathExternalFunction(BindingInfo, FuncInfo.Function) && FuncInfo.Function.IsBound())
				{
					continue;
				}

				//TODO: Remove use of userptr table here and just embed the instance data in the function lambda.
				UserPtrTable.SetNumZeroed(ScriptExecutableData.NumUserPtrs, false);

				//Next check DI functions.
				for (int32 i = 0; i < ScriptExecutableData.DataInterfaceInfo.Num(); i++)
				{
					const FNiagaraScriptDataInterfaceCompileInfo& ScriptDIInfo = ScriptExecutableData.DataInterfaceInfo[i];
					UNiagaraDataInterface* ScriptInterface = ScriptDataInterfaces[i];
					UNiagaraDataInterface* ExternalInterface = GetDataInterfaces()[i];

					if (ScriptDIInfo.Name == BindingInfo.OwnerName)
					{
						//Currently we must assume that any User DI is overridden but maybe we can be less conservative with this in future.
						if (ScriptDIInfo.NeedsPerInstanceBinding())
						{
							//This DI needs a binding per instance so we just bind to the external function hook which will call the correct binding for each instance.
							auto PerInstFunctionHookLambda = [ExecContext = this, NumPerInstanceFunctions, UserPtrIndex = ScriptDIInfo.UserPtrIdx](FVectorVMContext& Context)
							{
								ExecContext->PerInstanceFunctionHook(Context, NumPerInstanceFunctions, UserPtrIndex);
							};

							++NumPerInstanceFunctions;
							FuncInfo.Function = FVMExternalFunction::CreateLambda(PerInstFunctionHookLambda);
						}
						else
						{
							// first check to see if we should just use the one from the script
							if (ScriptExecutableData.CalledVMExternalFunctionBindings.IsValidIndex(FunctionIndex)
								&& ScriptInterface
								&& ExternalInterface == ScriptDataInterfaces[i])
							{
								const FVMExternalFunction& ScriptFuncBind = ScriptExecutableData.CalledVMExternalFunctionBindings[FunctionIndex];
								if (ScriptFuncBind.IsBound())
								{
									FuncInfo.Function = ScriptFuncBind;
									check(ScriptDIInfo.UserPtrIdx == INDEX_NONE);
									break;
								}
							}

							//If we don't need a call per instance we can just bind directly to the DI function call;
							check(ExternalInterface);
							ExternalInterface->GetVMExternalFunction(BindingInfo, nullptr, FuncInfo.Function);
						}
						break;
					}
				}

				for (int32 FunctionIt = 0; FunctionIt < FunctionCount; ++FunctionIt)
				{
					FunctionTable[FunctionIt] = &ExtFunctionInfo[FunctionIt].Function;
				}

				if (FuncInfo.Function.IsBound() == false)
				{
					UE_LOG(LogNiagara, Warning, TEXT("Error building data interface function table for system script!"));
					FunctionTable.Empty();
					return false;
				}
			}
		}
	}

	Parameters.Tick();

	return true;
}

bool FNiagaraSystemScriptExecutionContext::GeneratePerInstanceDIFunctionTable(FNiagaraSystemInstance* Inst, TArray<FNiagaraPerInstanceDIFuncInfo>& OutFunctions)
{
	const FNiagaraScriptExecutionParameterStore* ScriptParameterStore = Script->GetExecutionReadyParameterStore(ENiagaraSimTarget::CPUSim);
	const auto& ScriptDataInterfaces = ScriptParameterStore->GetDataInterfaces();
	const FNiagaraVMExecutableData& ScriptExecutableData = Script->GetVMExecutableData();

	for (int32 FunctionIndex = 0; FunctionIndex < ScriptExecutableData.CalledVMExternalFunctions.Num(); ++FunctionIndex)
	{
		const FVMExternalFunctionBindingInfo& BindingInfo = ScriptExecutableData.CalledVMExternalFunctions[FunctionIndex];

		for (int32 i = 0; i < ScriptExecutableData.DataInterfaceInfo.Num(); i++)
		{
			const FNiagaraScriptDataInterfaceCompileInfo& ScriptDIInfo = ScriptExecutableData.DataInterfaceInfo[i];
			//UNiagaraDataInterface* ScriptInterface = ScriptDataInterfaces[i];
			UNiagaraDataInterface* ExternalInterface = GetDataInterfaces()[i];

			if (ScriptDIInfo.Name == BindingInfo.OwnerName && ScriptDIInfo.NeedsPerInstanceBinding())
			{
				UNiagaraDataInterface* DIToBind = nullptr;
				FNiagaraPerInstanceDIFuncInfo& NewFuncInfo = OutFunctions.AddDefaulted_GetRef();
				void* InstData = nullptr;

				if (const int32* DIIndex = Inst->GetInstanceParameters().FindParameterOffset(FNiagaraVariable(ScriptDIInfo.Type, ScriptDIInfo.Name)))
				{
					//If this is a User DI we bind to the user DI and find instance data with it.
					if (UNiagaraDataInterface* UserInterface = Inst->GetInstanceParameters().GetDataInterface(*DIIndex))
					{
						DIToBind = UserInterface;
						InstData = Inst->FindDataInterfaceInstanceData(UserInterface);
					}
				}
				else
				{
					//Otherwise we use the script DI and search for instance data with that.
					DIToBind = ExternalInterface;
					InstData = Inst->FindDataInterfaceInstanceData(ExternalInterface);
				}

				if (DIToBind)
				{
					check(ExternalInterface->PerInstanceDataSize() == 0 || InstData);
					DIToBind->GetVMExternalFunction(BindingInfo, InstData, NewFuncInfo.Function);
					NewFuncInfo.InstData = InstData;
				}

				if (NewFuncInfo.Function.IsBound() == false)
				{
					return false;
				}
				break;
			}
		}
	}
	return true;
};

