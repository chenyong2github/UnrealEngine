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

#if WITH_EDITOR
	// UBlueprint interface
	virtual bool SupportedByDefaultBlueprintFactory() const override
	{
		return false;
	}
	
#endif

#if WITH_EDITORONLY_DATA
protected:
	virtual void LoadModulesRequiredForCompilation() override;
#endif
// End of UBlueprint interface
};
