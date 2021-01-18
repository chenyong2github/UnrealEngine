// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IKRigSolver.h"
#include "TransformSolver.generated.h"


UCLASS()
class IKRIG_API UTransformSolver : public UIKRigSolver
{
	GENERATED_BODY()

public:

	UTransformSolver();

	UPROPERTY(EditAnywhere, Category = "Solver")
	bool bEnablePosition = true;

	UPROPERTY(EditAnywhere, Category = "Solver")
	bool bEnableRotation = true;

	UPROPERTY(EditAnywhere, Category = "Solver")
	FIKRigEffector Effector;

protected:
	
	virtual void Init(const FIKRigTransforms& InGlobalTransform) override;
	virtual void Solve(
		FIKRigTransforms& InOutGlobalTransform, 
		const FIKRigGoalContainer& Goals,
		FControlRigDrawInterface* InOutDrawInterface) override;
	virtual bool IsSolverActive() const override;

private:
#if WITH_EDITOR
	virtual void UpdateEffectors() override;

	// UObject interface
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
	// UObject interface
#endif // WITH_EDITOR

	const FString TransformTargetName;
};

