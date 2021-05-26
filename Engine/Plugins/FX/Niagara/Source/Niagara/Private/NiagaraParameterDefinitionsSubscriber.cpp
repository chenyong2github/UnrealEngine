// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraParameterDefinitionsSubscriber.h"

#include "NiagaraCommon.h"
#include "NiagaraEditorDataBase.h"
#include "NiagaraParameterDefinitionsBase.h"
#include "NiagaraScriptSourceBase.h"

#if WITH_EDITORONLY_DATA

void INiagaraParameterDefinitionsSubscriber::InitParameterDefinitionsSubscriptions()
{
	for (UNiagaraParameterDefinitionsBase* ParameterDefinitions : GetSubscribedParameterDefinitions())
	{
		ParameterDefinitions->GetOnParameterDefinitionsChanged().AddRaw(this, &INiagaraParameterDefinitionsSubscriber::SynchronizeWithParameterDefinitions, FSynchronizeWithParameterDefinitionsArgs());
	}
	SynchronizeWithParameterDefinitions();
}

void INiagaraParameterDefinitionsSubscriber::CleanupParameterDefinitionsSubscriptions()
{
	for (UNiagaraParameterDefinitionsBase* ParameterDefinitions : GetSubscribedParameterDefinitions())
	{
		ParameterDefinitions->GetOnParameterDefinitionsChanged().RemoveAll(this);
	}
}

const TArray<UNiagaraParameterDefinitionsBase*> INiagaraParameterDefinitionsSubscriber::GetSubscribedParameterDefinitions()
{
	TArray<FParameterDefinitionsSubscription>& Subscriptions = GetParameterDefinitionsSubscriptions();
	TArray<UNiagaraParameterDefinitionsBase*> OutParameterDefinitions;
	for (int32 Idx = Subscriptions.Num() - 1; Idx > -1; --Idx)
	{
		if (Subscriptions[Idx].ParameterDefinitions == nullptr)
		{
			Subscriptions.RemoveAt(Idx);
		}
		else
		{
			OutParameterDefinitions.Add(Subscriptions[Idx].ParameterDefinitions);
		}
	}
	return OutParameterDefinitions;
}

const TArray<UNiagaraParameterDefinitionsBase*> INiagaraParameterDefinitionsSubscriber::GetSubscribedParameterDefinitionsPendingSynchronization()
{
	TArray<FParameterDefinitionsSubscription>& Subscriptions = GetParameterDefinitionsSubscriptions();
	TArray<UNiagaraParameterDefinitionsBase*> OutParameterDefinitions;
	for (int32 Idx = Subscriptions.Num() - 1; Idx > -1; --Idx)
	{
		if (Subscriptions[Idx].ParameterDefinitions == nullptr)
		{
			Subscriptions.RemoveAt(Idx);
		}
		else if (Subscriptions[Idx].ParameterDefinitions->GetChangeIdHash() != Subscriptions[Idx].CachedChangeIdHash)
		{
			OutParameterDefinitions.Add(Subscriptions[Idx].ParameterDefinitions);
		}
	}
	return OutParameterDefinitions;
}

void INiagaraParameterDefinitionsSubscriber::SubscribeToParameterDefinitions(UNiagaraParameterDefinitionsBase* NewParameterDefinitions, bool bDoNotAssertIfAlreadySubscribed /*= false*/)
{
	TArray<FParameterDefinitionsSubscription>& Subscriptions = GetParameterDefinitionsSubscriptions();
	const FGuid& NewParameterDefinitionsId = NewParameterDefinitions->GetDefinitionsUniqueId();
	for (const FParameterDefinitionsSubscription& Subscription : Subscriptions)
	{
		if (Subscription.ParameterDefinitions->GetDefinitionsUniqueId() == NewParameterDefinitionsId)
		{
			if(bDoNotAssertIfAlreadySubscribed == false)
			{ 
				ensureMsgf(false, TEXT("Tried to subscribe to parameter definitions that was already subscribed to!"));
			}
			return;
		}
	}

	FParameterDefinitionsSubscription& NewSubscription = Subscriptions.AddDefaulted_GetRef();
	NewSubscription.ParameterDefinitions = NewParameterDefinitions;
	NewSubscription.CachedChangeIdHash = NewParameterDefinitions->GetChangeIdHash();

	OnSubscribedParameterDefinitionsChangedDelegate.Broadcast();
}

void INiagaraParameterDefinitionsSubscriber::UnsubscribeFromParameterDefinitions(const FGuid& ParameterDefinitionsToRemoveId)
{
	TArray<FParameterDefinitionsSubscription>& Subscriptions = GetParameterDefinitionsSubscriptions();
	for (int32 Idx = Subscriptions.Num() - 1; Idx > -1; --Idx)
	{
		if (Subscriptions[Idx].ParameterDefinitions->GetDefinitionsUniqueId() == ParameterDefinitionsToRemoveId)
		{
			Subscriptions.RemoveAtSwap(Idx);
			//Synchronize after removing the subscription to remove the subscribed flag from all parameters that were subscribed to the removed definition.
			SynchronizeWithParameterDefinitions();
			OnSubscribedParameterDefinitionsChangedDelegate.Broadcast();
			return;
		}
	}
	ensureMsgf(false, TEXT("Tried to unsubscribe from parameter definitions that was not subscribed to!"));
}

