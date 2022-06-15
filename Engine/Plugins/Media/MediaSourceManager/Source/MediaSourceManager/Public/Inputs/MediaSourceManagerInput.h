// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "MediaSourceManagerInput.generated.h"

class UMediaSource;

/**
* Base class for an input for the media source manager.
* This provides a source from capture cards, movie files, etc.
*/
UCLASS(Abstract, BlueprintType)
class MEDIASOURCEMANAGER_API UMediaSourceManagerInput : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Get the name of this input.
	 */
	UFUNCTION(BlueprintCallable, Category = "Media|MediaSourceManager|Input")
	virtual FString GetDisplayName() PURE_VIRTUAL(UMediaSourceManagerInput::GetDisplayName, return FString(););

	/**
	 * Get a media source for this input.
	 */
	UFUNCTION(BlueprintCallable, Category = "Media|MediaSourceManager|Input")
	virtual UMediaSource* GetMediaSource() PURE_VIRTUAL(UMediaSourceManagerInput::GetMediaSource, return nullptr;);

};

