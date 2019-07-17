// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "Units/Highlevel/RigUnit_HighlevelBase.h"
#include "Rigs/RigHierarchyContainer.h"
#include "Constraint.h"
#include "ControlRigDefines.h"
#include "RigUnit_TransformConstraint.generated.h"

/** 
 * Spec define: https://wiki.it.epicgames.net/display/TeamOnline/Transform+Constraint
 */

USTRUCT()
struct FConstraintTarget
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "FConstraintTarget")
	FTransform Transform;

	UPROPERTY(EditAnywhere, Category = "FConstraintTarget")
	float Weight;

	UPROPERTY(EditAnywhere, Category = "FConstraintTarget")
	bool bMaintainOffset;

	UPROPERTY(EditAnywhere, Category = "FConstraintTarget", meta = (Constant))
	FTransformFilter Filter;

	FConstraintTarget()
		: Weight (1.f)
		, bMaintainOffset(true)
	{}
};

USTRUCT(meta=(DisplayName="Transform Constraint", Category="Transforms"))
struct FRigUnit_TransformConstraint : public FRigUnit_HighlevelBaseMutable
{
	GENERATED_BODY()

	FRigUnit_TransformConstraint()
		: BaseTransformSpace(ETransformSpaceMode::GlobalSpace)
	{}

	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(EditAnywhere, Category = "Constraint", meta = (Input, Constant, BoneName))
	FName Bone;

	UPROPERTY(EditAnywhere, Category = "Constraint", meta = (Input))
	ETransformSpaceMode BaseTransformSpace;

	// Transform op option. Use if ETransformSpace is BaseTransform
	UPROPERTY(EditAnywhere, Category = "Constraint", meta = (Input))
	FTransform BaseTransform;

	// Transform op option. Use if ETransformSpace is BaseJoint
	UPROPERTY(EditAnywhere, Category = "Constraint", meta = (Input, Constant, BoneName))
	FName BaseBone;

	UPROPERTY(EditAnywhere, Category = "Constraint", meta = (Input, ExpandByDefault, DefaultArraySize = 1))
	TArray<FConstraintTarget> Targets;

private:
	// note that Targets.Num () != ConstraintData.Num()
	TArray<FConstraintData>	ConstraintData;

	void AddConstraintData(ETransformConstraintType ConstraintType, const int32 TargetIndex, const FTransform& SourceTransform, const FTransform& InBaseTransform);

	TMap<int32, int32> ConstraintDataToTargets;
};
