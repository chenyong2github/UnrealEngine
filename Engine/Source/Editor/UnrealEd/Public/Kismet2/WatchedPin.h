// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WatchedPin.generated.h"

class UEdGraphPin;

/** Contains information about a watched pin in a Blueprint graph for local settings data.
 */
USTRUCT()
struct UNREALED_API FBlueprintWatchedPin
{
	GENERATED_BODY()

	FBlueprintWatchedPin();
	FBlueprintWatchedPin(const UEdGraphPin* Pin);

	/** Returns a reference to the underlying graph pin */
	UEdGraphPin* Get() const;

	/** Resets the pin watch to the given graph pin */
	void SetFromPin(const UEdGraphPin* Pin);

	bool operator==(const FBlueprintWatchedPin& Other) const
	{
		return PinId == Other.PinId && OwningNode == Other.OwningNode;
	}

private:
	/** Node that owns the pin that the watch is placed on */
	UPROPERTY()
	TSoftObjectPtr<UEdGraphNode> OwningNode;

	/** Unique ID of the pin that the watch is placed on */
	UPROPERTY()
	FGuid PinId;

	/** Holds a cached reference to the underlying pin object. We don't save this directly to settings data,
	 *  because it internally maintains a weak object reference to the owning node that it will then try to
	 *  load after parsing the underlying value from the user's local settings file. To avoid issues and
	 *	overhead of trying to load referenced assets when reading the config file at editor startup, we
	 *  maintain our own soft object reference for the settings data instead. Additionally, we can add more
	 *  context this way without affecting other parts of the engine that rely on the pin reference type.
	 */
	mutable FEdGraphPinReference CachedPinRef;
};