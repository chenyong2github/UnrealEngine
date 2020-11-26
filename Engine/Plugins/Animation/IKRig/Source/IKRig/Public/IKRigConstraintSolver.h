// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Contains IKRig Constraint Solver Definition 
 *
 */

#pragma once

#include "IKRigSolver.h"
#include "IKRigConstraintSolver.generated.h"

class UIKRigConstraint;

// run time processor 
UCLASS(config = Engine, hidecategories = UObject)
class IKRIG_API UIKRigConstraintSolver : public UIKRigSolver
{
	GENERATED_BODY()

	DECLARE_DELEGATE_TwoParams(FIKRigQueryConstraint, const FName& InConstraintName, UIKRigConstraint& OutConstraint);

public: 
	// input hierarchy and ref pose? 
	//virtual void Init(UIKRigSolverDefinition* InSolverDefinition, const FIKRigTransform& InRefPose, FIKRigGoalGetter& InGoalGetter, FSolveConstraint& InConstraintHandler) override;

	// input : goal getter or goals
	// output : modified pose - GlobalTransforms
	//virtual void Solve(FIKRigTransform& OutGlobalTransform) override;

	// register/unregister query function
	void RegisterQueryConstraintHandler(const FIKRigQueryConstraint& InQueryConstraintHandler);
	void UnregisterQueryConstraintHandler();

private:
	// delegate
	FIKRigQueryConstraint QueryConstraintHandler;
};

