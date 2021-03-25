// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IKRigSolver.h"
#include "TransformSolver.generated.h"


UCLASS(EditInlineNew)
class IKRIG_API UTransformSolver : public UIKRigSolver
{
	GENERATED_BODY()

	UTransformSolver();

public:

	UPROPERTY(EditAnywhere, Category = "Solver")
	bool bEnablePosition = true;

	UPROPERTY(EditAnywhere, Category = "Solver")
	bool bEnableRotation = true;

	UPROPERTY(EditAnywhere, Category = "Solver")
	FIKRigEffectorGoal Effector;

protected:
	
	virtual void Initialize(const FIKRigSkeleton& IKRigSkeleton) override;
	virtual void Solve(
		FIKRigSkeleton& IKRigSkeleton, 
		const FIKRigGoalContainer& Goals,
		FControlRigDrawInterface* InOutDrawInterface) override;
	virtual void CollectGoalNames(TSet<FName>& OutGoals) const override;

private:
	
	int32 BoneIndex;
};

