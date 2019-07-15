// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "Rigs/Hierarchy.h"
#include "RigUnit_MergeHierarchy.generated.h"

USTRUCT(meta=(DisplayName="Merge Hierarchy", Category="Hierarchy", Deprecated = "4.23.0"))
struct FRigUnit_MergeHierarchy : public FRigUnit
{
	GENERATED_BODY()

	virtual void Execute(const FRigUnitContext& Context) override;

	// hierarchy reference - currently only merge to base hierarchy and outputs
	UPROPERTY(meta = (Input, Output))
	FRigHierarchyRef TargetHierarchy;

	// hierarchy reference - currently only merge to base hierarchy and outputs
	// @todo : do we allow copying base to something else? Maybe... that doesn't work right now
	UPROPERTY(meta = (Input))
	FRigHierarchyRef SourceHierarchy;
};
