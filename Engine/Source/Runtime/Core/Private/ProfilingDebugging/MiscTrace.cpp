// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "ProfilingDebugging/MiscTrace.h"

#if MISCTRACE_ENABLED

#include "Trace/Trace.h"
#include "Misc/CString.h"
#include "HAL/PlatformTLS.h"
#include "HAL/PlatformTime.h"

UE_TRACE_EVENT_BEGIN(Misc, RegisterGameThread)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Misc, CreateThread)
	UE_TRACE_EVENT_FIELD(uint32, CurrentThreadId)
	UE_TRACE_EVENT_FIELD(uint32, CreatedThreadId)
	UE_TRACE_EVENT_FIELD(uint32, Priority)
	UE_TRACE_EVENT_FIELD(uint16, NameSize)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Misc, SetThreadGroup)
	UE_TRACE_EVENT_FIELD(uint32, ThreadId)
	UE_TRACE_EVENT_FIELD(uint16, GroupNameLength)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Misc, BeginThreadGroupScope)
	UE_TRACE_EVENT_FIELD(uint32, CurrentThreadId)
	UE_TRACE_EVENT_FIELD(uint16, GroupNameLength)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Misc, EndThreadGroupScope)
	UE_TRACE_EVENT_FIELD(uint32, CurrentThreadId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Misc, BookmarkSpec)
	UE_TRACE_EVENT_FIELD(const void*, BookmarkPoint)
	UE_TRACE_EVENT_FIELD(int32, Line)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Misc, Bookmark)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(const void*, BookmarkPoint)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Misc, BeginFrame)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint8, FrameType)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Misc, EndFrame)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint8, FrameType)
UE_TRACE_EVENT_END()

void FMiscTrace::OutputRegisterGameThread(uint32 Id)
{
	UE_TRACE_LOG(Misc, RegisterGameThread)
		<< RegisterGameThread.ThreadId(FPlatformTLS::GetCurrentThreadId());
}

void FMiscTrace::OutputCreateThread(uint32 Id, const TCHAR* Name, uint32 Priority)
{
	uint16 NameLength = FCString::Strlen(Name);
	auto NameCopy = [NameLength, Name](uint8* Out) {
		int32 i = 0;
		for (; i < NameLength; ++i)
		{
			Out[i] = Name[i] & 0x7f;
		}
		Out[i] = 0;
	};
	UE_TRACE_LOG(Misc, CreateThread, NameLength + 1)
		<< CreateThread.CurrentThreadId(FPlatformTLS::GetCurrentThreadId())
		<< CreateThread.CreatedThreadId(Id)
		<< CreateThread.Priority(Priority)
		<< CreateThread.NameSize(NameLength)
		<< CreateThread.Attachment(NameCopy);
}

static const char* GetThreadGroupName(ETraceThreadGroup Group)
{
	switch (Group)
	{
	case TraceThreadGroup_Render:
		return "Render";
	case TraceThreadGroup_TaskGraphHigh:
		return "TaskGraphHigh";
	case TraceThreadGroup_TaskGraphNormal:
		return "TaskGraphNormal";
	case TraceThreadGroup_TaskGraphLow:
		return "TaskGraphLow";
	case TraceThreadGroup_ThreadPool:
		return "ThreadPool";
	case TraceThreadGroup_BackgroundThreadPool:
		return "BackgroundThreadPool";
	case TraceThreadGroup_LargeThreadPool:
		return "LargeThreadPool";
	case TraceThreadGroup_AsyncLoading:
		return "AsyncLoading";
	case TraceThreadGroup_IOThreadPool:
		return "IOThreadPool";
	}
	check(false);
	return nullptr;
}

void FMiscTrace::OutputSetThreadGroup(uint32 Id, ETraceThreadGroup Group)
{
	const char* GroupName = GetThreadGroupName(Group);
	uint16 NameLength = strlen(GroupName);
	auto NameCopy = [NameLength, GroupName](uint8* Out) {
		int32 i = 0;
		for (; i < NameLength; ++i)
		{
			Out[i] = GroupName[i];
		}
		Out[i] = 0;
	};
	UE_TRACE_LOG(Misc, SetThreadGroup, NameLength + 1)
		<< SetThreadGroup.ThreadId(Id)
		<< SetThreadGroup.GroupNameLength(NameLength)
		<< SetThreadGroup.Attachment(NameCopy);
}

void FMiscTrace::OutputBeginThreadGroupScope(ETraceThreadGroup Group)
{
	const char* GroupName = GetThreadGroupName(Group);
	uint16 NameLength = strlen(GroupName);
	auto NameCopy = [NameLength, GroupName](uint8* Out) {
		int32 i = 0;
		for (; i < NameLength; ++i)
		{
			Out[i] = GroupName[i];
		}
		Out[i] = 0;
	};
	UE_TRACE_LOG(Misc, BeginThreadGroupScope, NameLength + 1)
		<< BeginThreadGroupScope.CurrentThreadId(FPlatformTLS::GetCurrentThreadId())
		<< BeginThreadGroupScope.Attachment(NameCopy);
}

void FMiscTrace::OutputEndThreadGroupScope()
{
	UE_TRACE_LOG(Misc, EndThreadGroupScope)
		<< EndThreadGroupScope.CurrentThreadId(FPlatformTLS::GetCurrentThreadId());
}

void FMiscTrace::OutputBookmarkSpec(const void* BookmarkPoint, const ANSICHAR* File, int32 Line, const TCHAR* Format)
{
	uint16 FileNameSize = strlen(File) + 1;
	uint16 FormatStringSize = (FCString::Strlen(Format) + 1) * sizeof(TCHAR);
	auto StringCopyFunc = [FileNameSize, FormatStringSize, File, Format](uint8* Out) {
		memcpy(Out, File, FileNameSize);
		memcpy(Out + FileNameSize, Format, FormatStringSize);
	};
	UE_TRACE_LOG(Misc, BookmarkSpec, FileNameSize + FormatStringSize)
		<< BookmarkSpec.BookmarkPoint(BookmarkPoint)
		<< BookmarkSpec.Line(Line)
		<< BookmarkSpec.Attachment(StringCopyFunc);
}

void FMiscTrace::OutputBookmarkInternal(const void* BookmarkPoint, uint16 EncodedFormatArgsSize, uint8* EncodedFormatArgs)
{
	UE_TRACE_LOG(Misc, Bookmark, EncodedFormatArgsSize)
		<< Bookmark.Cycle(FPlatformTime::Cycles64())
		<< Bookmark.BookmarkPoint(BookmarkPoint)
		<< Bookmark.Attachment(EncodedFormatArgs, EncodedFormatArgsSize);
}

void FMiscTrace::OutputBeginFrame(ETraceFrameType FrameType)
{
	UE_TRACE_LOG(Misc, BeginFrame)
		<< BeginFrame.Cycle(FPlatformTime::Cycles64())
		<< BeginFrame.FrameType(uint8(FrameType));
}

void FMiscTrace::OutputEndFrame(ETraceFrameType FrameType)
{
	UE_TRACE_LOG(Misc, EndFrame)
		<< EndFrame.Cycle(FPlatformTime::Cycles64())
		<< EndFrame.FrameType(uint8(FrameType));
}

#endif
