// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if UE_TRACE_ENABLED

#include <atomic>

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
template <typename Type>
using TTraceAtomic = std::atomic<Type>;

namespace Private
{

#if defined(_MSC_VER)
	#pragma warning(push)
	#pragma warning(disable : 4200) // non-standard zero-sized array
#endif

////////////////////////////////////////////////////////////////////////////////
struct FBuffer
{
	FBuffer()
	: Next(nullptr)
	, Used(sizeof(FBuffer))
	{
	}

	union
	{
		TTraceAtomic<FBuffer*>	Next;
		char					Padding0[PLATFORM_CACHE_LINE_SIZE];
	};
	union
	{
		TTraceAtomic<uint32>	Used;
		char					Padding1[PLATFORM_CACHE_LINE_SIZE];
	};
	uint32						Final;
	alignas(void*) uint8		Data[];
};

#if defined(_MSC_VER)
	#pragma warning(pop)
#endif

extern UE_TRACE_API TTraceAtomic<FBuffer*> GActiveBuffer;

static const uint16		BufferSizePow2	= 19;
static const uint32		BufferSize		= 1 << BufferSizePow2;
static const uint32		BufferSizeMask	= BufferSize - 1;
static const uint32		BufferRefBit	= BufferSize << 1;

extern UE_TRACE_API void* Writer_NextBuffer(FBuffer*, uint32, uint32);

} // Private

////////////////////////////////////////////////////////////////////////////////
inline uint8* Writer_BeginLog(uint16 EventUid, uint16 Size)
{
	using namespace Private;

	uint32 AllocSize = Size;
	AllocSize += sizeof(uint32);
	AllocSize += BufferRefBit;

	// Fetch buffer and claim some space in it.
	FBuffer* Buffer = GActiveBuffer.load(std::memory_order_acquire);
	uint32 PrevUsed = Buffer->Used.fetch_add(AllocSize, std::memory_order_relaxed);
	uint32 Used = PrevUsed + AllocSize;

	uint32* Out = (uint32*)(UPTRINT(Buffer) + (BufferSizeMask & PrevUsed));

	// Did we exhaust the active buffer?
	if (UNLIKELY(Used & BufferSize))
	{
		Out = (uint32*)Writer_NextBuffer(Buffer, PrevUsed, AllocSize);
		Buffer = (FBuffer*)(UPTRINT(Out) & ~UPTRINT(BufferSizeMask));
	}

	Out[0] = (uint32(Size) << 16)|uint32(EventUid);
	return (uint8*)(Out + 1);
}

////////////////////////////////////////////////////////////////////////////////
inline void Writer_EndLog(uint8* EventData)
{
	using namespace Private;
	auto* Buffer = (FBuffer*)(UPTRINT(EventData) & ~UPTRINT(BufferSizeMask));
	Buffer->Used.fetch_sub(BufferRefBit, std::memory_order_release);
}

} // namespace Trace

#endif // UE_TRACE_ENABLED
