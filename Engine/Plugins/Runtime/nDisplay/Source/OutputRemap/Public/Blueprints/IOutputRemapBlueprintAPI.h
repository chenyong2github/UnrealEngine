// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IOutputRemapBlueprintAPI.generated.h"

UINTERFACE(meta = (CannotImplementInterfaceInBlueprint))
class UOutputRemapBlueprintAPI : public UInterface
{
	GENERATED_BODY()
};

class IOutputRemapBlueprintAPI
{
	GENERATED_BODY()

public:
	/**
	* Reload all changed external remap files, and apply changes runtime
	*
	*/
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Reload Changed External Files"), Category = "OutputRemap")
	virtual void ReloadChangedExternalFiles() = 0;
};
