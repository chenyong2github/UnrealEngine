// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraEditorData.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraParameterDefinitionsBase.h"
#include "NiagaraParameterDefinitions.h"
#include "NiagaraScriptVariable.h"


TArray<TTuple<FName /*SyncedOldName*/, FName /*SyncedNewName*/>> UNiagaraEditorParametersAdapter::SynchronizeParametersWithParameterDefinitions(
	const TArray<UNiagaraParameterDefinitionsBase*> ParameterDefinitions,
	const TArray<FGuid>& ParameterDefinitionsParameterIds,
	const FSynchronizeWithParameterDefinitionsArgs& Args)
{
	TArray<TTuple<FName, FName>> OldToNewNameArr;
	TArray<UNiagaraScriptVariable*> TargetScriptVariables;
	if (Args.SpecificDestScriptVarIds.Num() > 0)
	{
		TargetScriptVariables = EditorOnlyScriptVars.FilterByPredicate([&Args](const UNiagaraScriptVariable* DestScriptVar) { return Args.SpecificDestScriptVarIds.Contains(DestScriptVar->Metadata.GetVariableGuid()); });
	}
	else
	{
		TargetScriptVariables = EditorOnlyScriptVars;
	}

	TArray<const UNiagaraScriptVariable*> TargetLibraryScriptVariables;
	const TArray<UNiagaraParameterDefinitions*> TargetParameterDefinitions = [&Args, &ParameterDefinitions]()->TArray<UNiagaraParameterDefinitions*> {
		const TArray<UNiagaraParameterDefinitions*> TempParameterDefinitions = FNiagaraEditorUtilities::DowncastParameterDefinitionsBaseArray(ParameterDefinitions);
		if (Args.SpecificDefinitionsUniqueIds.Num() > 0)
		{
			return TempParameterDefinitions.FilterByPredicate([&Args](const UNiagaraParameterDefinitions* ParameterDefinitionsItr) { return Args.SpecificDefinitionsUniqueIds.Contains(ParameterDefinitionsItr->GetDefinitionsUniqueId()); });
		}
		return TempParameterDefinitions;
	}();

	for (const UNiagaraParameterDefinitions* TargetParameterDefinitionsItr : TargetParameterDefinitions)
	{
		TargetLibraryScriptVariables.Append(TargetParameterDefinitionsItr->GetParametersConst());
	}

	auto GetTargetLibraryScriptVarWithSameId = [&TargetLibraryScriptVariables](const UNiagaraScriptVariable* GraphScriptVar)->const UNiagaraScriptVariable* {
		const FGuid& GraphScriptVarId = GraphScriptVar->Metadata.GetVariableGuid();
		if (const UNiagaraScriptVariable* const* FoundLibraryScriptVarPtr = TargetLibraryScriptVariables.FindByPredicate([GraphScriptVarId](const UNiagaraScriptVariable* LibraryScriptVar) { return LibraryScriptVar->Metadata.GetVariableGuid() == GraphScriptVarId; }))
		{
			return *FoundLibraryScriptVarPtr;
		}
		return nullptr;
	};

	auto GetTargetLibraryScriptVarWithSameName = [&TargetLibraryScriptVariables](const UNiagaraScriptVariable* GraphScriptVar)->const UNiagaraScriptVariable* {
		const FName& GraphScriptVarName = GraphScriptVar->Variable.GetName();
		if (const UNiagaraScriptVariable* const* FoundLibraryScriptVarPtr = TargetLibraryScriptVariables.FindByPredicate([GraphScriptVarName](const UNiagaraScriptVariable* LibraryScriptVar) { return LibraryScriptVar->Variable.GetName() == GraphScriptVarName; }))
		{
			return *FoundLibraryScriptVarPtr;
		}
		return nullptr;
	};

	if (Args.bSubscribeAllNameMatchParameters)
	{
		for (UNiagaraScriptVariable* TargetScriptVar : TargetScriptVariables)
		{
			if (const UNiagaraScriptVariable* TargetLibraryScriptVar = GetTargetLibraryScriptVarWithSameName(TargetScriptVar))
			{
				TargetScriptVar->SetIsSubscribedToParameterDefinitions(true);
				TargetScriptVar->Metadata.SetVariableGuid(TargetLibraryScriptVar->Metadata.GetVariableGuid());
				if (UNiagaraScriptVariable::DefaultsAreEquivalent(TargetScriptVar, TargetLibraryScriptVar) == false)
				{
					// Preserve the TargetScriptVars default value if it is not equivalent to prevent breaking changes from subscribing new parameters.
					TargetScriptVar->SetIsOverridingParameterDefinitionsDefaultValue(true);
				}
			}
		}
	}

	for (UNiagaraScriptVariable* TargetScriptVar : TargetScriptVariables)
	{
		if (TargetScriptVar->GetIsSubscribedToParameterDefinitions())
		{
			if (const UNiagaraScriptVariable* TargetLibraryScriptVar = GetTargetLibraryScriptVarWithSameId(TargetScriptVar))
			{
				TOptional<TTuple<FName, FName>> OptionalOldToNewName = SynchronizeEditorOnlyScriptVar(TargetLibraryScriptVar, TargetScriptVar);
				if (OptionalOldToNewName.IsSet())
				{
					OldToNewNameArr.Add(OptionalOldToNewName.GetValue());
				}
			}
			else if (ParameterDefinitionsParameterIds.Contains(TargetScriptVar->Metadata.GetVariableGuid()) == false)
			{
				// TargetScriptVar is marked as being sourced from a parameter definitions but no matching definition script variables were found, break the link to the parameter definitions for TargetScriptVar.
				TargetScriptVar->SetIsSubscribedToParameterDefinitions(false);
			}
		}
	}

	return OldToNewNameArr;
}

