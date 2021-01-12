// Copyright Epic Games, Inc. All Rights Reserved.


#include "IKRigSolver.h"
#include "IKRigDataTypes.h"
#include "IKRigDefinition.h"

// input hierarchy and ref pose? 
void UIKRigSolver::Init(const FIKRigTransformModifier& TransformModifier, FIKRigTransformGetter InRefPoseGetter, FIKRigGoalGetter InGoalGetter)
{
	//SolverDefinition = InSolverDefinition;

	RefPoseGetter = InRefPoseGetter;
	ensure(RefPoseGetter.IsBound());

	GoalGetter = InGoalGetter;
	ensure(GoalGetter.IsBound());

	InitInternal(TransformModifier);
}

bool UIKRigSolver::IsSolverActive() const 
{
	//return (SolverDefinition && bEnabled);
	return bEnabled;
}

// input : goal getter or goals
// output : modified pose - GlobalTransforms
void UIKRigSolver::Solve(FIKRigTransformModifier& InOutGlobalTransform, FControlRigDrawInterface* InOutDrawInterface)
{
	if (IsSolverActive())
	{
		SolveInternal(InOutGlobalTransform, InOutDrawInterface);
	}
}

bool UIKRigSolver::GetEffectorTarget(const FIKRigEffector& InEffector, FIKRigTarget& OutTarget) const
{
	//if (SolverDefinition && GoalGetter.IsBound())
	if (GoalGetter.IsBound())
	{	
		const FName* GoalName = GetEffectorToGoal().Find(InEffector);
		if (GoalName)
		{
			return GoalGetter.Execute(*GoalName, OutTarget);
		}
	}

	return false;
}

const FIKRigTransform& UIKRigSolver::GetReferencePose() const
{
	if (RefPoseGetter.IsBound())
	{
		return RefPoseGetter.Execute();
	}

	static FIKRigTransform Dummy;
	return Dummy;
}

void UIKRigSolver::CollectGoals(TArray<FName>& OutGoals)
{
	TArray<FName> LocalGoals;
	// this just accumulate on the current array
	// if you want to clear, clear before coming here
	EffectorToGoal.GenerateValueArray(LocalGoals);

	OutGoals += LocalGoals;
}

#if WITH_EDITOR
void UIKRigSolver::RenameGoal(const FName& OldName, const FName& NewName)
{
	for (auto Iter = EffectorToGoal.CreateIterator(); Iter; ++Iter)
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
	FName* GoalName = EffectorToGoal.Find(InEffector);
	if (!GoalName)
	{
		EffectorToGoal.Add(InEffector) = CreateUniqueGoalName(*InPrefix);
	}
}

void UIKRigSolver::EnsureToRemoveEffector(const FIKRigEffector& InEffector)
{
	EffectorToGoal.Remove(InEffector);
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
	Ar << EffectorToGoal;
}
