// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

////////////////////////////////////////////////////////////////////////////////
#if !defined(UE_TRACE_ENABLED)
#	if !UE_BUILD_SHIPPING && !IS_PROGRAM
#		if PLATFORM_WINDOWS || PLATFORM_PS4 || PLATFORM_XBOXONE
#			define UE_TRACE_ENABLED	1
#		endif
#	endif
#endif // !IS_PROGRAM

#if !defined(UE_TRACE_ENABLED)
#	define UE_TRACE_ENABLED		0
#endif

#if UE_TRACE_ENABLED
#	define UE_TRACE_IMPL(...)
#	define UE_TRACE_API			TRACELOG_API
#else
#	define UE_TRACE_IMPL(...)	{ return __VA_ARGS__; }
#	define UE_TRACE_API			inline
#endif



#if UE_TRACE_ENABLED

#define TRACE_PRIVATE_EVENT_DEFINE(LoggerName, EventName) \
	Trace::FEvent LoggerName##EventName##Event;

#define TRACE_PRIVATE_EVENT_BEGIN(LoggerName, EventName, ...) \
	TRACE_PRIVATE_EVENT_BEGIN_IMPL(static, LoggerName, EventName, ##__VA_ARGS__)

#define TRACE_PRIVATE_EVENT_BEGIN_EXTERN(LoggerName, EventName, ...) \
	TRACE_PRIVATE_EVENT_BEGIN_IMPL(extern, LoggerName, EventName, ##__VA_ARGS__)

#define TRACE_PRIVATE_EVENT_BEGIN_IMPL(LinkageType, LoggerName, EventName, ...) \
	LinkageType TRACE_PRIVATE_EVENT_DEFINE(LoggerName, EventName) \
	struct F##LoggerName##EventName##Fields \
	{ \
		static void FORCENOINLINE Initialize() \
		{ \
			static const bool bOnceOnly = [] () \
			{ \
				const uint32 Always = Trace::FEvent::Flag_Always; \
				const uint32 Important = Trace::FEvent::Flag_Important; \
				const uint32 Flags = (0, ##__VA_ARGS__); \
				F##LoggerName##EventName##Fields Fields; \
				const auto* Descs = (Trace::FFieldDesc*)(&Fields); \
				uint32 DescCount = uint32(sizeof(Fields) / sizeof(*Descs)); \
				const auto& LoggerLiteral = Trace::FLiteralName(#LoggerName); \
				const auto& EventLiteral = Trace::FLiteralName(#EventName); \
				Trace::FEvent::Create(&LoggerName##EventName##Event, LoggerLiteral, EventLiteral, Descs, DescCount, Flags); \
				return true; \
			}(); \
		} \
		Trace::TField<0, 

#define TRACE_PRIVATE_EVENT_FIELD(FieldType, FieldName) \
		FieldType> const FieldName = Trace::FLiteralName(#FieldName); \
		Trace::TField<decltype(FieldName)::Offset + decltype(FieldName)::Size,

#define TRACE_PRIVATE_EVENT_END() \
		Trace::EndOfFields> const EventSize_Private = {}; \
		Trace::TField<decltype(EventSize_Private)::Value, Trace::Attachment> const Attachment = {}; \
		explicit operator bool () const { return true; } \
	};

#define TRACE_PRIVATE_EVENT_IS_ENABLED(LoggerName, EventName) \
	( \
		(LoggerName##EventName##Event.bInitialized || (F##LoggerName##EventName##Fields::Initialize(), true)) \
		&& (LoggerName##EventName##Event.Enabled.Test) \
	)

#define TRACE_PRIVATE_EVENT_SIZE(LoggerName, EventName) \
	decltype(F##LoggerName##EventName##Fields::EventSize_Private)::Value

#define TRACE_PRIVATE_LOG(LoggerName, EventName, ...) \
	if (TRACE_PRIVATE_EVENT_IS_ENABLED(LoggerName, EventName)) \
		if (const auto& __restrict EventName = (F##LoggerName##EventName##Fields&)LoggerName##EventName##Event) \
			Trace::FEvent::FLogScope(LoggerName##EventName##Event.Uid, TRACE_PRIVATE_EVENT_SIZE(LoggerName, EventName), ##__VA_ARGS__)

#else

#define TRACE_PRIVATE_EVENT_DEFINE(LoggerName, EventName)

#define TRACE_PRIVATE_EVENT_BEGIN(LoggerName, EventName, ...) \
	TRACE_PRIVATE_EVENT_BEGIN_IMPL(LoggerName, EventName)

#define TRACE_PRIVATE_EVENT_BEGIN_EXTERN(LoggerName, EventName) \
	TRACE_PRIVATE_EVENT_BEGIN_IMPL(LoggerName, EventName)

#define TRACE_PRIVATE_EVENT_BEGIN_IMPL(LoggerName, EventName) \
	struct F##LoggerName##EventName##Dummy \
	{ \
		struct FTraceDisabled \
		{ \
			const FTraceDisabled& operator () (...) const { return *this; } \
		}; \
		const F##LoggerName##EventName##Dummy& operator << (const FTraceDisabled&) const \
		{ \
			return *this; \
		} \
		explicit operator bool () const { return false; }

#define TRACE_PRIVATE_EVENT_FIELD(FieldType, FieldName) \
		const FTraceDisabled& FieldName;

#define TRACE_PRIVATE_EVENT_END() \
		const FTraceDisabled& Attachment; \
	};

#define TRACE_PRIVATE_EVENT_IS_ENABLED(LoggerName, EventName) \
	(false)

#define TRACE_PRIVATE_LOG(LoggerName, EventName, ...) \
	if (const auto& EventName = *(F##LoggerName##EventName##Dummy*)1) \
		EventName

#endif // UE_TRACE_ENABLED
