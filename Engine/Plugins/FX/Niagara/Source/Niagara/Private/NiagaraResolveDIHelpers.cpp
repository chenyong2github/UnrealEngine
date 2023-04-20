// Copyright Epic Games, Inc. All Rights Reserved.

//#if WITH_EDITORONLY_DATA

#include "NiagaraResolveDIHelpers.h"

#include "Internationalization/Internationalization.h"
#include "NiagaraCommon.h"
#include "NiagaraConstants.h"
#include "NiagaraEmitter.h"
#include "NiagaraScript.h"
#include "NiagaraSimulationStageBase.h"
#include "NiagaraSystem.h"

#define LOCTEXT_NAMESPACE "NiagaraResolveDIHelpers"

namespace FNiagaraResolveDIHelpers
{
	void CollectDIBindingsAndAssignmentsForScript(
		const UNiagaraScript* TargetScript,
		const UNiagaraSystem* System,
		const FString& EmitterName,
		TMap<FNiagaraVariableBase, UNiagaraDataInterface*>& OutVariableAssignmentMap,
		TMap<FNiagaraVariableBase, FNiagaraVariableBase>& OutVariableBindingMap,
		TArray<FText>& OutErrorMessages)
	{
		if (TargetScript == nullptr)
		{
			return;
		}

		for (const FNiagaraScriptDataInterfaceInfo& CachedDefaultDataInterface : TargetScript->GetCachedDefaultDataInterfaces())
		{
			for (FName RegisteredParameterMapWrite : CachedDefaultDataInterface.RegisteredParameterMapWrites)
			{
				FNiagaraVariable WriteVariable(CachedDefaultDataInterface.Type, RegisteredParameterMapWrite);
				if (EmitterName.IsEmpty() == false)
				{
					WriteVariable = FNiagaraUtilities::ResolveAliases(WriteVariable, FNiagaraAliasContext().ChangeEmitterNameToEmitter(EmitterName));
				}

				if (CachedDefaultDataInterface.RegisteredParameterMapRead != NAME_None)
				{
					// Handle bindings.
					FNiagaraVariable ReadVariable(CachedDefaultDataInterface.Type, CachedDefaultDataInterface.RegisteredParameterMapRead);
					if (EmitterName.IsEmpty() == false)
					{
						ReadVariable = FNiagaraUtilities::ResolveAliases(ReadVariable, FNiagaraAliasContext().ChangeEmitterToEmitterName(EmitterName));
					}

					if (ReadVariable != WriteVariable)
					{
						FNiagaraVariableBase* CurrentBinding = OutVariableBindingMap.Find(WriteVariable);
						if (CurrentBinding != nullptr)
						{
							if (*CurrentBinding != ReadVariable)
							{
								OutErrorMessages.Add(FText::Format(
									LOCTEXT("MultipleBindingsFormat", "A data interface parameter was the target of multiple bindings in a single system.  The data interface used in the simulation may be incorrect.  Target Parameter: {0} First Read: {1} Current Read: {2}"),
									FText::FromName(WriteVariable.GetName()),
									FText::FromName(CurrentBinding->GetName()),
									FText::FromName(ReadVariable.GetName())));
							}
						}
						else
						{
							{
								OutVariableBindingMap.Add(WriteVariable, ReadVariable);
							}
						}
					}
				}
				else
				{
					// Handle assignments
					UNiagaraDataInterface** CurrentAssignmentPtr = OutVariableAssignmentMap.Find(WriteVariable);
					if (CurrentAssignmentPtr != nullptr)
					{
						if (*CurrentAssignmentPtr != CachedDefaultDataInterface.DataInterface)
						{
							OutErrorMessages.Add(FText::Format(
								LOCTEXT("MultipleAssignmentsFormat", "A data interface parameter was thh target of an assignment multiple times in a single system.  The data interface used in the simulation may be incorrect.  Target Parameter: {0} First Assignment: {1} Current Assignment: {2}"),
								FText::FromName(WriteVariable.GetName()),
								FText::FromString((*CurrentAssignmentPtr)->GetName()),
								FText::FromString((CachedDefaultDataInterface.DataInterface->GetName()))));
						}
					}
					else
					{
						OutVariableAssignmentMap.Add(WriteVariable, CachedDefaultDataInterface.DataInterface);
					}
				}
			}
		}
	}

