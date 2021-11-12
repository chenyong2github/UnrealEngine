// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeinSourceControlMetadataCommandlet.h"
#include "SkeinSourceControlMetadata.h"
#include "SkeinSourceControlUtils.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogSkeinMetadataCommandlet, Log, All);

int32 USkeinSourceControlMetadataCommandlet::Main(FString const& Params)
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
		UE_LOG(LogSkeinMetadataCommandlet, Warning, TEXT("You must specify one or more asset files using -Files=path/to/file1.uasset,path/to/file2.uasset"));
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
				OutputFolder = SkeinSourceControlUtils::FindSkeinIntermediateRoot(FileName);
			}
			if (OutputFolder.IsEmpty())
			{
				OutputFolder = FPaths::GetPath(FileName);
			}
			FPaths::ConvertRelativePathToFull(OutputFolder);
			FPaths::NormalizeDirectoryName(OutputFolder);

			FString MetadataFilename = SkeinSourceControlUtils::GetIntermediateMetadataPath(FileName, OutputFolder);
			FString ThumbnailFilename = SkeinSourceControlUtils::GetIntermediateThumbnailPath(FileName, OutputFolder);

			bool bExtractedMetadata = SkeinSourceControlMetadata::ExtractMetadata(FileName, MetadataFilename, ThumbnailFilename, Size);
			if (bExtractedMetadata)
			{
				UE_LOG(LogSkeinMetadataCommandlet, Error, TEXT("Written thumbnail and metadata for %s"), *FileName);
				++Succeeded;
			}
			else
			{
				UE_LOG(LogSkeinMetadataCommandlet, Error, TEXT("Could not write thumbnail or metadata for %s"), *FileName);
				++Failed;
			}
		}
		else
		{
			UE_LOG(LogSkeinMetadataCommandlet, Error, TEXT("Could not find input file: %s"), *FileName);
			++Failed;
		}
	}

	return Failed;
}
