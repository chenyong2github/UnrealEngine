// Copyright Epic Games, Inc. All Rights Reserved.

#include "FilterBlueprintFunctionLibrary.h"

ULevelSnapshotFilter* UFilterBlueprintFunctionLibrary::CreateFilterByClass(TSubclassOf<ULevelSnapshotFilter> Class, FName Name, UObject* Outer)
{
	if (IsValid(Outer))
	{
		Outer = GetTransientPackage();
	}
	
	return NewObject<ULevelSnapshotFilter>(Outer, Class, Name);
}
