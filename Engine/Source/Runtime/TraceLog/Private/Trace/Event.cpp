// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Trace/Private/Event.h"

#if UE_TRACE_ENABLED

#include "Trace/Private/Field.h"
#include "Trace/Private/Writer.inl"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
namespace Private
{

void Writer_EventCreate(FEvent*, const FLiteralName&, const FLiteralName&, const FFieldDesc*, uint32, uint32);

} // namespace Private



////////////////////////////////////////////////////////////////////////////////
void FEvent::Create(
	FEvent* Target,
	const FLiteralName& LoggerName,
	const FLiteralName& EventName,
	const FFieldDesc* FieldDescs,
	uint32 FieldCount,
	uint32 Flags)
{
	Private::Writer_EventCreate(Target, LoggerName, EventName, FieldDescs, FieldCount, Flags);
}

} // namespace Trace

#endif // UE_TRACE_ENABLED
