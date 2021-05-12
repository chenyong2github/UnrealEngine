// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraParameterDefinitions.h"

#include "NiagaraEditorModule.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraScriptVariable.h"


UNiagaraParameterDefinitions::UNiagaraParameterDefinitions(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bPromoteToTopInAddMenus = false;
}

UNiagaraParameterDefinitions::~UNiagaraParameterDefinitions()
{
	for (FParameterDefinitionsBindingNameSubscription& Subscription : ExternalParameterDefinitionsSubscriptions)
	{
		Subscription.SubscribedParameterDefinitions->GetOnParameterDefinitionsChanged().RemoveAll(this);
	}
}

void UNiagaraParameterDefinitions::PostLoad()
{
	Super::PostLoad();
	if (HasAnyFlags(RF_ClassDefaultObject | RF_NeedLoad) == false)
	{
		if (UniqueId.IsValid() == false)
		{
			UniqueId = FGuid::NewGuid();
		}

		SynchronizeWithSubscribedParameterDefinitions();
		InitBindings();
	}
}

void UNiagaraParameterDefinitions::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	OnParameterDefinitionsChangedDelegate.Broadcast();
}

void UNiagaraParameterDefinitions::InitBindings()
{
	for (FParameterDefinitionsBindingNameSubscription& Subscription : ExternalParameterDefinitionsSubscriptions)
	{
		Subscription.SubscribedParameterDefinitions->GetOnParameterDefinitionsChanged().AddUObject(this, &UNiagaraParameterDefinitions::SynchronizeWithSubscribedParameterDefinitions);
	}
}

void UNiagaraParameterDefinitions::AddParameter(const FNiagaraVariable& NewVariable)
{
	Modify();
	UNiagaraScriptVariable*& NewScriptVariable = ScriptVariables.Add_GetRef(NewObject<UNiagaraScriptVariable>(this, FName(), RF_Transactional));
	NewScriptVariable->Init(NewVariable, FNiagaraVariableMetaData());
	NewScriptVariable->SetIsStaticSwitch(false);
	NewScriptVariable->SetIsSubscribedToParameterDefinitions(true);
	FNiagaraEditorModule::Get().AddReservedLibraryParameterName(NewVariable.GetName());
	NotifyParameterDefinitionsChanged();
}

void UNiagaraParameterDefinitions::RemoveParameter(const FNiagaraVariable& VariableToRemove)
{
	const int32 Idx = ScriptVariables.IndexOfByPredicate([&VariableToRemove](const UNiagaraScriptVariable* ScriptVariable){ return ScriptVariable->Variable == VariableToRemove; });
	if (Idx != INDEX_NONE)
	{
		Modify();
		const FGuid RemovedScriptVarGuid = ScriptVariables[Idx]->Metadata.GetVariableGuid();
		// Make sure to remove any links to binding name subscriptions to external parameter libraries.
		UnsubscribeBindingNameFromExternalParameterDefinitions(RemovedScriptVarGuid);
		ScriptVariables.RemoveAtSwap(Idx, 1, false);
		FNiagaraEditorModule::Get().RemoveReservedLibraryParameterName(VariableToRemove.GetName());
		NotifyParameterDefinitionsChanged();
	}
}

void UNiagaraParameterDefinitions::RenameParameter(const FNiagaraVariable& VariableToRename, const FName NewName)
{
	if (UNiagaraScriptVariable** ScriptVariablePtr = ScriptVariables.FindByPredicate([&VariableToRename](const UNiagaraScriptVariable* ScriptVariable) { return ScriptVariable->Variable == VariableToRename; }))
	{
		const FName OldName = VariableToRename.GetName();
		Modify();
		UNiagaraScriptVariable* ScriptVariable = *ScriptVariablePtr;
		ScriptVariable->Modify();
		ScriptVariable->Variable.SetName(NewName);
		ScriptVariable->UpdateChangeId();
		FNiagaraEditorModule& EditorModule = FNiagaraEditorModule::Get();
		EditorModule.RemoveReservedLibraryParameterName(OldName);
		EditorModule.AddReservedLibraryParameterName(ScriptVariable->Variable.GetName());
		NotifyParameterDefinitionsChanged();
	}
}

const TArray<UNiagaraScriptVariable*>& UNiagaraParameterDefinitions::GetParametersConst() const
{
	return ScriptVariables;
}

int32 UNiagaraParameterDefinitions::GetChangeIdHash() const
{
	int32 ChangeIdHash = 0;
	for (const UNiagaraScriptVariable* ScriptVar : ScriptVariables)
	{
		ChangeIdHash = HashCombine(ChangeIdHash, GetTypeHash(ScriptVar->GetChangeId()));
	}
	return ChangeIdHash;
}

