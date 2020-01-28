// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "RigUnit_BoneName.generated.h"

/**
 * BoneName is used to represent a bone name in the graph
 */
USTRUCT(meta=(DisplayName="Bone Name", Category="Hierarchy", DocumentationPolicy = "Strict"))
struct FRigUnit_BoneName : public FRigUnit
{
	GENERATED_BODY()

	FRigUnit_BoneName()
	{}

	virtual FString GetUnitLabel() const override;

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Bone
	 */
	UPROPERTY(meta = (Input, Output, CustomWidget = "BoneName", Constant))
	FName Bone;
};

/**
 * SpaceName is used to represent a Space name in the graph
 */
USTRUCT(meta=(DisplayName="Space Name", Category="Hierarchy", DocumentationPolicy = "Strict"))
struct FRigUnit_SpaceName : public FRigUnit
{
	GENERATED_BODY()

	FRigUnit_SpaceName()
	{}

	virtual FString GetUnitLabel() const override;

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Space
	 */
	UPROPERTY(meta = (Input, Output, CustomWidget = "SpaceName", Constant))
	FName Space;
};

/**
 * ControlName is used to represent a Control name in the graph
 */
USTRUCT(meta=(DisplayName="Control Name", Category="Hierarchy", DocumentationPolicy = "Strict"))
struct FRigUnit_ControlName : public FRigUnit
{
	GENERATED_BODY()

	FRigUnit_ControlName()
	{}

	virtual FString GetUnitLabel() const override;

	RIGVM_METHOD()
	virtual void Execute(const FRigUnitContext& Context) override;

	/**
	 * The name of the Control
	 */
	UPROPERTY(meta = (Input, Output, CustomWidget = "ControlName", Constant))
	FName Control;
};
