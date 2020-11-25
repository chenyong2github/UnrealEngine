// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Contains IKRig Solver Definition 
 *
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "IKRigSolver.generated.h"

struct FIKRigTarget;
struct FIKRigTransform;
struct FIKRigTransformModifier;
class UIKRigSolverDefinition;

// run time processor 
UCLASS(Abstract, hidecategories = UObject)
class IKRIG_API UIKRigSolver : public UObject
{
	GENERATED_BODY()

public: 
	/** Required delegates to run this solver */
	DECLARE_DELEGATE_RetVal(const FIKRigTransform&, FIKRigTransformGetter);
	DECLARE_DELEGATE_RetVal_TwoParams(bool, FIKRigGoalGetter, const FName& /*InGoalName*/, FIKRigTarget& /*OutTarget*/);
//	DECLARE_DELEGATE_TwoParams(FSolveConstraint, TArray<FName>& /*Constraint*/, FIKRigTransform& /*CurrentTransform*/);

protected:

	UPROPERTY(VisibleAnywhere, Category = "Definition")
	UIKRigSolverDefinition* SolverDefinition;

public:
	UPROPERTY(EditAnywhere, Category = "Definition")
	bool bEnabled = true;

	// input hierarchy and ref pose? 
	void Init(UIKRigSolverDefinition* InSolverDefinition, FIKRigTransformGetter InRefPoseGetter, FIKRigGoalGetter InGoalGetter/*, FSolveConstraint& InConstraintHandler*/);

	// input : goal getter or goals
	// output : modified pose - GlobalTransforms
	// or use SolverInternal function
	void Solve(FIKRigTransformModifier& InOutGlobalTransform);

protected:
	virtual void InitInternal() {};
	virtual void SolveInternal(FIKRigTransformModifier& InOutGlobalTransform) {};
	virtual bool IsSolverActive() const;

	bool GetTaskTarget(const FName& TaskName, FIKRigTarget& OutTarget) const;
	const FIKRigTransform& GetReferencePose() const;
private:
	// delegate
	FIKRigTransformGetter RefPoseGetter;
	FIKRigGoalGetter GoalGetter;
	
//	FSolveConstraint ConstraintHandler;
};

