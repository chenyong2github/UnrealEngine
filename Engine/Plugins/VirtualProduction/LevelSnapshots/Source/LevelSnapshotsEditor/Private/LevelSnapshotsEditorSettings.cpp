// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotsEditorSettings.h"
#include "Misc/Paths.h"

#include "Application/SlateApplicationBase.h"
#include "HAL/PlatformApplicationMisc.h"

const ULevelSnapshotsEditorSettings* ULevelSnapshotsEditorSettings::Get()
{
	return GetDefault<ULevelSnapshotsEditorSettings>();
}

ULevelSnapshotsEditorSettings* ULevelSnapshotsEditorSettings::GetMutable()
{
	return GetMutableDefault<ULevelSnapshotsEditorSettings>();
}

ULevelSnapshotsEditorSettings::ULevelSnapshotsEditorSettings(const FObjectInitializer& ObjectInitializer)
{
	RootLevelSnapshotSaveDir.Path = "/Game/LevelSnapshots";
	LevelSnapshotSaveDir = "{map}/{year}-{month}-{day}";
	DefaultLevelSnapshotName = "{map}_{user}_{time}";

	bEnableLevelSnapshotsToolbarButton = true;
	bUseCreationForm = true;
	
	const FVector2D DefaultClientSize = FVector2D(400.f, 400.f);

	float DPIScale = 1.0f;

	if (FSlateApplicationBase::IsInitialized())
	{
		const FSlateRect WorkAreaRect = FSlateApplicationBase::Get().GetPreferredWorkArea();
		DPIScale = FPlatformApplicationMisc::GetDPIScaleFactorAtPoint(WorkAreaRect.Left, WorkAreaRect.Top);
	}

	PreferredCreationFormWindowWidth = DefaultClientSize.X * DPIScale;
	PreferredCreationFormWindowHeight = DefaultClientSize.Y * DPIScale;
}

FVector2D ULevelSnapshotsEditorSettings::GetLastCreationWindowSize() const
{
	return FVector2D(PreferredCreationFormWindowWidth, PreferredCreationFormWindowHeight);
}

void ULevelSnapshotsEditorSettings::SetLastCreationWindowSize(const FVector2D InLastSize)
{
	PreferredCreationFormWindowWidth = InLastSize.X;
	PreferredCreationFormWindowHeight = InLastSize.Y;
}

const FString& ULevelSnapshotsEditorSettings::GetNameOverride() const
{
	return LevelSnapshotNameOverride.IsSet() ? LevelSnapshotNameOverride.Get() : DefaultLevelSnapshotName;
}

void ULevelSnapshotsEditorSettings::SetNameOverride(const FString& InName)
{
	LevelSnapshotNameOverride = InName;
}

void ULevelSnapshotsEditorSettings::ValidateRootLevelSnapshotSaveDirAsGameContentRelative()
{
	// Enforce Game Content Dir
	if (!RootLevelSnapshotSaveDir.Path.StartsWith("/Game/"))
	{
		RootLevelSnapshotSaveDir.Path = "/Game/";
	}
}

void ULevelSnapshotsEditorSettings::SanitizePathInline(FString& InPath, const bool bSkipForwardSlash)
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

void ULevelSnapshotsEditorSettings::SanitizeAllProjectSettingsPaths(const bool bSkipForwardSlash)
{
	SanitizePathInline(RootLevelSnapshotSaveDir.Path, bSkipForwardSlash);
	SanitizePathInline(LevelSnapshotSaveDir, bSkipForwardSlash);
	SanitizePathInline(DefaultLevelSnapshotName, bSkipForwardSlash);
}

FFormatNamedArguments ULevelSnapshotsEditorSettings::GetFormatNamedArguments(const FString& InWorldName)
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

FText ULevelSnapshotsEditorSettings::ParseLevelSnapshotsTokensInText(const FText& InTextToParse, const FString& InWorldName)
{
	const FFormatNamedArguments& FormatArguments = GetFormatNamedArguments(InWorldName);

	return FText::Format(InTextToParse, FormatArguments);
}

bool ULevelSnapshotsEditorSettings::IsNameOverridden() const
{
	return LevelSnapshotNameOverride.IsSet();
}
