// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Contains IKRig Definition 
 *
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "IKRigConstraint.generated.h"

struct FIKRigTransforms;
struct FControlRigDrawInterface;

UCLASS(config = Engine, hidecategories = UObject, BlueprintType)
class IKRIG_API UIKRigConstraint : public UObject
{
	GENERATED_BODY()

	bool bInitialized = false;

public: 
	void Setup(const FIKRigTransforms& InOutTransformModifier);
	void SetAndApplyConstraint(FIKRigTransforms& InOutTransformModifier);

	virtual void Apply(FIKRigTransforms& InOutTransformModifier, FControlRigDrawInterface* InOutDrawInterface) {};
	virtual void SetupInternal(const FIKRigTransforms& InOutTransformModifier) {};
};