	void CollectDIBindingsAndAssignments(
		const UNiagaraSystem* System,
		TMap<FGuid, TMap<FNiagaraVariableBase, UNiagaraDataInterface*>>& OutEmitterIdToVariableAssignmentsMap,
		TMap<FGuid, TMap<FNiagaraVariableBase, FNiagaraVariableBase>>& OutEmitterIdToVariableBindingsMap,
		TArray<FText>& OutErrorMessages)
	{
		TMap<FNiagaraVariableBase, UNiagaraDataInterface*> VariableAssignmentMap;
		TMap<FNiagaraVariableBase, FNiagaraVariableBase> VariableBindingMap;

		// Add user parameters as assignments.
		for (const FNiagaraVariableWithOffset& UserParameterWithOffset : System->GetExposedParameters().ReadParameterVariables())
		{
			if (UserParameterWithOffset.IsDataInterface())
			{
				UNiagaraDataInterface* DataInterface = System->GetExposedParameters().GetDataInterface(UserParameterWithOffset.Offset);
				if (DataInterface != nullptr)
				{
					VariableAssignmentMap.Add(UserParameterWithOffset, DataInterface);
				}
			}
		}

		// Collect system and emitter scripts.
		CollectDIBindingsAndAssignmentsForScript(System->GetSystemSpawnScript(), System, FString(), VariableAssignmentMap, VariableBindingMap, OutErrorMessages);
		CollectDIBindingsAndAssignmentsForScript(System->GetSystemUpdateScript(), System, FString(), VariableAssignmentMap, VariableBindingMap, OutErrorMessages);
		OutEmitterIdToVariableAssignmentsMap.Add(FGuid(), VariableAssignmentMap);
		OutEmitterIdToVariableBindingsMap.Add(FGuid(), VariableBindingMap);

		// Collect emitter scripts.
		for (const FNiagaraEmitterHandle& EmitterHandle : System->GetEmitterHandles())
		{
			if (EmitterHandle.GetIsEnabled() == false)
			{
				continue;
			}

			FVersionedNiagaraEmitterData* VersionedNiagaraEmitterData = EmitterHandle.GetEmitterData();
			if (VersionedNiagaraEmitterData == nullptr)
			{
				continue;
			}

			TMap<FNiagaraVariableBase, UNiagaraDataInterface*> EmitterVariableAssignmentMap = VariableAssignmentMap;
			TMap<FNiagaraVariableBase, FNiagaraVariableBase> EmitterVariableBindingMap = VariableBindingMap;

			CollectDIBindingsAndAssignmentsForScript(VersionedNiagaraEmitterData->SpawnScriptProps.Script, System, EmitterHandle.GetUniqueInstanceName(), EmitterVariableAssignmentMap, EmitterVariableBindingMap, OutErrorMessages);
			CollectDIBindingsAndAssignmentsForScript(VersionedNiagaraEmitterData->UpdateScriptProps.Script, System, EmitterHandle.GetUniqueInstanceName(), EmitterVariableAssignmentMap, EmitterVariableBindingMap, OutErrorMessages);

			for (const FNiagaraEventScriptProperties& EventHandler : VersionedNiagaraEmitterData->GetEventHandlers())
			{
				CollectDIBindingsAndAssignmentsForScript(EventHandler.Script, System, EmitterHandle.GetUniqueInstanceName(), EmitterVariableAssignmentMap, EmitterVariableBindingMap, OutErrorMessages);
			}

			for (const UNiagaraSimulationStageBase* SimulationStage : VersionedNiagaraEmitterData->GetSimulationStages())
			{
				if (SimulationStage == nullptr || SimulationStage->bEnabled == false)
				{
					continue;
				}
				CollectDIBindingsAndAssignmentsForScript(SimulationStage->Script, System, EmitterHandle.GetUniqueInstanceName(), EmitterVariableAssignmentMap, EmitterVariableBindingMap, OutErrorMessages);
			}

			OutEmitterIdToVariableAssignmentsMap.Add(EmitterHandle.GetId(), EmitterVariableAssignmentMap);
			OutEmitterIdToVariableBindingsMap.Add(EmitterHandle.GetId(), EmitterVariableBindingMap);
		}
	}

