// Copyright Epic Games, Inc. All Rights Reserved.

#include "Settings/LevelSnapshotsEditorProjectSettings.h"

#include "Misc/Paths.h"

ULevelSnapshotsEditorProjectSettings::ULevelSnapshotsEditorProjectSettings(const FObjectInitializer& ObjectInitializer)
{
	bEnableLevelSnapshotsToolbarButton = true;
	bUseCreationForm = true;
	RootLevelSnapshotSaveDir.Path = "/Game/LevelSnapshots";
	LevelSnapshotSaveDir = "{year}-{month}-{day}/{map}";
	LevelSnapshotSaveDirOverride = LevelSnapshotSaveDir;
	DefaultLevelSnapshotName = "{map}_{user}_{time}";
	LevelSnapshotNameOverride = DefaultLevelSnapshotName;
}

const FString& ULevelSnapshotsEditorProjectSettings::GetNameOverride() const
{
	return LevelSnapshotNameOverride;
}

void ULevelSnapshotsEditorProjectSettings::SetNameOverride(const FString& InName)
{
	LevelSnapshotNameOverride = InName;
}

const FString& ULevelSnapshotsEditorProjectSettings::GetSaveDirOverride() const
{
	return LevelSnapshotSaveDirOverride;
}

void ULevelSnapshotsEditorProjectSettings::SetSaveDirOverride(const FString& InPath)
{
	LevelSnapshotSaveDirOverride = InPath;
}

void ULevelSnapshotsEditorProjectSettings::ValidateRootLevelSnapshotSaveDirAsGameContentRelative()
{
	// Enforce Game Content Dir
	if (!RootLevelSnapshotSaveDir.Path.StartsWith("/Game/"))
	{
		RootLevelSnapshotSaveDir.Path = "/Game/";
	}
}

void ULevelSnapshotsEditorProjectSettings::SanitizePathInline(FString& InPath, const bool bSkipForwardSlash)
{
	FString IllegalChars = FPaths::GetInvalidFileSystemChars().ReplaceEscapedCharWithChar() + " .";

	// In some cases we want to allow forward slashes in a path so that the end user can define a folder structure
	if (bSkipForwardSlash && IllegalChars.Contains("/"))
	{
		IllegalChars.ReplaceInline(TEXT("/"), TEXT(""));
	}

	for (int32 CharIndex = 0; CharIndex < IllegalChars.Len(); CharIndex++)
	{
		FString Char = FString().AppendChar(IllegalChars[CharIndex]);

		InPath.ReplaceInline(*Char, TEXT(""));
	}
}

void ULevelSnapshotsEditorProjectSettings::SanitizeAllProjectSettingsPaths(const bool bSkipForwardSlash)
{
	SanitizePathInline(RootLevelSnapshotSaveDir.Path, bSkipForwardSlash);
	SanitizePathInline(LevelSnapshotSaveDir, bSkipForwardSlash);
	SanitizePathInline(LevelSnapshotSaveDirOverride, bSkipForwardSlash);
	SanitizePathInline(DefaultLevelSnapshotName, bSkipForwardSlash);
}

FFormatNamedArguments ULevelSnapshotsEditorProjectSettings::GetFormatNamedArguments(const FString& InWorldName)
{
	FNumberFormattingOptions IntOptions;
	IntOptions.MinimumIntegralDigits = 2;

	const FDateTime& LocalNow = FDateTime::Now();

	FFormatNamedArguments FormatArguments;
	FormatArguments.Add("map", FText::FromString(InWorldName));
	FormatArguments.Add("user", FText::FromString(FPlatformProcess::UserName()));
	FormatArguments.Add("year", FText::FromString(FString::FromInt(LocalNow.GetYear())));
	FormatArguments.Add("month", FText::AsNumber(LocalNow.GetMonth(), &IntOptions));
	FormatArguments.Add("day", FText::AsNumber(LocalNow.GetDay(), &IntOptions));
	FormatArguments.Add("date", FText::Format(FText::FromString("{0}-{1}-{2}"), FormatArguments["year"], FormatArguments["month"], FormatArguments["day"]));
	FormatArguments.Add("time",
		FText::Format(
			FText::FromString("{0}-{1}-{2}"),
			FText::AsNumber(LocalNow.GetHour(), &IntOptions), FText::AsNumber(LocalNow.GetMinute(), &IntOptions), FText::AsNumber(LocalNow.GetSecond(), &IntOptions)));

	return FormatArguments;
}

bool ULevelSnapshotsEditorProjectSettings::IsNameOverridden() const
{
	return !LevelSnapshotNameOverride.Equals(DefaultLevelSnapshotName);
}

bool ULevelSnapshotsEditorProjectSettings::IsPathOverridden() const
{
	return !LevelSnapshotSaveDirOverride.Equals(LevelSnapshotSaveDir);
}
