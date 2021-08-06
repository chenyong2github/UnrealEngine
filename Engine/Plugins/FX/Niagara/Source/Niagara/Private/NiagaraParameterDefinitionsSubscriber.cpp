// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraParameterDefinitionsSubscriber.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "NiagaraCommon.h"
#include "NiagaraEditorDataBase.h"
#include "NiagaraParameterDefinitionsBase.h"
#include "NiagaraScriptSourceBase.h"


#if WITH_EDITORONLY_DATA

void INiagaraParameterDefinitionsSubscriber::PostLoadDefinitionsSubscriptions()
{
	TArray<FParameterDefinitionsSubscription>& Subscriptions = GetParameterDefinitionsSubscriptions();
	for (FParameterDefinitionsSubscription& Subscription : Subscriptions)
	{
		if (Subscription.DefinitionsId.IsValid() == false && Subscription.ParameterDefinitions_DEPRECATED != nullptr)
		{
			Subscription.DefinitionsId = Subscription.ParameterDefinitions_DEPRECATED->GetDefinitionsUniqueId();
		}
	}

	// When postloading definition subscriptions, we want to synchronize all parameters with all parameter definitions that are matching by name.
	// As such; Set bForceGatherDefinitions so that all NiagaraParameterDefinitions assets are gathered to consider for linking, and;
	// Set bSubscribeAllNameMatchParameters so that name matches are considered for linking parameters to parameter definitions.
	FSynchronizeWithParameterDefinitionsArgs Args;
	Args.bForceGatherDefinitions = true;
	Args.bSubscribeAllNameMatchParameters = true;
	SynchronizeWithParameterDefinitions(Args);
}

TArray<UNiagaraParameterDefinitionsBase*> INiagaraParameterDefinitionsSubscriber::GetSubscribedParameterDefinitions() const
{
	TArray<UNiagaraParameterDefinitionsBase*> Definitions = GetAllParameterDefinitions();
	const TArray<FParameterDefinitionsSubscription>& Subscriptions = GetParameterDefinitionsSubscriptions();
	TArray<UNiagaraParameterDefinitionsBase*> SubscribedDefinitions;

	for (const FParameterDefinitionsSubscription& Subscription : Subscriptions)
	{
		if (UNiagaraParameterDefinitionsBase* const* DefinitionPtr = Definitions.FindByPredicate([&Subscription](const UNiagaraParameterDefinitionsBase* Definition) { return Definition->GetDefinitionsUniqueId() == Subscription.DefinitionsId; }))
		{
			SubscribedDefinitions.Add(*DefinitionPtr);
		}
	}
	return SubscribedDefinitions;
}

bool INiagaraParameterDefinitionsSubscriber::GetIsSubscribedToParameterDefinitions(const UNiagaraParameterDefinitionsBase* Definition) const
{
	const TArray<FParameterDefinitionsSubscription>& Subscriptions = GetParameterDefinitionsSubscriptions();

	for (const FParameterDefinitionsSubscription& Subscription : Subscriptions)
	{
		if (Definition->GetDefinitionsUniqueId() == Subscription.DefinitionsId)
		{
			return true;
		}
	}
	return false;
}

UNiagaraParameterDefinitionsBase* INiagaraParameterDefinitionsSubscriber::FindSubscribedParameterDefinitionsById(const FGuid& DefinitionsId) const
{
	TArray<UNiagaraParameterDefinitionsBase*> SubscribedDefinitions = GetSubscribedParameterDefinitions();

	for (UNiagaraParameterDefinitionsBase* SubscribedDefinition : SubscribedDefinitions)
	{
		if (SubscribedDefinition->GetDefinitionsUniqueId() == DefinitionsId)
		{
			return SubscribedDefinition;
		}
	}
	return nullptr;
}

void INiagaraParameterDefinitionsSubscriber::SubscribeToParameterDefinitions(UNiagaraParameterDefinitionsBase* NewParameterDefinitions, bool bDoNotAssertIfAlreadySubscribed /*= false*/)
{
	TArray<FParameterDefinitionsSubscription>& Subscriptions = GetParameterDefinitionsSubscriptions();
	const FGuid& NewParameterDefinitionsId = NewParameterDefinitions->GetDefinitionsUniqueId();
	for (const FParameterDefinitionsSubscription& Subscription : Subscriptions)
	{
		if (Subscription.DefinitionsId == NewParameterDefinitionsId)
		{
			if (bDoNotAssertIfAlreadySubscribed == false)
			{
				ensureMsgf(false, TEXT("Tried to link to parameter definition that was already linked to!"));
			}
			return;
		}
	}

	FParameterDefinitionsSubscription& NewSubscription = Subscriptions.AddDefaulted_GetRef();
	NewSubscription.DefinitionsId = NewParameterDefinitionsId;
	NewSubscription.CachedChangeIdHash = NewParameterDefinitions->GetChangeIdHash();

	OnSubscribedParameterDefinitionsChangedDelegate.Broadcast();
}

