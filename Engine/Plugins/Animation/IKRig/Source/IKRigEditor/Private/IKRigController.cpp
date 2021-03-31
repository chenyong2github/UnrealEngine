// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigController.h"
#include "IKRigDefinition.h"
#include "AnimationRuntime.h"
#include "IKRigSolver.h"
#include "ScopedTransaction.h"
#include "IKRigBoneSetting.h"

#define LOCTEXT_NAMESPACE	"IKRigController"


// -------------------------------------------------------
// CONTROLLER <-> IKRIGDEFINITION CONNECTION
//

TMap<UIKRigDefinition*, UIKRigController*> UIKRigController::DefinitionToControllerMap;

UIKRigController* UIKRigController::GetControllerByRigDefinition(UIKRigDefinition* InIKRigDefinition)
{
	if (!InIKRigDefinition)
	{
		return nullptr;
	}
	
	UIKRigController** Controller = DefinitionToControllerMap.Find(InIKRigDefinition);
	if (Controller)
	{
		return *Controller;
	}

	UIKRigController* NewController = NewObject<UIKRigController>();
	DefinitionToControllerMap.Add(InIKRigDefinition) = NewController;
	NewController->SetIKRigDefinition(InIKRigDefinition);
	return NewController;
}

// this should be called by IKRigDefinition::BeginDestroy;
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

void UIKRigController::SetIKRigDefinition(UIKRigDefinition* InIKRigDefinition)
{
	IKRigDefinition = InIKRigDefinition;
}

// -------------------------------------------------------
// SKELETON
//

void UIKRigController::SetSkeleton(const FReferenceSkeleton& InSkeleton) const
{
	if (!IKRigDefinition)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("SetSkeleton_Label", "Set Skeleton"));
	IKRigDefinition->Modify();

	IKRigDefinition->Skeleton.Initialize(InSkeleton);
}

FIKRigSkeleton& UIKRigController::GetSkeleton() const
{
	return IKRigDefinition->Skeleton;
}


// -------------------------------------------------------
// SOLVERS
//

UIKRigSolver* UIKRigController::AddSolver(TSubclassOf<UIKRigSolver> InIKRigSolverClass)
{
	if (!IKRigDefinition)
	{
		return nullptr;
	}

	FScopedTransaction Transaction(LOCTEXT("AddSolver_Label", "Add Solver"));
	IKRigDefinition->Modify();

	UIKRigSolver* NewSolver = NewObject<UIKRigSolver>(IKRigDefinition, InIKRigSolverClass);
	check(NewSolver);

	IKRigDefinition->Solvers.Add(NewSolver);
	return NewSolver;
}

int32 UIKRigController::GetNumSolvers() const
{
	return IKRigDefinition ? IKRigDefinition->Solvers.Num() : 0;
}

UIKRigSolver* UIKRigController::GetSolver(int32 Index) const
{
	bool ValidSolver = IKRigDefinition && IKRigDefinition->Solvers.IsValidIndex(Index);
	return ValidSolver ? IKRigDefinition->Solvers[Index] : nullptr;
}

void UIKRigController::RemoveSolver(UIKRigSolver* SolverToDelete)
{
	if (!(IKRigDefinition && SolverToDelete))
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("RemoveSolver_Label", "Remove Solver"));
	IKRigDefinition->Modify();

	IKRigDefinition->Solvers.Remove(SolverToDelete);
}

UIKRigBoneSetting* UIKRigController::AddBoneSetting(TSubclassOf<UIKRigBoneSetting> NewSettingType) const
{
	
	FScopedTransaction Transaction(LOCTEXT("AddSetting_Label", "Add Bone Setting"));
	IKRigDefinition->Modify();

	UIKRigBoneSetting* NewBoneSetting = NewObject<UIKRigBoneSetting>(IKRigDefinition, NewSettingType);
	if (NewBoneSetting)
	{
		IKRigDefinition->BoneSettings.Add(NewBoneSetting);
		return NewBoneSetting;
	}
	
	return nullptr;	
}

// -----------------------------
// GOALS
//

void UIKRigController::GetGoalNames(TArray<FIKRigEffectorGoal>& OutGoals) const
{
	if (!IKRigDefinition)
	{
		return;
	}

	IKRigDefinition->GetGoalNamesFromSolvers(OutGoals);
}

void UIKRigController::RenameGoal(const FName& OldName, const FName& NewName) const
{
	if (!IKRigDefinition)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("RenameGoal_Label", "Rename Goal"));
	IKRigDefinition->Modify();

	for (UIKRigSolver* Solver : IKRigDefinition->Solvers)
	{
		Solver->RenameGoal(OldName, NewName);
	}
}

#undef LOCTEXT_NAMESPACE