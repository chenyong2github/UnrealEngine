// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TraceServices/AnalysisService.h"
#include "AnalysisServicePrivate.h"
#include "Trace/Analyzer.h"
#include "Trace/Analysis.h"
#include "Trace/DataStream.h"
#include "HAL/PlatformFile.h"
#include "Analyzers/LoadTimeTraceAnalysis.h"
#include "Analyzers/CpuProfilerTraceAnalysis.h"
#include "Analyzers/MiscTraceAnalysis.h"
#include "Analyzers/LogTraceAnalysis.h"
#include "Analyzers/GpuProfilerTraceAnalysis.h"
#include "Analyzers/PlatformFileTraceAnalysis.h"
#include "Analyzers/StatsTraceAnalysis.h"
#include "Math/RandomStream.h"
#include "Model/Bookmarks.h"
#include "Model/Log.h"
#include "Model/Threads.h"
#include "Model/FileActivity.h"
#include "Model/Frames.h"
#include "Model/LoadTimeProfiler.h"
#include "Model/Counters.h"

namespace Trace
{

void FAnalysisSessionLock::ReadAccessCheck() const
{
	checkf(IsReadOnly, TEXT("Trying to read from session while not in read only mode"));
}

void FAnalysisSessionLock::WriteAccessCheck() const
{
	checkf(!IsReadOnly, TEXT("Trying to edit session while in read only mode"));
	checkf(FPlatformTLS::GetCurrentThreadId() == OwnerThread, TEXT("Trying to edit session from thread without write access"));
}

void FAnalysisSessionLock::BeginRead()
{
	CriticalSection.Lock();
	check(OwnerThread == 0);
	OwnerThread = FPlatformTLS::GetCurrentThreadId();
	IsReadOnly = true;
}

void FAnalysisSessionLock::EndRead()
{
	check(FPlatformTLS::GetCurrentThreadId() == OwnerThread);
	OwnerThread = 0;
	IsReadOnly = false;
	CriticalSection.Unlock();
}

void FAnalysisSessionLock::BeginEdit()
{
	CriticalSection.Lock();
	check(OwnerThread == 0);
	check(!IsReadOnly);
	OwnerThread = FPlatformTLS::GetCurrentThreadId();
}

void FAnalysisSessionLock::EndEdit()
{
	check(OwnerThread == FPlatformTLS::GetCurrentThreadId());
	OwnerThread = 0;
	CriticalSection.Unlock();
}

FAnalysisSession::FAnalysisSession(const TCHAR* SessionName)
	: DurationSeconds(0.0)
	, Allocator(32 << 20)
	, ClassInfos(Allocator, 4096)
	, BookmarkProvider(MakeShared<FBookmarkProvider>(Lock))
	, LogProvider(MakeShared<FLogProvider>(Allocator, Lock))
	, ThreadProvider(MakeShared<FThreadProvider>(Lock))
	, FramesProvider(MakeShared<FFrameProvider>(Allocator, Lock))
	, TimingProfilerProvider(MakeShared<FTimingProfilerProvider>(Allocator, Lock))
	, FileActivityProvider(MakeShared<FFileActivityProvider>(Allocator, Lock))
	, LoadTimeProfilerProvider(MakeShared<FLoadTimeProfilerProvider>(Allocator, Lock))
	, CountersProvider(MakeShared<FCountersProvider>(Allocator, Lock))
{

}

const Trace::FClassInfo& FAnalysisSession::AddClassInfo(const TCHAR* ClassName)
{
	Lock.WriteAccessCheck();
	FClassInfo& ClassInfo = ClassInfos.PushBack();
	ClassInfo.Name = ClassName;
	return ClassInfo;
}

void FAnalysisSession::ReadBookmarkProvider(TFunctionRef<void(const IBookmarkProvider&)> Callback) const
{
	Lock.ReadAccessCheck();
	Callback(*BookmarkProvider);
}

void FAnalysisSession::ReadLogProvider(TFunctionRef<void(const ILogProvider&)> Callback) const
{
	Lock.ReadAccessCheck();
	Callback(*LogProvider);
}

void FAnalysisSession::ReadThreadProvider(TFunctionRef<void(const IThreadProvider&)> Callback) const
{
	Lock.ReadAccessCheck();
	Callback(*ThreadProvider);
}

void FAnalysisSession::ReadFramesProvider(TFunctionRef<void(const IFrameProvider&)> Callback) const
{
	Lock.ReadAccessCheck();
	Callback(*FramesProvider);
}

void FAnalysisSession::ReadTimingProfilerProvider(TFunctionRef<void(const ITimingProfilerProvider&)> Callback) const
{
	Lock.ReadAccessCheck();
	Callback(*TimingProfilerProvider);
}

void FAnalysisSession::ReadFileActivityProvider(TFunctionRef<void(const IFileActivityProvider&)> Callback) const
{
	Lock.ReadAccessCheck();
	Callback(*FileActivityProvider);
}

void FAnalysisSession::ReadLoadTimeProfilerProvider(TFunctionRef<void(const ILoadTimeProfilerProvider&)> Callback) const
{
	Lock.ReadAccessCheck();
	Callback(*LoadTimeProfilerProvider);
}

void FAnalysisSession::ReadCountersProvider(TFunctionRef<void(const ICountersProvider &)> Callback) const
{
	Lock.ReadAccessCheck();
	Callback(*CountersProvider);
}

FAnalysisService::FAnalysisService(TSharedRef<IStore> InTraceStore)
	: TraceStore(InTraceStore)
{

}

FAnalysisService::FAnalysisWorker::FAnalysisWorker(FAnalysisService& InOuter, TUniquePtr<Trace::IInDataStream>&& InDataStream, TSharedRef<FAnalysisSession> InAnalysisSession)
	: Outer(InOuter)
	, DataStream(MoveTemp(InDataStream))
	, AnalysisSession(InAnalysisSession)
{
	
}

void FAnalysisService::AnalyzeInternal(TSharedRef<FAnalysisSession> AnalysisSession, Trace::IInDataStream& DataStream)
{
	OnAnalysisStarted().Broadcast(AnalysisSession);

	TArray<TSharedRef<IAnalyzer>> Analyzers;
	{
		Trace::FAnalysisSessionEditScope _(AnalysisSession.Get());
		Analyzers.Add(MakeShared<FMiscTraceAnalyzer>(AnalysisSession));
		Analyzers.Add(MakeShared<FAsyncLoadingTraceAnalyzer>(AnalysisSession));
		Analyzers.Add(MakeShared<FCpuProfilerAnalyzer>(AnalysisSession));
		Analyzers.Add(MakeShared<FLogTraceAnalyzer>(AnalysisSession));
		Analyzers.Add(MakeShared<FGpuProfilerAnalyzer>(AnalysisSession));
		Analyzers.Add(MakeShared<FPlatformFileTraceAnalyzer>(AnalysisSession));
		Analyzers.Add(MakeShared<FStatsAnalyzer>(AnalysisSession));
	}

	FAnalysisContext Context;
	for (TSharedRef<Trace::IAnalyzer> Analyzer : Analyzers)
	{
		Context.AddAnalyzer(Analyzer.Get());
	}
	Trace::FAnalysisProcessor Processor = Context.Process();

	Processor.Start(DataStream);

	Analyzers.Empty();

	AnalysisSession->SetComplete();

	OnAnalysisFinished().Broadcast(AnalysisSession);
}

void FAnalysisService::FAnalysisWorker::DoWork()
{
	Outer.AnalyzeInternal(AnalysisSession.ToSharedRef(), *DataStream.Get());
	AnalysisSession = nullptr;
}

FAnalysisService::FMockGenerator::FMockGenerator(TSharedRef<FAnalysisSession> InAnalysisSession)
	: AnalysisSession(InAnalysisSession)
{

}

void FAnalysisService::FMockGenerator::CreateMockData()
{
	AnalysisSession->BeginEdit();

	TSharedRef<FTimingProfilerProvider> TimingProfilerProvider = AnalysisSession->EditTimingProfilerProvider();
	TSharedRef<FThreadProvider> ThreadProvider = AnalysisSession->EditThreadProvider();
	// Create Threads.
	{
		ThreadProvider->AddThread(1, TEXT("Main"), TPri_Normal);
		Timelines.Add(TimingProfilerProvider->EditCpuThreadTimeline(1));
		ThreadProvider->AddThread(2, TEXT("Rendering"), TPri_Normal);
		Timelines.Add(TimingProfilerProvider->EditCpuThreadTimeline(2));
		ThreadProvider->AddThread(3, TEXT("Game"), TPri_Normal);
		Timelines.Add(TimingProfilerProvider->EditCpuThreadTimeline(3));
		int32 ThreadId = 4;
		while (ThreadId <= 10)
		{
			ThreadProvider->AddThread(ThreadId, FString::Printf(TEXT("Thread %d"), ThreadId), TPri_SlightlyBelowNormal);
			ThreadProvider->SetThreadGroup(ThreadId, TraceThreadGroup_ThreadPool);
			Timelines.Add(TimingProfilerProvider->EditCpuThreadTimeline(ThreadId));
			++ThreadId;
		}
	}

	// Create Timers.
	{
		EventTypes.Add(TimingProfilerProvider->AddCpuTimer(TEXT("Update")));
		EventTypes.Add(TimingProfilerProvider->AddCpuTimer(TEXT("Collide")));
		EventTypes.Add(TimingProfilerProvider->AddCpuTimer(TEXT("Draw")));
		EventTypes.Add(TimingProfilerProvider->AddCpuTimer(TEXT("PostDraw")));
		EventTypes.Add(TimingProfilerProvider->AddCpuTimer(TEXT("Present")));
		EventTypes.Add(TimingProfilerProvider->AddCpuTimer(TEXT("OtherTimer1")));
		EventTypes.Add(TimingProfilerProvider->AddCpuTimer(TEXT("OtherTimer2")));

		/*AddTimer(TimerId++, TEXT("SomeGpuOp1"), TEXT("Render"), GpuScope, FLinearColor(1.0, 1.0, 1.0, 1.0));
		AddTimer(TimerId++, TEXT("SomeGpuOp2"), TEXT("Render"), GpuScope, FLinearColor(1.0, 1.0, 1.0, 1.0));
		AddTimer(TimerId++, TEXT("SomeGpuOp3"), TEXT("Render"), GpuScope, FLinearColor(1.0, 1.0, 1.0, 1.0));
		AddTimer(TimerId++, TEXT("ComputeSomething1"), TEXT("Render"), ComputeScope, FLinearColor(1.0, 1.0, 1.0, 1.0));
		AddTimer(TimerId++, TEXT("ComputeSomething2"), TEXT("Misc"), ComputeScope, FLinearColor(1.0, 1.0, 1.0, 1.0));*/
	}

	// Create a Timeline for each Thread.
	{
		static const int32 MinEventsPerTimeline = 1'000'000;
		static const int32 MaxEventsPerTimeline = 2'000'000;

		FRandomStream RandomStream(0);

		for (TSharedRef<FTimingProfilerProvider::TimelineInternal> Timeline : Timelines)
		{
			const int32 NumEvents = RandomStream.RandRange(MinEventsPerTimeline, MaxEventsPerTimeline);
			
			double Time = 0;

			int EventCount = NumEvents;
			while (EventCount > 0)
			{
				const double Duration = RandomStream.FRandRange(0.001, 0.01); // [1ms .. 10ms]
				InitTimingEventHierarchy(RandomStream, Timeline, EventCount, Time, Duration, 0);

				static const double MaxTimeBetweenEvents = 0.001; // 1ms
				Time += Duration + RandomStream.FRandRange(0, MaxTimeBetweenEvents);
			}
		}
	}

	AnalysisSession->EndEdit();
}

void FAnalysisService::FMockGenerator::InitTimingEventHierarchy(FRandomStream& InRandomStream, TSharedRef<FTimingProfilerProvider::TimelineInternal> InTimeline, int& InOutEventCount, const double InStartTime, const double InDuration, const int32 InLevel)
{
	static const int32 MaxLevels = 5;
	if (InLevel >= MaxLevels)
	{
		return;
	}

	const double EndTime = InStartTime + InDuration;

	if (InOutEventCount <= 0)
	{
		return;
	}
	
	FTimingProfilerEvent Event;
	Event.TimerIndex = EventTypes[InRandomStream.RandRange(0, EventTypes.Num() - 1)];
	InTimeline->AppendBeginEvent(InStartTime, Event);
	--InOutEventCount;
	
	int32 NumSubEvents = InRandomStream.RandRange(1, InRandomStream.RandRange(1, InDuration / 0.001));

	double Time = InStartTime;
	double RemainingDuration = InDuration;
	for (int SubEventIndex = 0; SubEventIndex < NumSubEvents; SubEventIndex++)
	{
		double TimeAdvance = InRandomStream.FRandRange(0, FMath::Min(0.01, RemainingDuration));
		Time += TimeAdvance;
		if (Time >= EndTime)
			break;
		RemainingDuration = EndTime - Time;

		double Duration = InRandomStream.FRandRange(0, RemainingDuration);
		InitTimingEventHierarchy(InRandomStream, InTimeline, InOutEventCount, Time, Duration, InLevel + 1);
		
		Time += Duration;
		RemainingDuration = EndTime - Time;

		if (InOutEventCount <= 0)
		{
			break;
		}
	}

	InTimeline->AppendEndEvent(EndTime);
}

FAnalysisService::~FAnalysisService()
{
	for (TSharedPtr<FAsyncTask<FAnalysisWorker>> Task : Tasks)
	{
		Task->EnsureCompletion();
	}
}

TSharedPtr<const IAnalysisSession> FAnalysisService::Analyze(const TCHAR* SessionName, TUniquePtr<Trace::IInDataStream>&& InDataStream)
{
	TSharedRef<FAnalysisSession> AnalysisSession = MakeShared<FAnalysisSession>(SessionName);
	TUniquePtr<Trace::IInDataStream> DataStream = MoveTemp(InDataStream);
	AnalyzeInternal(AnalysisSession, *DataStream.Get());
	return AnalysisSession;
}

TSharedPtr<const IAnalysisSession> FAnalysisService::StartAnalysis(const TCHAR* SessionName, TUniquePtr<Trace::IInDataStream>&& DataStream)
{
	TSharedRef<FAnalysisSession> AnalysisSession = MakeShared<FAnalysisSession>(SessionName);
	TSharedPtr<FAsyncTask<FAnalysisWorker>> Task = MakeShared<FAsyncTask<FAnalysisWorker>>(*this, MoveTemp(DataStream), AnalysisSession);
	Tasks.Add(Task);
	Task->StartBackgroundTask();
	return AnalysisSession;
}

TSharedPtr<const IAnalysisSession> FAnalysisService::MockAnalysis()
{
	TSharedRef<FAnalysisSession> AnalysisSession = MakeShared<FAnalysisSession>(TEXT("MockSession"));
	FMockGenerator MockGenerator(AnalysisSession);
	MockGenerator.CreateMockData();

	return AnalysisSession;
}

}
