// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateColor.h"
#include "UObject/Object.h"
#include "WebAPIEditorWidgetUtilities.generated.h"

/**
 * 
 */
UCLASS()
class WEBAPIEDITOR_API UWebAPIEditorWidgetUtilities : public UObject
{
	GENERATED_BODY()
};


class FWebAPIEditorWidgetsUtilities
{
public:
	// @todo: verb type should be enum
	static FSlateColor GetColorForVerb(uint8 InVerb) { return FSlateColor(FColor{255, 128, 64}); }
	static FName GetIconNameForVerb(uint8 InVerb) { return NAME_None; }
};
