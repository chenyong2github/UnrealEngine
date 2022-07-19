// Copyright Epic Games, Inc. All Rights Reserved.

#include "Library/DMXGDTFAssetImportData.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_EDITOR
FString UDMXGDTFAssetImportData::GetSourceFilePathAndName() const
{
	// Not GetFirstFilename(), we want the name even if the file doesn't exist on disk locally.
	return SourceData.SourceFiles.Num() > 0 ? SourceData.SourceFiles[0].RelativeFilename : FString();
}
#endif // WITH_EDITOR

#if WITH_EDITOR
void UDMXGDTFAssetImportData::SetSourceFile(const FString& FilePathAndName)
{
	FAssetImportInfo Info;
	Info.Insert(FAssetImportInfo::FSourceFile(FilePathAndName));
	SourceData = Info;

	if (!FPaths::FileExists(FilePathAndName))
	{
		return;
	}

	RawSourceData.ByteArray.Reset();
	FFileHelper::LoadFileToArray(RawSourceData.ByteArray, *FilePathAndName);
}
#endif // WITH_EDITOR
