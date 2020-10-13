// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/Highlevel/RigUnit_HighlevelBase.h"
#include "JacobianSolver.h"
#include "FBIKConstraintOption.h"
#include "FBIKDebugOption.h"
#include "RigUnit_FullbodyIK.generated.h"

USTRUCT()
struct FFBIKEndEffector
{
	GENERATED_BODY()
	/**
	 * The last item in the chain to solve - the effector
	 */
	UPROPERTY(meta = (Constant, CustomWidget = "BoneName"))
	FRigElementKey Item;

	UPROPERTY()
	FVector Position;

	UPROPERTY()
	float	PositionAlpha = 1.f;

	UPROPERTY()
	int32  PositionDepth = 1000;
	
	UPROPERTY()
	FQuat	Rotation;

	UPROPERTY()
	float	RotationAlpha = 1.f;

	UPROPERTY()
	int32  RotationDepth = 1000;

	/*
	 * Clamps the total length to target by this scale for each iteration 
	 * This helps to stabilize solver to reduce singularity by avoiding to try to reach target too far. 
	 */
	UPROPERTY()
	float	Pull = 0.f;

	FFBIKEndEffector()
		: Item(NAME_None, ERigElementType::Bone)
		, Position(FVector::ZeroVector)
		, Rotation(FQuat::Identity)
	{
	}

	FFBIKEndEffector(const FFBIKEndEffector& Other)
	{
		*this = Other;
	}

	FFBIKEndEffector& operator = (const FFBIKEndEffector& Other)
	{
		Item = Other.Item;
		Position = Other.Position;
		PositionAlpha = Other.PositionAlpha;
		PositionDepth = Other.PositionDepth;
		Rotation = Other.Rotation;
		RotationAlpha = Other.RotationAlpha;
		RotationDepth = Other.RotationDepth;
		Pull = Other.Pull;
		return *this;
	}
};

class FULLBODYIK_API FJacobianSolver_FullbodyIK : public FJacobianSolverBase
{
public:
	FJacobianSolver_FullbodyIK() : FJacobianSolverBase() {}

private:
	virtual void InitializeSolver(TArray<FFBIKLinkData>& InOutLinkData, TMap<int32, FFBIKEffectorTarget>& InOutEndEffectors) const override;
	virtual void PreSolve(TArray<FFBIKLinkData>& InOutLinkData, const TMap<int32, FFBIKEffectorTarget>& InEndEffectors) const override;
};


USTRUCT()
struct FRigUnit_FullbodyIK_WorkData
{
	GENERATED_BODY()

	FRigUnit_FullbodyIK_WorkData()
	{
	}

	/** list of Link Data for solvers - joints */
	TArray<FFBIKLinkData> LinkData;
	/** Effector Targets - search key is LinkData Index */
	TMap<int32, FFBIKEffectorTarget> EffectorTargets; 
	/** End Effector Link Indices - EndEffector index to LinkData index*/
	TArray<int32> EffectorLinkIndices;
	/** Map from LinkData index to Rig Hierarchy Index*/
	TMap<int32, FRigElementKey> LinkDataToHierarchyIndices;
	/** Map from Rig Hierarchy Index to LinkData index*/
	TMap<FRigElementKey, int32> HierarchyToLinkDataMap;
	/** Constraints data */
	TArray<ConstraintType> InternalConstraints;
	/* Current Solver */
	FJacobianSolver_FullbodyIK IKSolver;
	/** Debug Data */
	TArray<FJacobianDebugData> DebugData;
};

USTRUCT()
struct FSolverInput
{
	GENERATED_BODY()

	/*
	 * This value is applied to the target information for effectors, which influence back to 
	 * Joint's motion that are affected by the end effector
	 * The reason min/max is used when we apply the depth through the chain that are affected

	 */
	UPROPERTY()
	float	LinearMotionStrength = 3.f;

	UPROPERTY()
	float	MinLinearMotionStrength = 2.f;

	/*
	 * This value is applied to the target information for effectors, which influence back to 
	 * Joint's motion that are affected by the end effector
	 * The reason min/max is used when we apply the depth through the chain that are affected
	 */
	UPROPERTY()
	float	AngularMotionStrength = 3.f;

	UPROPERTY()
	float	MinAngularMotionStrength = 2.f;

	/* This is a scale value (range from 0-0.7) that is used to stablize the target vector. If less, it's more stable, but it can reduce speed of converge. */
	UPROPERTY()
	float	DefaultTargetClamp = 0.2f;

	/**
	 * The precision to use for the solver
	 */
	UPROPERTY()
	float Precision = 0.1f;

	/**
	* The precision to use for the fabrik solver
	*/
	UPROPERTY()
	float Damping = 30.f;

	/**
	 * The maximum number of iterations. Values between 4 and 16 are common.
	 */
	UPROPERTY()
	int32 MaxIterations = 30;

	/**
	 * Cheaper solution than default Jacobian Pseudo Inverse Damped Least Square
	 */
	UPROPERTY()
	bool bUseJacobianTranspose = false;
};

/**
 * Based on Jacobian solver at core, this can solve multi chains within a root using multi effectors
 */
USTRUCT(meta=(DisplayName="Fullbody IK", Category="Hierarchy", Keywords="Multi, Effector, N-Chain, FB, IK"))
struct FRigUnit_FullbodyIK : public FRigUnit_HighlevelBaseMutable
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	FRigUnit_FullbodyIK()
		: Root(NAME_None, ERigElementType::Bone)
	{
		MotionProperty.bForceEffectorRotationTarget = true;
	}

	/**
	 * The first bone in the chain to solve
	 */
	UPROPERTY(meta = (Input, Constant, CustomWidget = "BoneName"))
	FRigElementKey Root;

	UPROPERTY(meta = (Input))
	TArray<FFBIKEndEffector> Effectors;

	UPROPERTY(EditAnywhere, Category = FRigUnit_Jacobian, meta = (Input))
	TArray<FFBIKConstraintOption> Constraints;

	UPROPERTY(meta = (Input, Constant))
	FSolverInput	SolverProperty;

	UPROPERTY(meta = (Input, Constant))
	FMotionProcessInput MotionProperty;

	/**
	 * If set to true all of the global transforms of the children
	 * of this bone will be recalculated based on their local transforms.
	 * Note: This is computationally more expensive than turning it off.
	 */
	UPROPERTY(meta = (Input))
	bool bPropagateToChildren = true;

	UPROPERTY(meta = (Input))
	FFBIKDebugOption DebugOption;

	UPROPERTY(transient)
	FRigUnit_FullbodyIK_WorkData WorkData;
};
