// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "RigUnit_SetControlOffset.generated.h"

/**
 * SetControlOffset is used to perform a change in the hierarchy by setting a single control's transform.
 */
USTRUCT(meta=(DisplayName="Set Control Offset", Category="Setup", DocumentationPolicy="Strict", Keywords = "SetControlOffset,Initial,InitialTransform,SetInitialTransform,SetInitialControlTransform", PrototypeName = "SetControlValue"))
struct FRigUnit_SetControlOffset : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetControlOffset()
		: Control(NAME_None)
		, Space(EBoneGetterSetterMode::GlobalSpace)
	{}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Control to set the transform for.
	 */
	UPROPERTY(meta = (Input, CustomWidget = "ControlName" ))
	FName Control;

	/**
	 * The offse transform to set for the control
	 */
	UPROPERTY(meta = (Input, Output))
	FTransform Offset;

	/**
	 * Defines if the bone's transform should be set
	 * in local or global space.
	 */
	UPROPERTY(meta = (Input))
	EBoneGetterSetterMode Space;

	// user to internally cache the index of the bone
	UPROPERTY()
	FCachedRigElement CachedControlIndex;
};
