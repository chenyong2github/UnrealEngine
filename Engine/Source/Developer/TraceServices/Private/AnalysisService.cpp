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
	for (auto& KV : Providers)
	{
		IProvider* Provider = KV.Value;
		delete Provider;
	}
}

void FAnalysisSession::AddProvider(const FName& InName, IProvider* Provider)
{
	Providers.Add(InName, Provider);
}

const IProvider* FAnalysisSession::ReadProviderPrivate(const FName& InName) const
{
	IProvider* const* FindIt = Providers.Find(InName);
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

	TArray<IAnalyzer*> Analyzers;
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
		Analyzers.Add(new FMiscTraceAnalyzer(Session, *ThreadProvider, *BookmarkProvider, *LogProvider, *FrameProvider));
		Analyzers.Add(new FLogTraceAnalyzer(Session, *LogProvider));
		ModuleService.OnAnalysisBegin(Session, Analyzers);
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
