// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/NiagaraParameterDefinitionsSubscriberViewModel.h"

#include "Misc/Paths.h"
#include "NiagaraCommon.h"
#include "NiagaraEditorData.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraParameterDefinitions.h"
#include "NiagaraScriptSource.h"
#include "NiagaraScriptSourceBase.h"
#include "NiagaraScriptVariable.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"


void INiagaraParameterDefinitionsSubscriberViewModel::SubscribeToParameterDefinitions(UNiagaraParameterDefinitions* NewParameterDefinitions)
{
	GetParameterDefinitionsSubscriber()->SubscribeToParameterDefinitions(NewParameterDefinitions);
	NewParameterDefinitions->GetOnParameterDefinitionsChanged().AddRaw(this, &INiagaraParameterDefinitionsSubscriberViewModel::SynchronizeWithParameterDefinitions, FSynchronizeWithParameterDefinitionsArgs());
}

void INiagaraParameterDefinitionsSubscriberViewModel::UnsubscribeFromParameterDefinitions(const FGuid& ParameterDefinitionsToRemoveUniqueId)
{
	UNiagaraParameterDefinitions* DefinitionsToRemove = FindSubscribedParameterDefinitionsById(ParameterDefinitionsToRemoveUniqueId);
	if (DefinitionsToRemove != nullptr)
	{
		DefinitionsToRemove->GetOnParameterDefinitionsChanged().RemoveAll(this);
	}

	// Call UnsubscribeFromParameterDefinitions() even if they could not be found to use assert logic there.
	GetParameterDefinitionsSubscriber()->UnsubscribeFromParameterDefinitions(ParameterDefinitionsToRemoveUniqueId);
}

void INiagaraParameterDefinitionsSubscriberViewModel::SynchronizeWithParameterDefinitions(FSynchronizeWithParameterDefinitionsArgs Args /*= FSynchronizeWithParameterDefinitionsArgs()*/)
{
	GetParameterDefinitionsSubscriber()->SynchronizeWithParameterDefinitions(Args);
}

void INiagaraParameterDefinitionsSubscriberViewModel::SynchronizeScriptVarWithParameterDefinitions(UNiagaraScriptVariable* ScriptVarToSynchronize, bool bForce)
{
	FSynchronizeWithParameterDefinitionsArgs Args = FSynchronizeWithParameterDefinitionsArgs();
	Args.SpecificDestScriptVarIds.Add(ScriptVarToSynchronize->Metadata.GetVariableGuid());
	Args.bForceSynchronizeDefinitions = bForce;
	SynchronizeWithParameterDefinitions(Args); //@todo(ng) error if we failed to find the var
}

void INiagaraParameterDefinitionsSubscriberViewModel::SubscribeAllParametersToDefinitions(const FGuid& DefinitionsUniqueId)
{
	const UNiagaraParameterDefinitions* LibraryToSynchronize = FindSubscribedParameterDefinitionsById(DefinitionsUniqueId);
	if (LibraryToSynchronize == nullptr)
	{
		ensureMsgf(false, TEXT("Tried to synchronize all name matching parameters to library but failed to find subscribed library by Id!"));
		return;
	}

	const TArray<UNiagaraScriptVariable*>& LibraryScriptVars = LibraryToSynchronize->GetParametersConst();

	// Iterate each UNiagaraScriptVariable and find Library script vars with name matches to synchronize.
	bool bNeedSynchronize = false;
	for (UNiagaraScriptVariable* DestScriptVar : GetAllScriptVars())
	{
		// If the parameter is already subscribed, continue.
		if (DestScriptVar->GetIsSubscribedToParameterDefinitions())
		{
			continue;
		}

		const FName& DestScriptVarName = DestScriptVar->Variable.GetName();
		if (UNiagaraScriptVariable* const* LibraryScriptVarPtr = LibraryScriptVars.FindByPredicate([&DestScriptVarName](const UNiagaraScriptVariable* LibraryScriptVar) { return LibraryScriptVar->Variable.GetName() == DestScriptVarName; }))
		{
			// We found a library (source) script var with a name match. Mark the destination script var as synchronizing with a parameter definitions and overwrite the metadata by hand so that the unique ID guid matches with its source.
			UNiagaraScriptVariable* LibraryScriptVar = *LibraryScriptVarPtr;
			DestScriptVar->Modify();
			DestScriptVar->SetIsSubscribedToParameterDefinitions(true);
			DestScriptVar->Metadata.SetVariableGuid(LibraryScriptVar->Metadata.GetVariableGuid());
			bNeedSynchronize = true;
		}
	}

	if (bNeedSynchronize)
	{
		SynchronizeWithParameterDefinitions();
	}
}

