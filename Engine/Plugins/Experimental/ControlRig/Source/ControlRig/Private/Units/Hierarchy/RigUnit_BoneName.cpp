// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_BoneName.h"
#include "Units/RigUnitContext.h"

FString FRigUnit_BoneName::GetUnitLabel() const
{
	return FString::Printf(TEXT("%s Name"), *Bone.ToString());
}

FRigUnit_BoneName_Execute()
{
}

FString FRigUnit_SpaceName::GetUnitLabel() const
{
	return FString::Printf(TEXT("%s Name"), *Space.ToString());
}

FRigUnit_SpaceName_Execute()
{
}

FString FRigUnit_ControlName::GetUnitLabel() const
{
	return FString::Printf(TEXT("%s Name"), *Control.ToString());
}

FRigUnit_ControlName_Execute()
{
}