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
	: Name(SessionName)
	, DurationSeconds(0.0)
	, Allocator(32 << 20)
	, StringStore(Allocator)
	, BookmarkProvider(Lock, StringStore)
	, LogProvider(Allocator, Lock, StringStore)
	, ThreadProvider(Lock, StringStore)
	, FrameProvider(Allocator, Lock)
	, TimingProfilerProvider(Allocator, Lock, StringStore)
	, FileActivityProvider(Allocator, Lock)
	, LoadTimeProfilerProvider(Allocator, Lock, StringStore)
	, CounterProvider(Allocator, Lock)
{

}

FAnalysisService::FAnalysisService()
{

}

FAnalysisService::FAnalysisWorker::FAnalysisWorker(FAnalysisService& InOuter, TUniquePtr<Trace::IInDataStream>&& InDataStream, TSharedRef<FAnalysisSession> InAnalysisSession)
	: Outer(InOuter)
	, DataStream(MoveTemp(InDataStream))
	, AnalysisSession(InAnalysisSession)
{
	
}

void FAnalysisService::AnalyzeInternal(TSharedRef<FAnalysisSession> AnalysisSession, Trace::IInDataStream* DataStream)
{
	OnAnalysisStarted().Broadcast(AnalysisSession);

	TArray<IAnalyzer*> Analyzers;
	{
		Trace::FAnalysisSessionEditScope _(AnalysisSession.Get());
		Analyzers.Add(new FMiscTraceAnalyzer(AnalysisSession.Get()));
		Analyzers.Add(new FAsyncLoadingTraceAnalyzer(AnalysisSession.Get()));
		Analyzers.Add(new FCpuProfilerAnalyzer(AnalysisSession.Get()));
		Analyzers.Add(new FLogTraceAnalyzer(AnalysisSession.Get()));
		Analyzers.Add(new FGpuProfilerAnalyzer(AnalysisSession.Get()));
		Analyzers.Add(new FPlatformFileTraceAnalyzer(AnalysisSession.Get()));
		Analyzers.Add(new FStatsAnalyzer(AnalysisSession.Get()));
	}

	FAnalysisContext Context;
	for (Trace::IAnalyzer* Analyzer : Analyzers)
	{
		Context.AddAnalyzer(*Analyzer);
	}
	Trace::FAnalysisProcessor Processor = Context.Process();

	Processor.Start(*DataStream);

	for (Trace::IAnalyzer* Analyzer : Analyzers)
	{
		delete Analyzer;
	}
	Analyzers.Empty();

	delete DataStream;

	AnalysisSession->SetComplete();

	OnAnalysisFinished().Broadcast(AnalysisSession);
}

void FAnalysisService::FAnalysisWorker::DoWork()
{
	Outer.AnalyzeInternal(AnalysisSession.ToSharedRef(), DataStream.Release());
	AnalysisSession = nullptr;
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
	AnalyzeInternal(AnalysisSession, DataStream.Release());
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

}
