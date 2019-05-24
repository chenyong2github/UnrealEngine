// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/AnalysisService.h"
#include "Async/AsyncWork.h"
#include "Containers/HashTable.h"
#include "Misc/ScopeLock.h"
#include "Model/TimingProfiler.h"
#include "Common/StringStore.h"
#include "Model/Bookmarks.h"
#include "Model/Log.h"
#include "Model/Threads.h"
#include "Model/FileActivity.h"
#include "Model/Frames.h"
#include "Model/LoadTimeProfiler.h"
#include "Model/Counters.h"

namespace Trace
{

class FAnalysisSessionLock
{
public:
	void ReadAccessCheck() const;
	void WriteAccessCheck() const;

	void BeginRead();
	void EndRead();

	void BeginEdit();
	void EndEdit();

private:
	FCriticalSection CriticalSection;
	uint32 OwnerThread = 0;
	bool IsReadOnly = false;
};

class FAnalysisSession
	: public IAnalysisSession
{
public:
	FAnalysisSession(const TCHAR* SessionName);

	virtual const TCHAR* GetName() const { return *Name; }
	virtual bool IsAnalysisComplete() const override { return IsComplete; }
	virtual double GetDurationSeconds() const { return DurationSeconds; }

	void UpdateDuration(double Seconds) { Lock.WriteAccessCheck(); DurationSeconds = Seconds > DurationSeconds ? Seconds : DurationSeconds; }
	void SetComplete() { IsComplete = true; }

	FBookmarkProvider& EditBookmarkProvider() { return BookmarkProvider; }
	virtual const IBookmarkProvider& ReadBookmarkProvider() const override { return BookmarkProvider; }

	FLogProvider& EditLogProvider() { return LogProvider; }
	virtual const ILogProvider& ReadLogProvider() const override { return LogProvider; }

	FThreadProvider& EditThreadProvider() { return ThreadProvider; }
	virtual const IThreadProvider& ReadThreadProvider() const override { return ThreadProvider; }

	FFrameProvider& EditFrameProvider() { return FrameProvider; }
	virtual const IFrameProvider& ReadFrameProvider() const override { return FrameProvider; }

	FTimingProfilerProvider& EditTimingProfilerProvider() { return TimingProfilerProvider; }
	virtual const FTimingProfilerProvider& ReadTimingProfilerProvider() const override { return TimingProfilerProvider; }

	FFileActivityProvider& EditFileActivityProvider() { return FileActivityProvider; }
	virtual const FFileActivityProvider& ReadFileActivityProvider() const override { return FileActivityProvider; }

	FLoadTimeProfilerProvider& EditLoadTimeProfilerProvider() { return LoadTimeProfilerProvider; }
	virtual const ILoadTimeProfilerProvider& ReadLoadTimeProfilerProvider() const override { return LoadTimeProfilerProvider; }

	FCounterProvider& EditCounterProvider() { return CounterProvider; }
	virtual const ICounterProvider& ReadCounterProvider() const override { return CounterProvider; }

	const TCHAR* StoreString(const TCHAR* String) { return StringStore.Store(String); }

	virtual void BeginRead() const override { Lock.BeginRead(); }
	virtual void EndRead() const override { Lock.EndRead(); }

	void BeginEdit() { Lock.BeginEdit(); }
	void EndEdit() { Lock.EndEdit(); }

private:
	mutable FAnalysisSessionLock Lock;

	FString Name;
	bool IsComplete = false;
	double DurationSeconds = 0.0;
	FSlabAllocator Allocator;
	FStringStore StringStore;
	FBookmarkProvider BookmarkProvider;
	FLogProvider LogProvider;
	FThreadProvider ThreadProvider;
	FFrameProvider FrameProvider;
	FTimingProfilerProvider TimingProfilerProvider;
	FFileActivityProvider FileActivityProvider;
	FLoadTimeProfilerProvider LoadTimeProfilerProvider;
	FCounterProvider CounterProvider;
};

struct FAnalysisSessionEditScope
{
	FAnalysisSessionEditScope(FAnalysisSession& InAnalysisSession)
		: AnalysisSession(InAnalysisSession)
	{
		AnalysisSession.BeginEdit();
	}

	~FAnalysisSessionEditScope()
	{
		AnalysisSession.EndEdit();
	}

	FAnalysisSession& AnalysisSession;
};

class FAnalysisService
	: public IAnalysisService
{
public:
	FAnalysisService();
	virtual ~FAnalysisService();
	virtual TSharedPtr<const IAnalysisSession> Analyze(const TCHAR* SessionName, TUniquePtr<Trace::IInDataStream>&& DataStream) override;
	virtual TSharedPtr<const IAnalysisSession> StartAnalysis(const TCHAR* SessionName, TUniquePtr<Trace::IInDataStream>&& DataStream) override;
	virtual FAnalysisStartedEvent& OnAnalysisStarted() override { return AnalysisStartedEvent; }
	virtual FAnalysisFinishedEvent& OnAnalysisFinished() override { return AnalysisFinishedEvent; }

private:
	class FAnalysisWorker : public FNonAbandonableTask
	{
	public:
		FAnalysisWorker(FAnalysisService& Outer, TUniquePtr<Trace::IInDataStream>&& InDataStream, TSharedRef<FAnalysisSession> InAnalysisSession);
		void DoWork();
		FORCEINLINE TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(FAnalysisWorker, STATGROUP_ThreadPoolAsyncTasks);
		}

	private:
		FAnalysisService& Outer;
		TUniquePtr<Trace::IInDataStream> DataStream;
		TSharedPtr<FAnalysisSession> AnalysisSession;
	};

	void AnalyzeInternal(TSharedRef<FAnalysisSession> AnalysisSession, Trace::IInDataStream* DataStream);

	FAnalysisStartedEvent AnalysisStartedEvent;
	FAnalysisFinishedEvent AnalysisFinishedEvent;
	TArray<TSharedPtr<FAsyncTask<FAnalysisWorker>>> Tasks;
};

}