void INiagaraParameterDefinitionsSubscriberViewModel::SetParameterIsSubscribedToDefinitions(const FGuid& ScriptVarId, bool bIsSynchronizing)
{
	// Iterate each UNiagaraScriptVariable and find Library script vars with id matches to synchronize.
	TArray<UNiagaraScriptVariable*> ScriptVars = GetAllScriptVars();

	UNiagaraScriptVariable* const* ScriptVarPtr = ScriptVars.FindByPredicate([&ScriptVarId](const UNiagaraScriptVariable* ScriptVar) { return ScriptVar->Metadata.GetVariableGuid() == ScriptVarId; });
	if (ScriptVarPtr == nullptr)
	{
		ensureMsgf(false, TEXT("Tried to set parameter synchronizing state with subscribed parameter libraries but failed to find parameter by Id!"));
		return;
	}
	UNiagaraScriptVariable* ScriptVar = *ScriptVarPtr;

	auto TrySynchronizeScriptVarToLibraries = [this](UNiagaraScriptVariable* ScriptVar, TArray<UNiagaraParameterDefinitions*> ParameterDefinitions)->UNiagaraParameterDefinitions* /*FoundLibrary*/ {
		for (UNiagaraParameterDefinitions* ParameterDefinitionsItr : ParameterDefinitions)
		{
			for (const UNiagaraScriptVariable* LibraryScriptVar : ParameterDefinitionsItr->GetParametersConst())
			{
				if (LibraryScriptVar->Variable == ScriptVar->Variable)
				{	
					ScriptVar->Modify();
					ScriptVar->SetIsSubscribedToParameterDefinitions(true);
					ScriptVar->SetIsOverridingParameterDefinitionsDefaultValue(false);
					ScriptVar->Metadata.SetVariableGuid(LibraryScriptVar->Metadata.GetVariableGuid());
					FNiagaraStackGraphUtilities::SynchronizeVariableToLibraryAndApplyToGraph(ScriptVar);
					return ParameterDefinitionsItr;
				}
			}
		}
		return nullptr;
	};

	if (bIsSynchronizing == false)
	{
		ScriptVar->Modify();
		ScriptVar->SetIsSubscribedToParameterDefinitions(false);
	}
	else /*bIsSynchronizing == true*/
	{
		const bool bSkipSubscribed = true;
		if (const UNiagaraParameterDefinitions* FoundSubscribedLibrary = TrySynchronizeScriptVarToLibraries(ScriptVar, GetSubscribedParameterDefinitions()))
		{
			return;
		}
		else if (UNiagaraParameterDefinitions* FoundAvailableLibrary = TrySynchronizeScriptVarToLibraries(ScriptVar, GetAvailableParameterDefinitions(bSkipSubscribed)))
		{
			SubscribeToParameterDefinitions(FoundAvailableLibrary);
			return;
		}

		ensureMsgf(false, TEXT("Tried to set parameter to synchronize with parameter definitions but library parameter could not be found! Library name cache out of date!"));
	}
}

void INiagaraParameterDefinitionsSubscriberViewModel::SetParameterIsOverridingLibraryDefaultValue(const FGuid& ScriptVarId, bool bIsOverriding)
{
	// Iterate each UNiagaraScriptVariable and find Library script vars with id matches to synchronize.
	TArray<UNiagaraScriptVariable*> ScriptVars = GetAllScriptVars();

	UNiagaraScriptVariable* const* ScriptVarPtr = ScriptVars.FindByPredicate([&ScriptVarId](const UNiagaraScriptVariable* ScriptVar) { return ScriptVar->Metadata.GetVariableGuid() == ScriptVarId; });
	if (ScriptVarPtr == nullptr)
	{
		ensureMsgf(false, TEXT("Tried to set parameter synchronizing state with subscribed parameter libraries but failed to find parameter by Id!"));
		return;
	}
	UNiagaraScriptVariable* ScriptVar = *ScriptVarPtr;

	if (ScriptVar->GetIsSubscribedToParameterDefinitions() == false)
	{
		ensureMsgf(false, TEXT("Tried to set scriptvar to override library default value but it was not subscribed to a library!"));
		return;
	}
	
	ScriptVar->SetIsOverridingParameterDefinitionsDefaultValue(bIsOverriding);
	if (bIsOverriding == false)
	{
		FNiagaraStackGraphUtilities::SynchronizeVariableToLibraryAndApplyToGraph(ScriptVar);
	}
}

TArray<UNiagaraParameterDefinitions*> INiagaraParameterDefinitionsSubscriberViewModel::GetSubscribedParameterDefinitions()
{
	return FNiagaraEditorUtilities::DowncastParameterDefinitionsBaseArray(GetParameterDefinitionsSubscriber()->GetSubscribedParameterDefinitions());
}

