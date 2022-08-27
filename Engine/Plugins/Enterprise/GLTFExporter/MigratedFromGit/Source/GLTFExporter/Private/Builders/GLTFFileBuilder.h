// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Builders/GLTFTaskBuilder.h"
#include "Builders/GLTFMemoryArchive.h"

class FGLTFFileBuilder : public FGLTFTaskBuilder
{
public:

	FGLTFFileBuilder(const FString& FileName, const UGLTFExportOptions* ExportOptions);

	FString AddExternalFile(const FString& URI, const TSharedPtr<FGLTFMemoryArchive>& Archive = MakeShared<FGLTFMemoryArchive>());

	const TMap<FString, TSharedPtr<FGLTFMemoryArchive>>& GetExternalFiles() const;

	bool WriteExternalFiles(const FString& DirPath, bool bOverwrite = true);

private:

	TMap<FString, TSharedPtr<FGLTFMemoryArchive>> ExternalFiles;

	FString GetUniqueURI(const FString& URI) const;

protected:

	bool SaveToFile(const FString& FilePath, const TArray64<uint8>& FileData, bool bOverwrite);
};