void INiagaraParameterDefinitionsSubscriber::UnsubscribeFromParameterDefinitions(const FGuid& ParameterDefinitionsToRemoveId)
{
	TArray<FParameterDefinitionsSubscription>& Subscriptions = GetParameterDefinitionsSubscriptions();
	for (int32 Idx = Subscriptions.Num() - 1; Idx > -1; --Idx)
	{
		if (Subscriptions[Idx].DefinitionsId == ParameterDefinitionsToRemoveId)
		{
			Subscriptions.RemoveAtSwap(Idx);
			//Synchronize after removing the subscription to remove the subscribed flag from all parameters that were subscribed to the removed definition.
			SynchronizeWithParameterDefinitions();
			OnSubscribedParameterDefinitionsChangedDelegate.Broadcast();
			return;
		}
	}
	ensureMsgf(false, TEXT("Tried to unlink from parameter definition that was not linked to!"));
}

void INiagaraParameterDefinitionsSubscriber::SynchronizeWithParameterDefinitions(const FSynchronizeWithParameterDefinitionsArgs Args /*= FSynchronizeWithParameterDefinitionsArgs()*/)
{
	struct FDefinitionAndChangeIdHash
	{
		UNiagaraParameterDefinitionsBase* Definition;
		int32 ChangeIdHash;
	};

	const TArray<UNiagaraParameterDefinitionsBase*> AllDefinitions = GetAllParameterDefinitions();

	// Cache the definition assets ChangeIdHash for comparison.
	TArray<FDefinitionAndChangeIdHash> AllDefinitionAndChangeIdHashes;
	for (UNiagaraParameterDefinitionsBase* AllDefinitionsItr : AllDefinitions)
	{
		FDefinitionAndChangeIdHash& DefinitionAndChangeIdHash = AllDefinitionAndChangeIdHashes.Emplace_GetRef();
		DefinitionAndChangeIdHash.Definition = AllDefinitionsItr;
		DefinitionAndChangeIdHash.ChangeIdHash = AllDefinitionsItr->GetChangeIdHash();
	}

	// Collect the FGuid ParameterIds for every parameter in every definition asset.
	TSet<FGuid> DefinitionParameterIds;
	for (const UNiagaraParameterDefinitionsBase* AllDefinitionsItr : AllDefinitions)
	{
		DefinitionParameterIds.Append(AllDefinitionsItr->GetParameterIds());
	}

	// Filter out Target Definitions that do not have a subscription associated with their unique id.
	TArray<FParameterDefinitionsSubscription>& Subscriptions = GetParameterDefinitionsSubscriptions();
	TArray<FDefinitionAndChangeIdHash> TargetDefinitionAndChangeIdHashes;
	TArray<UNiagaraParameterDefinitionsBase*> TargetDefinitions;

	for (const FDefinitionAndChangeIdHash& DefinitionAndChangeIdHash : AllDefinitionAndChangeIdHashes)
	{
		if (const FParameterDefinitionsSubscription* Subscription = Subscriptions.FindByPredicate(
			[&DefinitionAndChangeIdHash](const FParameterDefinitionsSubscription& inSubscription){ return inSubscription.DefinitionsId == DefinitionAndChangeIdHash.Definition->GetDefinitionsUniqueId(); }))
		{
			if (Args.bForceGatherDefinitions)
			{
				TargetDefinitionAndChangeIdHashes.Add(DefinitionAndChangeIdHash);
				TargetDefinitions.Add(DefinitionAndChangeIdHash.Definition);
			}
			else if (Subscription->CachedChangeIdHash != DefinitionAndChangeIdHash.ChangeIdHash)
			{
				TargetDefinitionAndChangeIdHashes.Add(DefinitionAndChangeIdHash);
				TargetDefinitions.Add(DefinitionAndChangeIdHash.Definition);
			}
		}
	}

	// Filter out only specific definitions from target definitions if specified.
	if (Args.SpecificDefinitionsUniqueIds.Num() > 0)
	{
		TArray<UNiagaraParameterDefinitionsBase*> TempTargetDefinitions = TargetDefinitions.FilterByPredicate([&Args](const UNiagaraParameterDefinitionsBase* TargetDefinition) { return Args.SpecificDefinitionsUniqueIds.Contains(TargetDefinition->GetDefinitionsUniqueId()); });
		TargetDefinitions = TempTargetDefinitions;
	}

	// Add any additional definitions if specified.
	for (UNiagaraParameterDefinitionsBase* AdditionalParameterDefinitionsItr : Args.AdditionalParameterDefinitions)
	{
		FDefinitionAndChangeIdHash& DefinitionAndChangeIdHash = TargetDefinitionAndChangeIdHashes.Emplace_GetRef();
		DefinitionAndChangeIdHash.Definition = AdditionalParameterDefinitionsItr;
		DefinitionAndChangeIdHash.ChangeIdHash = AdditionalParameterDefinitionsItr->GetChangeIdHash();
	}

	// Synchronize source scripts.
	for (UNiagaraScriptSourceBase* SourceScript : GetAllSourceScripts())
	{
		SourceScript->SynchronizeGraphParametersWithParameterDefinitions(TargetDefinitions, AllDefinitions, DefinitionParameterIds, this, Args);
	}

	// Synchronize editor only script variables.
	TArray<TTuple<FName, FName>> OldToNewNameArr;
	OldToNewNameArr.Append(Args.AdditionalOldToNewNames);
	for (UNiagaraEditorParametersAdapterBase* ParametersAdapter : GetEditorOnlyParametersAdapters())
	{
		OldToNewNameArr.Append(ParametersAdapter->SynchronizeParametersWithParameterDefinitions(TargetDefinitions, AllDefinitions, DefinitionParameterIds, this, Args));
	}

	// Editor only script variable synchronization may also implicate variables set in the stack through underlying source script UNiagaraNodeAssignments and UNiagaraNodeMapGets; synchronize those here.
	for (const TTuple<FName, FName>& OldToNewName : OldToNewNameArr)
	{
		for (UNiagaraScriptSourceBase* SourceScript : GetAllSourceScripts())
		{
			SourceScript->RenameGraphAssignmentAndSetNodePins(OldToNewName.Key, OldToNewName.Value);
		}
	}

	// Only mark the parameter definitions synchronized if every parameter definition was evaluated for synchronization.
	if (Args.SpecificDestScriptVarIds.Num() == 0)
	{
		MarkParameterDefinitionSubscriptionsSynchronized(Args.SpecificDefinitionsUniqueIds);
	}

	// Synchronize owned subscribers with the owning subscribers definitions.
	for (INiagaraParameterDefinitionsSubscriber* OwnedSubscriber : GetOwnedParameterDefinitionsSubscribers())
	{
		FSynchronizeWithParameterDefinitionsArgs SubArgs = Args;
		SubArgs.AdditionalParameterDefinitions = TargetDefinitions;
		SubArgs.AdditionalOldToNewNames = OldToNewNameArr;
		OwnedSubscriber->SynchronizeWithParameterDefinitions(SubArgs);
	}

	OnSubscribedParameterDefinitionsChangedDelegate.Broadcast();
}

