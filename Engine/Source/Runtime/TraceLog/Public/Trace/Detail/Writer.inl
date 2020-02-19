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
	FWriteBuffer* __restrict	NextThread;
	FWriteBuffer* __restrict	NextBuffer;
	uint8* __restrict			Cursor;
	uint8* __restrict volatile	Committed;
	uint8* __restrict			Reaped;
	UPTRINT volatile			EtxOffset;
};



////////////////////////////////////////////////////////////////////////////////
extern TRACELOG_API uint32 volatile	GLogSerial;
TRACELOG_API FWriteBuffer*			Writer_NextBuffer(int32);
TRACELOG_API FWriteBuffer*			Writer_GetBuffer();

////////////////////////////////////////////////////////////////////////////////
#if IS_MONOLITHIC
extern thread_local FWriteBuffer* GTlsWriteBuffer;
inline FWriteBuffer* Writer_GetBuffer()
{
	return GTlsWriteBuffer;
}
#endif // IS_MONOLITHIC

} // Private



////////////////////////////////////////////////////////////////////////////////
struct FLogInstance
{
	uint8*	Ptr;
	void*	Internal;
};

////////////////////////////////////////////////////////////////////////////////
template <class HeaderType>
inline FLogInstance Writer_BeginLogPrelude(uint16 Size, bool bMaybeHasAux)
{
	using namespace Private;

	uint32 AllocSize = sizeof(HeaderType) + Size + int(bMaybeHasAux);

	FWriteBuffer* Buffer = Writer_GetBuffer();
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
	return {Cursor, Buffer};
}

////////////////////////////////////////////////////////////////////////////////
inline FLogInstance Writer_BeginLog(uint16 EventUid, uint16 Size, bool bMaybeHasAux)
{
	using namespace Private;

	FLogInstance Instance = Writer_BeginLogPrelude<FEventHeaderSync>(Size, bMaybeHasAux);

	// Event header
	auto* Header = (uint16*)(Instance.Ptr - sizeof(FEventHeaderSync::SerialHigh)); // FEventHeader1
	*(uint32*)(Header - 1) = uint32(AtomicIncrementRelaxed(&GLogSerial));
	Header[-2] = Size;
	Header[-3] = EventUid;

	return Instance;
}

////////////////////////////////////////////////////////////////////////////////
inline FLogInstance Writer_BeginLogNoSync(uint16 EventUid, uint16 Size, bool bMaybeHasAux)
{
	using namespace Private;

	FLogInstance Instance = Writer_BeginLogPrelude<FEventHeader>(Size, bMaybeHasAux);

	// Event header
	auto* Header = (uint16*)(Instance.Ptr);
	Header[-1] = Size;
	Header[-2] = EventUid;

	return Instance;

}

////////////////////////////////////////////////////////////////////////////////
inline void Writer_EndLog(FLogInstance Instance)
{
	using namespace Private;
	auto* Buffer = (FWriteBuffer*)(Instance.Internal);
	AtomicStoreRelease<uint8* __restrict>(&(Buffer->Committed), Buffer->Cursor);
}

} // namespace Trace

#endif // UE_TRACE_ENABLED
