// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceUtilities.h"
#include "NiagaraComponent.h"
#include "NiagaraComputeExecutionContext.h"
#include "NiagaraDataInterface.h"
#include "NiagaraSystem.h"

void FNiagaraDataInterfaceUtilities::ForEachVMFunctionEquals(class UNiagaraDataInterface* DataInterface, class UNiagaraSystem* NiagaraSystem, class UNiagaraComponent* Component, TFunction<bool(const FVMExternalFunctionBindingInfo&)> Action)
{
	if ( DataInterface == nullptr || NiagaraSystem == nullptr )
	{
		return;
	}

	// Find all override parameter names
	TArray<FName, TInlineAllocator<8>> OverrideParameterNames;
	if ( Component )
	{
		for (const UNiagaraDataInterface* OverrideDI : Component->GetOverrideParameters().GetDataInterfaces())
		{
			if ( (OverrideDI != nullptr) && ((OverrideDI == DataInterface) || OverrideDI->Equals(DataInterface)) )
			{
				if ( const FNiagaraVariableBase* Variable = Component->GetOverrideParameters().FindVariable(OverrideDI) )
				{
					OverrideParameterNames.AddUnique(Variable->GetName());
				}
			}
		}
	}
	else
	{
		for (const UNiagaraDataInterface* OverrideDI : NiagaraSystem->GetExposedParameters().GetDataInterfaces())
		{
			if ( (OverrideDI != nullptr) && ((OverrideDI == DataInterface) || OverrideDI->Equals(DataInterface)) )
			{
				if ( const FNiagaraVariableBase* Variable = NiagaraSystem->GetExposedParameters().FindVariable(OverrideDI) )
				{
					OverrideParameterNames.AddUnique(Variable->GetName());
				}
			}
		}
	}

	// Loop through all scripts
	bool bContinueSearching = true;
	NiagaraSystem->ForEachScript(
		[&](UNiagaraScript* Script)
		{
			if (bContinueSearching == false)
			{
				return;
			}

			const FNiagaraVMExecutableData& ScriptExecutableData = Script->GetVMExecutableData();
			if (!ScriptExecutableData.IsValid())
			{
				return;
			}

			const TArray<FNiagaraScriptDataInterfaceInfo>& CachedDefaultDIs = Script->GetCachedDefaultDataInterfaces();
			const TArray<FNiagaraScriptDataInterfaceCompileInfo>& DataInterfaceInfos = ScriptExecutableData.DataInterfaceInfo;
			for ( const FVMExternalFunctionBindingInfo& FunctionBinding : ScriptExecutableData.CalledVMExternalFunctions )
			{
				const int NumDataInterface = FMath::Min(DataInterfaceInfos.Num(), CachedDefaultDIs.Num());	// Note: Should always be equal but lets be safe
				for ( int iDataInterface=0; iDataInterface < NumDataInterface; ++iDataInterface )
				{
					const FNiagaraScriptDataInterfaceCompileInfo& DataInterfaceInfo = DataInterfaceInfos[iDataInterface];
					if ( DataInterfaceInfo.Name != FunctionBinding.OwnerName )
					{
						continue;
					}

					const FNiagaraScriptDataInterfaceInfo& CachedDefaultDI = CachedDefaultDIs[iDataInterface];
					if (CachedDefaultDI.DataInterface == nullptr || !DataInterfaceInfo.MatchesClass(DataInterface->GetClass()))
					{
						// Would be odd not to match here, but we are being safe
						break;
					}

					// Do we have a match?
					if (CachedDefaultDI.DataInterface->Equals(DataInterface) || OverrideParameterNames.Contains(CachedDefaultDI.Name))
					{
						if ( Action(FunctionBinding) == false )
						{
							bContinueSearching = false;
							return;
						}
					}
					break;
				}
			}
		}
	);
}

