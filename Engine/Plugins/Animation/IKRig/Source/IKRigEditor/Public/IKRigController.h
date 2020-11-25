// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * IKRigController
 *
 * Mutates IKRig asset side
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "IKRigDefinition.h"
#include "IKRigController.generated.h"

// DELEGATE for solver chnage
// DELEGATE for hierarchy changes
// DELEGATE for profile changes
// DELEGATE for constraint changes
// DELEGATE for goals

class UIKRigSolverDefinition;
class UIKRigConstraint;
struct FIKRigHierarchy;
struct FReferenceSkeleton;


UCLASS(config = Engine, hidecategories = UObject, BlueprintType)
class IKRIGEDITOR_API UIKRigController : public UObject
{
	GENERATED_BODY()

public:
	// IKRigDefinition set up
	void SetIKRigDefinition(UIKRigDefinition* InIKRigDefinition);

	// hierarchy operators
	void SetSkeleton(const FReferenceSkeleton& InSkeleton);
	// const hierarchy getter
	const FIKRigHierarchy* GetHierarchy() const
	{
		return (IKRigDefinition)? &IKRigDefinition->Hierarchy : nullptr;
	}

	const FIKRigTransform* GetReferenceTransform() const
	{
		return (IKRigDefinition) ? &IKRigDefinition->ReferencePose : nullptr;
	}

	bool AddBone(const FName& InName, const FName& InParent, const FTransform& InGlobalTransform);
	bool RemoveBone(const FName& InName);
	bool RenameBone(const FName& InOldName, const FName& InNewName);
	bool ReparentBone(const FName& InName, const FName& InNewParent);
	void ResetHierarchy();

	// todo: more APIs for hierarchy operators

	// solver operators
	UIKRigSolverDefinition* AddSolver(TSubclassOf<UIKRigSolverDefinition> InIKRigSolverDefinitionClass);
	int32 GetTotalSolverCount() const;
	UIKRigSolverDefinition* GetSolver(int32 Index) const;
	void RemoveSolver(UIKRigSolverDefinition* SolverToDelete);
	void AutoConfigure(UIKRigSolverDefinition* SolverDef);
	bool CanAutoConfigure(UIKRigSolverDefinition* SolverDef) const;

	// constraint operators
	// create new profile
	void CreateNewProfile(FName& InNewProfileName);
	bool RemoveConstraintProfile(const FName& InProfileName);
	void RenameProfile(FName InCurrentProfileName, FName& InNewProfileName);

	UIKRigConstraint* AddConstraint(TSubclassOf<UIKRigConstraint> NewConstraintType, FName& InOutNewName, FName InProfile=NAME_None);
	UIKRigConstraint* GetConstraint(const FName& InProfileName, const FName& InName) const;
	bool RemoveConstraint(const FName& InConstraintName);

	void GetConstraintProfileNames(TArray<FName>& OutProfileNames) const;
	void GetConstraintNames(TArray<FName>& OutConstraintNames) const;

	// goal operators
	void QueryGoals(TArray<FName>& OutGoals) const;
	void RenameGoal(const FName& OldName, const FName& NewName);

	// this is to modify default value
	FIKRigGoal* GetGoal(const FName& InGoalName);
	const FIKRigGoal* GetGoal(const FName& InGoalName) const;

	// BEGIN UObject
	virtual void BeginDestroy() override;
	// END UObject
private:
	UIKRigDefinition* IKRigDefinition = nullptr;

	bool ValidateSolver(UIKRigSolverDefinition* const SolverDef) const;
	void UpdateGoal();

	void InitializeIKRigSolverDefinition(UIKRigSolverDefinition* SolverDef);
	void UninitializeIKRigSolverDefinition(UIKRigSolverDefinition* SolverDef);

	TMap<UIKRigSolverDefinition*, FDelegateHandle> SolverDelegateHandles;
};

