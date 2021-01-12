// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionEditorSettings.h"

#define LOCTEXT_NAMESPACE "WorldPartitionEditor"

#if WITH_EDITOR
bool UWorldPartitionEditorSettings::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		const FName PropertyName = InProperty->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UWorldPartitionEditorSettings, bEnableWorldPartition))
		{
			return true;
		}
	}

	return bEnableWorldPartition;
}
#endif

#undef LOCTEXT_NAMESPACE