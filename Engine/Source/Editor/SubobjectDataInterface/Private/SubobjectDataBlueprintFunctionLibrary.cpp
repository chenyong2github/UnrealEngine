// Copyright Epic Games, Inc. All Rights Reserved.

#include "SubobjectDataBlueprintFunctionLibrary.h"

void USubobjectDataBlueprintFunctionLibrary::GetData(const FSubobjectDataHandle& DataHandle, FSubobjectData& OutData)
{
	TSharedPtr<FSubobjectData> DataPtr = DataHandle.GetSharedDataPtr();
	if(DataPtr.IsValid())
	{
		OutData = *DataPtr.Get();
	}
}
