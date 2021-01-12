// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigController.h"
#include "IKRigDefinition.h"
#include "AnimationRuntime.h"
#include "IKRigSolver.h"
#include "ScopedTransaction.h"
#include "IKRigConstraint.h"

#define LOCTEXT_NAMESPACE	"IKRigController"

TMap<UIKRigDefinition*, UIKRigController*> UIKRigController::DefinitionToControllerMap;

// currently it's not clear the lifecycle of this map
// usually during editor, they will be present, 
UIKRigController* UIKRigController::GetControllerByRigDefinition(UIKRigDefinition* InIKRigDefinition)
{
	if (InIKRigDefinition)
	{
		UIKRigController** Controller = DefinitionToControllerMap.Find(InIKRigDefinition);
		if (Controller)
		{
			return *Controller;
		}
		else
		{
			UIKRigController* NewController = NewObject<UIKRigController>();
			DefinitionToControllerMap.Add(InIKRigDefinition) = NewController;
			NewController->SetIKRigDefinition(InIKRigDefinition);
			InIKRigDefinition->IKRigDefinitionBeginDestroy.AddStatic(&UIKRigController::RemoveControllerByRigDefinition);
			return NewController;
		}
	}

	return nullptr;
}

// this shoudl be called by IKRigDefinition::BeginDestroy;
void UIKRigController::RemoveControllerByRigDefinition(UIKRigDefinition* InIKRigDefinition)
{
	DefinitionToControllerMap.Remove(InIKRigDefinition);
}

void UIKRigController::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	// since static member, we just add only for default object
	if (InThis && InThis->IsTemplate())
	{
		for (auto Iter = DefinitionToControllerMap.CreateIterator(); Iter; ++Iter)
		{
			// we add controllers to the list. so that it doesn't GCed
			// when do we delete from the list?
			UIKRigController* Obj = Iter.Value();
			Collector.AddReferencedObject(Obj);
		}
	}
}

void UIKRigController::BeginDestroy() 
{
	if (IKRigDefinition)
	{
		for (int32 Index = 0; Index < IKRigDefinition->Solvers.Num(); ++Index)
		{
			UninitializeSolver(IKRigDefinition->Solvers[Index]);
		}
	}

	Super::BeginDestroy();
}

 // IKRigDefinition set up
void UIKRigController::SetIKRigDefinition(UIKRigDefinition* InIKRigDefinition)
{
	if (IKRigDefinition)
	{
		for (int32 Index = 0; Index < IKRigDefinition->Solvers.Num(); ++Index)
		{
			UninitializeSolver(IKRigDefinition->Solvers[Index]);
		}
	}

	IKRigDefinition = InIKRigDefinition;

	if (IKRigDefinition)
	{
		for (int32 Index = 0; Index < IKRigDefinition->Solvers.Num(); ++Index)
		{
			InitializeSolver(IKRigDefinition->Solvers[Index]);
		}
	}
}

void UIKRigController::InitializeSolver(UIKRigSolver* Solver)
{
	if (Solver)
	{
		// we want to register this controller delegate
		// if there are multiple controllers managing one IKRigDefinition
		// we want to ensure that works 
		FDelegateHandle DelegateHandle = Solver->GoalNeedsUpdateDelegate.AddUObject(this, &UIKRigController::UpdateGoal);
		SolverDelegateHandles.Add(Solver) = DelegateHandle;

		Solver->UpdateEffectors();
	}
}

void UIKRigController::UninitializeSolver(UIKRigSolver* Solver)
{
	if (Solver)
	{
		FDelegateHandle* DelegateHandle = SolverDelegateHandles.Find(Solver);
		if (ensure(DelegateHandle))
		{
			Solver->GoalNeedsUpdateDelegate.Remove(*DelegateHandle);
		}
	}
}