void INiagaraParameterDefinitionsSubscriber::SynchronizeWithParameterDefinitions(const FSynchronizeWithParameterDefinitionsArgs Args /*= FSynchronizeWithParameterDefinitionsArgs()*/)
{
	TArray<UNiagaraParameterDefinitionsBase*> ParameterDefinitions = Args.bForceSynchronizeDefinitions ? GetSubscribedParameterDefinitions() : GetSubscribedParameterDefinitionsPendingSynchronization();
	for (UNiagaraParameterDefinitionsBase* AdditionalParameterDefinitionsItr : Args.AdditionalParameterDefinitions)
	{
		ParameterDefinitions.AddUnique(AdditionalParameterDefinitionsItr);
	}

	const TArray<FGuid> ParameterDefinitionsParameterIds = GetSubscribedParameterDefinitionsParameterIds();
	for (UNiagaraScriptSourceBase* SourceScript : GetAllSourceScripts())
	{
		SourceScript->SynchronizeGraphParametersWithParameterDefinitions(ParameterDefinitions, ParameterDefinitionsParameterIds, Args);
	}

	TArray<TTuple<FName, FName>> OldToNewNameArr;
	for (UNiagaraEditorParametersAdapterBase* ParametersAdapter : GetEditorOnlyParametersAdapters())
	{
		OldToNewNameArr.Append(ParametersAdapter->SynchronizeParametersWithParameterDefinitions(ParameterDefinitions, ParameterDefinitionsParameterIds, Args));
	}

	// Editor only script var synchronization may also implicate variables set in the stack through underlying source script UNiagaraNodeAssignments and UNiagaraNodeMapGets; synchronize those here.
	for (const TTuple<FName, FName>& OldToNewName : OldToNewNameArr)
	{
		for (UNiagaraScriptSourceBase* SourceScript : GetAllSourceScripts())
		{
			SourceScript->RenameGraphAssignmentAndSetNodePins(OldToNewName.Key, OldToNewName.Value);
		}
	}

	// Only mark the parameter definitions synchronized if every definition was evaluated for synchronization.
	if (Args.SpecificDestScriptVarIds.Num() == 0)
	{
		MarkSubscribedParameterDefinitionsSynchronized(Args.SpecificDefinitionsUniqueIds);
	}

	for (INiagaraParameterDefinitionsSubscriber* OwnedSubscriber : GetOwnedParameterDefinitionsSubscribers())
	{
		// Synchronize owned subscribers with the owning subscribers definitions.
		FSynchronizeWithParameterDefinitionsArgs SubArgs = Args;
		SubArgs.AdditionalParameterDefinitions = ParameterDefinitions;
		OwnedSubscriber->SynchronizeWithParameterDefinitions(SubArgs);
	}

	OnSubscribedParameterDefinitionsChangedDelegate.Broadcast();
}

UNiagaraParameterDefinitionsBase* INiagaraParameterDefinitionsSubscriber::FindSubscribedParameterDefinitionsById(const FGuid& DefinitionsId)
{	
	const TArray<UNiagaraParameterDefinitionsBase*> SubscribedParameterDefinitions = GetSubscribedParameterDefinitions();
	UNiagaraParameterDefinitionsBase* const* FoundDefinitionsPtr = SubscribedParameterDefinitions.FindByPredicate([&DefinitionsId](const UNiagaraParameterDefinitionsBase* Definitions){ return Definitions->GetDefinitionsUniqueId() == DefinitionsId; });
	if (FoundDefinitionsPtr != nullptr)
	{
		return *FoundDefinitionsPtr;
	}
	return nullptr;
}

TArray<FGuid> INiagaraParameterDefinitionsSubscriber::GetSubscribedParameterDefinitionsParameterIds()
{
	TArray<FGuid> Ids;
	for (const UNiagaraParameterDefinitionsBase* Definitions : GetSubscribedParameterDefinitions())
	{
		Ids.Append(Definitions->GetParameterIds());
	}
	return Ids;
}

void INiagaraParameterDefinitionsSubscriber::MarkSubscribedParameterDefinitionsSynchronized(TArray<FGuid> SynchronizedSubscribedParameterDefinitionsIds /*= TArray<FGuid>()*/)
{
	TArray<FParameterDefinitionsSubscription>& Subscriptions = GetParameterDefinitionsSubscriptions();
	for (FParameterDefinitionsSubscription& Subscription : Subscriptions)
	{
		if (SynchronizedSubscribedParameterDefinitionsIds.Num() > 0 && SynchronizedSubscribedParameterDefinitionsIds.Contains(Subscription.ParameterDefinitions->GetDefinitionsUniqueId()) == false)
		{
			continue;
		}
		Subscription.CachedChangeIdHash = Subscription.ParameterDefinitions->GetChangeIdHash();
	}
}
#endif
