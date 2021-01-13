// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Contains RotationLimit Definition 
 *
 */

#pragma once

#include "IKRigConstraint.h"
#include "RotationLimitConstraint.generated.h"

UCLASS(config = Engine, hidecategories = UObject, BlueprintType)
class IKRIG_API URotationLimitConstraint : public UIKRigConstraint
{
	GENERATED_BODY()

	URotationLimitConstraint()
		: BaseIndex(INDEX_NONE)
		, ConstrainedIndex(INDEX_NONE)
		, BaseFrameOffset(EForceInit::ForceInitToZero)
		, RelativelRefPose(FVector::ZeroVector)
		, bXLimitSet(false)
		, bYLimitSet(false)
		, bZLimitSet(false)
		, Limit(0.f)
	{
	}

	int32 BaseIndex;
	int32 ConstrainedIndex;

	// frame of reference for this
	FQuat		BaseFrameOffset; // include any offset and in their local space
	FTransform	RelativelRefPose;

	UPROPERTY(EditAnywhere, Category = "Limit")
	FName TargetBone;

	// xyz - twist/swing1/swing2
	UPROPERTY(EditAnywhere, Category = "Limit")
	uint8 bXLimitSet : 1;
	UPROPERTY(EditAnywhere, Category = "Limit")
	uint8 bYLimitSet : 1;
	UPROPERTY(EditAnywhere, Category = "Limit")
	uint8 bZLimitSet : 1;

	UPROPERTY(EditAnywhere, Category = "Limit")
	FVector Limit;

	// apply to local rotation if you want to modify offset of limit - i.e. knee
	UPROPERTY(EditAnywhere, Category = "Offset")
	FRotator Offset;
public: 
	virtual void SetupInternal(const FIKRigTransforms& InOutTransformModifier) override;
	virtual void Apply(FIKRigTransforms& InOutTransformModifier, FControlRigDrawInterface* InOutDrawInterface) override;
};

