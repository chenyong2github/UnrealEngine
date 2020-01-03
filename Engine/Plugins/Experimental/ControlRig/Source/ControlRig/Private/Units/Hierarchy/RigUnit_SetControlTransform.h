// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "RigUnit_SetControlTransform.generated.h"


/**
 * SetControlTransform is used to perform a change in the hierarchy by setting a single bone's transform.
 */
USTRUCT(meta=(DisplayName="Set Control", Category="Hierarchy", DocumentationPolicy="Strict", Keywords = "SetControlTransform"))
struct FRigUnit_SetControlTransform : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetControlTransform()
		: Space(EBoneGetterSetterMode::LocalSpace)
		, CachedControlIndex(INDEX_NONE)
	{}

	virtual FString GetUnitLabel() const override;

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Control to set the transform for.
	 */
	UPROPERTY(meta = (Input, ControlName, Constant))
	FName Control;

	/**
	 * The transform value to set for the given Control.
	 */
	UPROPERTY(meta = (Input, Output))
	FTransform Transform;

	/**
	 * Defines if the bone's transform should be set
	 * in local or global space.
	 */
	UPROPERTY(meta = (Input))
	EBoneGetterSetterMode Space;

	// Used to cache the internally used bone index
	UPROPERTY()
	int32 CachedControlIndex;
};
