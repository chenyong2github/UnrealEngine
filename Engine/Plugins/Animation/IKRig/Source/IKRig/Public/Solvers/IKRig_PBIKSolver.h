// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "IKRigSolver.h"
#include "Core/PBIKSolver.h"
#include "PBIK_Shared.h"

#include "IKRig_PBIKSolver.generated.h"

USTRUCT()
struct FIKRig_PBIKEffector
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Settings)
	FIKRigEffectorGoal Goal;

	UPROPERTY(EditAnywhere, Category = Settings)
	float StrengthAlpha = 1.0f;

	UPROPERTY(Transient)
	int32 IndexInSolver = -1;
};

UCLASS(EditInlineNew, config = Engine, hidecategories = UObject)
class IKRIG_API UIKRigPBIKSolver : public UIKRigSolver
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = Settings)
	FName RootBone;
	
	UPROPERTY(EditAnywhere, Category = Settings)
	TArray<FIKRig_PBIKEffector> Effectors;

	UPROPERTY(EditAnywhere, Category = Settings)
	TArray<FPBIKBoneSetting> BoneSettings;

	UPROPERTY(EditAnywhere, Category = Settings)
	FPBIKSolverSettings Settings;

	virtual void Initialize(const FIKRigSkeleton& IKRigSkeleton) override;
	virtual void Solve(
		FIKRigSkeleton& IKRigSkeleton,
		const FIKRigGoalContainer& Goals,
		FControlRigDrawInterface* InOutDrawInterface) override;
	virtual void AddGoalsInSolver(TArray<FIKRigEffectorGoal>& OutGoals) const override;

private:

	FPBIKSolver Solver;
};

