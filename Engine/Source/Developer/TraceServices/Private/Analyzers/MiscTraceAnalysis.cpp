// Copyright Epic Games, Inc. All Rights Reserved.
#include "MiscTraceAnalysis.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "Model/LogPrivate.h"
#include "Model/ThreadsPrivate.h"
#include "Model/BookmarksPrivate.h"
#include "Model/FramesPrivate.h"
#include "Model/Channel.h"
#include "Common/Utils.h"

FMiscTraceAnalyzer::FMiscTraceAnalyzer(Trace::IAnalysisSession& InSession,
									   Trace::FThreadProvider& InThreadProvider,
									   Trace::FBookmarkProvider& InBookmarkProvider,
									   Trace::FLogProvider& InLogProvider,
									   Trace::FFrameProvider& InFrameProvider,
									   Trace::FChannelProvider& InChannelProvider)
	: Session(InSession)
	, ThreadProvider(InThreadProvider)
	, BookmarkProvider(InBookmarkProvider)
	, LogProvider(InLogProvider)
	, FrameProvider(InFrameProvider)
	, ChannelProvider(InChannelProvider)
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
	Builder.RouteEvent(RouteId_ChannelAnnounce, "Trace", "ChannelAnnounce");
	Builder.RouteEvent(RouteId_ChannelToggle, "Trace", "ChannelToggle");
}

void FMiscTraceAnalyzer::OnThreadInfo(const FThreadInfo& ThreadInfo)
{
	uint32 ThreadId = ThreadInfo.GetId();
	FString Name = ThreadInfo.GetName();

	Trace::FAnalysisSessionEditScope _(Session);

	ThreadProvider.AddThread(ThreadId, *Name, EThreadPriority(ThreadInfo.GetSortHint()));

	const ANSICHAR* GroupNameA = ThreadInfo.GetGroupName();
	if (*GroupNameA)
	{
		const TCHAR* GroupName = Session.StoreString(ANSI_TO_TCHAR(GroupNameA));
		ThreadProvider.SetThreadGroup(ThreadId, GroupName);
	}
}

bool FMiscTraceAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	Trace::FAnalysisSessionEditScope _(Session);

	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
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
		double Timestamp = Context.EventTime.AsSeconds(Cycle);
		BookmarkProvider.AppendBookmark(Timestamp, BookmarkPoint, EventData.GetAttachment());
		LogProvider.AppendMessage(BookmarkPoint, Timestamp, EventData.GetAttachment());
		break;
	}
	case RouteId_BeginFrame:
	{
		uint64 Cycle = EventData.GetValue<uint64>("Cycle");
		uint8 FrameType = EventData.GetValue<uint8>("FrameType");
		check(FrameType < TraceFrameType_Count);
		FrameProvider.BeginFrame(ETraceFrameType(FrameType), Context.EventTime.AsSeconds(Cycle));
		break;
	}
	case RouteId_EndFrame:
	{
		uint64 Cycle = EventData.GetValue<uint64>("Cycle");
		uint8 FrameType = EventData.GetValue<uint8>("FrameType");
		check(FrameType < TraceFrameType_Count);
		FrameProvider.EndFrame(ETraceFrameType(FrameType), Context.EventTime.AsSeconds(Cycle));
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
			FrameProvider.BeginFrame(FrameType, Context.EventTime.AsSeconds(Cycle));
		}
		else
		{
			FrameProvider.EndFrame(FrameType, Context.EventTime.AsSeconds(Cycle));
		}
		break;
	}

	case RouteId_ChannelAnnounce:
		OnChannelAnnounce(Context);
		break;

	case RouteId_ChannelToggle:
		OnChannelToggle(Context);
		break;

	// Begin retired events
	//
	case RouteId_RegisterGameThread:
	{
		const uint32 ThreadId = FTraceAnalyzerUtils::GetThreadIdField(Context);
		ThreadProvider.AddGameThread(ThreadId);
		break;
	}
	case RouteId_CreateThread:
	{
		const uint32 CreatedThreadId = FTraceAnalyzerUtils::GetThreadIdField(Context, "CreatedThreadId");
		const EThreadPriority Priority = static_cast<EThreadPriority>(EventData.GetValue<uint32>("Priority"));
		const TCHAR* CreatedThreadName = reinterpret_cast<const TCHAR*>(EventData.GetAttachment());
		ThreadProvider.AddThread(CreatedThreadId, CreatedThreadName, Priority);
		const uint32 CurrentThreadId = FTraceAnalyzerUtils::GetThreadIdField(Context, "CurrentThreadId");
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
		const uint32 ThreadId = FTraceAnalyzerUtils::GetThreadIdField(Context);
		ThreadProvider.SetThreadGroup(ThreadId, GroupName);
		break;
	}
	case RouteId_BeginThreadGroupScope:
	{
		const TCHAR* GroupName = Session.StoreString(ANSI_TO_TCHAR(reinterpret_cast<const char*>(EventData.GetAttachment())));
		const uint32 CurrentThreadId = FTraceAnalyzerUtils::GetThreadIdField(Context, "CurrentThreadId");
		FThreadState* ThreadState = GetThreadState(CurrentThreadId);
		ThreadState->ThreadGroupStack.Push(GroupName);
		break;
	}
	case RouteId_EndThreadGroupScope:
	{
		const uint32 CurrentThreadId = FTraceAnalyzerUtils::GetThreadIdField(Context, "CurrentThreadId");
		FThreadState* ThreadState = GetThreadState(CurrentThreadId);
		ThreadState->ThreadGroupStack.Pop();
		break;
	}
	//
	// End retired events
	}

	return true;
}

void FMiscTraceAnalyzer::OnChannelAnnounce(const FOnEventContext& Context)
{
	FString ChannelName = FTraceAnalyzerUtils::LegacyAttachmentString<ANSICHAR>("Name", Context);
	uint32 ChannelId = Context.EventData.GetValue<uint32>("Id");
	bool bEnabled = Context.EventData.GetValue<bool>("IsEnabled");
	bool bReadOnly = Context.EventData.GetValue<bool>("ReadOnly", false);

	ChannelProvider.AnnounceChannel(*ChannelName, ChannelId, bReadOnly);
	ChannelProvider.UpdateChannel(ChannelId, bEnabled);
}

void FMiscTraceAnalyzer::OnChannelToggle(const FOnEventContext& Context)
{
	uint32 ChannelId = Context.EventData.GetValue<uint32>("Id");
	bool bEnabled = Context.EventData.GetValue<bool>("IsEnabled");
	ChannelProvider.UpdateChannel(ChannelId, bEnabled);
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
