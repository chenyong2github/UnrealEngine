// Copyright Epic Games, Inc. All Rights Reserved.

#include "ToolTargets/ToolTarget.h"

bool FToolTargetTypeRequirements::AreSatisfiedBy(UClass* Class) const
{
	// Required type must either be null (no requirement) or our type, or our parent type.
	if (!BaseType || BaseType == Class ||
		Class->IsChildOf(BaseType))
	{
		// we have to support all the required interfaces
		for (const UClass* Interface : Interfaces)
		{
			if (!Class->ImplementsInterface(Interface))
			{
				return false;
			}
		}
		return true;
	}
	return false;
}

bool FToolTargetTypeRequirements::AreSatisfiedBy(UToolTarget* ToolTarget) const
{
	return !ToolTarget ? false : AreSatisfiedBy(ToolTarget->GetClass());
}