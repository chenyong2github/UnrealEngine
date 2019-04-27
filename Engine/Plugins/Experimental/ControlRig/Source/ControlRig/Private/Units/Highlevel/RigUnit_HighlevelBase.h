// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Units/RigUnit.h"
#include "RigUnit_HighlevelBase.generated.h"

UENUM()
enum class EControlRigVectorKind : uint8
{
	Direction,
	Location
};

USTRUCT(meta=(Abstract, Category="Debug", NodeColor = "0.4 0.05 0.4"))
struct FRigUnit_HighlevelBase : public FRigUnit
{
	GENERATED_BODY()
};

USTRUCT(meta=(Abstract, Category="Debug", NodeColor = "0.4 0.05 0.4"))
struct FRigUnit_HighlevelBaseMutable : public FRigUnitMutable
{
	GENERATED_BODY()
};
