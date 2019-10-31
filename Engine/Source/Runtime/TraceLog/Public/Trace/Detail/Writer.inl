// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
	FWriteBuffer* __restrict	Next;
	uint8* __restrict			Cursor;
	uint8* __restrict volatile	Committed;
	uint8* __restrict			Reaped;
	uint32						ThreadId;
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
extern TRACELOG_API void* volatile		GLastEvent;
TRACELOG_API FWriteBuffer*				Writer_NextBuffer(uint16);

////////////////////////////////////////////////////////////////////////////////
#if IS_MONOLITHIC
extern thread_local FWriteTlsContext TlsContext;
inline FWriteBuffer* Writer_GetBuffer()
{
	return TlsContext.GetBuffer();
}
#else
TRACELOG_API FWriteBuffer* Writer_GetBuffer();
#endif

} // Private



////////////////////////////////////////////////////////////////////////////////
struct FLogInstance
{
	uint8*					Ptr;
	Private::FWriteBuffer*	Internal;
};

////////////////////////////////////////////////////////////////////////////////
inline FLogInstance Writer_BeginLog(uint16 EventUid, uint16 Size)
{
	using namespace Private;

	static const uint32 HeaderSize = sizeof(FEventHeader);
	uint32 AllocSize = Size + HeaderSize;

	FWriteBuffer* Buffer = Writer_GetBuffer();
	uint8* Cursor = (Buffer->Cursor += AllocSize);
	if (UNLIKELY(UPTRINT(Cursor) > UPTRINT(Buffer)))
	{
		Buffer = Writer_NextBuffer(AllocSize);
		Cursor = Buffer->Cursor;
	}
	Cursor -= AllocSize;

	uint32* Out = (uint32*)Cursor;
	Out[0] = (uint32(Size) << 16)|uint32(EventUid);
	return {(uint8*)(Out + 1), Buffer};
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
