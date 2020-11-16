// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trace/Detail/Important/SharedBuffer.h"

#if UE_TRACE_ENABLED

#include "CoreTypes.h"
#include "Trace/Detail/Atomic.h"
#include "Trace/Detail/Important/ImportantLogScope.inl"

namespace Trace {
namespace Private {

////////////////////////////////////////////////////////////////////////////////
void*	Writer_MemoryAllocate(SIZE_T, uint32);
void	Writer_MemoryFree(void*, uint32);
void	Writer_CacheData(uint8*, uint32);
void	Writer_InitializeCache();
void	Writer_ShutdownCache();

////////////////////////////////////////////////////////////////////////////////
static FSharedBuffer	GNullSharedBuffer	= { 0, FSharedBuffer::RefInit };
FSharedBuffer* volatile GSharedBuffer		= &GNullSharedBuffer;
static FSharedBuffer*	GTailBuffer;		// = nullptr
static uint32			GTailPreSent;		// = 0
static const uint32		GBlockSize			= 1024;//16 << 10;
extern FStatistics		GTraceStatistics;

////////////////////////////////////////////////////////////////////////////////
static FSharedBuffer* Writer_CreateSharedBuffer(uint32 SizeHint=0)
{
	const uint32 OverheadSize = sizeof(FSharedBuffer) + sizeof(uint32);

	uint32 BlockSize = GBlockSize;
	if (SizeHint > GBlockSize - OverheadSize)
	{
		BlockSize += SizeHint - GBlockSize;
		BlockSize += sizeof(FSharedBuffer);
		BlockSize += GBlockSize - 1;
		BlockSize &= ~(GBlockSize - 1);
	}

	void* Block = Writer_MemoryAllocate(BlockSize, alignof(FSharedBuffer));
	auto* Buffer = (FSharedBuffer*)(UPTRINT(Block) + BlockSize) - 1;

	Buffer->Size = uint32(UPTRINT(Buffer) - UPTRINT(Block));
	Buffer->Size -= sizeof(uint32); // to preceed event data with a small header when sending.
	Buffer->Cursor = (Buffer->Size << FSharedBuffer::CursorShift) | FSharedBuffer::RefInit;
	Buffer->Next = nullptr;
	Buffer->Final = 0;

	return Buffer;
}

////////////////////////////////////////////////////////////////////////////////
FNextSharedBuffer Writer_NextSharedBuffer(FSharedBuffer* Buffer, int32 RegionStart, int32 NegSizeAndRef)
{
	FSharedBuffer* NextBuffer;
	while (true)
	{
		bool bBufferOwner = (RegionStart >= 0);
		if (LIKELY(bBufferOwner))
		{
			uint32 Size = -NegSizeAndRef >> FSharedBuffer::CursorShift;
			NextBuffer = Writer_CreateSharedBuffer(Size);
			Buffer->Next = NextBuffer;
			Buffer->Final = RegionStart >> FSharedBuffer::CursorShift;
			AtomicStoreRelease(&GSharedBuffer, NextBuffer);
		}
		else
		{
			for (;; PlatformYield())
			{
				NextBuffer = AtomicLoadAcquire(&GSharedBuffer);
				if (NextBuffer != Buffer)
				{
					break;
				}
			}
		}

		AtomicAddRelease(&(Buffer->Cursor), int32(FSharedBuffer::RefBit));

		// Try and allocate some space in the next buffer.
		RegionStart = AtomicAddRelaxed(&(NextBuffer->Cursor), NegSizeAndRef);
		if (LIKELY(RegionStart + NegSizeAndRef >= 0))
		{
			break;
		}

		Buffer = NextBuffer;
	}

	return { NextBuffer, RegionStart };
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_RetireSharedBufferImpl()
{
	// Send any unsent data.
	uint8* Data = (uint8*)GTailBuffer - GTailBuffer->Size + GTailPreSent;
	if (uint32 SendSize = UPTRINT(GTailBuffer) - UPTRINT(Data) - GTailBuffer->Final)
	{
		Writer_CacheData(Data, SendSize);
	}

	FSharedBuffer* Temp = GTailBuffer->Next;
	void* Block = (uint8*)GTailBuffer - GTailBuffer->Size - sizeof(uint32);
	Writer_MemoryFree(Block, GBlockSize);
	GTailBuffer = Temp;
	GTailPreSent = 0;
}

////////////////////////////////////////////////////////////////////////////////
static void Writer_RetireSharedBuffer()
{
	// Spin until the buffer's no longer being used.
	for (;; PlatformYield())
	{
		int32 TailCursor = AtomicLoadAcquire(&(GTailBuffer->Cursor));
		if (LIKELY(((TailCursor + 1) & FSharedBuffer::RefInit) == 0))
		{
			break;
		}
	}

	Writer_RetireSharedBufferImpl();
}

////////////////////////////////////////////////////////////////////////////////
void Writer_UpdateSharedBuffers()
{
	FSharedBuffer* HeadBuffer = AtomicLoadAcquire(&GSharedBuffer);
	while (true)
	{
		if (GTailBuffer != HeadBuffer)
		{
			Writer_RetireSharedBuffer();
			continue;
		}

		int32 Cursor = AtomicLoadAcquire(&(HeadBuffer->Cursor));
		if ((Cursor + 1) & FSharedBuffer::RefInit)
		{
			continue;
		}

		Cursor = Cursor >> FSharedBuffer::CursorShift;
		if (Cursor < 0)
		{
			Writer_RetireSharedBufferImpl();
			break;
		}

		uint32 PreSentBias = HeadBuffer->Size - GTailPreSent;
		if (uint32 Sendable = PreSentBias - Cursor)
		{
			uint8* Data = (uint8*)(UPTRINT(HeadBuffer) - PreSentBias);
			Writer_CacheData(Data, Sendable);
			GTailPreSent += Sendable;
		}

		break;
	}
}

////////////////////////////////////////////////////////////////////////////////
void Writer_InitializeSharedBuffers()
{
	Writer_InitializeCache();

	FSharedBuffer* Buffer = Writer_CreateSharedBuffer();

	GTailBuffer = Buffer;
	GTailPreSent = 0;

	AtomicStoreRelease(&GSharedBuffer, Buffer);
}

////////////////////////////////////////////////////////////////////////////////
void Writer_ShutdownSharedBuffers()
{
	Writer_ShutdownCache();
}

} // namespace Private
} // namespace Trace

#endif // UE_TRACE_ENABLED
