// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "Containers/DynamicRHIResourceArray.h"

/*
 * Can store arbitrary data so long as it follows alignment restrictions. Intended mostly for read only data uploaded from CPU.
 * Allows sparse allocations and updates from CPU.
 * Float4 versions exist for platforms that don't yet support byte address buffers.
 */
 
// Must be aligned to 4 bytes
RENDERCORE_API void MemsetBuffer( FRHICommandList& RHICmdList, const FRWByteAddressBuffer& DstBuffer, uint32 Value, uint32 NumBytes, uint32 DstOffset = 0 );
RENDERCORE_API void MemcpyBuffer( FRHICommandList& RHICmdList, const FRWByteAddressBuffer& DstBuffer, const FRWByteAddressBuffer& SrcBuffer, uint32 NumBytes, uint32 DstOffset = 0, uint32 SrcOffset = 0 );
RENDERCORE_API bool ResizeBuffer( FRHICommandList& RHICmdList, FRWByteAddressBuffer& Buffer, uint32 NumBytes, const TCHAR* DebugName );

// Must be aligned to 16 bytes
RENDERCORE_API void MemsetBufferFloat4( FRHICommandList& RHICmdList, const FRWBufferStructured& DstBuffer, uint32 Value, uint32 NumBytes, uint32 DstOffset = 0 );
RENDERCORE_API void MemcpyBufferFloat4( FRHICommandList& RHICmdList, const FRWBufferStructured& DstBuffer, const FRWBufferStructured& SrcBuffer, uint32 NumBytes, uint32 SrcOffset = 0, uint32 DstOffset = 0 );
RENDERCORE_API bool ResizeBufferFloat4( FRHICommandList& RHICmdList, FRWBufferStructured& Buffer, uint32 NumBytes, const TCHAR* InDebugName );

RENDERCORE_API void MemcpyTextureToTexture( FRHICommandList& RHICmdList, const FTextureRWBuffer2D& SrcTexture, const FTextureRWBuffer2D& DstTexture, uint32 SrcOffset, uint32 DstOffset, uint32 NumBytes, uint32 BytesPerLine );
RENDERCORE_API bool ResizeTexture( FRHICommandList& RHICmdList, FTextureRWBuffer2D& Texture, uint32 NumBytes, uint32 BytesPerLine );

class FScatterUploadBuffer
{
public:
	FByteAddressBuffer ScatterBuffer;
	FByteAddressBuffer UploadBuffer;

	uint32*	ScatterData	= nullptr;
	uint8*	UploadData	= nullptr;

	uint32 	NumScatters = 0;
	uint32 	MaxScatters = 0;
	uint32	NumScattersAllocated = 0;
	uint32	NumBytesPerElement = 0;

	bool	bFloat4Buffer = false;

	RENDERCORE_API void Init( uint32 NumElements, uint32 InNumBytesPerElement, bool bInFloat4Buffer, const TCHAR* DebugName );
	RENDERCORE_API void UploadToBuffer( FRHICommandList& RHICmdList, FRHIUnorderedAccessView* DstBufferUAV, bool bFlush );
	RENDERCORE_API void UploadToTexture( FRHICommandList& RHICmdList, FTextureRWBuffer2D& DstTexture, uint32 BytesPerLine, bool bFlush );

	void Add( uint32 Index, const void* Data, uint32 Num = 1 )
	{
		void* Dst = Add_GetRef( Index, Num );
		FMemory::Memcpy( Dst, Data, Num * NumBytesPerElement );
	}

	void* Add_GetRef( uint32 Index, uint32 Num = 1 )
	{
		checkSlow( NumScatters + Num <= MaxScatters );
		checkSlow( ScatterData != nullptr );
		checkSlow( UploadData != nullptr );

		for( uint32 i = 0; i < Num; i++ )
		{
			ScatterData[ i ] = Index + i;
		}

		void* Result = UploadData;

		ScatterData += Num;
		UploadData += Num * NumBytesPerElement;
		NumScatters += Num;
		return Result;
	}

	void Release()
	{
		ScatterBuffer.Release();
		UploadBuffer.Release();
	}

	uint32 GetNumBytes() const
	{
		return ScatterBuffer.NumBytes + UploadBuffer.NumBytes;
	}
};