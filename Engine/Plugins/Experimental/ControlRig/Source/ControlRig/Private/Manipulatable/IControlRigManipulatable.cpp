// Copyright Epic Games, Inc. All Rights Reserved.

#include "Manipulatable/IControlRigManipulatable.h"

IControlRigManipulatable::IControlRigManipulatable()
{
	bManipulationEnabled = false;
}

IControlRigManipulatable::~IControlRigManipulatable()
{
	OnFilterControl.Clear();
	OnControlModified.Clear();
#if WITH_EDITOR
	OnControlSelected.Clear();
#endif
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