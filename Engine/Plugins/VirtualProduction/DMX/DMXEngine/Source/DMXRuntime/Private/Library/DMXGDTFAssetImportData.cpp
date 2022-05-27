// Copyright Epic Games, Inc. All Rights Reserved.

#include "Library/DMXGDTFAssetImportData.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_EDITOR
FString UDMXGDTFAssetImportData::GetSourceFilePathAndName() const
{
	return GetFirstFilename();
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

	RawZipFile.Reset();
	FFileHelper::LoadFileToArray(RawZipFile, *FilePathAndName);
}
#endif // WITH_EDITOR