	void ResolveDIsForScript(
		const UNiagaraSystem* System,
		UNiagaraScript* TargetScript,
		const FString& EmitterName,
		TMap<FNiagaraVariableBase, UNiagaraDataInterface*>& VariableAssignmentMap,
		TMap<FNiagaraVariableBase, FNiagaraVariableBase>& VariableBindingMap,
		TArray<FText>& OutErrorMessages)
	{
		TArray<FNiagaraScriptResolvedDataInterfaceInfo> ResolvedDataInterfaces;
		ResolvedDataInterfaces.Reserve(TargetScript->GetCachedDefaultDataInterfaces().Num());
		TArray<FNiagaraResolvedUserDataInterfaceBinding> UserDataInterfaceBindings;
		int32 ResolvedDataInterfaceIndex = 0;
		for (const FNiagaraScriptDataInterfaceInfo& CachedDefaultDataInterface : TargetScript->GetCachedDefaultDataInterfaces())
		{
			FNiagaraScriptResolvedDataInterfaceInfo& ResolvedDataInterface = ResolvedDataInterfaces.AddDefaulted_GetRef();
			ResolvedDataInterface.Name = CachedDefaultDataInterface.Name;
			ResolvedDataInterface.CompileName = CachedDefaultDataInterface.CompileName;
			ResolvedDataInterface.EmitterName = CachedDefaultDataInterface.EmitterName;
			ResolvedDataInterface.UserPtrIdx = CachedDefaultDataInterface.UserPtrIdx;

			if (CachedDefaultDataInterface.RegisteredParameterMapRead != NAME_None)
			{
				// If this DI is read from a parameter try to resolve it through the binding and assignment maps.
				FNiagaraVariable ReadVariable(CachedDefaultDataInterface.Type, CachedDefaultDataInterface.RegisteredParameterMapRead);
				if (EmitterName.IsEmpty() == false)
				{
					ReadVariable = FNiagaraUtilities::ResolveAliases(ReadVariable, FNiagaraAliasContext().ChangeEmitterToEmitterName(EmitterName));
				}

				FNiagaraVariableBase BoundVariable;
				FNiagaraVariableBase* CurrentBoundVariable = &ReadVariable;
				TSet<FNiagaraVariableBase> SeenBoundVariables;
				bool bCircularReferenceDetected = false;
				while (CurrentBoundVariable != nullptr && bCircularReferenceDetected == false)
				{
					SeenBoundVariables.Add(*CurrentBoundVariable);
					BoundVariable = *CurrentBoundVariable;
					CurrentBoundVariable = VariableBindingMap.Find(BoundVariable);

					if (CurrentBoundVariable != nullptr && SeenBoundVariables.Contains(*CurrentBoundVariable))
					{
						bCircularReferenceDetected = true;
						OutErrorMessages.Add(FText::Format(
							LOCTEXT("CircularDependencyWarningFormat", "A data interface parameter circular dependency found.  The data interface used in the simulation may be incorrect.  Target Parameter: {0} Resolved Parameter: {1}"),
							FText::FromName(ReadVariable.GetName()),
							FText::FromName(CurrentBoundVariable->GetName())));
					}
				}

				UNiagaraDataInterface** BoundDataInterfacePtr = VariableAssignmentMap.Find(BoundVariable);
				if (BoundDataInterfacePtr != nullptr)
				{
					ResolvedDataInterface.ResolvedVariable = BoundVariable;
					ResolvedDataInterface.SourceVariable = ReadVariable;
					ResolvedDataInterface.bIsInternal = false;
					ResolvedDataInterface.ResolvedDataInterface = *BoundDataInterfacePtr;
				}

				if (FNiagaraUserRedirectionParameterStore::IsUserParameter(BoundVariable))
				{
					const int32* UserParameterIndex = System->GetExposedParameters().FindParameterOffset(BoundVariable);
					if (UserParameterIndex != nullptr && *UserParameterIndex != INDEX_NONE)
					{
						UserDataInterfaceBindings.Add(FNiagaraResolvedUserDataInterfaceBinding(*UserParameterIndex, ResolvedDataInterfaceIndex));
					}
				}
			}

			if (ResolvedDataInterface.ResolvedDataInterface == nullptr)
			{
				// If the DI was not read from a parameter or couldn't be found, use the one cached during compilation, and give it an internal
				// name to prevent it from being bound incorrectly.
				FNameBuilder NameBuilder;
				NameBuilder.Append(FNiagaraConstants::InternalNamespaceString);
				NameBuilder.AppendChar(TEXT('.'));
				ResolvedDataInterface.Name.AppendString(NameBuilder);

				FNiagaraVariable InternalVariable(CachedDefaultDataInterface.Type, FName(NameBuilder.ToString()));
				ResolvedDataInterface.ResolvedVariable = InternalVariable;
				ResolvedDataInterface.SourceVariable = InternalVariable;
				ResolvedDataInterface.bIsInternal = true;
				ResolvedDataInterface.ResolvedDataInterface = CachedDefaultDataInterface.DataInterface;
			}

			ResolvedDataInterfaceIndex++;
		}
		TargetScript->SetResolvedDataInterfaces(ResolvedDataInterfaces);
		TargetScript->SetResolvedUserDataInterfaceBindings(UserDataInterfaceBindings);
	}

