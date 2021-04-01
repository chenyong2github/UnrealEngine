// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "PropertySelection.generated.h"

USTRUCT()
struct FPropertySelection
{
	GENERATED_BODY()

	bool IsPropertySelected(const FProperty* Property) const
	{
		return SelectedPropertyPaths.Contains(Property);
	}
	void RemoveProperty(const FProperty* Property)
	{
		// TODO: SelectedPropertyPaths should have const FProperty in it to avoid const_cast hack
		SelectedPropertyPaths.Remove(const_cast<FProperty*>(Property));
	}

	/* The user cares about these properties */
	UPROPERTY()
	TArray<TFieldPath<FProperty>> SelectedPropertyPaths;
	
};
		
		

		