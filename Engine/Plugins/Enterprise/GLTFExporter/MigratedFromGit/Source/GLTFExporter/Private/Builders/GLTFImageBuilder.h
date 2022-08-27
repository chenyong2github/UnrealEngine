// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Builders/GLTFBufferBuilder.h"

enum class ERGBFormat : int8;

class FGLTFImageBuilder : public FGLTFBufferBuilder
{
protected:

	FGLTFImageBuilder();

public:

	FGLTFJsonImageIndex AddImage(const void* CompressedData, int64 CompressedByteLength, EGLTFJsonMimeType MimeType, const FString& Name);
	FGLTFJsonImageIndex AddImage(const void* RawData, int64 ByteLength, FIntPoint Size, ERGBFormat Format, int32 BitDepth, const FString& Name);

	FGLTFJsonImageIndex AddImage(const FColor* Pixels, FIntPoint Size, const FString& Name);

	virtual bool Serialize(FArchive& Archive, const FString& FilePath) override;

private:

	TMap<FGLTFJsonImageIndex, TArray64<uint8>> ImageDataLookup;

	FGLTFJsonImageIndex FindImage(const void* CompressedData, int64 CompressedByteLength, EGLTFJsonMimeType MimeType) const;
};
