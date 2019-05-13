// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Logging/LogTrace.h"

#if LOGTRACE_ENABLED

#include "Trace/Trace.h"
#include "Templates/Function.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformTLS.h"

UE_TRACE_EVENT_BEGIN(Logging, LogCategory)
	UE_TRACE_EVENT_FIELD(const void*, CategoryPointer)
	UE_TRACE_EVENT_FIELD(uint16, NameLength)
	UE_TRACE_EVENT_FIELD(uint8, DefaultVerbosity)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Logging, LogMessageSpec)
	UE_TRACE_EVENT_FIELD(const void*, LogPoint)
	UE_TRACE_EVENT_FIELD(const void*, CategoryPointer)
	UE_TRACE_EVENT_FIELD(int32, Line)
	UE_TRACE_EVENT_FIELD(uint16, FileNameLength)
	UE_TRACE_EVENT_FIELD(uint16, FormatStringLength)
	UE_TRACE_EVENT_FIELD(uint8, Verbosity)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Logging, LogMessage)
	UE_TRACE_EVENT_FIELD(const void*, LogPoint)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

void FLogTrace::OutputLogCategory(const FLogCategoryBase* Category, const TCHAR* Name, ELogVerbosity::Type DefaultVerbosity)
{
	uint16 NameLength = FCString::Strlen(Name);
	auto NameCopy = [NameLength, Name](uint8* Out) {
		TCHAR* OutTChar = reinterpret_cast<TCHAR*>(Out);
		for (int i = 0; i < NameLength; ++i)
		{
			*OutTChar++ = Name[i];
		}
	};
	UE_TRACE_LOG(Logging, LogCategory, NameLength * 2)
		<< LogCategory.CategoryPointer(Category)
		<< LogCategory.NameLength(NameLength)
		<< LogCategory.DefaultVerbosity(DefaultVerbosity)
		<< LogCategory.Attachment(NameCopy);
}

void FLogTrace::OutputLogMessageSpec(const void* LogPoint, const FLogCategoryBase* Category, ELogVerbosity::Type Verbosity, const ANSICHAR* File, int32 Line, const TCHAR* Format)
{
	uint16 FileNameLength = strlen(File);
	uint16 FormatStringLength = FCString::Strlen(Format);
	auto StringCopyFunc = [FileNameLength, FormatStringLength, File, Format](uint8* Out) {
		ANSICHAR* OutAnsiChar = reinterpret_cast<ANSICHAR*>(Out);
		for (int i = 0; i < FileNameLength; ++i)
		{
			*OutAnsiChar++ = File[i];
		}
		TCHAR* OutTChar = reinterpret_cast<TCHAR*>(OutAnsiChar);
		for (int i = 0; i < FormatStringLength; ++i)
		{
			*OutTChar++ = Format[i];
		}
	};
	UE_TRACE_LOG(Logging, LogMessageSpec, FileNameLength + FormatStringLength * 2)
		<< LogMessageSpec.LogPoint(LogPoint)
		<< LogMessageSpec.CategoryPointer(Category)
		<< LogMessageSpec.Line(Line)
		<< LogMessageSpec.FileNameLength(FileNameLength)
		<< LogMessageSpec.FormatStringLength(FormatStringLength)
		<< LogMessageSpec.Verbosity(Verbosity)
		<< LogMessageSpec.Attachment(StringCopyFunc);
}

void FLogTrace::OutputLogMessageInternal(const void* LogPoint, uint16 EncodedFormatArgsSize, uint8* EncodedFormatArgs)
{
	UE_TRACE_LOG(Logging, LogMessage, EncodedFormatArgsSize)
		<< LogMessage.LogPoint(LogPoint)
		<< LogMessage.Cycle(FPlatformTime::Cycles64())
		<< LogMessage.ThreadId(FPlatformTLS::GetCurrentThreadId())
		<< LogMessage.Attachment(EncodedFormatArgs, EncodedFormatArgsSize);
}

#endif
