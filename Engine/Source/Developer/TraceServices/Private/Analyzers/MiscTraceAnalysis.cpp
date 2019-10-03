// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "MiscTraceAnalysis.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "Model/LogPrivate.h"
#include "Model/ThreadsPrivate.h"
#include "Model/BookmarksPrivate.h"
#include "Model/FramesPrivate.h"
#include "Common/Utils.h"

FMiscTraceAnalyzer::FMiscTraceAnalyzer(Trace::IAnalysisSession& InSession,
									   Trace::FThreadProvider& InThreadProvider,
									   Trace::FBookmarkProvider& InBookmarkProvider,
									   Trace::FLogProvider& InLogProvider,
									   Trace::FFrameProvider& InFrameProvider)
	: Session(InSession)
	, ThreadProvider(InThreadProvider)
	, BookmarkProvider(InBookmarkProvider)
	, LogProvider(InLogProvider)
	, FrameProvider(InFrameProvider)
{
	Trace::FLogCategory& BookmarkLogCategory = LogProvider.GetCategory(Trace::FLogProvider::ReservedLogCategory_Bookmark);
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
	Builder.RouteEvent(RouteId_BeginGameFrame, "Misc", "BeginGameFrame");
	Builder.RouteEvent(RouteId_EndGameFrame, "Misc", "EndGameFrame");
	Builder.RouteEvent(RouteId_BeginRenderFrame, "Misc", "BeginRenderFrame");
	Builder.RouteEvent(RouteId_EndRenderFrame, "Misc", "EndRenderFrame");
}

bool FMiscTraceAnalyzer::OnEvent(uint16 RouteId, const FOnEventContext& Context)
{
	Trace::FAnalysisSessionEditScope _(Session);

	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_RegisterGameThread:
	{
		uint32 ThreadId = EventData.GetValue<uint32>("ThreadId");
		ThreadProvider.AddGameThread(ThreadId);
		break;
	}
	case RouteId_CreateThread:
	{
		uint32 CreatedThreadId = EventData.GetValue<uint32>("CreatedThreadId");
		EThreadPriority Priority = static_cast<EThreadPriority>(EventData.GetValue<uint32>("Priority"));
		ThreadProvider.AddThread(CreatedThreadId, reinterpret_cast<const TCHAR*>(EventData.GetAttachment()), Priority);
		uint32 CurrentThreadId = EventData.GetValue<uint32>("CurrentThreadId");
		FThreadState* ThreadState = GetThreadState(CurrentThreadId);
		if (ThreadState->ThreadGroupStack.Num())
		{
			ThreadProvider.SetThreadGroup(CreatedThreadId, ThreadState->ThreadGroupStack.Top());
		}
		break;
	}
	case RouteId_SetThreadGroup:
	{
		const TCHAR* GroupName = Session.StoreString(ANSI_TO_TCHAR(reinterpret_cast<const char*>(EventData.GetAttachment())));
		uint32 ThreadId = EventData.GetValue<uint32>("ThreadId");
		ThreadProvider.SetThreadGroup(ThreadId, GroupName);
		break;
	}
	case RouteId_BeginThreadGroupScope:
	{
		const TCHAR* GroupName = Session.StoreString(ANSI_TO_TCHAR(reinterpret_cast<const char*>(EventData.GetAttachment())));
		uint32 CurrentThreadId = EventData.GetValue<uint32>("CurrentThreadId");
		FThreadState* ThreadState = GetThreadState(CurrentThreadId);
		ThreadState->ThreadGroupStack.Push(GroupName);
		break;
	}
	case RouteId_EndThreadGroupScope:
	{
		uint32 CurrentThreadId = EventData.GetValue<uint32>("CurrentThreadId");
		FThreadState* ThreadState = GetThreadState(CurrentThreadId);
		ThreadState->ThreadGroupStack.Pop();
		break;
	}
	case RouteId_BookmarkSpec:
	{
		uint64 BookmarkPoint = EventData.GetValue<uint64>("BookmarkPoint");
		Trace::FBookmarkSpec& Spec = BookmarkProvider.GetSpec(BookmarkPoint);
		Spec.Line = EventData.GetValue<int32>("Line");
		const ANSICHAR* File = reinterpret_cast<const ANSICHAR*>(EventData.GetAttachment());
		Spec.File = Session.StoreString(ANSI_TO_TCHAR(File));
		Spec.FormatString = Session.StoreString(reinterpret_cast<const TCHAR*>(EventData.GetAttachment() + strlen(File) + 1));

		Trace::FLogMessageSpec& LogMessageSpec = LogProvider.GetMessageSpec(BookmarkPoint);
		LogMessageSpec.Category = &LogProvider.GetCategory(Trace::FLogProvider::ReservedLogCategory_Bookmark);
		LogMessageSpec.Line = Spec.Line;
		LogMessageSpec.File = Spec.File;
		LogMessageSpec.FormatString = Spec.FormatString;
		LogMessageSpec.Verbosity = ELogVerbosity::Log;
		break;
	}
	case RouteId_Bookmark:
	{
		uint64 BookmarkPoint = EventData.GetValue<uint64>("BookmarkPoint");
		uint64 Cycle = EventData.GetValue<uint64>("Cycle");
		double Timestamp = Context.SessionContext.TimestampFromCycle(Cycle);
		BookmarkProvider.AppendBookmark(Timestamp, BookmarkPoint, EventData.GetAttachment());
		LogProvider.AppendMessage(BookmarkPoint, Timestamp, EventData.GetAttachment());
		break;
	}
	case RouteId_BeginFrame:
	{
		uint64 Cycle = EventData.GetValue<uint64>("Cycle");
		uint8 FrameType = EventData.GetValue<uint8>("FrameType");
		check(FrameType < TraceFrameType_Count);
		FrameProvider.BeginFrame(ETraceFrameType(FrameType), Context.SessionContext.TimestampFromCycle(Cycle));
		break;
	}
	case RouteId_EndFrame:
	{
		uint64 Cycle = EventData.GetValue<uint64>("Cycle");
		uint8 FrameType = EventData.GetValue<uint8>("FrameType");
		check(FrameType < TraceFrameType_Count);
		FrameProvider.EndFrame(ETraceFrameType(FrameType), Context.SessionContext.TimestampFromCycle(Cycle));
		break;
	}
	case RouteId_BeginGameFrame:
	case RouteId_EndGameFrame:
	case RouteId_BeginRenderFrame:
	case RouteId_EndRenderFrame:
	{
		ETraceFrameType FrameType;
		if (RouteId == RouteId_BeginGameFrame || RouteId == RouteId_EndGameFrame)
		{
			FrameType = TraceFrameType_Game;
		}
		else
		{
			FrameType = TraceFrameType_Rendering;
		}
		const uint8* BufferPtr = EventData.GetAttachment();
		uint64 CycleDiff = FTraceAnalyzerUtils::Decode7bit(BufferPtr);
		uint64 Cycle = LastFrameCycle[FrameType] + CycleDiff;
		LastFrameCycle[FrameType] = Cycle;
		if (RouteId == RouteId_BeginGameFrame || RouteId == RouteId_BeginRenderFrame)
		{
			FrameProvider.BeginFrame(FrameType, Context.SessionContext.TimestampFromCycle(Cycle));
		}
		else
		{
			FrameProvider.EndFrame(FrameType, Context.SessionContext.TimestampFromCycle(Cycle));
		}
		break;
	}
	}

	return true;
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
