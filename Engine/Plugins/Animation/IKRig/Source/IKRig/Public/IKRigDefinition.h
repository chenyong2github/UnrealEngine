// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "IKRigBoneSetting.h"
#include "IKRigHierarchy.h"
#include "IKRigDataTypes.h"

#include "IKRigDefinition.generated.h"

class UIKRigSolver;

UCLASS(Blueprintable)
class IKRIG_API UIKRigDefinition : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY()
	TSoftObjectPtr<UObject> SourceAsset;

	/** parent/child relationships stored separately from skeleton-specific transforms */
	UPROPERTY(VisibleAnywhere, Category = "Skeleton")
	FIKRigHierarchy Hierarchy;

	/** default transforms of the reference pose skeleton */
	UPROPERTY(VisibleAnywhere, Category = "Skeleton")
	TArray<FTransform> RefPoseTransforms;

	UPROPERTY(EditAnywhere, instanced, Category = BoneSettings)
	TArray<UIKRigBoneSetting*> BoneSettings;

	UPROPERTY(EditAnywhere, Category = BoneSettings)
	TArray<FName> ExcludedBones;

	/** stack of solvers, of varying types, executed in serial fashion where
	output of prior solve is input to the next */
	UPROPERTY(EditAnywhere, instanced, Category = "Solvers")
	TArray<UIKRigSolver*> Solvers;

	void GetGoalNamesFromSolvers(TArray<FName>& OutGoalNames) const;

private:

	// BEGIN UObject functions 
	virtual void PostLoad() override;
	// END UObject functions

#if WITH_EDITOR

	//** add a bone. Return false if it already has conflict name or parent is not found */
	bool AddBone(const FName& InName, const FName& InParent, const FTransform& InGlobalTransform);
	//** reset skeleton data */
	void ResetHierarchy();
	// ensure it's sorted from parent to children
	void EnsureSortedCorrectly(bool bReSortIfNeeded=false);

#endif // WITH_EDITOR

	friend class UIKRigController;
};

