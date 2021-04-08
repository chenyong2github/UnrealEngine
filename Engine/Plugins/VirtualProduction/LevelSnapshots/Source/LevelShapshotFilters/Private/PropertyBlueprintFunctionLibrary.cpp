// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertyBlueprintFunctionLibrary.h"

FString UPropertyBlueprintFunctionLibrary::GetPropertyOriginPath(const TFieldPath<FProperty>& Property)
{
	return Property.ToString();
}

FString UPropertyBlueprintFunctionLibrary::GetPropertyName(const TFieldPath<FProperty>& Property)
{
	return Property->GetName();
}
