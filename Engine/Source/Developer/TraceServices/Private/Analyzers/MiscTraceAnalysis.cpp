// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "MiscTraceAnalysis.h"
#include "AnalysisServicePrivate.h"
#include "Model/Log.h"
#include "Model/Threads.h"
#include "Model/Bookmarks.h"
#include "Model/Frames.h"

FMiscTraceAnalyzer::FMiscTraceAnalyzer(TSharedRef<Trace::FAnalysisSession> InSession)
	: Session(InSession)
	, ThreadProvider(InSession->EditThreadProvider())
	, BookmarkProvider(InSession->EditBookmarkProvider())
	, LogProvider(InSession->EditLogProvider())
	, FrameProvider(InSession->EditFramesProvider())
{
	Trace::FLogCategory& BookmarkLogCategory = LogProvider->GetCategory(Trace::FLogProvider::ReservedLogCategory_Bookmark);
	BookmarkLogCategory.Name = TEXT("LogBookmark");
	BookmarkLogCategory.DefaultVerbosity = ELogVerbosity::All;
}

void FMiscTraceAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	auto& Builder = Context.InterfaceBuilder;

	Builder.RouteEvent(RouteId_RegisterGameThread, "Misc", "RegisterGameThread");
	Builder.RouteEvent(RouteId_CreateThread, "Misc", "CreateThread");
	Builder.RouteEvent(RouteId_SetThreadGroup, "Misc", "SetThreadGroup");
	Builder.RouteEvent(RouteId_BeginThreadGroupScope, "Misc", "BeginThreadGroupScope");
	Builder.RouteEvent(RouteId_EndThreadGroupScope, "Misc", "EndThreadGroupScope");
	Builder.RouteEvent(RouteId_BookmarkSpec, "Misc", "BookmarkSpec");
	Builder.RouteEvent(RouteId_Bookmark, "Misc", "Bookmark");
	Builder.RouteEvent(RouteId_BeginFrame, "Misc", "BeginFrame");
	Builder.RouteEvent(RouteId_EndFrame, "Misc", "EndFrame");
}

static ETraceThreadGroup GetThreadGroupFromName(const char* GroupName)
{
	if (!strcmp(GroupName, "Render"))
	{
		return TraceThreadGroup_Render;
	}
	else if (!strcmp(GroupName, "AsyncLoading"))
	{
		return TraceThreadGroup_AsyncLoading;
	}
	else if (!strcmp(GroupName, "TaskGraphHigh"))
	{
		return TraceThreadGroup_TaskGraphHigh;
	}
	else if (!strcmp(GroupName, "TaskGraphNormal"))
	{
		return TraceThreadGroup_TaskGraphNormal;
	}
	else if (!strcmp(GroupName, "TaskGraphLow"))
	{
		return TraceThreadGroup_TaskGraphLow;
	}
	else if (!strcmp(GroupName, "ThreadPool"))
	{
		return TraceThreadGroup_ThreadPool;
	}
	else if (!strcmp(GroupName, "BackgroundThreadPool"))
	{
		return TraceThreadGroup_BackgroundThreadPool;
	}
	else if (!strcmp(GroupName, "LargeThreadPool"))
	{
		return TraceThreadGroup_LargeThreadPool;
	}
	else if (!strcmp(GroupName, "IOThreadPool"))
	{
		return TraceThreadGroup_IOThreadPool;
	}
	else
	{
		check(false);
		return ETraceThreadGroup(0);
	}
}

