// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeinSourceControlThumbnailCommandlet.h"
#include "SkeinSourceControlThumbnail.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogSkeinThumbnailCommandlet, Log, All);

int32 USkeinSourceControlThumbnailCommandlet::Main(FString const& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	ParseCommandLine(*Params, Tokens, Switches);

	int32 Size = 256;
	TArray<FString> Files;

	for (const FString& Switch : Switches)
	{
		FString FilesSwitch;
		FString FileCountsStr;
		if (Switch.Split(TEXT("Files="), &FilesSwitch, &FileCountsStr) || Switch.Split(TEXT("f="), &FilesSwitch, &FileCountsStr))
		{
			TArray<FString> FileCountStrs;
			if (FileCountsStr.ParseIntoArray(FileCountStrs, TEXT(",")) > 0)
			{
				Files = MoveTemp(FileCountStrs);
			}
		}
		else if (FParse::Value(*Switch, TEXT("Size="), Size) || FParse::Value(*Switch, TEXT("s="), Size))
		{
			continue;
		}
	}

	if (Files.IsEmpty())
	{
		UE_LOG(LogSkeinThumbnailCommandlet, Warning, TEXT("You must specify one or more asset files using -Files=path/to/file1.uasset,path/to/file2.uasset"));
		return -1;
	}

	int32 Failed = 0;
	int32 Succeeded = 0;

	for (const FString& FileName : Files)
	{
		if (FPaths::FileExists(FileName))
		{
			FString OutputFolder;
			if (Tokens.Num() != 0)
			{
				OutputFolder = Tokens[0];
			}
			if (OutputFolder.IsEmpty())
			{
				OutputFolder = FPaths::GetPath(FileName);
			}
			FPaths::ConvertRelativePathToFull(OutputFolder);
			FPaths::NormalizeDirectoryName(OutputFolder);

			TStringBuilder<512> OutputFilenameBuilder;
			OutputFilenameBuilder << OutputFolder;
			OutputFilenameBuilder << FPlatformMisc::GetDefaultPathSeparator();
			OutputFilenameBuilder << FPaths::GetBaseFilename(FileName);
			OutputFilenameBuilder << TEXT(".png");

			FString OutputFilename = OutputFilenameBuilder.ToString();
			FPaths::NormalizeFilename(OutputFilename);

			if (SkeinSourceControlThumbnail::WriteThumbnailToDisk(FileName, OutputFilename, Size))
			{
				UE_LOG(LogSkeinThumbnailCommandlet, Display, TEXT("Written %dx%d output: %s"), Size, Size, *OutputFilename);
				++Succeeded;
			}
			else
			{
				UE_LOG(LogSkeinThumbnailCommandlet, Error, TEXT("Could not write %dx%d output file: %s"), Size, Size, *OutputFilename);
				++Failed;
			}
		}
		else
		{
			UE_LOG(LogSkeinThumbnailCommandlet, Error, TEXT("Could not find input file: %s"), *FileName);
			++Failed;
		}
	}

	return Failed;
}
