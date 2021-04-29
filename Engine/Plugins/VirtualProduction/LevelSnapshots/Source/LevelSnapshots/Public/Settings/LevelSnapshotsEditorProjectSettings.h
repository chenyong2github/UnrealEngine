// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RestorationBlacklist.h"
#include "Engine/EngineTypes.h"
#include "LevelSnapshotsEditorProjectSettings.generated.h"

UCLASS(config = Engine, defaultconfig)
class LEVELSNAPSHOTS_API ULevelSnapshotsEditorProjectSettings : public UObject
{
	GENERATED_BODY()
public:
	
	ULevelSnapshotsEditorProjectSettings(const FObjectInitializer& ObjectInitializer);

	const FString& GetNameOverride() const;
	void SetNameOverride(const FString& InName);

	const FString& GetSaveDirOverride() const;
	void SetSaveDirOverride(const FString& InPath);
	
	void ValidateRootLevelSnapshotSaveDirAsGameContentRelative();
	
	static void SanitizePathInline(FString& InPath, const bool bSkipForwardSlash);

	/* Removes /?:&\*"<>|%#@^ . from project settings path strings. Optionally the forward slash can be kept so that the user can define a file structure. */
	void SanitizeAllProjectSettingsPaths(const bool bSkipForwardSlash);
	
	static FFormatNamedArguments GetFormatNamedArguments(const FString& InWorldName);

	bool IsNameOverridden() const;
	bool IsPathOverridden() const;

	/* Specifies classes and properties that should never be captured nor restored. */
	UPROPERTY(config, EditAnywhere, Category = "Level Snapshots|Behavior")
	FRestorationBlacklist Blacklist;
	
	UPROPERTY(config, EditAnywhere, Category = "Level Snapshots|Editor", meta = (ConfigRestartRequired = true))
	bool bEnableLevelSnapshotsToolbarButton;

	UPROPERTY(config, EditAnywhere, Category = "Level Snapshots|Editor")
	bool bUseCreationForm;

	// Must be a directory in the Game Content folder ("/Game/"). For best results, use the picker.  
	UPROPERTY(config, EditAnywhere, Category = "Level Snapshots|Saving", meta = (RelativeToGameContentDir, ContentDir))
	FDirectoryPath RootLevelSnapshotSaveDir;

	/** The format to use for the resulting filename. Extension will be added automatically. Any tokens of the form {token} will be replaced with the corresponding value:
	 * {map}		- The name of the captured map or level
	 * {user}		- The current OS user account name
	 * {year}       - The current year
	 * {month}      - The current month
	 * {day}        - The current day
	 * {date}       - The current date from the local computer in the format of {year}-{month}-{day}
	 * {time}       - The current time from the local computer in the format of hours-minutes-seconds
	 */
	UPROPERTY(config, EditAnywhere, Category = "Level Snapshots|Saving")
	FString LevelSnapshotSaveDir;

	/** The format to use for the resulting filename. Extension will be added automatically. Any tokens of the form {token} will be replaced with the corresponding value:
	 * {map}		- The name of the captured map or level
	 * {user}		- The current OS user account name
	 * {year}       - The current year
	 * {month}      - The current month
	 * {day}        - The current day
	 * {date}       - The current date from the local computer in the format of {year}-{month}-{day}
	 * {time}       - The current time from the local computer in the format of hours-minutes-seconds
	 */
	UPROPERTY(config, EditAnywhere, Category = "Level Snapshots|Saving")
	FString DefaultLevelSnapshotName;

private:
	
	/* If the user overrides the Save Dir in the creation form, the override will be saved here so it can be recalled. */
	FString LevelSnapshotSaveDirOverride;
	
	/* If the user overrides the Name field in the creation form, the override will be saved here so it can be recalled. */
	FString LevelSnapshotNameOverride;
};
