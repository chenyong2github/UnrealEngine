// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Detail/Trace.h"
#include "Detail/Field.h"

////////////////////////////////////////////////////////////////////////////////
namespace Trace
{

UE_TRACE_API bool	SendTo(const TCHAR* Host) UE_TRACE_IMPL(false);
UE_TRACE_API bool	WriteTo(const TCHAR* Path) UE_TRACE_IMPL(false);
UE_TRACE_API uint32 ToggleEvent(const TCHAR* Wildcard, bool bState) UE_TRACE_IMPL(false);

} // namespace Trace

////////////////////////////////////////////////////////////////////////////////
#define UE_TRACE_EVENT_DEFINE(LoggerName, EventName)			TRACE_PRIVATE_EVENT_DEFINE(LoggerName, EventName)
#define UE_TRACE_EVENT_BEGIN(LoggerName, EventName, ...)		TRACE_PRIVATE_EVENT_BEGIN(LoggerName, EventName, ##__VA_ARGS__)
#define UE_TRACE_EVENT_BEGIN_EXTERN(LoggerName, EventName, ...)	TRACE_PRIVATE_EVENT_BEGIN_EXTERN(LoggerName, EventName, ##__VA_ARGS__)
#define UE_TRACE_EVENT_FIELD(FieldType, FieldName)				TRACE_PRIVATE_EVENT_FIELD(FieldType, FieldName)
#define UE_TRACE_EVENT_END()									TRACE_PRIVATE_EVENT_END()
#define UE_TRACE_EVENT_IS_ENABLED(LoggerName, EventName)		TRACE_PRIVATE_EVENT_IS_ENABLED(LoggerName, EventName)
#define UE_TRACE_LOG(LoggerName, EventName, ...)				TRACE_PRIVATE_LOG(LoggerName, EventName, ##__VA_ARGS__)

#include "Detail/EventDef.inl"
#include "Detail/Writer.inl"
