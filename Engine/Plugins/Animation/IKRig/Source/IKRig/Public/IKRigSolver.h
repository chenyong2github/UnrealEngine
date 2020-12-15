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

struct FIKRigEffector;
struct FIKRigTarget;
struct FIKRigTransform;
struct FIKRigTransformModifier;
class UIKRigSolverDefinition;
struct FControlRigDrawInterface;

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
	void Init(UIKRigSolverDefinition* InSolverDefinition, const FIKRigTransformModifier& TransformModifier, FIKRigTransformGetter InRefPoseGetter, FIKRigGoalGetter InGoalGetter/*, FSolveConstraint& InConstraintHandler*/);

	// input : goal getter or goals
	// output : modified pose - GlobalTransforms
	// or use SolverInternal function
	void Solve(FIKRigTransformModifier& InOutGlobalTransform, FControlRigDrawInterface* InOutDrawInterface);

protected:
	// during we don't mutate bone transform, but you can read it
	virtual void InitInternal(const FIKRigTransformModifier& InGlobalTransform) {};
	virtual void SolveInternal(FIKRigTransformModifier& InOutGlobalTransform, FControlRigDrawInterface* InOutDrawInterface) {};
	virtual bool IsSolverActive() const;

	bool GetEffectorTarget(const FIKRigEffector& InEffector, FIKRigTarget& OutTarget) const;
	const FIKRigTransform& GetReferencePose() const;
private:
	// delegate
	FIKRigTransformGetter RefPoseGetter;
	FIKRigGoalGetter GoalGetter;
	
//	FSolveConstraint ConstraintHandler;
};

