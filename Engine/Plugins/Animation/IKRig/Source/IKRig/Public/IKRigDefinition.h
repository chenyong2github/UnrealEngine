// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "IKRigConstraint.h"
#include "IKRigDataTypes.h"
#include "IKRigHierarchy.h"
#include "IKRigDefinition.generated.h"

class UIKRigSolver;

UCLASS(Blueprintable)
class IKRIG_API UIKRigDefinition : public UObject
{
	GENERATED_BODY()

public:

	UIKRigDefinition();

	/** Source Asset imported */
	UPROPERTY()
	TSoftObjectPtr<UObject> SourceAsset;

	UPROPERTY(EditAnywhere, instanced, Category = Constraints)
	TArray<UIKRigConstraint*> Constraints;

public:

	// ensure the goal name is unique
	void EnsureCreateUniqueGoalName(FName& InOutGoal) const;

	// goal related APIs
	const TMap<FName, FIKRigGoal>& GetGoals() const
	{
		return IKGoals;
	}

	TArray<FName> GetGoalsNames() const
	{
		TArray<FName> NameArray;
		IKGoals.GenerateKeyArray(NameArray);
		return NameArray;
	}

	const TArray<FTransform>& GetReferencePose() const
	{
		return RefPoseTransforms;
	}

	const TArray<UIKRigSolver*>& GetSolvers() const 
	{
		return Solvers;
	}

	const FIKRigHierarchy& GetHierarchy() const
	{
		return Hierarchy;
	}
private:
	/*********** Skeleton *********************/

	/* Hierarchy
	 * 
	 * This just is a relationship. Parent/child relationship
	 * sorted by parent to children
	*/
	UPROPERTY(VisibleAnywhere, Category = "Hierarchy")
	FIKRigHierarchy Hierarchy;

	// this is default transform of reference pose
	// the separate with Hierarchy is for retargeting
	// during retargeting, we want to ensure it works with different transform
	// (this can be replaced during runtime for a retarget)
	// do not cache or rely on this value inside of solver
	// use the incoming value when initialize
	// this has to match with Hierarchy data index
	// IKRigDefinition interface handles the integrity of (this and Hierarchy)
	// Use IKRigController for APIs
	UPROPERTY(VisibleAnywhere, Category = "Hierarchy")
	TArray<FTransform> RefPoseTransforms;

	// stack of solvers, executed in order
	UPROPERTY(EditAnywhere, instanced, Category = "Solver")
	TArray<UIKRigSolver*> Solvers;

private:

	/*********** Goals with Default Value******************/
	// goals data
	// this is cached by "Unique Internal Name" here
	// where in the runtime, it will cache by "Display Name"
	// as that is what's searched to set the value by game
	// but during editing time, we use internal name 
	// as identifier
	UPROPERTY(EditAnywhere, Category = "Goals")
	TMap<FName, FIKRigGoal> IKGoals;

	// BEGIN UObject functions 
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	// END UObject functions

#if WITH_EDITOR
	// https ://drive.google.com/file/d/1EO0Ijojx-0jommdHuZv1JYxfCdoFLd44/view

	/* Goal operators */
	void UpdateGoal();

	/** Hierarchy modifires */
	// add a bone. Return false if it already has conflict name or parent is not found
	bool AddBone(const FName& InName, const FName& InParent, const FTransform& InGlobalTransform);
	// remove a bone. Returns false if it is not found
	// if it has a children, it will remove all the children with it
	bool RemoveBone(const FName& InName);
	// rename just change the name
	// return false if not found
	bool RenameBone(const FName& InOldName, const FName& InNewName);
	// reparent to InNewParent
	// returns false 
	//	if not found - either InName or InParent
	//	Or if InNewParent is invalid (i.e. it's a child of InName)
	bool ReparentBone(const FName& InName, const FName& InNewParent);
	// reset 
	void ResetHierarchy();

	// ensure it's sorted from parent to children
	void EnsureSortedCorrectly(bool bReSortIfNeeded=false);
	void Sanitize(); // hopefully we can get rid of this

	// TODO: delegate for hierarchy changes
	DECLARE_MULTICAST_DELEGATE_OneParam(FBoneAdded, FName /*BoneName*/);
	DECLARE_MULTICAST_DELEGATE_OneParam(FBoneRemoved, FName /*BoneName*/);
	DECLARE_MULTICAST_DELEGATE_TwoParams(FBoneRenamed, FName /*OldName*/, FName /*NewName*/);
	DECLARE_MULTICAST_DELEGATE_TwoParams(FBoneReparented, FName /*Name*/, FName /*ParentName*/);

	DECLARE_MULTICAST_DELEGATE_OneParam(FIKRigDefinitionBeginDestroy, UIKRigDefinition* Definition);

	FBoneAdded BoneAddedDelegate;
	FBoneRemoved BoneRemovedDelegate;
	FBoneRenamed BoneRenamedDelegate;
	FBoneReparented BoneReparentedDelegate;
	FIKRigDefinitionBeginDestroy IKRigDefinitionBeginDestroy;

#endif // WITH_EDITOR

	friend class UIKRigController;
};

