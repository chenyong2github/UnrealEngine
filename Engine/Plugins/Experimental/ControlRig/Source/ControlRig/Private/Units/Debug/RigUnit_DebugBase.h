// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "RigUnit_DebugBase.generated.h"

USTRUCT(meta=(Abstract, Category="Debug", NodeColor = "0.25 0.25 0.05"))
struct FRigUnit_DebugBase : public FRigUnit
{
	GENERATED_BODY()
};

USTRUCT(meta=(Abstract, Category="Debug", NodeColor = "0.25 0.25 0.05"))
struct FRigUnit_DebugBaseMutable : public FRigUnitMutable
{
	GENERATED_BODY()
};