void FNiagaraDataInterfaceUtilities::ForEachGpuFunctionEquals(class UNiagaraDataInterface* DataInterface, class UNiagaraSystem* NiagaraSystem, class UNiagaraComponent* Component, TFunction<bool(const FNiagaraDataInterfaceGeneratedFunction&)> Action)
{
	if ( DataInterface == nullptr || NiagaraSystem == nullptr )
	{
		return;
	}

	// Find all override parameter names
	TArray<FName, TInlineAllocator<8>> OverrideParameterNames;
	if ( Component )
	{
		for (const UNiagaraDataInterface* OverrideDI : Component->GetOverrideParameters().GetDataInterfaces())
		{
			if ( (OverrideDI != nullptr) && ((OverrideDI == DataInterface) || OverrideDI->Equals(DataInterface)) )
			{
				if ( const FNiagaraVariableBase* Variable = Component->GetOverrideParameters().FindVariable(OverrideDI) )
				{
					OverrideParameterNames.AddUnique(Variable->GetName());
				}
			}
		}
	}
	else
	{
		for (const UNiagaraDataInterface* OverrideDI : NiagaraSystem->GetExposedParameters().GetDataInterfaces())
		{
			if ( (OverrideDI != nullptr) && ((OverrideDI == DataInterface) || OverrideDI->Equals(DataInterface)) )
			{
				if ( const FNiagaraVariableBase* Variable = NiagaraSystem->GetExposedParameters().FindVariable(OverrideDI) )
				{
					OverrideParameterNames.AddUnique(Variable->GetName());
				}
			}
		}
	}

	// Loop through all scripts
	bool bContinueSearching = true;
	NiagaraSystem->ForEachScript(
		[&](UNiagaraScript* Script)
		{
			if (bContinueSearching == false)
			{
				return;
			}

			if (FNiagaraShaderScript* ShaderScript = Script->GetRenderThreadScript())
			{
				const TArray<FNiagaraScriptDataInterfaceInfo>& CachedDefaultDIs = Script->GetCachedDefaultDataInterfaces();
				const TArray<FNiagaraDataInterfaceGPUParamInfo>& DataInterfaceParamInfos = ShaderScript->GetDataInterfaceParamInfo();

				const int NumDataInterfaces = FMath::Min(CachedDefaultDIs.Num(), DataInterfaceParamInfos.Num());	// Note: Should always be equal but lets be safe
				for (int iDataInterface = 0; iDataInterface < NumDataInterfaces; ++iDataInterface)
				{
					const FNiagaraScriptDataInterfaceInfo& CachedDefaultDI = CachedDefaultDIs[iDataInterface];
					if (CachedDefaultDI.DataInterface == nullptr)
					{
						continue;
					}
					if (CachedDefaultDI.DataInterface->Equals(DataInterface) || OverrideParameterNames.Contains(CachedDefaultDI.Name))
					{
						for (const FNiagaraDataInterfaceGeneratedFunction& GeneratedFunction : DataInterfaceParamInfos[iDataInterface].GeneratedFunctions)
						{
							if (Action(GeneratedFunction) == false)
							{
								bContinueSearching = false;
								return;
							}
						}
					}
				}
			}
		}
	);
}

