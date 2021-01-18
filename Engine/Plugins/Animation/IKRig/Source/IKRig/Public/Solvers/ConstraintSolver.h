// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IKRigSolver.h"
#include "ConstraintSolver.generated.h"

class UIKRigBoneSettings;


UCLASS(config = Engine, hidecategories = UObject)
class IKRIG_API UIKRigConstraintSolver : public UIKRigSolver
{
	GENERATED_BODY()

	//DECLARE_DELEGATE_TwoParams(FIKRigQueryConstraint, const FName& InConstraintName, UIKRigConstraint& OutConstraint);


public: 

	virtual void Init(const FIKRigTransforms& InGlobalTransform) override;
	virtual void Solve(
		FIKRigTransforms& InOutGlobalTransform,
		const FIKRigGoalContainer& Goals,
		FControlRigDrawInterface* InOutDrawInterface) override;

	// this can be utilized for just subset of constraints
	//void SolverConstraints(const TArray<FName>& ConstraintsList, FIKRigTransformModifier& InOutGlobalTransform, FControlRigDrawInterface* InOutDrawInterface);

	// register/unregister query function
	//void RegisterQueryConstraintHandler(const FIKRigQueryConstraint& InQueryConstraintHandler);
	//void UnregisterQueryConstraintHandler();

	//void SetConstraintDefinition(UIKRigConstraintDefinition* InConstraintDefinition);

private:
	// delegate
	//FIKRigQueryConstraint QueryConstraintHandler;

	// current active profile
	//UPROPERTY(transient)
	//FName ActiveProfile;

	// instanced information you can mutate from ConstraintDefinition
	// we want uproperty, so that it doesn't GC-ed
	//UPROPERTY(transient)
	//TMap<FName, FIKRigConstraintProfile> ConstraintProfiles;
};