// hierarchy operators
void UIKRigController::SetSkeleton(const FReferenceSkeleton& InSkeleton)
{
	if (IKRigDefinition)
	{
		FScopedTransaction Transaction(LOCTEXT("SetSkeleton_Label", "Set Skeleton"));
		IKRigDefinition->Modify();

		IKRigDefinition->ResetHierarchy();

		const TArray<FMeshBoneInfo>& RefBoneInfo = InSkeleton.GetRefBoneInfo();
		TArray<FTransform> RefPoseInCS;
		FAnimationRuntime::FillUpComponentSpaceTransforms(InSkeleton, InSkeleton.GetRefBonePose(), RefPoseInCS);
		ensure(RefPoseInCS.Num() == RefBoneInfo.Num());
		for (int32 Index=0; Index<RefBoneInfo.Num(); ++Index)
		{
			int32 ParentIndex = RefBoneInfo[Index].ParentIndex;
			ensure (IKRigDefinition->AddBone(RefBoneInfo[Index].Name, (ParentIndex != INDEX_NONE)? RefBoneInfo[ParentIndex].Name : NAME_None, RefPoseInCS[Index]));
		}

		ensure(IKRigDefinition->Hierarchy.GetNum() == IKRigDefinition->ReferencePose.GetNum());
	}
}

bool UIKRigController::AddBone(const FName& InName, const FName& InParent, const FTransform& InGlobalTransform)
{
	if (IKRigDefinition)
	{
		FScopedTransaction Transaction(LOCTEXT("AddBone_Label", "Add Bone"));
		IKRigDefinition->Modify();

		return IKRigDefinition->AddBone(InName, InParent, InGlobalTransform);
	}

	return false;
}

bool UIKRigController::RemoveBone(const FName& InName)
{
	if (IKRigDefinition)
	{
		FScopedTransaction Transaction(LOCTEXT("RemoveBone_Label", "Remove Bone"));
		IKRigDefinition->Modify();

		return IKRigDefinition->RemoveBone(InName);
	}

	return false;
}
bool UIKRigController::RenameBone(const FName& InOldName, const FName& InNewName)
{
	if (IKRigDefinition)
	{
		FScopedTransaction Transaction(LOCTEXT("RenameBone_Label", "Rename Bone"));
		IKRigDefinition->Modify();

		return IKRigDefinition->RenameBone(InOldName, InNewName);
	}

	return false;
}
bool UIKRigController::ReparentBone(const FName& InName, const FName& InNewParent)
{
	if (IKRigDefinition)
	{
		FScopedTransaction Transaction(LOCTEXT("ReparentBone_Label", "Reparent Bone"));
		IKRigDefinition->Modify();

		return IKRigDefinition->ReparentBone(InName, InNewParent);
	}

	return false;
}
void UIKRigController::ResetHierarchy()
{
	if (IKRigDefinition)
	{
		FScopedTransaction Transaction(LOCTEXT("ResetHierarchy_Label", "Reset Hierarchy"));
		IKRigDefinition->Modify();

		IKRigDefinition->ResetHierarchy();
	}
}

// solver operators
UIKRigSolver* UIKRigController::AddSolver(TSubclassOf<UIKRigSolver> InIKRigSolverClass)
{
	if (IKRigDefinition)
	{
		FScopedTransaction Transaction(LOCTEXT("AddSolver_Label", "Add Solver"));
		IKRigDefinition->Modify();

		UIKRigSolver* NewSolver = NewObject<UIKRigSolver>(IKRigDefinition, InIKRigSolverClass);
		check(NewSolver);

		// todo: set delegate for the goal update
		IKRigDefinition->Solvers.Add(NewSolver);
		InitializeSolver(NewSolver);
		IKRigDefinition->UpdateGoal();
		return NewSolver;
	}

	return nullptr;
}

int32 UIKRigController::GetNumSolvers() const
{
	if (IKRigDefinition)
	{
		return IKRigDefinition->Solvers.Num();
	}

	return 0;
}