void FNiagaraDataInterfaceUtilities::ForEachVMFunction(class UNiagaraDataInterface* DataInterface, class FNiagaraSystemInstance* SystemInstance, TFunction<bool(const FVMExternalFunctionBindingInfo&)> Action)
{
	if ( !DataInterface || !SystemInstance )
	{
		return;
	}

	auto SearchVMContext =
		[](const FNiagaraScriptExecutionContextBase* ExecContext, UNiagaraDataInterface* DataInterface, const TFunction<bool(const FVMExternalFunctionBindingInfo&)>& Action) -> bool
		{
			if (ExecContext == nullptr || ExecContext->Script == nullptr)
			{
				return true;
			}

			const FNiagaraVMExecutableData& ScriptExecutableData = ExecContext->Script->GetVMExecutableData();
			if (!ScriptExecutableData.IsValid())
			{
				return true;
			}

			const TArray<UNiagaraDataInterface*>& DataInterfaces = ExecContext->GetDataInterfaces();
			const int32 NumDataInterfaces = FMath::Min(ScriptExecutableData.DataInterfaceInfo.Num(), DataInterfaces.Num());		// Should be equal, but be safe
			for (const FVMExternalFunctionBindingInfo& FunctionBinding : ScriptExecutableData.CalledVMExternalFunctions)
			{
				for (int32 iDataInterface = 0; iDataInterface < NumDataInterfaces; ++iDataInterface)
				{
					const FNiagaraScriptDataInterfaceCompileInfo& DataInterfaceInfo = ScriptExecutableData.DataInterfaceInfo[iDataInterface];
					if (FunctionBinding.OwnerName != DataInterfaceInfo.Name)
					{
						continue;
					}
					if (DataInterfaces[iDataInterface] != DataInterface)
					{
						continue;
					}

					if (Action(FunctionBinding) == false)
					{
						return false;
					}
				}
			}
			return true;
		};


	// Search system scripts (always VM)
	TSharedPtr<FNiagaraSystemSimulation, ESPMode::ThreadSafe> SystemSimulation = SystemInstance->GetSystemSimulation();
	if ( SystemSimulation.IsValid() )
	{
		if (SearchVMContext(SystemSimulation->GetSpawnExecutionContext(), DataInterface, Action) == false)
		{
			return;
		}
		if (SearchVMContext(SystemSimulation->GetUpdateExecutionContext(), DataInterface, Action) == false)
		{
			return;
		}
	}

	// Search emitter scripts
	for (const TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>& EmitterInstance : SystemInstance->GetEmitters())
	{
		if (EmitterInstance->IsDisabled() || EmitterInstance->GetCachedEmitter() == nullptr || EmitterInstance->GetGPUContext() != nullptr)
		{
			continue;
		}

		if ( SearchVMContext(&EmitterInstance->GetSpawnExecutionContext(), DataInterface, Action) == false )
		{
			return;
		}

		if (SearchVMContext(&EmitterInstance->GetUpdateExecutionContext(), DataInterface, Action) == false)
		{
			return;
		}

		for ( const FNiagaraScriptExecutionContext& EventExecContext : EmitterInstance->GetEventExecutionContexts() )
		{
			if (SearchVMContext(&EventExecContext, DataInterface, Action) == false)
			{
				return;
			}
		}
	}
}

void FNiagaraDataInterfaceUtilities::ForEachGpuFunction(class UNiagaraDataInterface* DataInterface, class FNiagaraSystemInstance* SystemInstance, TFunction<bool(const FNiagaraDataInterfaceGeneratedFunction&)> Action)
{
	if ( !DataInterface || !SystemInstance )
	{
		return;
	}

	for (const TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>& EmitterInstance : SystemInstance->GetEmitters())
	{
		if (EmitterInstance->IsDisabled() || EmitterInstance->GetCachedEmitter() == nullptr || EmitterInstance->GetGPUContext() == nullptr)
		{
			continue;
		}

		const FNiagaraScriptInstanceParameterStore& ParameterStore = EmitterInstance->GetGPUContext()->CombinedParamStore;
		const TArray<UNiagaraDataInterface*>& DataInterfaces = ParameterStore.GetDataInterfaces();
		const TArray<FNiagaraDataInterfaceGPUParamInfo>& DataInterfaceParamInfo = EmitterInstance->GetGPUContext()->GPUScript_RT->GetDataInterfaceParamInfo();
		const int32 NumDataInterface = FMath::Min(DataInterfaces.Num(), DataInterfaceParamInfo.Num());		// Should be equal, but be safe
		for ( int32 iDataInterface=0; iDataInterface < NumDataInterface; ++iDataInterface)
		{
			if ( DataInterfaces[iDataInterface] != DataInterface )
			{
				continue;
			}

			for (const FNiagaraDataInterfaceGeneratedFunction& GeneratedFunction : DataInterfaceParamInfo[iDataInterface].GeneratedFunctions)
			{
				if (Action(GeneratedFunction) == false)
				{
					return;
				}
			}
		}
	}
}
