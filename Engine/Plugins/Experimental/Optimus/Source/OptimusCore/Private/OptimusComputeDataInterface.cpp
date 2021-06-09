// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusComputeDataInterface.h"

#include "UObject/UObjectIterator.h"


// Cached list of data interfaces.
TArray<UClass*> UOptimusComputeDataInterface::CachedClasses;


TArray<UClass*> UOptimusComputeDataInterface::GetAllComputeDataInterfaceClasses()
{
	if (CachedClasses.IsEmpty())
	{
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (!Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NotPlaceable) &&
				Class->IsChildOf(StaticClass()))
			{
				CachedClasses.Add(Class);
			}
		}
	}
	return CachedClasses;
}
