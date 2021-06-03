// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IKRigDefinition.h"
#include "IKRigSolver.h"

#include "IKRig_BodyMover.generated.h"

UCLASS()
class IKRIG_API UIKRig_BodyMoverEffector : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY(VisibleAnywhere, Category = "Body Mover Effector")
	FName GoalName;

	UPROPERTY(VisibleAnywhere, Category = "Body Mover Effector")
	FName BoneName;

	/** Scale the amount this effector rotates the body. Range is 0-10. Default is 1.0. */
	UPROPERTY(EditAnywhere, Category = "Body Mover Effector", meta = (ClampMin = "0", ClampMa = "10", UIMin = "0.0", UIMax = "10.0"))
	float RotationMultiplier = 1.0f;
};

UCLASS(EditInlineNew)
class IKRIG_API UIKRig_BodyMover : public UIKRigSolver
{
	GENERATED_BODY()

public:
	
	UIKRig_BodyMover();

	/** The target bone to move with the effectors. */
	UPROPERTY(VisibleAnywhere, Category = "Body Mover Solver")
	FName BodyBone;

	/** Blend the translational effect of this solver on/off. Range is 0-1. Default is 1.0. */
	UPROPERTY(EditAnywhere, Category = "Body Mover Solver", meta = (ClampMin = "0", ClampMa = "1", UIMin = "0.0", UIMax = "1.0"))
	float PositionAlpha = 1.0f;

	/** Blend the total rotational effect on/off. Range is 0-1. Default is 1.0. */
	UPROPERTY(EditAnywhere, Category = "Body Mover Solver", meta = (ClampMin = "0", ClampMa = "1", UIMin = "0.0", UIMax = "1.0"))
	float RotationAlpha = 1.0f;

	/** Blend the X-axis rotational effect on/off. Range is 0-1. Default is 1.0. */
	UPROPERTY(EditAnywhere, Category = "Body Mover Solver", meta = (ClampMin = "0", ClampMa = "1", UIMin = "0.0", UIMax = "1.0"))
	float RotateXAlpha = 1.0f;

	/** Blend the Y-axis rotational effect on/off. Range is 0-1. Default is 1.0. */
	UPROPERTY(EditAnywhere, Category = "Body Mover Solver", meta = (ClampMin = "0", ClampMa = "1", UIMin = "0.0", UIMax = "1.0"))
	float RotateYAlpha = 1.0f;

	/** Blend the Z-axis rotational effect on/off. Range is 0-1. Default is 1.0. */
	UPROPERTY(EditAnywhere, Category = "Body Mover Solver", meta = (ClampMin = "0", ClampMa = "1", UIMin = "0.0", UIMax = "1.0"))
	float RotateZAlpha = 1.0f;
	
	UPROPERTY()
	TArray<UIKRig_BodyMoverEffector*> Effectors;

	/** UIKRigSolver interface */
	virtual void Initialize(const FIKRigSkeleton& IKRigSkeleton) override;
	virtual void Solve(FIKRigSkeleton& IKRigSkeleton, const FIKRigGoalContainer& Goals) override;
	virtual void UpdateSolverSettings(UIKRigSolver* InSettings) override;
	virtual bool IsBoneAffectedBySolver(const FName& BoneName, const FIKRigSkeleton& IKRigSkeleton) const override;
	// goals
	virtual void AddGoal(const UIKRigEffectorGoal& NewGoal) override;
	virtual void RemoveGoal(const FName& GoalName) override;
	virtual void RenameGoal(const FName& OldName, const FName& NewName) override;
	virtual void SetGoalBone(const FName& GoalName, const FName& NewBoneName) override;
	virtual bool IsGoalConnected(const FName& GoalName) const override;
	virtual UObject* GetEffectorWithGoal(const FName& GoalName) override;
	// root bone can be set on this solver
	virtual bool CanSetRootBone() const override { return true; };
	virtual void SetRootBone(const FName& RootBoneName) override;
	/** END UIKRigSolver interface */

private:

	int32 GetIndexOfGoal(const FName& OldName) const;

	static void ExtractRotation(
	    const FVector& DX,
	    const FVector& DY,
	    const FVector& DZ,
	    FQuat &Q,
	    const unsigned int maxIter);

	int32 BodyBoneIndex;
};

