// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "PrimitiveUniformShaderParameters.h"

void MemsetBuffer(FRHICommandList& RHICmdList, const FRWBufferStructured& DstBuffer, const FVector4& Value, uint32 NumFloat4s, uint32 DstOffsetInFloat4s);
void MemcpyBuffer(FRHICommandList& RHICmdList, const FRWBufferStructured& SrcBuffer, const FRWBufferStructured& DstBuffer, uint32 NumFloat4s, uint32 SrcOffset = 0, uint32 DstOffset = 0);
void MemcpyTextureToTexture(FRHICommandList& RHICmdList, const FTextureRWBuffer2D& SrcBuffer, const FTextureRWBuffer2D& DstBuffer, uint32 SrcOffset, uint32 DstOffset, uint32 NumFloat4s, uint32 FloatsPerLine);
bool ResizeBufferIfNeeded(FRHICommandList& RHICmdList, FRWBufferStructured& Buffer, uint32 NumFloat4s);
bool ResizeTextureIfNeeded(FRHICommandList& RHICmdList, FTextureRWBuffer2D& Texture, uint32 NumFloat4s, uint32 PrimitiveStride);

class FScatterUploadBuilder
{
public:

	FReadBuffer& ScatterBuffer;
	FReadBuffer& UploadBuffer;

	uint32* ScatterData;
	FVector4* UploadData;

	uint32 AllocatedNumScatters;
	uint32 NumScatters;
	uint32 StrideInFloat4s;

public:
	FScatterUploadBuilder(uint32 NumUploads, uint32 InStrideInFloat4s, FReadBuffer& InScatterBuffer, FReadBuffer& InUploadBuffer);

	void UploadTo(FRHICommandList& RHICmdList, FRWBufferStructured& DstBuffer);

	void UploadTo_Flush(FRHICommandList& RHICmdList, FRWBufferStructured& DstBuffer);

	void Add(uint32 Index, const FVector4* Data)
	{
		checkSlow(NumScatters < AllocatedNumScatters);
		checkSlow( ScatterData != nullptr );
		checkSlow( UploadData != nullptr );

		for (uint32 i = 0; i < StrideInFloat4s; i++)
		{
			ScatterData[i] = Index * StrideInFloat4s + i;
			UploadData[i] = Data[i];
		}

		ScatterData += StrideInFloat4s;
		UploadData += StrideInFloat4s;
		NumScatters += StrideInFloat4s;
	}
};

class FTextureScatterUploadBuilder : public FScatterUploadBuilder
{
public:
	FTextureScatterUploadBuilder(uint32 NumUploads, uint32 InStrideInFloat4s, FReadBuffer& InScatterBuffer, FReadBuffer& InUploadBuffer)
		:	FScatterUploadBuilder(NumUploads, InStrideInFloat4s, InScatterBuffer, InUploadBuffer) 
	{}

	static int32 GetMaxPrimitivesUpdate(uint32 NumUploads, uint32 InStrideInFloat4s);
	void TextureUploadTo(FRHICommandList& RHICmdList, FTextureRWBuffer2D& DstTexture, uint32 NumFloat4, uint32 FloatsPerLine);
	void TextureUploadTo_Flush(FRHICommandList& RHICmdList, FTextureRWBuffer2D& DstTexture, uint32 NumFloat4, uint32 FloatsPerLine);
};