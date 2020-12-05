// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Contains IKRigSolverDefinition Implementation
 *
 */

#include "IKRigSolverDefinition.h"
#include "IKRigDefinition.h"

void UIKRigSolverDefinition::CollectGoals(TArray<FName>& OutGoals)
{
	TArray<FName> LocalGoals;
	// this just accumulate on the current array
	// if you want to clear, clear before coming here
	EffectorToGoal.GenerateValueArray(LocalGoals);

	OutGoals += LocalGoals;
}

#if WITH_EDITOR
void UIKRigSolverDefinition::RenameGoal(const FName& OldName, const FName& NewName)
{
	for (auto Iter = EffectorToGoal.CreateIterator(); Iter; ++Iter)
	{
		if (Iter.Value() == OldName)
		{
			Iter.Value() = NewName;
		}
	}
}

void UIKRigSolverDefinition::EnsureUniqueGoalName(FName& InOutUniqueGoalName) const
{
	// call delegate 
	UIKRigDefinition* IKRigDef = CastChecked<UIKRigDefinition>(GetOuter());
	IKRigDef->EnsureCreateUniqueGoalName(InOutUniqueGoalName);
}

FName UIKRigSolverDefinition::CreateUniqueGoalName(const TCHAR* Suffix) const
{
	if (Suffix)
	{
		FString NewGoalStr = DisplayName + TEXT("_") + Suffix;
		// replace any whitespace with _
		NewGoalStr = NewGoalStr.Replace(TEXT(" \t"), TEXT("_"));
		FName NewGoalName = FName(*NewGoalStr, FNAME_Add);
		EnsureUniqueGoalName(NewGoalName);

		return NewGoalName;
	}

	return NAME_None;
}

void UIKRigSolverDefinition::OnGoalHasBeenUpdated()
{
	GoalNeedsUpdateDelegate.Broadcast();
}

void UIKRigSolverDefinition::EnsureToAddEffector(const FIKRigEffector& InEffector, const FString& InPrefix)
{
	FName* GoalName = EffectorToGoal.Find(InEffector);
	if (!GoalName)
	{
		EffectorToGoal.Add(InEffector) = CreateUniqueGoalName(*InPrefix);
	}
}

void UIKRigSolverDefinition::EnsureToRemoveEffector(const FIKRigEffector& InEffector)
{
	EffectorToGoal.Remove(InEffector);
}

#endif // WITH_EDITOR

void UIKRigSolverDefinition::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	UpdateEffectors();
#endif // WITH_EDITOR
}

void UIKRigSolverDefinition::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	// serialize these manually because that's the custom types
	Ar << EffectorToGoal;
}
