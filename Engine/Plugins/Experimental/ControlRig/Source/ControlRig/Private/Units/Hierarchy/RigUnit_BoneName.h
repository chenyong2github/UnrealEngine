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
	UPROPERTY(meta = (Input, Output, BoneName, Constant))
	FName Bone;
};
