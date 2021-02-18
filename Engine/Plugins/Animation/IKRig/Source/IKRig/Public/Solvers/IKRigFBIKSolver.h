// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Contains Transform Solver Execution
 *
 */

#pragma once

#include "IKRigSolver.h"
#include "FBIKShared.h"
#include "FBIKConstraintOption.h"
#include "FBIKDebugOption.h"
#include "IKRigFBIKSolver.generated.h"

struct FFBIKLinkData;

USTRUCT()
struct FFBIKRigEffector
{
	GENERATED_BODY()

	/**
	* The last item in the chain to solve - the effector
	*/
	UPROPERTY(EditAnywhere, Category = FFBIKRigEffector)
	FIKRigEffectorGoal Target;

	UPROPERTY(EditAnywhere, Category = FFBIKRigEffector)
	int32  PositionDepth = 1000;

	UPROPERTY(EditAnywhere, Category = FFBIKRigEffector)
	int32  RotationDepth = 1000;

	/*
	 * Clamps the total length to target by this scale for each iteration
	 * This helps to stabilize solver to reduce singularity by avoiding to try to reach target too far.
	 */
	UPROPERTY(EditAnywhere, Category = FFBIKRigEffector)
	float Pull = 0.f;

	FFBIKRigEffector()
	{
	}
};

 // run time for UFullBodyIKSolverDefinition
UCLASS(EditInlineNew)
class IKRIG_API UIKRigFBIKSolver : public UIKRigSolver
{
	GENERATED_BODY()

public:
	
	UPROPERTY(EditAnywhere, Category = Effectors)
	FName Root;

	UPROPERTY(EditAnywhere, Category = Effectors)
	TArray<FFBIKRigEffector> Effectors;

	UPROPERTY(EditAnywhere, Category = Solver)
	FSolverInput SolverProperty;

	UPROPERTY(EditAnywhere, Category = Motion)
	FMotionProcessInput MotionProperty;

	UPROPERTY(EditAnywhere, Category = Solver)
	FFBIKDebugOption DebugOption;

	UIKRigFBIKSolver();

private:

	/** list of Link Data for solvers - joints */
	TArray<FFBIKLinkData> LinkData;
	/** Effector Targets - search key is LinkData Index */
	TMap<int32, FFBIKEffectorTarget> EffectorTargets; 
	/** End Effector Link Indices - EndEffector index to LinkData index*/
	TArray<int32> EffectorLinkIndices;
	/** Map from LinkData index to Hierarchy Index*/
	TMap<int32, int32> LinkDataToHierarchyIndices;
	/** Map from Rig Hierarchy Index to LinkData index*/
	TMap<int32, int32> HierarchyToLinkDataMap;
	/** Constraints data */
	TArray<ConstraintType> InternalConstraints;
	/* Current Solver */
	FJacobianSolver_FullbodyIK IKSolver;
	/** Debug Data */
	TArray<FJacobianDebugData> DebugData;

protected:
	virtual void Init(const FIKRigTransforms& InGlobalTransforms) override;
	virtual void Solve(
		FIKRigTransforms& InOutGlobalTransforms,
		const FIKRigGoalContainer& Goals,
		FControlRigDrawInterface* InOutDrawInterface) override;
	virtual void CollectGoalNames(TSet<FName>& OutGoals) const override;

public:
	static const FName EffectorTargetPrefix;
};