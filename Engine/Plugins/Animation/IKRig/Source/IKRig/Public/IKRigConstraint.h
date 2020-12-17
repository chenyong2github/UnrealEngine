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

struct FIKRigTransformModifier;
struct FControlRigDrawInterface;

UCLASS(config = Engine, hidecategories = UObject, BlueprintType)
class IKRIG_API UIKRigConstraint : public UObject
{
	GENERATED_BODY()

	bool bInitialized = false;

public: 
	void Setup(FIKRigTransformModifier& InOutTransformModifier);
	void SetAndApplyConstraint(FIKRigTransformModifier& InOutTransformModifier);

	virtual void Apply(FIKRigTransformModifier& InOutTransformModifier, FControlRigDrawInterface* InOutDrawInterface) {};
	virtual void SetupInternal(FIKRigTransformModifier& InOutTransformModifier) {};
};

