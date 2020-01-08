// Copyright Epic Games, Inc. All Rights Reserved.

#include "Manipulatable/IControlRigManipulatable.h"

UControlRigManipulatable::UControlRigManipulatable(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

IControlRigManipulatable::IControlRigManipulatable()
{
	bManipulationEnabled = false;
}

bool IControlRigManipulatable::SetControlGlobalTransform(const FName& InControlName, const FTransform& InGlobalTransform)
{
	FRigControlValue Value = GetControlValueFromGlobalTransform(InControlName, InGlobalTransform);
	if (OnFilterControl.IsBound())
	{
		FRigControl* Control = FindControl(InControlName);
		if (Control)
		{
			OnFilterControl.Broadcast(this, *Control, Value);
		}
	}
	SetControlValue(InControlName, Value, true /* notify */);
	return true;
}