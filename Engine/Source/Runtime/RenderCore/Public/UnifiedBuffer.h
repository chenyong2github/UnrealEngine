// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "Containers/DynamicRHIResourceArray.h"

/*
 * Can store arbitrary data so long as it follows alignment restrictions. Intended mostly for read only data uploaded from CPU.
 * Allows sparse allocations and updates from CPU.
 * Float4 versions exist for platforms that don't yet support byte address buffers.
 */

template<typename ResourceType>
extern RENDERCORE_API void MemsetResource(FRHICommandList& RHICmdList, const ResourceType& DstBuffer, uint32 Value, uint32 NumBytes, uint32 DstOffset = 0);
template<typename ResourceType>
extern RENDERCORE_API void MemcpyResource(FRHICommandList& RHICmdList, const ResourceType& DstBuffer, const ResourceType& SrcBuffer, uint32 NumBytes, uint32 DstOffset = 0, uint32 SrcOffset = 0);
template<typename ResourceType>
extern RENDERCORE_API bool ResizeResourceIfNeeded(FRHICommandList& RHICmdList, ResourceType& Texture, uint32 NumBytes, const TCHAR* DebugName);

class FScatterUploadBuffer
{
public:
	enum { PrimitiveDataStrideInFloat4s = 36 };

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

	template<typename ResourceType>
	RENDERCORE_API void ResourceUploadTo(FRHICommandList& RHICmdList, ResourceType& DstBuffer, bool bFlush = false);

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