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
#include "Model/MemoryPrivate.h"
#include "Model/Channel.h"
#include "Model/DiagnosticsPrivate.h"

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
	checkf(GThreadCurrentReadLockCount == 0, TEXT("Trying to lock for edit while holding read access"));
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

FAnalysisSession::FAnalysisSession(const TCHAR* SessionName, TUniquePtr<Trace::IInDataStream>&& InDataStream)
	: Name(SessionName)
	, DurationSeconds(0.0)
	, Allocator(32 << 20)
	, StringStore(Allocator)
	, DataStream(MoveTemp(InDataStream))
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

void FAnalysisSession::Start()
{
	FAnalysisContext Context;
	for (Trace::IAnalyzer* Analyzer : ReadAnalyzers())
	{
		Context.AddAnalyzer(*Analyzer);
	}
	Processor = Context.Process(*DataStream);
}

void FAnalysisSession::Stop(bool bAndWait) const
{
	DataStream->Close();
	if (bAndWait)
	{
		Wait();
	}
}

void FAnalysisSession::Wait() const
{
	Processor.Wait();
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

FAnalysisService::~FAnalysisService()
{
}

TSharedPtr<const IAnalysisSession> FAnalysisService::Analyze(const TCHAR* SessionUri)
{
	TSharedPtr<const IAnalysisSession> AnalysisSession = StartAnalysis(SessionUri);
	AnalysisSession->Wait();
	return AnalysisSession;
}

TSharedPtr<const IAnalysisSession> FAnalysisService::StartAnalysis(const TCHAR* SessionUri)
{
	struct FFileDataStream
		: public IInDataStream
	{
		virtual int32 Read(void* Data, uint32 Size) override
		{
			if (Remaining <= 0)
			{
				return 0;
			}

			Size = (Size < Remaining) ? Size : Remaining;
			Remaining -= Size;
			return Handle->Read((uint8*)Data, Size) ? Size : 0;
		}

		TUniquePtr<IFileHandle> Handle;
		int64 Remaining;
	};

	IPlatformFile& FileSystem = IPlatformFile::GetPlatformPhysical();
	IFileHandle* Handle = FileSystem.OpenRead(SessionUri, true);
	if (!Handle)
	{
		return nullptr;
	}

	FFileDataStream* FileStream = new FFileDataStream();
	FileStream->Handle = TUniquePtr<IFileHandle>(Handle);
	FileStream->Remaining = Handle->Size();

	TUniquePtr<IInDataStream> DataStream(FileStream);
	return StartAnalysis(SessionUri, MoveTemp(DataStream));
}

TSharedPtr<const IAnalysisSession> FAnalysisService::StartAnalysis(const TCHAR* SessionName, TUniquePtr<Trace::IInDataStream>&& DataStream)
{
	TSharedRef<FAnalysisSession> Session = MakeShared<FAnalysisSession>(SessionName, MoveTemp(DataStream));

	Trace::FAnalysisSessionEditScope _(*Session);

	FBookmarkProvider* BookmarkProvider = new FBookmarkProvider(*Session);
	Session->AddProvider(FBookmarkProvider::ProviderName, BookmarkProvider);

	FLogProvider* LogProvider = new FLogProvider(*Session);
	Session->AddProvider(FLogProvider::ProviderName, LogProvider);

	FThreadProvider* ThreadProvider = new FThreadProvider(*Session);
	Session->AddProvider(FThreadProvider::ProviderName, ThreadProvider);

	FFrameProvider* FrameProvider = new FFrameProvider(*Session);
	Session->AddProvider(FFrameProvider::ProviderName, FrameProvider);

	FCounterProvider* CounterProvider = new FCounterProvider(*Session, *FrameProvider);
	Session->AddProvider(FCounterProvider::ProviderName, CounterProvider);

	FNetProfilerProvider* NetProfilerProvider = new FNetProfilerProvider(*Session);
	Session->AddProvider(FNetProfilerProvider::ProviderName, NetProfilerProvider);

	FChannelProvider* ChannelProvider = new FChannelProvider();
	Session->AddProvider(FChannelProvider::ProviderName, ChannelProvider);

	FMemoryProvider* MemoryProvider = new FMemoryProvider(*Session);
	Session->AddProvider(FMemoryProvider::ProviderName, MemoryProvider);

	FDiagnosticsProvider* DiagnosticsProvider = new FDiagnosticsProvider(*Session);
	Session->AddProvider(FDiagnosticsProvider::ProviderName, DiagnosticsProvider);

	Session->AddAnalyzer(new FMiscTraceAnalyzer(*Session, *ThreadProvider, *BookmarkProvider, *LogProvider, *FrameProvider, *ChannelProvider));
	Session->AddAnalyzer(new FLogTraceAnalyzer(*Session, *LogProvider));

	ModuleService.OnAnalysisBegin(*Session);

	Session->Start();
	return Session;
}

}
