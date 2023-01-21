// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hierarchies/BaseModifierGroup.h"

void UBaseModifierGroup::PostInitProperties()
{
	GroupName = GetFName();
	UObject::PostInitProperties();
}

#if WITH_EDITOR
void UBaseModifierGroup::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UBaseModifierGroup, GroupName))
	{
		// TODO:
	}
	
	UObject::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
