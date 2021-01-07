// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackObjectIssueGenerator.h"
#include "NiagaraPlatformSet.h"


void FNiagaraPlatformSetIssueGenerator::GenerateIssues(void* Object, UNiagaraStackObject* StackObject, TArray<UNiagaraStackEntry::FStackIssue>& NewIssues)
{
	FNiagaraPlatformSet* PlatformSet = (FNiagaraPlatformSet*)Object;

	TArray<FName> InvalidCVars;
	for (FNiagaraPlatformSetCVarCondition& CVarCondition : PlatformSet->CVarConditions)
	{
		IConsoleVariable* CVar = CVarCondition.GetCVar();
		if (CVar == nullptr)
		{
			InvalidCVars.AddUnique(CVarCondition.CVarName);
		}
	}

	for (FName& CVarName : InvalidCVars)
	{
		NewIssues.Add(UNiagaraStackEntry::FStackIssue(
			EStackIssueSeverity::Error,
			NSLOCTEXT("StackObject", "InvalidCVarConditionShort", "Platform Set has an invalid CVar condition!"),
			FText::Format(FTextFormat::FromString("{0} is not a valid CVar name."), FText::FromName(CVarName)),
			StackObject->GetStackEditorDataKey(),
			false));
	}
}
