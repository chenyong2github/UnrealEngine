// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IKRigSolver.h"
#include "ConstraintSolver.generated.h"


UCLASS(EditInlineNew, config = Engine, hidecategories = UObject)
class IKRIG_API UIKRigConstraintSolver : public UIKRigSolver
{
	GENERATED_BODY()

public: 

	virtual void Init(const FIKRigTransforms& InGlobalTransform) override;
	virtual void Solve(
		FIKRigTransforms& InOutGlobalTransform,
		const FIKRigGoalContainer& Goals,
		FControlRigDrawInterface* InOutDrawInterface) override;
	virtual void CollectGoalNames(TSet<FName>& OutGoals) const override;
};

