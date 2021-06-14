// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigContextMenuContext.h"

#include "ControlRigBlueprint.h"
#include "ControlRigEditor.h"

UControlRigBlueprint* UControlRigContextMenuContext::GetControlRigBlueprint() const
{
	if (ControlRigEditor.IsValid())
	{
		return ControlRigEditor.Pin()->GetControlRigBlueprint();
	}

	return nullptr;
}