TArray<FGuid> UNiagaraParameterDefinitions::GetParameterIds() const
{
	TArray<FGuid> OutParameterIds;
	OutParameterIds.AddUninitialized(ScriptVariables.Num());
	for (int32 Idx = 0; Idx < ScriptVariables.Num(); ++Idx)
	{
		OutParameterIds[Idx] = ScriptVariables[Idx]->Metadata.GetVariableGuid();
	}
	return OutParameterIds;
}

UNiagaraScriptVariable* UNiagaraParameterDefinitions::GetScriptVariable(const FNiagaraVariable& Var)
{
	if (UNiagaraScriptVariable* const* ScriptVarPtr = ScriptVariables.FindByPredicate([Var](const UNiagaraScriptVariable* ScriptVar) { return ScriptVar->Variable == Var; }))
	{
		return *ScriptVarPtr;
	}
	return nullptr;
}

UNiagaraScriptVariable* UNiagaraParameterDefinitions::GetScriptVariable(const FGuid& ScriptVarId)
{
	if (UNiagaraScriptVariable* const* ScriptVarPtr = ScriptVariables.FindByPredicate([ScriptVarId](const UNiagaraScriptVariable* ScriptVar) { return ScriptVar->Metadata.GetVariableGuid() == ScriptVarId; }))
	{
		return *ScriptVarPtr;
	}
	return nullptr;
}

void UNiagaraParameterDefinitions::SubscribeBindingNameToExternalParameterDefinitions(UNiagaraParameterDefinitions* NewParameterDefinitions, const FGuid& ExternalScriptVarId, const FGuid& InternalScriptVarId)
{
	FParameterDefinitionsBindingNameSubscription* LibrarySubscription = ExternalParameterDefinitionsSubscriptions.FindByPredicate([NewParameterDefinitions](const FParameterDefinitionsBindingNameSubscription& Subscription) { return Subscription.SubscribedParameterDefinitions->GetUniqueID() == NewParameterDefinitions->GetUniqueID(); });
	if (!LibrarySubscription)
	{
		FParameterDefinitionsBindingNameSubscription& NewLibrarySubscription = ExternalParameterDefinitionsSubscriptions.AddDefaulted_GetRef();
		NewLibrarySubscription.SubscribedParameterDefinitions = NewParameterDefinitions;
		LibrarySubscription = &NewLibrarySubscription;
	}

	FScriptVarBindingNameSubscription* BindingNameSubscription = LibrarySubscription->BindingNameSubscriptions.FindByPredicate([&ExternalScriptVarId](const FScriptVarBindingNameSubscription& Subscription){ return Subscription.ExternalScriptVarId == ExternalScriptVarId; });
	if (!BindingNameSubscription)
	{
		FScriptVarBindingNameSubscription& NewBindingNameSubscription = LibrarySubscription->BindingNameSubscriptions.AddDefaulted_GetRef();
		NewBindingNameSubscription.ExternalScriptVarId = ExternalScriptVarId;
		BindingNameSubscription = &NewBindingNameSubscription;
	}

	if (ensureMsgf(BindingNameSubscription->InternalScriptVarIds.Contains(InternalScriptVarId) == false, TEXT("Tried to add internal script var key that was already subscribed!")))
	{
		BindingNameSubscription->InternalScriptVarIds.Add(InternalScriptVarId);
	}
}

void UNiagaraParameterDefinitions::UnsubscribeBindingNameFromExternalParameterDefinitions(const FGuid& InternalScriptVarToUnsubscribeId)
{
	for (FParameterDefinitionsBindingNameSubscription& LibrarySubscription : ExternalParameterDefinitionsSubscriptions)
	{
		for (FScriptVarBindingNameSubscription& BindingNameSubscription : LibrarySubscription.BindingNameSubscriptions)
		{
			BindingNameSubscription.InternalScriptVarIds.Remove(InternalScriptVarToUnsubscribeId);
		}
	}
}

