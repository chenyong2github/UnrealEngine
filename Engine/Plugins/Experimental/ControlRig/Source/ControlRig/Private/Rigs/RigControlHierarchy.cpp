// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rigs/RigControlHierarchy.h"

////////////////////////////////////////////////////////////////////////////////
// FRigControl
////////////////////////////////////////////////////////////////////////////////

void FRigControl::ApplyLimits(FRigControlValue& InOutValue) const
{
	InOutValue.ApplyLimits(bLimitTranslation, bLimitRotation, bLimitScale, ControlType, MinimumValue, MaximumValue);
}

FTransform FRigControl::GetTransformFromValue(ERigControlValueType InValueType) const
{
	return GetValue(InValueType).GetAsTransform(ControlType, PrimaryAxis);
}

void FRigControl::SetValueFromTransform(const FTransform& InTransform, ERigControlValueType InValueType)
{
	GetValue(InValueType).SetFromTransform(InTransform, ControlType, PrimaryAxis);

	if (InValueType == ERigControlValueType::Current)
	{
		ApplyLimits(GetValue(InValueType));
	}
}

////////////////////////////////////////////////////////////////////////////////
// FRigControlHierarchy
////////////////////////////////////////////////////////////////////////////////

FRigControlHierarchy::FRigControlHierarchy()
{
}

void FRigControlHierarchy::PostLoad()
{
	for (FRigControl& Control : Controls)
	{
		for (int32 ValueType = 0; ValueType <= (int32)ERigControlValueType::Maximum; ValueType++)
		{
			FRigControlValue& Value = Control.GetValue((ERigControlValueType)ValueType);
			if (!Value.IsValid())
			{
				Value.GetRef<FTransform>() = Value.Storage_DEPRECATED;
			}
		}
	}
}