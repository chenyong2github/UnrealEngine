// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "ActorBrowsingModeSettings.generated.h"


/**
 * Implements the settings for the Actor Browsing Outliner (also known as the World Outliner).
 */
UCLASS(config = EditorPerProjectUserSettings)
class UActorBrowsingModeSettings
	: public UObject
{
	GENERATED_UCLASS_BODY()

	/** True when the Scene Outliner is hiding temporary/run-time Actors */
	UPROPERTY(config)
	uint32 bHideTemporaryActors : 1;

	/** True when the Scene Outliner is showing only Actors that exist in the current level */
	UPROPERTY(config)
	uint32 bShowOnlyActorsInCurrentLevel : 1;

	/** True when the Scene Outliner is only displaying selected Actors */
	UPROPERTY(config)
	uint32 bShowOnlySelectedActors : 1;

	/** True when the Scene Outliner is not displaying Actor Components*/
	UPROPERTY(config)
	uint32 bHideActorComponents : 1;

	/** True when the Scene Outliner is not displaying LevelInstances */
	UPROPERTY(config)
	uint32 bHideLevelInstanceHierarchy : 1;

	/** True when the Scene Outliner is not displaying unloaded actors */
	UPROPERTY(config)
	uint32 bHideUnloadedActors : 1;
};
