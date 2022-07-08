// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/SoftObjectPtr.h"

#include "MediaSourceManagerSettings.generated.h"

class UMediaSourceManager;

/**
* Settings for the media source manager.
*/
UCLASS(config=Game, defaultconfig)
class MEDIASOURCEMANAGER_API UMediaSourceManagerSettings : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Call this to get the manager.
	 */
	UMediaSourceManager* GetManager() const;

#if WITH_EDITOR

	/** Register with this to get a callback when the manager changes. */
	FSimpleMulticastDelegate OnManagerChanged;

	/**
	 * Call this to set the manager.
	 */
	void SetManager(UMediaSourceManager* InManager);

#endif // WITH_EDITOR

private:

	/** Stores the current manager asset. */
	UPROPERTY(config, EditAnywhere, Category = "MediaSourceManager")
	TSoftObjectPtr<UMediaSourceManager> Manager;
};
