// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "RigUnit_SetControlColor.generated.h"

/**
 * SetControlTransform is used to change the gizmo color on a control at runtime
 */
USTRUCT(meta=(DisplayName="Set Control Color", Category="Hierarchy", DocumentationPolicy="Strict", Keywords = "SetControlColor", PrototypeName = "SetControlColor"))
struct FRigUnit_SetControlColor : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetControlColor()
		: Control(NAME_None)
		, Color(FLinearColor::Black)
		, CachedControlIndex(INDEX_NONE)
	{}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Control to set the transform for.
	 */
	UPROPERTY(meta = (Input, CustomWidget = "ControlName", Constant))
	FName Control;

	/**
	 * The color to set for the control
	 */
	UPROPERTY(meta = (Input))
	FLinearColor Color;

	// Used to cache the internally used bone index
	UPROPERTY()
	int32 CachedControlIndex;
};
