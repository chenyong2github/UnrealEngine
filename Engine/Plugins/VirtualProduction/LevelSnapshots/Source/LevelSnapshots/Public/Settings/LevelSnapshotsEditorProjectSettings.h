// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RestorationBlacklist.h"
#include "LevelSnapshotsEditorProjectSettings.generated.h"

UCLASS(config = Engine, defaultconfig)
class LEVELSNAPSHOTS_API ULevelSnapshotsEditorProjectSettings : public UObject
{
	GENERATED_BODY()
public:
	
	ULevelSnapshotsEditorProjectSettings(const FObjectInitializer& ObjectInitializer);

	FVector2D GetLastCreationWindowSize() const;
	/* Setting the Window Size through code will not save the size to the config. To make sure it's saved, call SaveConfig(). */
	void SetLastCreationWindowSize(const FVector2D InLastSize);

	/* Specifies classes and properties that should never be captured nor restored. */
	UPROPERTY(config, EditAnywhere, Category = "Level Snapshots|Behavior")
	FRestorationBlacklist Blacklist;
	
	UPROPERTY(config, EditAnywhere, Category = "Level Snapshots|Editor", meta = (ConfigRestartRequired = true))
	bool bEnableLevelSnapshotsToolbarButton;

	UPROPERTY(config, EditAnywhere, Category = "Level Snapshots|Editor")
	bool bUseCreationForm;

	/* If true, clicking on an actor group under 'Modified Actors' will select the actor in the scene. The previous selection will be deselected. */
	UPROPERTY(config, EditAnywhere, Category = "Level Snapshots|Editor")
	bool bClickActorGroupToSelectActorInScene;

	UPROPERTY(config, EditAnywhere, Category = "Level Snapshots|Editor")
	float PreferredCreationFormWindowWidth;

	UPROPERTY(config, EditAnywhere, Category = "Level Snapshots|Editor")
	float PreferredCreationFormWindowHeight;
};
