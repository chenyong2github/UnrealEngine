// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DisplayClusterLightCardActor.h"

#include "DisplayClusterLightCardTemplate.generated.h"

/**
 * A template asset to store appearance settings from Light Card actors.
 */
UCLASS(NotBlueprintType, NotBlueprintable, NotPlaceable, PerObjectConfig, config=EditorPerProjectUserSettings)
class DISPLAYCLUSTERLIGHTCARDEDITOR_API UDisplayClusterLightCardTemplate : public UObject
{
	GENERATED_BODY()

public:
	/** The instanced template object containing user settings for the light card. */
	UPROPERTY(VisibleAnywhere, Instanced, Category = Template, NoClear, meta = (ShowOnlyInnerProperties))
	TObjectPtr<ADisplayClusterLightCardActor> LightCardActor;

	/** If the user has marked this a favorite template. */
	UPROPERTY(Config)
	bool bIsFavorite;
};
