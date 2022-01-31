// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusHelpers.h"


FName Optimus::GetUniqueNameForScopeAndClass(UObject* InScopeObj, UClass* InClass, FName InName)
{
	// If there's already an object with this name, then attempt to make the name unique.
	// For some reason, MakeUniqueObjectName does not already do this check, hence this function.
	if (StaticFindObjectFast(InClass, InScopeObj, InName) != nullptr)
	{
		InName = MakeUniqueObjectName(InScopeObj, InClass, InName);
	}

	return InName;
}
