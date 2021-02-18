// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDebuggerCommon.h"

//////////////////////////////////////////////////////////////////////////

FNiagaraDebugHUDSettingsData::FNiagaraDebugHUDSettingsData()
{
	ActorFilter = TEXT("*");
	ComponentFilter = TEXT("*");
	SystemFilter = TEXT("*");
	EmitterFilter = TEXT("*");
}

//////////////////////////////////////////////////////////////////////////

#if WITH_EDITOR

void UNiagaraDebugHUDSettings::PostEditChangeProperty()
{
	OnChangedDelegate.Broadcast();
	SaveConfig();
}

void UNiagaraDebugHUDSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	PostEditChangeProperty();
}
#endif

//////////////////////////////////////////////////////////////////////////

FString FNiagaraDebugHUDVariable::BuildVariableString(const TArray<FNiagaraDebugHUDVariable>& Variables)
{
	FString Output;
	for (const FNiagaraDebugHUDVariable& Variable : Variables)
	{
		if (Variable.bEnabled && Variable.Name.Len() > 0)
		{
			if (Output.Len() > 0)
			{
				Output.Append(TEXT(","));
			}
			Output.Append(Variable.Name);
		}
	}
	return Output;
};

void FNiagaraDebugHUDVariable::InitFromString(const FString& VariablesString, TArray<FNiagaraDebugHUDVariable>& OutVariables)
{
	TArray<FString> Variables;
	VariablesString.ParseIntoArray(Variables, TEXT(","));
	for (const FString& Var : Variables)
	{
		FNiagaraDebugHUDVariable& NewVar = OutVariables.AddDefaulted_GetRef();
		NewVar.bEnabled = true;
		NewVar.Name = Var;
	}
}