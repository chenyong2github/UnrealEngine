// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFFileBuilder.h"
#include "Misc/FileHelper.h"

FGLTFFileBuilder::FGLTFFileBuilder(const FString& FileName, const UGLTFExportOptions* ExportOptions)
	: FGLTFTaskBuilder(FileName, ExportOptions)
{
}

FString FGLTFFileBuilder::AddExternalFile(const FString& URI, const TSharedPtr<FGLTFMemoryArchive>& Archive)
{
	const FString UnqiueURI = GetUniqueURI(URI);
	ExternalFiles.Add(UnqiueURI, Archive);
	return UnqiueURI;
}

const TMap<FString, TSharedPtr<FGLTFMemoryArchive>>& FGLTFFileBuilder::GetExternalFiles() const
{
	return ExternalFiles;
}

bool FGLTFFileBuilder::WriteExternalFiles(const FString& DirPath, bool bOverwrite)
{
	for (const TPair<FString, TSharedPtr<FGLTFMemoryArchive>>& ExternalFile : ExternalFiles)
	{
		const FString FilePath = FPaths::Combine(DirPath, *ExternalFile.Key);
		const TArray64<uint8>& FileData = *ExternalFile.Value;

		if (!SaveToFile(FilePath, FileData, bOverwrite))
		{
			return false;
		}
	}

	return true;
}

FString FGLTFFileBuilder::GetUniqueURI(const FString& URI) const
{
	if (!ExternalFiles.Contains(URI))
	{
		return URI;
	}

	const FString BaseFilename = FPaths::GetBaseFilename(URI);
	const FString FileExtension = FPaths::GetExtension(URI, true);
	FString UnqiueURI;

	int32 Suffix = 1;
	do
	{
		UnqiueURI = BaseFilename + TEXT('_') + FString::FromInt(Suffix) + FileExtension;
		Suffix++;
	}
	while (ExternalFiles.Contains(UnqiueURI));

	return UnqiueURI;
}

bool FGLTFFileBuilder::SaveToFile(const FString& FilePath, const TArray64<uint8>& FileData, bool bOverwrite)
{
	const uint32 WriteFlags = bOverwrite ? FILEWRITE_None : FILEWRITE_NoReplaceExisting;

	if (!FFileHelper::SaveArrayToFile(FileData, *FilePath, &IFileManager::Get(), WriteFlags))
	{
		LogError(FString::Printf(TEXT("Failed to save file: %s"), *FilePath));
		return false;
	}

	return true;
}