	void SynchronizeMatchingInternalResolvedDataInterfaces(TArrayView<const FNiagaraScriptResolvedDataInterfaceInfo> SourceResolvedDataInterfaces, TArray<FNiagaraScriptResolvedDataInterfaceInfo>& TargetResolvedDataInterfaces)
	{
		for (const FNiagaraScriptResolvedDataInterfaceInfo& SourceResolvedDataInterface : SourceResolvedDataInterfaces)
		{
			if (SourceResolvedDataInterface.bIsInternal)
			{
				for (FNiagaraScriptResolvedDataInterfaceInfo& TargetResolvedDataInterface : TargetResolvedDataInterfaces)
				{
					if (TargetResolvedDataInterface.bIsInternal && TargetResolvedDataInterface.ResolvedVariable == SourceResolvedDataInterface.ResolvedVariable)
					{
						TargetResolvedDataInterface.ResolvedDataInterface = SourceResolvedDataInterface.ResolvedDataInterface;
						break;
					}
				}
			}
		}
	}

	/** Handles the special case where internal data interfaces defined in particle update need to be copied to particle spawn so that they're using the same instance. */
	void ResolveInternalDIsForInterpolatedSpawning(UNiagaraScript* ParticleSpawnScript, UNiagaraScript* ParticleUpdateScript)
	{
		TArray<FNiagaraScriptResolvedDataInterfaceInfo> ResolvedSpawnDataInterfaces;
		ResolvedSpawnDataInterfaces.Append(ParticleSpawnScript->GetResolvedDataInterfaces());

		TArrayView<const FNiagaraScriptResolvedDataInterfaceInfo> ResolvedUpdateDataInterfaces = ParticleUpdateScript->GetResolvedDataInterfaces();
		SynchronizeMatchingInternalResolvedDataInterfaces(ResolvedUpdateDataInterfaces, ResolvedSpawnDataInterfaces);

		ParticleSpawnScript->SetResolvedDataInterfaces(ResolvedSpawnDataInterfaces);
	}

	/** Handles the special case where internal data interfaces defined in particle spawn or particle update need to be copied to the gpu script so that they're using the same instance. */
	void ResolveInternalDIsForGpuScripts(UNiagaraScript* ParticleSpawnScript, UNiagaraScript* ParticleUpdateScript, UNiagaraScript* ParticleGpuScript)
	{
		TArray<FNiagaraScriptResolvedDataInterfaceInfo> ResolvedGpuDataInterfaces;
		ResolvedGpuDataInterfaces.Append(ParticleGpuScript->GetResolvedDataInterfaces());

		TArrayView<const FNiagaraScriptResolvedDataInterfaceInfo> ResolvedSpawnDataInterfaces = ParticleSpawnScript->GetResolvedDataInterfaces();
		SynchronizeMatchingInternalResolvedDataInterfaces(ResolvedSpawnDataInterfaces, ResolvedGpuDataInterfaces);

		TArrayView<const FNiagaraScriptResolvedDataInterfaceInfo> ResolvedUpdateDataInterfaces = ParticleUpdateScript->GetResolvedDataInterfaces();
		SynchronizeMatchingInternalResolvedDataInterfaces(ResolvedUpdateDataInterfaces, ResolvedGpuDataInterfaces);

		ParticleGpuScript->SetResolvedDataInterfaces(ResolvedGpuDataInterfaces);
	}