UIKRigSolver* UIKRigController::GetSolver(int32 Index) const
{
	if (IKRigDefinition && IKRigDefinition->Solvers.IsValidIndex(Index))
	{
		return IKRigDefinition->Solvers[Index];
	}

	return nullptr;
}

void UIKRigController::RemoveSolver(UIKRigSolver* SolverToDelete)
{
	if (IKRigDefinition && SolverToDelete)
	{
		FScopedTransaction Transaction(LOCTEXT("RemoveSolver_Label", "Remove Solver"));
		IKRigDefinition->Modify();

		UninitializeSolver(SolverToDelete);
		IKRigDefinition->Solvers.Remove(SolverToDelete);
		IKRigDefinition->UpdateGoal();
	}
}

bool UIKRigController::ValidateSolver(UIKRigSolver* const Solver) const
{
	return (IKRigDefinition && Solver && IKRigDefinition->Solvers.Find(Solver) != INDEX_NONE);
}

void UIKRigController::UpdateGoal()
{
	if (IKRigDefinition)
	{
		FScopedTransaction Transaction(LOCTEXT("UpdateGoal_Label", "Update Goal"));
		IKRigDefinition->Modify();

		IKRigDefinition->UpdateGoal();
		OnGoalModified.Broadcast();
	}
}

FName UIKRigController::GetGoalName(UIKRigSolver* InSolver, const FIKRigEffector& InEffector)
{
	if (InSolver)
	{
		FName* GoalName = InSolver->EffectorToGoal.Find(InEffector);
		if (GoalName)
		{
			return *GoalName;
		}
	}

	return NAME_None;
}

void UIKRigController::SetGoalName(UIKRigSolver* InSolver, const FIKRigEffector& InEffector, const FName& NewGoalName)
{
	if (InSolver)
	{
		FScopedTransaction Transaction(LOCTEXT("SetGoalName_Label", "Set Goal Name"));
		InSolver->Modify();

		FName* GoalName = InSolver->EffectorToGoal.Find(InEffector);
		if (GoalName)
		{
			*GoalName = NewGoalName;

			UpdateGoal();
		}
	}
}

UIKRigConstraint* UIKRigController::AddConstraint(TSubclassOf<UIKRigConstraint> NewConstraintType)
{
	
	FScopedTransaction Transaction(LOCTEXT("AddConstraint_Label", "Add Constraint"));
	IKRigDefinition->Modify();

	UIKRigConstraint* NewRigConstraint = NewObject<UIKRigConstraint>(IKRigDefinition, NewConstraintType);
	if (NewRigConstraint)
	{
		IKRigDefinition->Constraints.Add(NewRigConstraint);
		return NewRigConstraint;
	}
	
	return nullptr;	
}

// goal operators
void UIKRigController::QueryGoals(TArray<FName>& OutGoals) const
{
	if (IKRigDefinition)
	{
		IKRigDefinition->GetGoals().GenerateKeyArray(OutGoals);
	}
}

void UIKRigController::RenameGoal(const FName& OldName, const FName& NewName)
{
	// ensure we don't have used NewDisplayName 
	if (IKRigDefinition)
	{
		FScopedTransaction Transaction(LOCTEXT("RenameGoal_Label", "Rename Goal"));
		IKRigDefinition->Modify();

		for (UIKRigSolver* Solver : IKRigDefinition->Solvers)
		{
			if (Solver)
			{
				Solver->RenameGoal(OldName, NewName);
			}
		}
		
		// update goal list
		IKRigDefinition->UpdateGoal();
	}
}

FIKRigGoal* UIKRigController::GetGoal(const FName& InGoalName) 
{
	if (IKRigDefinition)
	{
		return IKRigDefinition->IKGoals.Find(InGoalName);
	}

	return nullptr;
}

const FIKRigGoal* UIKRigController::GetGoal(const FName& InGoalName) const
{
	if (IKRigDefinition)
	{
		return IKRigDefinition->IKGoals.Find(InGoalName);
	}

	return nullptr;
}

#undef LOCTEXT_NAMESPACE