// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "RigUnit_SimBase.generated.h"

USTRUCT(meta=(Abstract, Category = "Simulation", NodeColor = "0.25 0.05 0.05"))
struct FRigUnit_SimBase : public FRigUnit
{
	GENERATED_BODY()
};

USTRUCT(meta=(Abstract, Category = "Simulation", NodeColor = "0.25 0.05 0.05"))
struct FRigUnit_SimBaseMutable : public FRigUnitMutable
{
	GENERATED_BODY()
};
