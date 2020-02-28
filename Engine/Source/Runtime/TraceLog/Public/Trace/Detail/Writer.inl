// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if UE_TRACE_ENABLED

#include "Atomic.h"
#include "Protocol.h"

namespace Trace {
namespace Private {

////////////////////////////////////////////////////////////////////////////////
struct FWriteBuffer
{
	uint32						Overflow;
	uint16						Size;
	uint16						ThreadId;
	FWriteBuffer* __restrict	NextThread;
	FWriteBuffer* __restrict	NextBuffer;
	uint8* __restrict			Cursor;
	uint8* __restrict volatile	Committed;
	uint8* __restrict			Reaped;
	UPTRINT volatile			EtxOffset;
};

////////////////////////////////////////////////////////////////////////////////
TRACELOG_API FWriteBuffer*		Writer_NextBuffer(int32);
TRACELOG_API FWriteBuffer*		Writer_GetBuffer();

////////////////////////////////////////////////////////////////////////////////
#if IS_MONOLITHIC
extern thread_local FWriteBuffer* GTlsWriteBuffer;
inline FWriteBuffer* Writer_GetBuffer()
{
	return GTlsWriteBuffer;
}
#endif // IS_MONOLITHIC

} // namespace Private
} // namespace Trace

#endif // UE_TRACE_ENABLED