void FMiscTraceAnalyzer::OnEvent(uint16 RouteId, const FOnEventContext& Context)
{
	Trace::FAnalysisSessionEditScope _(Session.Get());

	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_RegisterGameThread:
	{
		uint32 ThreadId = EventData.GetValue("ThreadId").As<uint32>();
		ThreadProvider->AddGameThread(ThreadId);
		break;
	}
	case RouteId_CreateThread:
	{
		uint32 CreatedThreadId = EventData.GetValue("CreatedThreadId").As<uint32>();
		EThreadPriority Priority = static_cast<EThreadPriority>(EventData.GetValue("Priority").As<uint32>());
		ThreadProvider->AddThread(CreatedThreadId, reinterpret_cast<const TCHAR*>(EventData.GetAttachment()), Priority);
		uint32 CurrentThreadId = EventData.GetValue("CurrentThreadId").As<uint32>();
		FThreadState* ThreadState = GetThreadState(CurrentThreadId);
		if (ThreadState->ThreadGroupStack.Num())
		{
			ThreadProvider->SetThreadGroup(CreatedThreadId, ThreadState->ThreadGroupStack.Top());
		}
		break;
	}
	case RouteId_SetThreadGroup:
	{
		const char* GroupName = reinterpret_cast<const char*>(EventData.GetAttachment());
		ETraceThreadGroup ThreadGroup = GetThreadGroupFromName(GroupName);
		uint32 ThreadId = EventData.GetValue("ThreadId").As<uint32>();
		ThreadProvider->SetThreadGroup(ThreadId, ThreadGroup);
		break;
	}
	case RouteId_BeginThreadGroupScope:
	{
		const char* GroupName = reinterpret_cast<const char*>(EventData.GetAttachment());
		ETraceThreadGroup ThreadGroup = GetThreadGroupFromName(GroupName);
		uint32 CurrentThreadId = EventData.GetValue("CurrentThreadId").As<uint32>();
		FThreadState* ThreadState = GetThreadState(CurrentThreadId);
		ThreadState->ThreadGroupStack.Push(ThreadGroup);
		break;
	}
	case RouteId_EndThreadGroupScope:
	{
		uint32 CurrentThreadId = EventData.GetValue("CurrentThreadId").As<uint32>();
		FThreadState* ThreadState = GetThreadState(CurrentThreadId);
		ThreadState->ThreadGroupStack.Pop();
		break;
	}
	case RouteId_BookmarkSpec:
	{
		uint64 BookmarkPoint = EventData.GetValue("BookmarkPoint").As<uint64>();
		Trace::FBookmarkSpec& Spec = BookmarkProvider->GetSpec(BookmarkPoint);
		Spec.Line = EventData.GetValue("Line").As<int32>();
		const ANSICHAR* File = reinterpret_cast<const ANSICHAR*>(EventData.GetAttachment());
		Spec.File = Session->StoreString(ANSI_TO_TCHAR(File));
		Spec.FormatString = Session->StoreString(reinterpret_cast<const TCHAR*>(EventData.GetAttachment() + strlen(File) + 1));

		Trace::FLogMessageSpec& LogMessageSpec = LogProvider->GetMessageSpec(BookmarkPoint);
		LogMessageSpec.Category = &LogProvider->GetCategory(Trace::FLogProvider::ReservedLogCategory_Bookmark);
		LogMessageSpec.Line = Spec.Line;
		LogMessageSpec.File = Spec.File;
		LogMessageSpec.FormatString = Spec.FormatString;
		LogMessageSpec.Verbosity = ELogVerbosity::Log;
		break;
	}
	case RouteId_Bookmark:
	{
		uint64 BookmarkPoint = EventData.GetValue("BookmarkPoint").As<uint64>();
		uint64 Cycle = EventData.GetValue("Cycle").As<uint64>();
		double Timestamp = Context.SessionContext.TimestampFromCycle(Cycle);
		BookmarkProvider->AppendBookmark(Timestamp, BookmarkPoint, EventData.GetAttachment());
		LogProvider->AppendMessage(BookmarkPoint, Timestamp, EventData.GetAttachment());
		break;
	}
	case RouteId_BeginFrame:
	{
		uint64 Cycle = EventData.GetValue("Cycle").As<uint64>();
		uint8 FrameType = EventData.GetValue("FrameType").As<uint8>();
		check(FrameType < TraceFrameType_Count);
		FrameProvider->BeginFrame(ETraceFrameType(FrameType), Context.SessionContext.TimestampFromCycle(Cycle));
		break;
	}
	case RouteId_EndFrame:
	{
		uint64 Cycle = EventData.GetValue("Cycle").As<uint64>();
		uint8 FrameType = EventData.GetValue("FrameType").As<uint8>();
		check(FrameType < TraceFrameType_Count);
		FrameProvider->EndFrame(ETraceFrameType(FrameType), Context.SessionContext.TimestampFromCycle(Cycle));
		break;
	}
	}
}

FMiscTraceAnalyzer::FThreadState* FMiscTraceAnalyzer::GetThreadState(uint32 ThreadId)
{
	if (!ThreadStateMap.Contains(ThreadId))
	{
		TSharedRef<FThreadState> ThreadState = MakeShared<FThreadState>();
		ThreadStateMap.Add(ThreadId, ThreadState);
	}
	return &ThreadStateMap[ThreadId].Get();
}
