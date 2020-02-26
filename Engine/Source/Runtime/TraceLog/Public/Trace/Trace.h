// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Detail/Trace.h"

////////////////////////////////////////////////////////////////////////////////
#if UE_TRACE_ENABLED
#	define UE_TRACE_IMPL(...)
#	define UE_TRACE_API			TRACELOG_API
#else
#	define UE_TRACE_IMPL(...)	{ return __VA_ARGS__; }
#	define UE_TRACE_API			inline
#endif

////////////////////////////////////////////////////////////////////////////////
namespace Trace
{

struct FInitializeDesc
{
	bool			bUseWorkerThread	= true;
	uint32			MaxMemoryHintMb		= 64;
};

UE_TRACE_API void	Initialize(const FInitializeDesc& Desc) UE_TRACE_IMPL();
UE_TRACE_API void	Update() UE_TRACE_IMPL();
UE_TRACE_API bool	SendTo(const TCHAR* Host, uint32 Port=0) UE_TRACE_IMPL(false);
UE_TRACE_API bool	WriteTo(const TCHAR* Path) UE_TRACE_IMPL(false);
UE_TRACE_API bool	ToggleChannel(const TCHAR* ChannelName, bool bEnabled) UE_TRACE_IMPL(false);

} // namespace Trace

////////////////////////////////////////////////////////////////////////////////
#define UE_TRACE_EVENT_DEFINE(LoggerName, EventName)			TRACE_PRIVATE_EVENT_DEFINE(LoggerName, EventName)
#define UE_TRACE_EVENT_BEGIN(LoggerName, EventName, ...)		TRACE_PRIVATE_EVENT_BEGIN(LoggerName, EventName, ##__VA_ARGS__)
#define UE_TRACE_EVENT_BEGIN_EXTERN(LoggerName, EventName, ...)	TRACE_PRIVATE_EVENT_BEGIN_EXTERN(LoggerName, EventName, ##__VA_ARGS__)
#define UE_TRACE_EVENT_FIELD(FieldType, FieldName)				TRACE_PRIVATE_EVENT_FIELD(FieldType, FieldName)
#define UE_TRACE_EVENT_END()									TRACE_PRIVATE_EVENT_END()
#define UE_TRACE_LOG(LoggerName, EventName, ChannelsExpr, ...)	TRACE_PRIVATE_LOG(LoggerName, EventName, ChannelsExpr, ##__VA_ARGS__)

////////////////////////////////////////////////////////////////////////////////
#define UE_TRACE_CHANNEL(ChannelName)							TRACE_PRIVATE_CHANNEL(ChannelName)
#define UE_TRACE_CHANNEL_EXTERN(ChannelName, ...)				TRACE_PRIVATE_CHANNEL_EXTERN(ChannelName, ##__VA_ARGS__)
#define UE_TRACE_CHANNEL_DEFINE(ChannelName)					TRACE_PRIVATE_CHANNEL_DEFINE(ChannelName)
#define UE_TRACE_CHANNELEXPR_IS_ENABLED(ChannelsExpr)			TRACE_PRIVATE_CHANNELEXPR_IS_ENABLED(ChannelsExpr)