TArray<UNiagaraParameterDefinitions*> INiagaraParameterDefinitionsSubscriberViewModel::GetAvailableParameterDefinitions(bool bSkipSubscribedParameterDefinitions)
{
	const TArray<UNiagaraParameterDefinitions*> SubscribedParameterDefinitions = bSkipSubscribedParameterDefinitions ? GetSubscribedParameterDefinitions() : TArray<UNiagaraParameterDefinitions*>();
	auto GetParameterDefinitionsIsSubscribed = [&SubscribedParameterDefinitions](const UNiagaraParameterDefinitions* ParameterDefinitions)->bool {
		return SubscribedParameterDefinitions.ContainsByPredicate([&ParameterDefinitions](const UNiagaraParameterDefinitions* SubscribedParameterDefinitions) { return ParameterDefinitions->GetDefinitionsUniqueId() == SubscribedParameterDefinitions->GetDefinitionsUniqueId(); });
	};

	TArray<FAssetData> ParameterDefinitionsAssetData;
	TArray<UNiagaraParameterDefinitions*> AvailableParameterDefinitions;
	TArray<FString> ExternalPackagePaths = {  GetSourceObjectPackagePathName() };
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

TArray<UNiagaraScriptVariable*> INiagaraParameterDefinitionsSubscriberViewModel::GetAllScriptVars()
{
	TArray<UNiagaraScriptVariable*> OutScriptVars;
	for (UNiagaraScriptSourceBase* SourceScript : GetParameterDefinitionsSubscriber()->GetAllSourceScripts())
	{
		TArray<UNiagaraScriptVariable*> TempScriptVars;
		CastChecked<UNiagaraScriptSource>(SourceScript)->NodeGraph->GetAllMetaData().GenerateValueArray(TempScriptVars);
		OutScriptVars.Append(TempScriptVars);
	}
	for (UNiagaraEditorParametersAdapterBase* EditorOnlyParametersAdapter : GetParameterDefinitionsSubscriber()->GetEditorOnlyParametersAdapters())
	{
		OutScriptVars.Append(CastChecked<UNiagaraEditorParametersAdapter>(EditorOnlyParametersAdapter)->GetParameters());
	}

	return OutScriptVars;
}

UNiagaraParameterDefinitions* INiagaraParameterDefinitionsSubscriberViewModel::FindSubscribedParameterDefinitionsById(const FGuid& LibraryId)
{
	UNiagaraParameterDefinitionsBase* FoundParameterDefinitions = GetParameterDefinitionsSubscriber()->FindSubscribedParameterDefinitionsById(LibraryId);
	if (FoundParameterDefinitions != nullptr)
	{
		return CastChecked<UNiagaraParameterDefinitions>(FoundParameterDefinitions);
	}
	return nullptr;
}

UNiagaraScriptVariable* INiagaraParameterDefinitionsSubscriberViewModel::FindScriptVarById(const FGuid& ScriptVarId)
{
	UNiagaraScriptVariable* const* ScriptVarPtr = GetAllScriptVars().FindByPredicate([&ScriptVarId](const UNiagaraScriptVariable* ScriptVar){ return ScriptVar->Metadata.GetVariableGuid() == ScriptVarId; });
	return ScriptVarPtr != nullptr ? *ScriptVarPtr : nullptr;
}

UNiagaraScriptVariable* INiagaraParameterDefinitionsSubscriberViewModel::FindSubscribedParameterDefinitionsScriptVarByName(const FName& ScriptVarName)
{
	for (UNiagaraParameterDefinitions* ParameterDefinitions : GetSubscribedParameterDefinitions())
	{
		UNiagaraScriptVariable* const* FoundScriptVarPtr = ParameterDefinitions->GetParametersConst().FindByPredicate([ScriptVarName](const UNiagaraScriptVariable* ScriptVar){ return ScriptVar->Variable.GetName() == ScriptVarName; });
		if (FoundScriptVarPtr != nullptr)
		{
			return *FoundScriptVarPtr;
		}
	}
	return nullptr;
}

FOnSubscribedParameterDefinitionsChanged& INiagaraParameterDefinitionsSubscriberViewModel::GetOnSubscribedParameterDefinitionsChangedDelegate()
{
	return GetParameterDefinitionsSubscriber()->GetOnSubscribedParameterDefinitionsChangedDelegate();
}

FString INiagaraParameterDefinitionsSubscriberViewModel::GetSourceObjectPackagePathName()
{
	return FPaths::GetPath(GetParameterDefinitionsSubscriber()->GetSourceObjectPathName());
}