TArray<UNiagaraParameterDefinitionsBase*> INiagaraParameterDefinitionsSubscriber::GetAllParameterDefinitions() const
{
	TArray<UNiagaraParameterDefinitionsBase*> OutParameterDefinitions;

	TArray<FAssetData> ParameterDefinitionsAssetData;
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistryModule.GetRegistry().GetAssetsByClass(TEXT("NiagaraParameterDefinitions"), ParameterDefinitionsAssetData);
	for (const FAssetData& ParameterDefinitionsAssetDatum : ParameterDefinitionsAssetData)
	{
		UNiagaraParameterDefinitionsBase* ParameterDefinitions = Cast<UNiagaraParameterDefinitionsBase>(ParameterDefinitionsAssetDatum.GetAsset());
		if (ParameterDefinitions == nullptr)
		{
			ensureMsgf(false, TEXT("Failed to load parameter definition from asset registry!"));
			continue;
		}
		OutParameterDefinitions.Add(ParameterDefinitions);
	}
	return OutParameterDefinitions;
}

void INiagaraParameterDefinitionsSubscriber::MarkParameterDefinitionSubscriptionsSynchronized(TArray<FGuid> SynchronizedParameterDefinitionsIds /*= TArray<FGuid>()*/)
{
	TArray<UNiagaraParameterDefinitionsBase*> Definitions = GetAllParameterDefinitions();
	TArray<FParameterDefinitionsSubscription>& Subscriptions = GetParameterDefinitionsSubscriptions();

	for (FParameterDefinitionsSubscription& Subscription : Subscriptions)
	{
		if (SynchronizedParameterDefinitionsIds.Num() > 0 && SynchronizedParameterDefinitionsIds.Contains(Subscription.DefinitionsId) == false)
		{
			continue;
		}
		else if (UNiagaraParameterDefinitionsBase* const* DefinitionPtr = Definitions.FindByPredicate([&Subscription](const UNiagaraParameterDefinitionsBase* Definition) { return Definition->GetDefinitionsUniqueId() == Subscription.DefinitionsId; }))
		{
			Subscription.CachedChangeIdHash = (*DefinitionPtr)->GetChangeIdHash();
		}
	}
}
#endif
