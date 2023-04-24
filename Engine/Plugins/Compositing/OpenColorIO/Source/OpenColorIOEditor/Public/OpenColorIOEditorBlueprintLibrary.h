// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"

#include "OpenColorIOEditorBlueprintLibrary.generated.h"

struct FOpenColorIODisplayConfiguration;

UCLASS()
class UOpenColorIOEditorBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	/**
	 * Set the active editor viewport's display configuration color transform .
	 *
	 * @param InDisplayConfiguration Display configuration color transform
	 */
	UFUNCTION(BlueprintCallable, Category = "OpenColorIO")
	static OPENCOLORIOEDITOR_API void SetActiveViewportConfiguration(const FOpenColorIODisplayConfiguration& InConfiguration);
};
