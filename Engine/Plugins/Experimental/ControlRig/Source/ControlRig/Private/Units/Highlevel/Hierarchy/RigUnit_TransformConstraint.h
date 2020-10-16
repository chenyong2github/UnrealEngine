// Copyright Epic Games, Inc. All Rights Reserved.

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

USTRUCT()
struct FRigUnit_TransformConstraint_WorkData
{
	GENERATED_BODY()

	// note that Targets.Num () != ConstraintData.Num()
	UPROPERTY()
	TArray<FConstraintData>	ConstraintData;

	UPROPERTY()
	TMap<int32, int32> ConstraintDataToTargets;
};

USTRUCT(meta=(DisplayName="Transform Constraint", Category="Transforms", Deprecated = "4.25"))
struct FRigUnit_TransformConstraint : public FRigUnit_HighlevelBaseMutable
{
	GENERATED_BODY()

	FRigUnit_TransformConstraint()
		: BaseTransformSpace(ETransformSpaceMode::GlobalSpace)
		, bUseInitialTransforms(true)
	{}

	virtual FRigElementKey DetermineSpaceForPin(const FString& InPinPath, void* InUserContext) const override
	{
		if (InPinPath.StartsWith(TEXT("Targets")))
		{
			if (BaseTransformSpace == ETransformSpaceMode::BaseJoint)
			{
				return FRigElementKey(BaseBone, ERigElementType::Bone);
			}

			if (BaseTransformSpace == ETransformSpaceMode::LocalSpace)
			{
				if (const FRigHierarchyContainer* Container = (const FRigHierarchyContainer*)InUserContext)
				{
					int32 BoneIndex = Container->BoneHierarchy.GetIndex(Bone);
					if (BoneIndex != INDEX_NONE)
					{
						return Container->BoneHierarchy[BoneIndex].GetParentElementKey();
					}
				}
			}
		}
		return FRigElementKey();
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(EditAnywhere, Category = "Constraint", meta = (Input))
	FName Bone;

	UPROPERTY(EditAnywhere, Category = "Constraint", meta = (Input))
	ETransformSpaceMode BaseTransformSpace;

	// Transform op option. Use if ETransformSpace is BaseTransform
	UPROPERTY(EditAnywhere, Category = "Constraint", meta = (Input))
	FTransform BaseTransform;

	// Transform op option. Use if ETransformSpace is BaseJoint
	UPROPERTY(EditAnywhere, Category = "Constraint", meta = (Input))
	FName BaseBone;

	UPROPERTY(EditAnywhere, Category = "Constraint", meta = (Input, ExpandByDefault, DefaultArraySize = 1))
	TArray<FConstraintTarget> Targets;

	// If checked the initial transform will be used for the constraint data
	UPROPERTY(EditAnywhere, Category = "Constraint", meta = (Input, Constant))
	bool bUseInitialTransforms;

private:

	UPROPERTY(transient)
	FRigUnit_TransformConstraint_WorkData WorkData;
};

/**
 * Constrains an item's transform to multiple items' transforms
 */
USTRUCT(meta=(DisplayName="Transform Constraint", Category="Transforms", Keywords = "Parent,Orient,Scale"))
struct FRigUnit_TransformConstraintPerItem : public FRigUnit_HighlevelBaseMutable
{
	GENERATED_BODY()

	FRigUnit_TransformConstraintPerItem()
		: BaseTransformSpace(ETransformSpaceMode::GlobalSpace)
		, bUseInitialTransforms(true)
	{
		Item = BaseItem = FRigElementKey(NAME_None, ERigElementType::Bone);
	}

	virtual FRigElementKey DetermineSpaceForPin(const FString& InPinPath, void* InUserContext) const override
	{
		if (InPinPath.StartsWith(TEXT("Targets")))
		{
			if (BaseTransformSpace == ETransformSpaceMode::BaseJoint)
			{
				return BaseItem;
			}

			if (BaseTransformSpace == ETransformSpaceMode::LocalSpace)
			{
				if (const FRigHierarchyContainer* Container = (const FRigHierarchyContainer*)InUserContext)
				{
					return Container->GetParentKey(Item);
				}
			}
		}
		return FRigElementKey();
	}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	UPROPERTY(EditAnywhere, Category = "Constraint", meta = (Input, ExpandByDefault))
	FRigElementKey Item;

	UPROPERTY(EditAnywhere, Category = "Constraint", meta = (Input))
	ETransformSpaceMode BaseTransformSpace;

	// Transform op option. Use if ETransformSpace is BaseTransform
	UPROPERTY(EditAnywhere, Category = "Constraint", meta = (Input))
	FTransform BaseTransform;

	// Transform op option. Use if ETransformSpace is BaseJoint
	UPROPERTY(EditAnywhere, Category = "Constraint", meta = (Input))
	FRigElementKey BaseItem;

	UPROPERTY(EditAnywhere, Category = "Constraint", meta = (Input, ExpandByDefault, DefaultArraySize = 1))
	TArray<FConstraintTarget> Targets;

	// If checked the initial transform will be used for the constraint data
	UPROPERTY(EditAnywhere, Category = "Constraint", meta = (Input, Constant))
	bool bUseInitialTransforms;

private:
	static void AddConstraintData(const FRigVMFixedArray<FConstraintTarget>& Targets, ETransformConstraintType ConstraintType, const int32 TargetIndex, const FTransform& SourceTransform, const FTransform& InBaseTransform, TArray<FConstraintData>& OutConstraintData, TMap<int32, int32>& OutConstraintDataToTargets);

	UPROPERTY(transient)
	FRigUnit_TransformConstraint_WorkData WorkData;
};
