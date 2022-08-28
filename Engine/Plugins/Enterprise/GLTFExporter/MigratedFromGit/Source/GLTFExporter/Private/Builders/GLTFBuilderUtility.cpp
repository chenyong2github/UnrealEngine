// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFBuilderUtility.h"

FString FGLTFBuilderUtility::GetMeshName(const UStaticMesh* StaticMesh, int32 LODIndex)
{
	FString Name;

	if (StaticMesh != nullptr)
	{
		StaticMesh->GetName(Name);
		if (LODIndex != 0) Name += TEXT("_LOD") + FString::FromInt(LODIndex);
	}

	return Name;
}

const TCHAR* FGLTFBuilderUtility::GetFileExtension(EGLTFJsonMimeType MimeType)
{
	switch (MimeType)
	{
		case EGLTFJsonMimeType::PNG:  return TEXT(".png");
		case EGLTFJsonMimeType::JPEG: return TEXT(".jpg");
		default:                      return TEXT(".unknown");
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
