// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Contains IKRig Definition runtime code. This takes IKRigDefinition asset as an input
 *
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "IKRigDataTypes.h"
#include "IKRigProcessor.generated.h"


class UIKRigConstraintSolver;
class UIKRigSolver;

UCLASS(BlueprintType)
class IKRIG_API UIKRigProcessor : public UObject
{
	GENERATED_BODY()

private:
	// skeleton
	// this just is a relationship. Parent/child relationship
	// sorted by parent to children
	UPROPERTY(EditAnywhere, Category = "RigDefinition")
	UIKRigDefinition* RigDefinition = nullptr;

	// reference transform
	UPROPERTY(transient)
	FIKRigTransform ReferenceTransform;

	// solvers
	UPROPERTY(transient)
	TArray<UIKRigSolver*> Solvers;

	// constraint solver
	UPROPERTY(transient)
	UIKRigConstraintSolver* CostraintSolver;

	// goals - copy from RigDefinition
	// modified in runtime
	UPROPERTY(transient)
	TMap<FName, FIKRigGoal> IKGoals;

	FIKRigTransformModifier TransformModifier;

	bool bInitialized = false;

public: 

	// IKRigProcessor implementation functions
	// Set IKRigDefinition 
	// if bInitialize is true, it will use Default Ref transform of IKRigDefinition to initialize
	void SetIKRigDefinition(UIKRigDefinition* InRigDefinition, bool bInitialize = false);
	void Initialize(const FIKRigTransform& InRefTransform);
	void Reinitialize();
	void Terminate();
	void Solve();

	// update goal functions
	void SetGoalPosition(const FName& GoalName, const FVector& InPosition);
	void SetGoalRotation(const FName& GoalName, const FRotator& InRotation);
	void SetGoalTarget(const FName& GoalName, const FIKRigTarget& InTarget);
	void GetGoals(TArray<FName>& OutNames) const;
	FIKRigTransformModifier& GetIKRigTransformModifier();
	const FIKRigHierarchy* GetHierarchy() const;

	void ResetToRefPose();
private:
	FIKRigTarget* FindGoal(const FName& GoalName);

	// delegate functions
	bool GoalGetter(const FName& InGoalName, FIKRigTarget& OutTarget);
	const FIKRigTransform& GetRefPoseGetter();
};

