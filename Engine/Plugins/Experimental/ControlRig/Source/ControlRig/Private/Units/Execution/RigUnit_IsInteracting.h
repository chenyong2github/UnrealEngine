// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "ControlRigDefines.h"
#include "RigUnit_IsInteracting.generated.h"

/**
 * Returns true if the Control Rig is being interacted
 */
USTRUCT(meta=(DisplayName="Is Interacting", Category="Execution", TitleColor="1 0 0", NodeColor="0.1 0.1 0.1", Keywords="Gizmo,Manipulation,Interaction", Varying))
struct CONTROLRIG_API FRigUnit_IsInteracting : public FRigUnit
{
	GENERATED_BODY()

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	// The execution result
	UPROPERTY(EditAnywhere, Transient, DisplayName = "Interacting", Category = "Execution", meta = (Output))
	bool bIsInteracting = false;
};
