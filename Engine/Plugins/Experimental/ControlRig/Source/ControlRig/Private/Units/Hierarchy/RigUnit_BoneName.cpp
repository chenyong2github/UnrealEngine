// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigUnit_BoneName.h"
#include "Units/RigUnitContext.h"

FString FRigUnit_BoneName::GetUnitLabel() const
{
	return FString::Printf(TEXT("%s Name"), *Bone.ToString());
}

FRigUnit_BoneName_Execute()
{
}