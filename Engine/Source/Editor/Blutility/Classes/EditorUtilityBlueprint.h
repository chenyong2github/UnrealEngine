// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/**
 * Blueprint for editor utilities
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/Blueprint.h"
#include "EditorUtilityBlueprint.generated.h"

UCLASS()
class BLUTILITY_API UEditorUtilityBlueprint : public UBlueprint
{
	GENERATED_UCLASS_BODY()

	// UBlueprint interface
	virtual bool SupportedByDefaultBlueprintFactory() const override
	{
		return false;
	}
// End of UBlueprint interface
};
