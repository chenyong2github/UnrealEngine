// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackObjectIssueGenerator.h"
#include "NiagaraPlatformSet.h"


void FNiagaraPlatformSetIssueGenerator::GenerateIssues(void* Object, UNiagaraStackObject* StackObject, TArray<UNiagaraStackEntry::FStackIssue>& NewIssues)
{
	FNiagaraPlatformSet* PlatformSet = (FNiagaraPlatformSet*)Object;

	for (FNiagaraPlatformSetCVarCondition& CVarCondition : PlatformSet->CVarConditions)
	{
		IConsoleVariable* CVar = CVarCondition.GetCVar();
		if (CVar == nullptr)
		{
			NewIssues.Add(UNiagaraStackEntry::FStackIssue(
				EStackIssueSeverity::Error,
				NSLOCTEXT("StackObject", "InvalidCVarConditionShort", "Platform Set has an invalid CVar condition!"),
				FText::Format(FTextFormat::FromString("{0} is not a valid CVar name."), FText::FromName(CVarCondition.CVarName)),
				StackObject->GetStackEditorDataKey(),
				false));
		}
	}
}
