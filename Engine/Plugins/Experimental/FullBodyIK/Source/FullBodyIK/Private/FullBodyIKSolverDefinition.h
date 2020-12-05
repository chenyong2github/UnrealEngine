// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Contains Fullbody IK solver Definition
 *
 */

#pragma once

#include "IKRigSolverDefinition.h"
#include "FBIKShared.h"
#include "FBIKConstraintOption.h"
#include "FullBodyIKSolverDefinition.generated.h"

USTRUCT()
struct FFBIKRigEffector
{
	GENERATED_BODY()
	/**
	 * The last item in the chain to solve - the effector
	 */
	UPROPERTY(EditAnywhere, Category = FFBIKRigEffector)
	FIKRigEffector Target;

	UPROPERTY(EditAnywhere, Category = FFBIKRigEffector)
	int32  PositionDepth = 1000;
	
	UPROPERTY(EditAnywhere, Category = FFBIKRigEffector)
	int32  RotationDepth = 1000;

	/*
	 * Clamps the total length to target by this scale for each iteration 
	 * This helps to stabilize solver to reduce singularity by avoiding to try to reach target too far. 
	 */
	UPROPERTY(EditAnywhere, Category = FFBIKRigEffector)
	float	Pull = 0.f;

	FFBIKRigEffector()
	{
	}
};

UCLASS(BlueprintType)
class FULLBODYIK_API UFullBodyIKSolverDefinition : public UIKRigSolverDefinition
{
	GENERATED_BODY()

public:
	UFullBodyIKSolverDefinition();

	UPROPERTY(EditAnywhere, Category = Effectors)
	FName Root;

	UPROPERTY(EditAnywhere, Category = Effectors)
	TArray<FFBIKRigEffector> Effectors;
//	UPROPERTY(EditAnywhere, Category = Constraint)
//	TArray<FFBIKConstraintOption> Constraints;

	UPROPERTY(EditAnywhere, Category = Solver)
	FSolverInput	SolverProperty;

	UPROPERTY(EditAnywhere, Category = Motion)
	FMotionProcessInput MotionProperty;

private:
#if WITH_EDITOR
	virtual void UpdateEffectors() override;

	// UObject interface
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
	// UObject interface
#endif // WITH_EDITOR

public:
	static const FName EffectorTargetPrefix;
};

