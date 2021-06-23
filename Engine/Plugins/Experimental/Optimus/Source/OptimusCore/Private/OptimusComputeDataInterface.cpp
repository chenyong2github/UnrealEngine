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
				UOptimusComputeDataInterface* DataInterface = Cast<UOptimusComputeDataInterface>(Class->GetDefaultObject());
				if (DataInterface && DataInterface->IsVisible())
				{
					CachedClasses.Add(Class);
				}
			}
		}
	}
	return CachedClasses;
}
