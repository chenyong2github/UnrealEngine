// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

#include "Blueprints/IOutputRemapBlueprintAPI.h"
#include "OutputRemapBlueprintAPIImpl.generated.h"


/**
 * Blueprint API interface implementation
 */
UCLASS()
class UOutputRemapAPIImpl
	: public UObject
	, public IOutputRemapBlueprintAPI
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Reload Changed External Files"), Category = "OutputRemap")
	virtual void ReloadChangedExternalFiles() override;
};