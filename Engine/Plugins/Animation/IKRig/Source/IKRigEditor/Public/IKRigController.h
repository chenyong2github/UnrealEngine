// Copyright Epic Games, Inc. All Rights Reserved.

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
struct FIKRigConstraintProfile;


UCLASS(config = Engine, hidecategories = UObject, BlueprintType)
class IKRIGEDITOR_API UIKRigController : public UObject
{
	GENERATED_BODY()

public:

	void SetSkeleton(const FReferenceSkeleton& InSkeleton);
	const FIKRigHierarchy* GetHierarchy() const
	{
		return (IKRigDefinition)? &IKRigDefinition->Hierarchy : nullptr;
	}

	const TArray<FTransform>& GetRefPoseTransforms() const
	{
		if (IKRigDefinition)
		{
			return IKRigDefinition->RefPoseTransforms;
		}

		static TArray<FTransform> Dummy;
		return Dummy;
	}

	bool AddBone(const FName& InName, const FName& InParent, const FTransform& InGlobalTransform);
	bool RemoveBone(const FName& InName);
	bool RenameBone(const FName& InOldName, const FName& InNewName);
	bool ReparentBone(const FName& InName, const FName& InNewParent);
	void ResetHierarchy();

	// todo: more APIs for hierarchy operators

	// solver operators
	UIKRigSolver* AddSolver(TSubclassOf<UIKRigSolver> InSolverClass);
	void RemoveSolver(UIKRigSolver* SolverToDelete);
	UIKRigSolver* GetSolver(int32 Index) const;
	int32 GetNumSolvers() const;

	// constraints
	UIKRigConstraint* AddConstraint(TSubclassOf<UIKRigConstraint> NewConstraintType);

	// goal operators
	void QueryGoals(TArray<FName>& OutGoals) const;
	// why do we need rename? @todo: remove - maybe in the future when we want a tool that does rename automatically this could be useful, but for now it's not really
	// goals are collected from solvers, so they could change name, and if it doesn't exists, it doesn't exist
	void RenameGoal(const FName& OldName, const FName& NewName);

	FName GetGoalName(UIKRigSolver* InSolver, const FIKRigEffector& InEffector);
	void SetGoalName(UIKRigSolver* InSolver, const FIKRigEffector& InEffector, const FName& NewGoalName);

	// this is to modify default value
	FIKRigGoal* GetGoal(const FName& InGoalName);
	const FIKRigGoal* GetGoal(const FName& InGoalName) const;

	// BEGIN UObject
	virtual void BeginDestroy() override;
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	// END UObject

	// Controller getter
	// Use this interface to get controller 
	static UIKRigController* GetControllerByRigDefinition(UIKRigDefinition* InIKRigDefinition);

	// delegates
	DECLARE_MULTICAST_DELEGATE(FGoalModified);

	FGoalModified OnGoalModified;
private:
	UPROPERTY(transient)
	UIKRigDefinition* IKRigDefinition = nullptr;

	bool ValidateSolver(UIKRigSolver* const Solver) const;
	void UpdateGoal();

	FIKRigConstraintProfile* GetConstraintProfile(const FName& InProfileName) const;

	void InitializeSolver(UIKRigSolver* Solver);
	void UninitializeSolver(UIKRigSolver* Solver);

	TMap<UIKRigSolver*, FDelegateHandle> SolverDelegateHandles;

	static TMap<UIKRigDefinition*, UIKRigController*> DefinitionToControllerMap;

	void SetIKRigDefinition(UIKRigDefinition* InIKRigDefinition);

	void EnsureUniqueConstraintName(FName& InOutName);

	// called by IKRigDefinition when it's begin destroy
	static void RemoveControllerByRigDefinition(UIKRigDefinition* InIKRigDefinition);

	// friend class 
	friend class UIKRigDefinition;
};

   