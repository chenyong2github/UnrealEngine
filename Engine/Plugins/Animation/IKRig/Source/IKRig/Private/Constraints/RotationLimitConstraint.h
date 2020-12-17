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

	int32 BaseIndex;
	int32 ConstrainedIndex;

	// frame of reference for this
	FQuat		BaseFrameOffset; // include any offset and in their local space
	FTransform	RelativelRefPose;

	// xyz - twist/swing1/swing2
	UPROPERTY(EditAnywhere, Category = "Limit")
	uint8 bXLimitSet : 1;
	UPROPERTY(EditAnywhere, Category = "Limit")
	uint8 bYLimitSet : 1;
	UPROPERTY(EditAnywhere, Category = "Limit")
	uint8 bZLimitSet : 1;
	UPROPERTY(EditAnywhere, Category = "Limit")
	FVector Limit;
public: 
	virtual void SetupInternal(FIKRigTransformModifier& InOutTransformModifier) override {};
	virtual void Apply(FIKRigTransformModifier& InOutTransformModifier, FControlRigDrawInterface* InOutDrawInterface) override {};
};

