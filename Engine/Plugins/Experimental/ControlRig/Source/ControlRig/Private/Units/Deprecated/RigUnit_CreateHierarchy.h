// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "Rigs/RigBoneHierarchy.h"
#include "RigUnit_CreateHierarchy.generated.h"

USTRUCT(meta=(DisplayName="Create Hierarchy", Category="Hierarchy", Deprecated = "4.23.0"))
struct FRigUnit_CreateHierarchy : public FRigUnit
{
	GENERATED_BODY()

	virtual void Execute(const FRigUnitContext& Context) override;

	// hierarchy reference - currently only merge to base hierarchy and outputs
	UPROPERTY(meta = (Input, Output))
	FRigHierarchyRef NewHierarchy;

	UPROPERTY(meta = (Input))
	FRigHierarchyRef SourceHierarchy;

	UPROPERTY(meta = (Input))
	FName Root;
};
