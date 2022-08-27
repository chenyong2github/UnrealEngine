// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Builders/GLTFBufferBuilder.h"

enum class ERGBFormat : int8;

class FGLTFImageBuilder : public FGLTFBufferBuilder
{
public:

	FGLTFJsonImageIndex AddImage(const TArray64<uint8>& RawData, int32 Width, int32 Height, ERGBFormat RawFormat, int32 BitDepth, bool bFloatFormat, const FString& Name = TEXT(""), EGLTFJsonMimeType MimeType = EGLTFJsonMimeType::PNG, int32 Quality = 100);

	FGLTFJsonImageIndex AddImage(const FTextureSource& Image, const FString& Name = TEXT(""), EGLTFJsonMimeType MimeType = EGLTFJsonMimeType::PNG, int32 Quality = 100);

	virtual bool Serialize(FArchive& Archive, const FString& FilePath) override;

private:

	TMap<FGLTFJsonImageIndex, TArray64<uint8>> ImageDataLookup;
};
