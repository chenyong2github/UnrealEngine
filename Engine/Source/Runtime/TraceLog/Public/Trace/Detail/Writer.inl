// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if UE_TRACE_ENABLED

#include "Atomic.h"
#include "Protocol.h"

namespace Trace
{

namespace Private
{

////////////////////////////////////////////////////////////////////////////////
struct FWriteBuffer
{
	uint32						Overflow;
	uint32						ThreadId;
	FWriteBuffer* __restrict	Next;
	uint8* __restrict			Cursor;
	uint8* __restrict volatile	Committed;
	uint8* __restrict			Reaped;
	UPTRINT volatile			EtxOffset;
};



////////////////////////////////////////////////////////////////////////////////
struct FWriteTlsContext
{
							FWriteTlsContext();
							~FWriteTlsContext();
	bool					HasValidBuffer() const;
	void					SetBuffer(FWriteBuffer*);
	uint32					GetThreadId() const;
	FWriteBuffer*			GetBuffer() const { return Buffer; }

private:
	FWriteBuffer*			Buffer;
	uint32					ThreadId;
	static uint8			DefaultBuffer[sizeof(FWriteBuffer)];
	static UPTRINT volatile	ThreadIdCounter;
};



////////////////////////////////////////////////////////////////////////////////
extern TRACELOG_API uint32 volatile	GLogSerial;
TRACELOG_API FWriteBuffer*			Writer_NextBuffer(uint16);
TRACELOG_API FWriteBuffer*			Writer_GetBuffer();

////////////////////////////////////////////////////////////////////////////////
#if IS_MONOLITHIC
extern thread_local FWriteTlsContext TlsContext;
inline FWriteBuffer* Writer_GetBuffer()
{
	return TlsContext.GetBuffer();
}
#endif // IS_MONOLITHIC

} // Private



////////////////////////////////////////////////////////////////////////////////
struct FLogInstance
{
	uint8*					Ptr;
	Private::FWriteBuffer*	Internal;
};

////////////////////////////////////////////////////////////////////////////////
inline FLogInstance Writer_BeginLog(uint16 EventUid, uint16 Size, bool bMaybeHasAux)
{
	using namespace Private;

	FWriteBuffer* Buffer = Writer_GetBuffer();
	uint32 AllocSize = Size + sizeof(FEventHeader) + int(bMaybeHasAux);
	Buffer->Cursor += AllocSize;
	if (UNLIKELY(Buffer->Cursor > (uint8*)Buffer))
	{
		Buffer = Writer_NextBuffer(AllocSize);
	}

	// The auxilary data null terminator.
	if (bMaybeHasAux)
	{
		Buffer->Cursor[-1] = 0;
	}

	uint8* Cursor = Buffer->Cursor - Size - int(bMaybeHasAux);

	// Event header
	auto* Header = (uint16*)(Cursor - sizeof(FEventHeader::SerialHigh)); // FEventHeader1
	*(uint32*)(Header - 1) = uint32(AtomicIncrementRelaxed(&GLogSerial));
	Header[-2] = Size;
	Header[-3] = EventUid;

	return {Cursor, Buffer};
}

////////////////////////////////////////////////////////////////////////////////
inline void Writer_EndLog(FLogInstance Instance)
{
	using namespace Private;
	FWriteBuffer* Buffer = Instance.Internal;
	Private::AtomicStoreRelease<uint8* __restrict>(&(Buffer->Committed), Buffer->Cursor);
}

} // namespace Trace

#endif // UE_TRACE_ENABLED