void UNiagaraParameterDefinitions::SynchronizeWithSubscribedParameterDefinitions()
{
	for (int32 LibraryIdx = ExternalParameterDefinitionsSubscriptions.Num() - 1; LibraryIdx > -1; --LibraryIdx)
	{
		FParameterDefinitionsBindingNameSubscription& LibrarySubscription = ExternalParameterDefinitionsSubscriptions[LibraryIdx];
		if (LibrarySubscription.SubscribedParameterDefinitions == nullptr)
		{
			ExternalParameterDefinitionsSubscriptions.RemoveAt(LibraryIdx);
			continue;
		}

		for (int32 BindingIdx = LibrarySubscription.BindingNameSubscriptions.Num() - 1; BindingIdx > -1; --BindingIdx)
		{
			const FScriptVarBindingNameSubscription& BindingNameSubscription = LibrarySubscription.BindingNameSubscriptions[BindingIdx];
			const FGuid& ExternalScriptVarId = BindingNameSubscription.ExternalScriptVarId;
			if (const UNiagaraScriptVariable* ExternalScriptVar = LibrarySubscription.SubscribedParameterDefinitions->GetScriptVariable(ExternalScriptVarId))
			{
				const FGuid& ExternalScriptVarChangeId = ExternalScriptVar->GetChangeId();
				const FName& ExternalScriptVarName = ExternalScriptVar->Variable.GetName();
				for (const FGuid& InternalScriptVarId : BindingNameSubscription.InternalScriptVarIds)
				{
					if (UNiagaraScriptVariable* const* ScriptVarPtr = ScriptVariables.FindByPredicate([&InternalScriptVarId](const UNiagaraScriptVariable* ScriptVar) { return ScriptVar->Metadata.GetVariableGuid() == InternalScriptVarId; }))
					{
						UNiagaraScriptVariable* ScriptVar = *ScriptVarPtr;
						if (ScriptVar->GetChangeId() != ExternalScriptVarChangeId)
						{
							ScriptVar->DefaultBinding.SetName(ExternalScriptVarName);
							ScriptVar->SetChangeId(ExternalScriptVarChangeId);
							NotifyParameterDefinitionsChanged();
						}
					}
					else
					{
						ensureMsgf(false, TEXT("Failed to find script variable with matching key to subscriptions list! Deleted a parameter without deleting the subscription record!"));
					}
				}
			}
			else
			{
				// Did not find external script var in the external parameter libraries script variables; it has been deleted since last synchronization. Remove this subscription record.
				LibrarySubscription.BindingNameSubscriptions.RemoveAt(BindingIdx);
				if (LibrarySubscription.BindingNameSubscriptions.Num() == 0)
				{
					ExternalParameterDefinitionsSubscriptions.RemoveAt(LibraryIdx);
				}
			}
		}
	}
}

TArray<UNiagaraParameterDefinitions*> UNiagaraParameterDefinitions::GetAvailableParameterDefinitions(bool bSkipSubscribedParameterDefinitions) const
{
	const TArray<UNiagaraParameterDefinitions*> SubscribedParameterDefinitions = bSkipSubscribedParameterDefinitions ? GetSubscribedParameterDefinitions() : TArray<UNiagaraParameterDefinitions*>();
	auto GetParameterDefinitionsIsSubscribed = [&SubscribedParameterDefinitions](const UNiagaraParameterDefinitions* ParameterDefinitions)->bool {
		return SubscribedParameterDefinitions.ContainsByPredicate([&ParameterDefinitions](const UNiagaraParameterDefinitions* SubscribedParameterDefinitions) { return ParameterDefinitions->GetDefinitionsUniqueId() == SubscribedParameterDefinitions->GetDefinitionsUniqueId(); });
	};

	TArray<FAssetData> ParameterDefinitionsAssetData;
	TArray<UNiagaraParameterDefinitions*> AvailableParameterDefinitions;
	TArray<FString> ExternalPackagePaths = { GetPackage()->GetOutermost()->GetPathName() };
	ensureMsgf(FNiagaraEditorUtilities::GetAvailableParameterDefinitions(ExternalPackagePaths, ParameterDefinitionsAssetData), TEXT("Failed to get parameter libraries!"));

	for (const FAssetData& ParameterDefinitionsAssetDatum : ParameterDefinitionsAssetData)
	{
		UNiagaraParameterDefinitions* ParameterDefinitions = Cast<UNiagaraParameterDefinitions>(ParameterDefinitionsAssetDatum.GetAsset());
		if (ParameterDefinitions == nullptr)
		{
			continue;
		}
		else if (bSkipSubscribedParameterDefinitions && GetParameterDefinitionsIsSubscribed(ParameterDefinitions))
		{
			continue;
		}
		AvailableParameterDefinitions.Add(ParameterDefinitions);
	}

	return AvailableParameterDefinitions;
}

const TArray<UNiagaraParameterDefinitions*> UNiagaraParameterDefinitions::GetSubscribedParameterDefinitions() const
{
	TArray<UNiagaraParameterDefinitions*> OutSubscribedParameterDefinitions;
	for (const FParameterDefinitionsBindingNameSubscription& Subscription : ExternalParameterDefinitionsSubscriptions)
	{
		OutSubscribedParameterDefinitions.Add(Subscription.SubscribedParameterDefinitions);
	}
	return OutSubscribedParameterDefinitions;
}

void UNiagaraParameterDefinitions::NotifyParameterDefinitionsChanged()
{
	OnParameterDefinitionsChangedDelegate.Broadcast();
}
