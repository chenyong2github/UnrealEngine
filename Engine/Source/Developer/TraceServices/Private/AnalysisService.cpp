// Copyright Epic Games, Inc. All Rights Reserved.

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

thread_local FAnalysisSessionLock* GThreadCurrentSessionLock;
thread_local int32 GThreadCurrentReadLockCount;
thread_local int32 GThreadCurrentWriteLockCount;

void FAnalysisSessionLock::ReadAccessCheck() const
{
	checkf(GThreadCurrentSessionLock == this && (GThreadCurrentReadLockCount > 0 || GThreadCurrentWriteLockCount > 0) , TEXT("Trying to read from session outside of a ReadScope"));
}

void FAnalysisSessionLock::WriteAccessCheck() const
{
	checkf(GThreadCurrentSessionLock == this && GThreadCurrentWriteLockCount > 0, TEXT("Trying to write to session outside of an EditScope"));
}

void FAnalysisSessionLock::BeginRead()
{
	check(!GThreadCurrentSessionLock || GThreadCurrentSessionLock == this);
	checkf(GThreadCurrentWriteLockCount == 0, TEXT("Trying to lock for read while holding write access"));
	if (GThreadCurrentReadLockCount++ == 0)
	{
		GThreadCurrentSessionLock = this;
		RWLock.ReadLock();
	}
}

void FAnalysisSessionLock::EndRead()
{
	check(GThreadCurrentReadLockCount > 0);
	if (--GThreadCurrentReadLockCount == 0)
	{
		RWLock.ReadUnlock();
		GThreadCurrentSessionLock = nullptr;
	}
}

void FAnalysisSessionLock::BeginEdit()
{
	check(!GThreadCurrentSessionLock || GThreadCurrentSessionLock == this);
	checkf(GThreadCurrentWriteLockCount == 0, TEXT("Trying to lock for edit while holding read access"));
	if (GThreadCurrentWriteLockCount++ == 0)
	{
		GThreadCurrentSessionLock = this;
		RWLock.WriteLock();
	}
}

void FAnalysisSessionLock::EndEdit()
{
	check(GThreadCurrentWriteLockCount > 0);
	if (--GThreadCurrentWriteLockCount == 0)
	{
		RWLock.WriteUnlock();
		GThreadCurrentSessionLock = nullptr;
	}
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
