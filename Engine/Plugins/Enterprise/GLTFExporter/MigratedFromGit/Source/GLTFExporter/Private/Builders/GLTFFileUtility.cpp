// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFFileUtility.h"

const TCHAR* FGLTFFileUtility::GetFileExtension(EGLTFJsonMimeType MimeType)
{
	switch (MimeType)
	{
		case EGLTFJsonMimeType::PNG:  return TEXT(".png");
		case EGLTFJsonMimeType::JPEG: return TEXT(".jpg");
		default:
			checkNoEntry();
			return TEXT("");
	}
}

FString FGLTFFileUtility::GetUniqueFilename(const FString& BaseFilename, const FString& FileExtension, const TSet<FString>& UniqueFilenames)
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

bool FGLTFFileUtility::IsGlbFile(const FString& Filename)
{
	return FPaths::GetExtension(Filename).Equals(TEXT("glb"), ESearchCase::IgnoreCase);
}

