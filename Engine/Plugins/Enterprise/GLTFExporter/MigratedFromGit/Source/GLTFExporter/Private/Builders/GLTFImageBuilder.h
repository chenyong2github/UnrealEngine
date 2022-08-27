// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Builders/GLTFBufferBuilder.h"

enum class ERGBFormat : int8;

class FGLTFImageBuilder : public FGLTFBufferBuilder
{
public:

	FGLTFImageBuilder(const UGLTFExportOptions* ExportOptions);

	FGLTFJsonImageIndex AddImage(const void* CompressedData, int64 CompressedByteLength, EGLTFJsonMimeType MimeType, const FString& Name);
	FGLTFJsonImageIndex AddImage(const void* RawData, int64 ByteLength, FIntPoint Size, ERGBFormat Format, int32 BitDepth, const FString& Name);

	FGLTFJsonImageIndex AddImage(const FByteBulkData& BulkData, EGLTFJsonMimeType MimeType, const FString& Name);
	FGLTFJsonImageIndex AddImage(const FByteBulkData& BulkData, FIntPoint Size, ERGBFormat Format, int32 BitDepth, const FString& Name);

	FGLTFJsonImageIndex AddImage(const FColor* Pixels, FIntPoint Size, const FString& Name);

	virtual bool Serialize(FArchive& Archive, const FString& FilePath) override;

private:

	TMap<FGLTFJsonImageIndex, TArray64<uint8>> ImageDataLookup;
};
