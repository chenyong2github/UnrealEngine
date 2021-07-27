// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"
#include "LevelSnapshotsEditorDataManagementSettings.generated.h"

UCLASS(config = Engine, defaultconfig)
class LEVELSNAPSHOTS_API ULevelSnapshotsEditorDataManagementSettings : public UObject
{
	GENERATED_BODY()
public:
	
	ULevelSnapshotsEditorDataManagementSettings(const FObjectInitializer& ObjectInitializer);

	const FString& GetNameOverride() const;
	void SetNameOverride(const FString& InName);
	
	void ValidateRootLevelSnapshotSaveDirAsGameContentRelative();
	
	static void SanitizePathInline(FString& InPath, const bool bSkipForwardSlash);

	/* Removes /?:&\*"<>|%#@^ . from project settings path strings. Optionally the forward slash can be kept so that the user can define a file structure. */
	void SanitizeAllProjectSettingsPaths(const bool bSkipForwardSlash);
	
	static FFormatNamedArguments GetFormatNamedArguments(const FString& InWorldName);

	UFUNCTION(BlueprintCallable, Category = "Level Snapshots")
	static FText ParseLevelSnapshotsTokensInText(const FText& InTextToParse, const FString& InWorldName);

	bool IsNameOverridden() const;

	// Must be a directory in the Game Content folder ("/Game/"). For best results, use the picker.  
	UPROPERTY(config, EditAnywhere, Category = "Level Snapshots", meta = (RelativeToGameContentDir, ContentDir))
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
	UPROPERTY(config, EditAnywhere, Category = "Level Snapshots")
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
	UPROPERTY(config, EditAnywhere, Category = "Level Snapshots")
	FString DefaultLevelSnapshotName;

private:
	
	/* If the user overrides the Name field in the creation form, the override will be saved here so it can be recalled. */
	FString LevelSnapshotNameOverride;
};
