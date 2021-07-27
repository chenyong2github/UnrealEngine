// Copyright Epic Games, Inc. All Rights Reserved.

#include "Settings/LevelSnapshotsEditorDataManagementSettings.h"

#include "Misc/Paths.h"

ULevelSnapshotsEditorDataManagementSettings::ULevelSnapshotsEditorDataManagementSettings(const FObjectInitializer& ObjectInitializer)
{
	RootLevelSnapshotSaveDir.Path = "/Game/LevelSnapshots";
	LevelSnapshotSaveDir = "{map}/{year}-{month}-{day}";
	DefaultLevelSnapshotName = "{map}_{user}_{time}";
	LevelSnapshotNameOverride = DefaultLevelSnapshotName;
}

const FString& ULevelSnapshotsEditorDataManagementSettings::GetNameOverride() const
{
	return LevelSnapshotNameOverride;
}

void ULevelSnapshotsEditorDataManagementSettings::SetNameOverride(const FString& InName)
{
	LevelSnapshotNameOverride = InName;
}

void ULevelSnapshotsEditorDataManagementSettings::ValidateRootLevelSnapshotSaveDirAsGameContentRelative()
{
	// Enforce Game Content Dir
	if (!RootLevelSnapshotSaveDir.Path.StartsWith("/Game/"))
	{
		RootLevelSnapshotSaveDir.Path = "/Game/";
	}
}

void ULevelSnapshotsEditorDataManagementSettings::SanitizePathInline(FString& InPath, const bool bSkipForwardSlash)
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

void ULevelSnapshotsEditorDataManagementSettings::SanitizeAllProjectSettingsPaths(const bool bSkipForwardSlash)
{
	SanitizePathInline(RootLevelSnapshotSaveDir.Path, bSkipForwardSlash);
	SanitizePathInline(LevelSnapshotSaveDir, bSkipForwardSlash);
	SanitizePathInline(DefaultLevelSnapshotName, bSkipForwardSlash);
}

FFormatNamedArguments ULevelSnapshotsEditorDataManagementSettings::GetFormatNamedArguments(const FString& InWorldName)
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

FText ULevelSnapshotsEditorDataManagementSettings::ParseLevelSnapshotsTokensInText(const FText& InTextToParse, const FString& InWorldName)
{
	const FFormatNamedArguments& FormatArguments = GetFormatNamedArguments(InWorldName);

	return FText::Format(InTextToParse, FormatArguments);
}

bool ULevelSnapshotsEditorDataManagementSettings::IsNameOverridden() const
{
	return !LevelSnapshotNameOverride.Equals(DefaultLevelSnapshotName);
}
