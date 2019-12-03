// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TraceServices/AnalysisService.h"
#include "AnalysisServicePrivate.h"
#include "Trace/Analyzer.h"
#include "Trace/Analysis.h"
#include "Trace/DataStream.h"
#include "HAL/PlatformFile.h"
#include "Analyzers/MiscTraceAnalysis.h"
#include "Analyzers/LogTraceAnalysis.h"
#include "Math/RandomStream.h"
#include "ModuleServicePrivate.h"
#include "Model/LogPrivate.h"
#include "Model/FramesPrivate.h"
#include "Model/BookmarksPrivate.h"
#include "Model/ThreadsPrivate.h"
#include "Model/CountersPrivate.h"
#include "Model/NetProfilerProvider.h"

namespace Trace
{

void FAnalysisSessionLock::ReadAccessCheck() const
{
	checkf(IsReadOnly || FPlatformTLS::GetCurrentThreadId() == OwnerThread, TEXT("Trying to read from session while someone else is writing"));
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
{

}

FAnalysisSession::~FAnalysisSession()
{
	for (int32 AnalyzerIndex = Analyzers.Num() - 1; AnalyzerIndex >= 0; --AnalyzerIndex)
	{
		delete Analyzers[AnalyzerIndex];
	}
	for (int32 ProviderIndex = Providers.Num() - 1; ProviderIndex >= 0; --ProviderIndex)
	{
		delete Providers[ProviderIndex];
	}
}

void FAnalysisSession::AddAnalyzer(IAnalyzer* Analyzer)
{
	Analyzers.Add(Analyzer);
}

void FAnalysisSession::AddProvider(const FName& InName, IProvider* Provider)
{
	Providers.Add(Provider);
	ProvidersMap.Add(InName, Provider);
}

const IProvider* FAnalysisSession::ReadProviderPrivate(const FName& InName) const
{
	IProvider* const* FindIt = ProvidersMap.Find(InName);
	if (FindIt)
	{
		return *FindIt;
	}
	else
	{
		return nullptr;
	}
}

IProvider* FAnalysisSession::EditProviderPrivate(const FName& InName)
{
	IProvider** FindIt = ProvidersMap.Find(InName);
	if (FindIt)
	{
		return *FindIt;
	}
	else
	{
		return nullptr;
	}
}

FAnalysisService::FAnalysisService(FModuleService& InModuleService)
	: ModuleService(InModuleService)
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

	{
		IAnalysisSession& Session = AnalysisSession.Get();
		Trace::FAnalysisSessionEditScope _(Session);

		FBookmarkProvider* BookmarkProvider = new FBookmarkProvider(Session);
		Session.AddProvider(FBookmarkProvider::ProviderName, BookmarkProvider);

		FLogProvider* LogProvider = new FLogProvider(Session);
		Session.AddProvider(FLogProvider::ProviderName, LogProvider);

		FThreadProvider* ThreadProvider = new FThreadProvider(Session);
		Session.AddProvider(FThreadProvider::ProviderName, ThreadProvider);

		FFrameProvider* FrameProvider = new FFrameProvider(Session);
		Session.AddProvider(FFrameProvider::ProviderName, FrameProvider);

		FCounterProvider* CounterProvider = new FCounterProvider(Session, *FrameProvider);
		Session.AddProvider(FCounterProvider::ProviderName, CounterProvider);

		FNetProfilerProvider* NetProfilerProvider = new FNetProfilerProvider(Session);
		Session.AddProvider(FNetProfilerProvider::ProviderName, NetProfilerProvider);

		Session.AddAnalyzer(new FMiscTraceAnalyzer(Session, *ThreadProvider, *BookmarkProvider, *LogProvider, *FrameProvider));
		Session.AddAnalyzer(new FLogTraceAnalyzer(Session, *LogProvider));

		ModuleService.OnAnalysisBegin(Session);
	}
	
	FAnalysisContext Context;
	for (Trace::IAnalyzer* Analyzer : AnalysisSession->ReadAnalyzers())
	{
		Context.AddAnalyzer(*Analyzer);
	}
	Trace::FAnalysisProcessor Processor = Context.Process(*DataStream);
	Processor.Wait();

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

TSharedPtr<const IAnalysisSession> FAnalysisService::Analyze(const TCHAR* SessionUri)
{
	IInDataStream* DataStream = Trace::DataStream_ReadFile(SessionUri);
	if (!DataStream)
	{
		return nullptr;
	}
	TSharedRef<FAnalysisSession> AnalysisSession = MakeShared<FAnalysisSession>(SessionUri);
	AnalyzeInternal(AnalysisSession, DataStream);
	return AnalysisSession;
}

TSharedPtr<const IAnalysisSession> FAnalysisService::StartAnalysis(const TCHAR* SessionUri)
{
	TUniquePtr<IInDataStream> DataStream(Trace::DataStream_ReadFile(SessionUri));
	if (!DataStream)
	{
		return nullptr;
	}
	TSharedRef<FAnalysisSession> AnalysisSession = MakeShared<FAnalysisSession>(SessionUri);
	TSharedPtr<FAsyncTask<FAnalysisWorker>> Task = MakeShared<FAsyncTask<FAnalysisWorker>>(*this, MoveTemp(DataStream), AnalysisSession);
	Tasks.Add(Task);
	Task->StartBackgroundTask();
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
