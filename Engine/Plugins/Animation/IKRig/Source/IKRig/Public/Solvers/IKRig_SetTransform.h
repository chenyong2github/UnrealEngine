// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IKRigSolver.h"

#include "IKRig_SetTransform.generated.h"


UCLASS(EditInlineNew)
class IKRIG_API UIKRig_SetTransform : public UIKRigSolver
{
	GENERATED_BODY()

public:
	
	UPROPERTY(EditAnywhere, Category = "Solver")
	FIKRigEffectorGoal Effector;
	
	UPROPERTY(EditAnywhere, Category = "Solver")
	bool bEnablePosition = true;

	UPROPERTY(EditAnywhere, Category = "Solver")
	bool bEnableRotation = true;

protected:
	
	virtual void Initialize(const FIKRigSkeleton& IKRigSkeleton) override;
	virtual void Solve(
		FIKRigSkeleton& IKRigSkeleton, 
		const FIKRigGoalContainer& Goals,
		FControlRigDrawInterface* InOutDrawInterface) override;
	virtual void AddGoalsInSolver(TArray<FIKRigEffectorGoal>& OutGoals) const override;

private:
	
	int32 BoneIndex;
};

