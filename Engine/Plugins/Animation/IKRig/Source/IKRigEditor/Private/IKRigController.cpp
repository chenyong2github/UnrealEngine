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

const FIKRigHierarchy* UIKRigController::GetHierarchy() const
{
	return IKRigDefinition ? &IKRigDefinition->Hierarchy : nullptr;
}

const TArray<FTransform>& UIKRigController::GetRefPoseTransforms() const
{
	if (IKRigDefinition)
	{
		return IKRigDefinition->RefPoseTransforms;
	}

	static TArray<FTransform> Dummy;
	return Dummy;
}

void UIKRigController::SetSkeleton(const FReferenceSkeleton& InSkeleton)
{
	if (!IKRigDefinition)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("SetSkeleton_Label", "Set Skeleton"));
	IKRigDefinition->Modify();

	IKRigDefinition->ResetHierarchy();

	const TArray<FMeshBoneInfo>& RefBoneInfo = InSkeleton.GetRefBoneInfo();
	TArray<FTransform> RefPoseInCS;
	FAnimationRuntime::FillUpComponentSpaceTransforms(InSkeleton, InSkeleton.GetRefBonePose(), RefPoseInCS);
	ensure(RefPoseInCS.Num() == RefBoneInfo.Num());
	for (int32 Index = 0; Index < RefBoneInfo.Num(); ++Index)
	{
		int32 ParentIndex = RefBoneInfo[Index].ParentIndex;
		ensure(IKRigDefinition->AddBone(RefBoneInfo[Index].Name, (ParentIndex != INDEX_NONE) ? RefBoneInfo[ParentIndex].Name : NAME_None, RefPoseInCS[Index]));
	}

	ensure(IKRigDefinition->Hierarchy.GetNum() == IKRigDefinition->RefPoseTransforms.Num());
}

bool UIKRigController::AddBone(const FName& InName, const FName& InParent, const FTransform& InGlobalTransform)
{
	if (!IKRigDefinition)
	{
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("AddBone_Label", "Add Bone"));
	IKRigDefinition->Modify();

	return IKRigDefinition->AddBone(InName, InParent, InGlobalTransform);
}


void UIKRigController::ResetHierarchy()
{
	if (!IKRigDefinition)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("ResetHierarchy_Label", "Reset Hierarchy"));
	IKRigDefinition->Modify();

	IKRigDefinition->ResetHierarchy();
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

void UIKRigController::GetGoalNames(TArray<FName>& OutGoals) const
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