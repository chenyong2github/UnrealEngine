// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Json/GLTFJsonEnums.h"

struct FGLTFBuilderUtility
{
	static FString GetLODName(const UObject* Object, int32 LODIndex);

	static const TCHAR* GetFileExtension(EGLTFJsonMimeType MimeType);

	static FString GetUniqueFilename(const FString& BaseFilename, const FString& FileExtension, const TSet<FString>& UniqueFilenames);
};
