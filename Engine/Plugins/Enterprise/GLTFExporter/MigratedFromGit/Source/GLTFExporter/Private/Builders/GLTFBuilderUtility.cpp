// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFBuilderUtility.h"

const TCHAR* FGLTFBuilderUtility::GetFileExtension(EGLTFJsonMimeType MimeType)
{
	switch (MimeType)
	{
		case EGLTFJsonMimeType::PNG:  return TEXT(".png");
		case EGLTFJsonMimeType::JPEG: return TEXT(".jpg");
		default:
			checkNoEntry();
			return TEXT(".unknown");
	}
}

FString FGLTFBuilderUtility::GetUniqueFilename(const FString& BaseFilename, const FString& FileExtension, const TSet<FString>& UniqueFilenames)
{
	FString Filename = BaseFilename + FileExtension;
	if (!UniqueFilenames.Contains(Filename))
	{
		return Filename;
	}

	int32 Suffix = 1;
	do
	{
		Filename = BaseFilename + TEXT('_') + FString::FromInt(Suffix) + FileExtension;
		Suffix++;
	}
	while (UniqueFilenames.Contains(Filename));

	return Filename;
}

bool FGLTFBuilderUtility::IsGlbFile(const FString& Filename)
{
	return FPaths::GetExtension(Filename).Equals(TEXT("glb"), ESearchCase::IgnoreCase);
}
