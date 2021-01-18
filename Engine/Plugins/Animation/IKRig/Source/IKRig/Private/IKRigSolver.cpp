// Copyright Epic Games, Inc. All Rights Reserved.


#include "IKRigSolver.h"
#include "IKRigDataTypes.h"
#include "IKRigDefinition.h"


void UIKRigSolver::SolveInternal(
	FIKRigTransforms& InOutGlobalTransform,
	const FIKRigGoalContainer& Goals,
	FControlRigDrawInterface* InOutDrawInterface)
{
	if (IsSolverActive())
	{
		Solve(InOutGlobalTransform, Goals, InOutDrawInterface);
	}
}

bool UIKRigSolver::IsSolverActive() const
{
	return bEnabled;
}

bool UIKRigSolver::GetGoalForEffector(
	const FIKRigEffector& InEffector,
	const FIKRigGoalContainer& Goals,
	FIKRigGoal& OutGoal) const
{
	const FName* GoalName = EffectorToGoalName.Find(InEffector);
	if (!GoalName)
	{
		return false;
	}

	return Goals.GetGoalByName(*GoalName, OutGoal);
}

void UIKRigSolver::AppendGoalNamesToArray(TArray<FName>& OutGoals)
{
	TArray<FName> LocalGoals;
	EffectorToGoalName.GenerateValueArray(LocalGoals);
	OutGoals += LocalGoals;
}

#if WITH_EDITOR
void UIKRigSolver::RenameGoal(const FName& OldName, const FName& NewName)
{
	for (auto Iter = EffectorToGoalName.CreateIterator(); Iter; ++Iter)
	{
		if (Iter.Value() == OldName)
		{
			Iter.Value() = NewName;
		}
	}
}

void UIKRigSolver::EnsureUniqueGoalName(FName& InOutUniqueGoalName) const
{
	// call delegate 
	UIKRigDefinition* IKRigDef = CastChecked<UIKRigDefinition>(GetOuter());
	IKRigDef->EnsureCreateUniqueGoalName(InOutUniqueGoalName);
}

FName UIKRigSolver::CreateUniqueGoalName(const TCHAR* Suffix) const
{
	if (Suffix)
	{
		FString NewGoalStr = FString("") + TEXT("NewGoal_") + Suffix;
		// replace any whitespace with _
		NewGoalStr = NewGoalStr.Replace(TEXT(" \t"), TEXT("_"));
		FName NewGoalName = FName(*NewGoalStr, FNAME_Add);
		EnsureUniqueGoalName(NewGoalName);

		return NewGoalName;
	}

	return NAME_None;
}

void UIKRigSolver::OnGoalHasBeenUpdated()
{
	GoalNeedsUpdateDelegate.Broadcast();
}

void UIKRigSolver::EnsureToAddEffector(const FIKRigEffector& InEffector, const FString& InPrefix)
{
	FName* GoalName = EffectorToGoalName.Find(InEffector);
	if (!GoalName)
	{
		EffectorToGoalName.Add(InEffector) = CreateUniqueGoalName(*InPrefix);
	}
}

void UIKRigSolver::EnsureToRemoveEffector(const FIKRigEffector& InEffector)
{
	EffectorToGoalName.Remove(InEffector);
}

#endif // WITH_EDITOR

void UIKRigSolver::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	UpdateEffectors();
#endif // WITH_EDITOR
}

void UIKRigSolver::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	// serialize these manually because that's the custom types
	Ar << EffectorToGoalName;
}