TOptional<TTuple<FName /*SyncedOldName*/, FName /*SyncedNewName*/>> UNiagaraEditorParametersAdapter::SynchronizeEditorOnlyScriptVar(const UNiagaraScriptVariable* SourceScriptVar, UNiagaraScriptVariable* DestScriptVar /*= nullptr*/)
{
	if (DestScriptVar == nullptr)
	{
		const FGuid& SourceScriptVarId = SourceScriptVar->Metadata.GetVariableGuid();
		UNiagaraScriptVariable** ScriptVarPtr = EditorOnlyScriptVars.FindByPredicate([&SourceScriptVarId](const UNiagaraScriptVariable* ScriptVar) { return ScriptVar->Metadata.GetVariableGuid() == SourceScriptVarId; });
		if (ScriptVarPtr == nullptr)
		{
			// Failed to find a DestScriptVar with an Id matching that of SourceScriptVar.
			return TOptional<TTuple<FName, FName>>();
		}
		DestScriptVar = *ScriptVarPtr;
	}

	if (DestScriptVar->GetChangeId() != SourceScriptVar->GetChangeId())
	{
		const FName OldParameterName = DestScriptVar->Variable.GetName();
		const FName NewParameterName = SourceScriptVar->Variable.GetName();

		DestScriptVar->Variable = SourceScriptVar->Variable;
		DestScriptVar->Metadata.Description = SourceScriptVar->Metadata.Description;
		DestScriptVar->SetChangeId(SourceScriptVar->GetChangeId());

		if (OldParameterName != NewParameterName)
		{
			return TOptional<TTuple<FName, FName>>(TTuple<FName, FName>(OldParameterName, NewParameterName));
		}
	}
	return TOptional<TTuple<FName, FName>>();
}

bool UNiagaraEditorParametersAdapter::SynchronizeParameterDefinitionsScriptVariableRemoved(const FGuid& RemovedScriptVarId)
{
	for (UNiagaraScriptVariable* ScriptVar : EditorOnlyScriptVars)
	{
		if (ScriptVar->Metadata.GetVariableGuid() == RemovedScriptVarId)
		{
			ScriptVar->SetIsSubscribedToParameterDefinitions(false);
			return true;
		}
	}
	return false;
}