	void ResolveDIs(
		UNiagaraSystem* System,
		const TMap<FGuid, TMap<FNiagaraVariableBase, UNiagaraDataInterface*>>& EmitterIdToVariableAssignmentsMap,
		const TMap<FGuid, TMap<FNiagaraVariableBase, FNiagaraVariableBase>>& EmitterIdToVariableBindingsMap,
		TArray<FText>& OutErrorMessages)
	{
		TMap<FNiagaraVariableBase, UNiagaraDataInterface*> VariableAssignmentMap = EmitterIdToVariableAssignmentsMap[FGuid()];
		TMap<FNiagaraVariableBase, FNiagaraVariableBase> VariableBindingMap = EmitterIdToVariableBindingsMap[FGuid()];
		ResolveDIsForScript(System, System->GetSystemSpawnScript(), FString(), VariableAssignmentMap, VariableBindingMap, OutErrorMessages);
		ResolveDIsForScript(System, System->GetSystemUpdateScript(), FString(), VariableAssignmentMap, VariableBindingMap, OutErrorMessages);

		for (const FNiagaraEmitterHandle& EmitterHandle : System->GetEmitterHandles())
		{
			if (EmitterHandle.GetIsEnabled() == false)
			{
				continue;
			}

			FVersionedNiagaraEmitterData* VersionedNiagaraEmitterData = EmitterHandle.GetEmitterData();
			if (VersionedNiagaraEmitterData == nullptr)
			{
				continue;
			}

			TMap<FNiagaraVariableBase, UNiagaraDataInterface*> EmitterVariableAssignmentMap = EmitterIdToVariableAssignmentsMap[EmitterHandle.GetId()];
			TMap<FNiagaraVariableBase, FNiagaraVariableBase> EmitterVariableBindingMap = EmitterIdToVariableBindingsMap[EmitterHandle.GetId()];

			ResolveDIsForScript(System, VersionedNiagaraEmitterData->SpawnScriptProps.Script, EmitterHandle.GetUniqueInstanceName(), EmitterVariableAssignmentMap, EmitterVariableBindingMap, OutErrorMessages);
			ResolveDIsForScript(System, VersionedNiagaraEmitterData->UpdateScriptProps.Script, EmitterHandle.GetUniqueInstanceName(), EmitterVariableAssignmentMap, EmitterVariableBindingMap, OutErrorMessages);
			ResolveDIsForScript(System, VersionedNiagaraEmitterData->GetGPUComputeScript(), EmitterHandle.GetUniqueInstanceName(), EmitterVariableAssignmentMap, EmitterVariableBindingMap, OutErrorMessages);

			if (VersionedNiagaraEmitterData->bInterpolatedSpawning)
			{
				ResolveInternalDIsForInterpolatedSpawning(VersionedNiagaraEmitterData->SpawnScriptProps.Script, VersionedNiagaraEmitterData->UpdateScriptProps.Script);
			}

			if (VersionedNiagaraEmitterData->SimTarget == ENiagaraSimTarget::GPUComputeSim)
			{
				ResolveInternalDIsForGpuScripts(VersionedNiagaraEmitterData->SpawnScriptProps.Script, VersionedNiagaraEmitterData->EmitterUpdateScriptProps.Script, VersionedNiagaraEmitterData->GetGPUComputeScript());
			}

			for (const FNiagaraEventScriptProperties& EventHandler : VersionedNiagaraEmitterData->GetEventHandlers())
			{
				ResolveDIsForScript(System, EventHandler.Script, EmitterHandle.GetUniqueInstanceName(), EmitterVariableAssignmentMap, EmitterVariableBindingMap, OutErrorMessages);
			}

			for (const UNiagaraSimulationStageBase* SimulationStage : VersionedNiagaraEmitterData->GetSimulationStages())
			{
				if (SimulationStage == nullptr || SimulationStage->bEnabled == false)
				{
					continue;
				}
				ResolveDIsForScript(System, SimulationStage->Script, EmitterHandle.GetUniqueInstanceName(), EmitterVariableAssignmentMap, EmitterVariableBindingMap, OutErrorMessages);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE

//#endif // WITH_EDITORONLY_DATA