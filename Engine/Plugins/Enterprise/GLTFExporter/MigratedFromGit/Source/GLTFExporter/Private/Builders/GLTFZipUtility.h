// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Builders/GLTFMessageBuilder.h"
#include "CoreMinimal.h"

class FGLTFZipUtility
{
public:

	static bool ExtractToDirectory(const FString& SourceFilePath, const FString& DestinationDirectoryPath, FGLTFMessageBuilder& Builder);

private:

	static bool ExtractCurrentFile(void* ZipFile, const FString& DestinationDirectoryPath, FGLTFMessageBuilder& Builder);
};
