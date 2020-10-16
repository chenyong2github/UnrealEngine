// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "RigUnit_SetControlVisibility.generated.h"

/**
 * SetControlVisibility is used to change the gizmo visibility on a control at runtime
 */
USTRUCT(meta=(DisplayName="Set Control Visibility", Category="Hierarchy", DocumentationPolicy="Strict", Keywords = "SetControlVisibility,Visibility,Hide,Show,Hidden,Visible,SetGizmoVisibility", PrototypeName = "SetControlVisibility"))
struct FRigUnit_SetControlVisibility : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_SetControlVisibility()
		: Item(NAME_None, ERigElementType::Control)
		, Pattern(FString())
		, bVisible(true)
	{}

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Control to set the visibility for.
	 */
	UPROPERTY(meta = (Input, ExpandByDefault))
	FRigElementKey Item;

	/**
	 * If the ControlName is set to None this can be used to look for a series of Controls
	 */
	UPROPERTY(meta = (Input, Constant))
	FString Pattern;

	/**
	 * The color to set for the control
	 */
	UPROPERTY(meta = (Input))
	bool bVisible;

	// Used to cache the internally used control index
	UPROPERTY()
	TArray<FCachedRigElement> CachedControlIndices;
};
