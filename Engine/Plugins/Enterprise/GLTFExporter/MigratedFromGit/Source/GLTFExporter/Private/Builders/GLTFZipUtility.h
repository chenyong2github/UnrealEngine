// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FGLTFZipUtility
{
public:

	static bool ExtractToDirectory(const FString& ArchiveFile, const FString& TargetDirectory);

private:

	static bool ExtractCurrentFile(void* ZipHandle, const FString& TargetDirectory);
};
