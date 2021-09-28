// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeParameterLayout.h"
#include "StateTreeVariableLayout.h"
#include "StateTreeConstantStorage.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"


#if WITH_EDITOR
bool FStateTreeParameterLayout::ResolveVariables(const FStateTreeVariableLayout& Variables, FStateTreeConstantStorage& Constants)
{
	for (FStateTreeParameter& Param : Parameters)
	{
		if (!Param.Variable.ResolveHandle(Variables, Constants))
		{
			return false;
		}
	}

	return true;
}

bool FStateTreeParameterLayout::IsCompatible(const FStateTreeVariableLayout* ParameterLayout) const
{
	if (!ParameterLayout)
	{
		return Parameters.Num() == 0;
	}

	if (Parameters.Num() != ParameterLayout->Variables.Num())
	{
		return false;
	}
	for (int32 i = 0; i < Parameters.Num(); i++)
	{
		if (!Parameters[i].IsCompatible(ParameterLayout->Variables[i]))
		{
			return false;
		}
	}
	return true;
}

void FStateTreeParameterLayout::UpdateLayout(const FStateTreeVariableLayout* NewLayout)
{
	if (!NewLayout)
	{
		Reset();
		return;
	}

	TMap<FGuid, FStateTreeVariable> OldVariables;
	for (const FStateTreeParameter& Param : Parameters)
	{
		OldVariables.Add(Param.ID, Param.Variable);
	}

	Parameters.SetNum(NewLayout->Variables.Num());
	for (int32 i = 0; i < NewLayout->Variables.Num(); i++)
	{
		FStateTreeParameter& Param = Parameters[i];
		const FStateTreeVariableDesc& Var = NewLayout->Variables[i];
		Param.Name = Var.Name;
		Param.ID = Var.ID;
		Param.Variable.InitializeFromDesc(EStateTreeVariableBindingMode::Typed, Var);

		// Try to carry over the previous values.
		FStateTreeVariable* OldVar = OldVariables.Find(Param.ID);
		if (OldVar)
		{
			Param.Variable.CopyValueFrom(*OldVar);
		}
	}
}
#endif // WITH_EDITOR
