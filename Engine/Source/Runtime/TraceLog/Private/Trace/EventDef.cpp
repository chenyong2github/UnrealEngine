// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trace/Detail/EventDef.h"

#if UE_TRACE_ENABLED

#include "Trace/Detail/Field.h"
#include "Trace/Detail/Writer.inl"

namespace Trace
{

namespace Private
{

void Writer_EventCreate(FEventDef*, const FLiteralName&, const FLiteralName&, const FFieldDesc*, uint32, uint32);

} // namespace Private



////////////////////////////////////////////////////////////////////////////////
void FEventDef::Create(
	FEventDef* Target,
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
