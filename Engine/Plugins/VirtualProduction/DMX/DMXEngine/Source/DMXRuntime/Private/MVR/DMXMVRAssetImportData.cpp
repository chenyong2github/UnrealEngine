// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVR/DMXMVRAssetImportData.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"


#if WITH_EDITOR
void UDMXMVRAssetImportData::SetSourceFile(const FString& FilePathAndName)
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
