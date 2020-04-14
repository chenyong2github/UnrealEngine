// Copyright Epic Games, Inc. All Rights Reserved.

#include "Editor/PropertyEditorTestObject.h"

bool UPropertyEditorTestObject::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPropertyEditorTestObject, DisabledByCanEditChange))
	{
		return false;
	}

	return true;
}