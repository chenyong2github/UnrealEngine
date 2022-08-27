// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine.h"
#include "Json/GLTFJsonEnums.h"

enum class EImageFormat : signed char;
enum class ERGBFormat : int8;

struct FGLTFBuilderUtility
{
	static FString GetMeshName(const UStaticMesh* StaticMesh, int32 LODIndex)
	{
		FString Name;

		if (StaticMesh != nullptr)
		{
			StaticMesh->GetName(Name);
			if (LODIndex != 0) Name += TEXT("_LOD") + FString::FromInt(LODIndex);
		}

		return Name;
	}

	static bool CompressImage(const void* RawData, int64 ByteLength, int32 InWidth, int32 InHeight, ERGBFormat InRawFormat, int32 InBitDepth, TArray64<uint8>& OutCompressedData, EImageFormat OutCompressionFormat, int32 OutCompressionQuality);

	static const TCHAR* GetFileExtension(EGLTFJsonMimeType MimeType)
	{
		switch (MimeType)
		{
			case EGLTFJsonMimeType::PNG:  return TEXT(".png");
			case EGLTFJsonMimeType::JPEG: return TEXT(".jpg");
			default:                      return TEXT(".unknown");
		}
	}
};
