// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"
#include "LevelSnapshotsEditorProjectSettings.generated.h"

UCLASS(config = Engine, defaultconfig)
class LEVELSNAPSHOTS_API ULevelSnapshotsEditorProjectSettings : public UObject
{
	GENERATED_BODY()

public:
	
	ULevelSnapshotsEditorProjectSettings(const FObjectInitializer& ObjectInitializer)
	{
		bEnableLevelSnapshotsToolbarButton = true;
		bUseCreationForm = true;
		RootLevelSnapshotSaveDir.Path = "/Game/LevelSnapshots";
		LevelSnapshotSaveDir = "{year}-{month}-{day}/{map}";
		DefaultLevelSnapshotName = "{map}_{user}_{time}";
	}

	UPROPERTY(config, EditAnywhere, Category = "Level Snapshots", meta = (ConfigRestartRequired = true))
		bool bEnableLevelSnapshotsToolbarButton;

	UPROPERTY(config, EditAnywhere, Category = "Level Snapshots")
		bool bUseCreationForm;

	UPROPERTY(config, EditAnywhere, Category = "Level Snapshots")
		FDirectoryPath RootLevelSnapshotSaveDir;

	/** The format to use for the resulting filename. Extension will be added automatically. Any tokens of the form {token} will be replaced with the corresponding value:
	 * {map}		- The name of the captured map
	 * {user}		- The current OS user account name
	 * {slate}		- The current slate name, if Take Recorder is enabled. Otherwise blank.
	 * {take}		- The current take number, if Take Recorder is enabled. Otherwise blank.
	 * {date}       - The date in the format of {year}.{month}.{day}
	 * {year}       - The current year
	 * {month}      - The current month
	 * {day}        - The current day
	 * {time}       - The current time in the format of hours.minutes.seconds
	 */
	UPROPERTY(config, EditAnywhere, Category = "Level Snapshots")
		FString LevelSnapshotSaveDir;

	/** The format to use for the resulting filename. Extension will be added automatically. Any tokens of the form {token} will be replaced with the corresponding value:
	 * {map}		- The name of the captured map
	 * {user}		- The current OS user account name
	 * {slate}		- The current slate name, if Take Recorder is enabled. Otherwise blank.
	 * {take}		- The current take number, if Take Recorder is enabled. Otherwise blank.
	 * {date}       - The date in the format of {year}.{month}.{day}
	 * {year}       - The current year
	 * {month}      - The current month
	 * {day}        - The current day
	 * {time}       - The current time in the format of hours.minutes.seconds
	 */
	UPROPERTY(config, EditAnywhere, Category = "Level Snapshots")
		FString DefaultLevelSnapshotName;
};
