// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "IKRigBoneSetting.h"
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

	UPROPERTY()
	TSoftObjectPtr<UObject> SourceAsset;

	UPROPERTY(EditAnywhere, instanced, Category = BoneSettings)
	TArray<UIKRigBoneSetting*> BoneSettings;

	UPROPERTY(EditAnywhere, Category = BoneSettings)
	TArray<FName> ExcludedBones;

public:

	void EnsureCreateUniqueGoalName(FName& InOutGoal) const;

	const TMap<FName, FIKRigGoal>& GetGoals() const
	{
		return Goals;
	}

	TArray<FName> GetGoalNames() const
	{
		TArray<FName> NameArray;
		Goals.GenerateKeyArray(NameArray);
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
	
	/**
	* Parent/child relationships stored separately from skeleton-specific transforms
	*/
	UPROPERTY(VisibleAnywhere, Category = "Skeleton")
	FIKRigHierarchy Hierarchy;

	/**
	* These are the default transforms of the reference pose.
	* 
	* Transforms are separated from the hierarchical representation
	* 
	* These transforms are treated as the common base, from which all
	* retargeting operations will be compared against.
	* 
	* Do no cache these values inside a Solver. Rely instead, only on the 
	* transforms passed to the Solver::Init()
	*/
	UPROPERTY(VisibleAnywhere, Category = "Skeleton")
	TArray<FTransform> RefPoseTransforms;

	/**
	* Stack of solvers, of varying types, executed in serial fashion where
	* output of prior solve is input to the next.
	*/
	UPROPERTY(EditAnywhere, instanced, Category = "Solvers")
	TArray<UIKRigSolver*> Solvers;

	/** Goals (ie, named transforms) are mapped to solver effectors by name. */
	UPROPERTY(EditAnywhere, Category = "Goals")
	TMap<FName, FIKRigGoal> Goals;

	// BEGIN UObject functions 
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	// END UObject functions

#if WITH_EDITOR
	// https ://drive.google.com/file/d/1EO0Ijojx-0jommdHuZv1JYxfCdoFLd44/view

	/* Goal operators */
	void UpdateGoal();

	/** hierarchy modifiers */
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

