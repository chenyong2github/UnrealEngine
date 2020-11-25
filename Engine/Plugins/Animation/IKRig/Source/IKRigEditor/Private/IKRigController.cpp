// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * IKController implementation class
 *
 */

#include "IKRigController.h"
#include "IKRigDefinition.h"
#include "AnimationRuntime.h"
#include "IKRigSolverDefinition.h"

void UIKRigController::BeginDestroy() 
{
	if (IKRigDefinition)
	{
		for (int32 Index = 0; Index < IKRigDefinition->SolverDefinitions.Num(); ++Index)
		{
			UninitializeIKRigSolverDefinition(IKRigDefinition->SolverDefinitions[Index]);
		}
	}

	Super::BeginDestroy();
}

 // IKRigDefinition set up
void UIKRigController::SetIKRigDefinition(UIKRigDefinition* InIKRigDefinition)
{
	if (IKRigDefinition)
	{
		for (int32 Index = 0; Index < IKRigDefinition->SolverDefinitions.Num(); ++Index)
		{
			UninitializeIKRigSolverDefinition(IKRigDefinition->SolverDefinitions[Index]);
		}
	}

	IKRigDefinition = InIKRigDefinition;

	if (IKRigDefinition)
	{
		for (int32 Index = 0; Index < IKRigDefinition->SolverDefinitions.Num(); ++Index)
		{
			InitializeIKRigSolverDefinition(IKRigDefinition->SolverDefinitions[Index]);
		}
	}
}

void UIKRigController::InitializeIKRigSolverDefinition(UIKRigSolverDefinition* SolverDef)
{
	if (SolverDef)
	{
		// we want to register this controller delegate
		// if there are multiple controllers managing one IKRigDefinition
		// we want to ensure that works 
		FDelegateHandle DelegateHandle = SolverDef->GoalNeedsUpdateDelegate.AddUObject(this, &UIKRigController::UpdateGoal);
		SolverDelegateHandles.Add(SolverDef) = DelegateHandle;

		SolverDef->UpdateTaskList();
	}
}

void UIKRigController::UninitializeIKRigSolverDefinition(UIKRigSolverDefinition* SolverDef)
{
	if (SolverDef)
	{
		FDelegateHandle* DelegateHandle = SolverDelegateHandles.Find(SolverDef);
		if (ensure(DelegateHandle))
		{
			SolverDef->GoalNeedsUpdateDelegate.Remove(*DelegateHandle);
		}
	}
}

// hierarchy operators
void UIKRigController::SetSkeleton(const FReferenceSkeleton& InSkeleton)
{
	if (IKRigDefinition)
	{
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
		return IKRigDefinition->AddBone(InName, InParent, InGlobalTransform);
	}

	return false;
}

bool UIKRigController::RemoveBone(const FName& InName)
{
	if (IKRigDefinition)
	{
		return IKRigDefinition->RemoveBone(InName);
	}

	return false;
}
bool UIKRigController::RenameBone(const FName& InOldName, const FName& InNewName)
{
	if (IKRigDefinition)
	{
		return IKRigDefinition->RenameBone(InOldName, InNewName);
	}

	return false;
}
bool UIKRigController::ReparentBone(const FName& InName, const FName& InNewParent)
{
	if (IKRigDefinition)
	{
		return IKRigDefinition->ReparentBone(InName, InNewParent);
	}

	return false;
}
void UIKRigController::ResetHierarchy()
{
	if (IKRigDefinition)
	{
		IKRigDefinition->ResetHierarchy();
	}
}

// solver operators
UIKRigSolverDefinition* UIKRigController::AddSolver(TSubclassOf<UIKRigSolverDefinition> InIKRigSolverDefinitionClass)
{
	if (IKRigDefinition)
	{
		UIKRigSolverDefinition* NewSolver = NewObject<UIKRigSolverDefinition>(IKRigDefinition, InIKRigSolverDefinitionClass);
		check(NewSolver);

		// todo: set delegate for the goal update
		IKRigDefinition->SolverDefinitions.Add(NewSolver);
		InitializeIKRigSolverDefinition(NewSolver);
		IKRigDefinition->UpdateGoal();
		return NewSolver;
	}

	return nullptr;
}

int32 UIKRigController::GetTotalSolverCount() const
{
	if (IKRigDefinition)
	{
		return IKRigDefinition->SolverDefinitions.Num();
	}

	return 0;
}

UIKRigSolverDefinition* UIKRigController::GetSolver(int32 Index) const
{
	if (IKRigDefinition && IKRigDefinition->SolverDefinitions.IsValidIndex(Index))
	{
		return IKRigDefinition->SolverDefinitions[Index];
	}

	return nullptr;
}

void UIKRigController::RemoveSolver(UIKRigSolverDefinition* SolverToDelete)
{
	if (IKRigDefinition && SolverToDelete)
	{
		UninitializeIKRigSolverDefinition(SolverToDelete);
		IKRigDefinition->SolverDefinitions.Remove(SolverToDelete);
		IKRigDefinition->UpdateGoal();
	}
}

bool UIKRigController::ValidateSolver(UIKRigSolverDefinition* const SolverDef) const
{
	return (IKRigDefinition && SolverDef && IKRigDefinition->SolverDefinitions.Find(SolverDef) != INDEX_NONE);
}

void UIKRigController::UpdateGoal()
{
	if (IKRigDefinition)
	{
		IKRigDefinition->UpdateGoal();
	}
}

void UIKRigController::AutoConfigure(UIKRigSolverDefinition* SolverDef)
{
	if (ValidateSolver(SolverDef))
	{
		SolverDef->AutoConfigure();
	}
}

bool UIKRigController::CanAutoConfigure(UIKRigSolverDefinition* SolverDef) const
{
	if (ValidateSolver(SolverDef))
	{
		return SolverDef->CanAutoConfigure();
	}

	return false;
}

// constraint operators
// create new profile
void UIKRigController::CreateNewProfile(FName& InNewProfileName)
{
	
}

bool UIKRigController::RemoveConstraintProfile(const FName& InProfileName)
{
	return false;
}

void UIKRigController::RenameProfile(FName InCurrentProfileName, FName& InNewProfileName)
{

}

UIKRigConstraint* UIKRigController::AddConstraint(TSubclassOf<UIKRigConstraint> NewConstraintType, FName& InOutNewName, FName InProfile /*= NAME_None*/)
{
	return nullptr;	
}

UIKRigConstraint* UIKRigController::GetConstraint(const FName& InProfileName, const FName& InName) const
{
	return nullptr;
}

bool UIKRigController::RemoveConstraint(const FName& InConstraintName)
{
	return false;
}

void UIKRigController::GetConstraintProfileNames(TArray<FName>& OutProfileNames) const
{

}

void UIKRigController::GetConstraintNames(TArray<FName>& OutConstraintNames) const
{

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
		for (UIKRigSolverDefinition* SolverDef : IKRigDefinition->SolverDefinitions)
		{
			if (SolverDef)
			{
				SolverDef->RenameGoal(OldName, NewName);
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