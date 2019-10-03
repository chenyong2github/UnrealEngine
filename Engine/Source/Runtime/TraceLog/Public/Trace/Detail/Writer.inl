// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if UE_TRACE_ENABLED

#include "Atomic.h"

namespace Trace
{

namespace Private
{

////////////////////////////////////////////////////////////////////////////////
#if defined(_MSC_VER)
	#pragma warning(push)
	#pragma warning(disable : 4200) // non-standard zero-sized array
#endif

////////////////////////////////////////////////////////////////////////////////
struct FWriteBuffer
{
	union
	{
		uint8*			Cursor;
		FWriteBuffer*	Next;
	};
	uint32				ThreadId;
	uint8				Data[];
};

#if defined(_MSC_VER)
	#pragma warning(pop)
#endif

////////////////////////////////////////////////////////////////////////////////
extern UE_TRACE_API void* volatile	GLastEvent;
UE_TRACE_API uint8*					Writer_NextBuffer(uint16);
UE_TRACE_API FWriteBuffer*			Writer_GetBuffer();

#if IS_MONOLITHIC
extern thread_local FWriteBuffer* GWriteBuffer;
inline FWriteBuffer* Writer_GetBuffer()
{
	return GWriteBuffer;
}
#endif

} // Private

////////////////////////////////////////////////////////////////////////////////
inline uint8* Writer_BeginLog(uint16 EventUid, uint16 Size)
{
	using namespace Private;

	static const uint32 HeaderSize = sizeof(void*) + sizeof(uint32);
	uint32 AllocSize = ((Size + HeaderSize) + 7) & ~7;

	FWriteBuffer* Buffer = Writer_GetBuffer();
	uint8* Cursor = (Buffer->Cursor -= AllocSize);
	if (UNLIKELY(PTRINT(Cursor) < PTRINT(Buffer->Data)))
	{
		Cursor = Writer_NextBuffer(AllocSize);
	}

	uint32* Out = (uint32*)(Cursor + sizeof(void*));
	Out[0] = (uint32(Size) << 16)|uint32(EventUid);
	return (uint8*)(Out + 1);
}

////////////////////////////////////////////////////////////////////////////////
inline void Writer_EndLog(uint8* EventData)
{
	using namespace Private;

	EventData -= sizeof(void*) + sizeof(uint32);

	// Add the event into the master linked list of events.
	while (true)
	{
		void* Expected = AtomicLoadRelaxed(&GLastEvent);
		*(void**)EventData = Expected;
		if (AtomicCompareExchangeRelease(&GLastEvent, (void*)EventData, Expected))
		{
			break;
		}
	}
}

} // namespace Trace

#endif // UE_TRACE_ENABLED
