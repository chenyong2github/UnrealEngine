// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IKRigSolver.h"

#include "Core/PBIKSolver.h"
#include "PBIK_Shared.h"

#include "IKRig_PBIK.generated.h"


USTRUCT(BlueprintType)
struct FPBIKRigEffector
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = FFBIKRigEffector)
	FIKRigEffectorGoal Target;

	UPROPERTY(EditAnywhere, Category = FFBIKRigEffector)
	float OffsetAlpha = 1.0f;

	UPROPERTY(EditAnywhere, Category = FFBIKRigEffector)
	float StrengthAlpha = 1.0f;
};


UCLASS(EditInlineNew)
class PBIK_API UIKRig_PBIK : public UIKRigSolver
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Settings)
	FName Root;

	UPROPERTY(EditAnywhere, Category = Settings)
	TArray<FPBIKRigEffector> Effectors;
	UPROPERTY(transient)
	TArray<int32> EffectorSolverIndices;

	UPROPERTY(EditAnywhere, Category = Settings)
	TArray<FPBIKBoneSetting> BoneSettings;

	UPROPERTY(EditAnywhere, Category = Settings)
	FPBIKSolverSettings SolverSettings;

protected:
	virtual void Init(const FIKRigTransforms& InGlobalTransforms) override;
	virtual void Solve(
		FIKRigTransforms& InOutGlobalTransforms,
		const FIKRigGoalContainer& Goals,
		FControlRigDrawInterface* InOutDrawInterface) override;
	virtual void CollectGoalNames(TSet<FName>& OutGoals) const override;
};