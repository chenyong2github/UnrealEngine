// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * IKController implementation class
 *
 */

#include "IKRigController.h"
#include "IKRigDefinition.h"
#include "AnimationRuntime.h"
#include "IKRigSolverDefinition.h"
#include "ScopedTransaction.h"
#include "IKRigConstraintDefinition.h"
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
			// we add controllers to the list. so that it doens't GCed
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

		SolverDef->UpdateEffectors();
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
UIKRigSolverDefinition* UIKRigController::AddSolver(TSubclassOf<UIKRigSolverDefinition> InIKRigSolverDefinitionClass)
{
	if (IKRigDefinition)
	{
		FScopedTransaction Transaction(LOCTEXT("AddSolver_Label", "Add Solver"));
		IKRigDefinition->Modify();

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
		FScopedTransaction Transaction(LOCTEXT("RemoveSolver_Label", "Remove Solver"));
		IKRigDefinition->Modify();

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
		FScopedTransaction Transaction(LOCTEXT("UpdateGoal_Label", "Update Goal"));
		IKRigDefinition->Modify();

		IKRigDefinition->UpdateGoal();
		OnGoalModified.Broadcast();
	}
}

FName UIKRigController::GetGoalName(UIKRigSolverDefinition* InSolverDefinition, const FIKRigEffector& InEffector)
{
	if (InSolverDefinition)
	{
		FName* GoalName = InSolverDefinition->EffectorToGoal.Find(InEffector);
		if (GoalName)
		{
			return *GoalName;
		}
	}

	return NAME_None;
}

void UIKRigController::SetGoalName(UIKRigSolverDefinition* InSolverDefinition, const FIKRigEffector& InEffector, const FName& NewGoalName)
{
	if (InSolverDefinition)
	{
		FScopedTransaction Transaction(LOCTEXT("SetGoalName_Label", "Set Goal Name"));
		InSolverDefinition->Modify();

		FName* GoalName = InSolverDefinition->EffectorToGoal.Find(InEffector);
		if (GoalName)
		{
			*GoalName = NewGoalName;

			UpdateGoal();
		}
	}
}

void UIKRigController::AutoConfigure(UIKRigSolverDefinition* SolverDef)
{
	if (ValidateSolver(SolverDef))
	{
		FScopedTransaction Transaction(LOCTEXT("AutoConfigure_Label", "Auto Congirure"));
		SolverDef->Modify();

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
	if (IKRigDefinition && InNewProfileName != NAME_None)
	{
		FScopedTransaction Transaction(LOCTEXT("CreateNewProfile_Label", "Create New Constraint Profile"));
		IKRigDefinition->Modify();
		// we just find or add
		IKRigDefinition->ConstraintDefinitions->ConstraintProfiles.FindOrAdd(InNewProfileName);
	}
}

bool UIKRigController::RemoveConstraintProfile(const FName& InProfileName)
{
	if (IKRigDefinition && InProfileName != NAME_None)
	{
		FScopedTransaction Transaction(LOCTEXT("RemoveProfile_Label", "Remove Constraint Profile"));
		IKRigDefinition->Modify();
		// we just find or add
		return IKRigDefinition->ConstraintDefinitions->ConstraintProfiles.Remove(InProfileName) > 0;

	}

	return false;
}

void UIKRigController::RenameProfile(FName InCurrentProfileName, FName& InNewProfileName)
{
	// write this 
	ensure (false);
	// we don't allow change default profile until we support saving the default profile
}

FIKRigConstraintProfile* UIKRigController::GetConstraintProfile(const FName& InProfileName) const
{
	if (IKRigDefinition)
	{
		FName SearchProfileName = (InProfileName == NAME_None)? UIKRigConstraintDefinition::DefaultProfileName : InProfileName;
		return IKRigDefinition->ConstraintDefinitions->ConstraintProfiles.Find(SearchProfileName);
	}

	return nullptr;
}

void UIKRigController::EnsureUniqueConstraintName(FName& InOutName)
{
	int32 Index = 1;

	TArray<FName> ConstraintNames;

	GetConstraintNames(ConstraintNames);

	FString NewName = InOutName.ToString();
	while (ConstraintNames.Contains(FName(*NewName)))
	{
		NewName = FString::Format(TEXT("{0}_{1}"), { InOutName.ToString(), Index++ });
	}

	InOutName = FName(*NewName);
}

UIKRigConstraint* UIKRigController::AddConstraint(TSubclassOf<UIKRigConstraint> NewConstraintType, FName& InOutNewName, FName InProfile /*= NAME_None*/)
{
	FIKRigConstraintProfile* Profile = GetConstraintProfile(InProfile);
	if (Profile)
	{
		FScopedTransaction Transaction(LOCTEXT("AddConstraint_Label", "Add Constraint"));
		IKRigDefinition->Modify();

		EnsureUniqueConstraintName(InOutNewName);
		UIKRigConstraint* NewRigConstraint = NewObject<UIKRigConstraint>(IKRigDefinition, NewConstraintType, InOutNewName);
		if (NewRigConstraint)
		{
		
			Profile->Constraints.Add(NewRigConstraint->GetFName(), NewRigConstraint);
			return NewRigConstraint;
		}
	}
	return nullptr;	
}

UIKRigConstraint* UIKRigController::GetConstraint(const FName& InProfileName, const FName& InName) const
{
	FIKRigConstraintProfile* Profile = GetConstraintProfile(InProfileName);
	if (Profile)
	{
		UIKRigConstraint** RigConstraint = Profile->Constraints.Find(InName);
		if (RigConstraint)
		{
			return *RigConstraint;
		}
	}

	return nullptr;
}

bool UIKRigController::RemoveConstraint(const FName& InConstraintName)
{
	// remove this in all profile
	if (IKRigDefinition)
	{
		FScopedTransaction Transaction(LOCTEXT("AddConstraint_Label", "Add Constraint"));
		IKRigDefinition->Modify();

		for (auto Iter = IKRigDefinition->ConstraintDefinitions->ConstraintProfiles.CreateIterator(); Iter; ++Iter)
		{
			FIKRigConstraintProfile& Profile = Iter.Value();
			for (auto InnerIter = Profile.Constraints.CreateIterator(); InnerIter; ++InnerIter)
			{
				// possible the constraint name may not be there
				// what this means, when we rename, we have to rename all of them
				Profile.Constraints.Remove(InConstraintName);
			}
		}
		// remove return?
		return true;
	}

	return false;
}

void UIKRigController::GetConstraintProfileNames(TArray<FName>& OutProfileNames) const
{
	if (IKRigDefinition)
	{
		OutProfileNames.Reset();
		IKRigDefinition->ConstraintDefinitions->ConstraintProfiles.GenerateKeyArray(OutProfileNames);
	}
}

void UIKRigController::GetConstraintNames(TArray<FName>& OutConstraintNames) const
{
	// still can be null if IKRigDefinition is nullptr
	FIKRigConstraintProfile* DefaultProfile = GetConstraintProfile(NAME_None);
	if (DefaultProfile)
	{
		OutConstraintNames.Reset();
		DefaultProfile->Constraints.GenerateKeyArray(OutConstraintNames);
	}
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

#undef LOCTEXT_NAMESPACE