// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/AnalysisService.h"
#include "Async/AsyncWork.h"
#include "Containers/HashTable.h"
#include "Misc/ScopeLock.h"
#include "Model/TimingProfiler.h"
#include "Common/StringStore.h"

struct FRandomStream;

namespace Trace
{

class FBookmarkProvider;
class FLogProvider;
class FThreadProvider;
class FFileActivityProvider;
class FFrameProvider;
class FLoadTimeProfilerProvider;
class FCountersProvider;

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

	const FClassInfo& AddClassInfo(const TCHAR* Name);

	TSharedRef<FBookmarkProvider> EditBookmarkProvider() { return BookmarkProvider; }
	virtual void ReadBookmarkProvider(TFunctionRef<void(const IBookmarkProvider&)> Callback) const override;

	TSharedRef<FLogProvider> EditLogProvider() { return LogProvider; }
	virtual void ReadLogProvider(TFunctionRef<void(const ILogProvider&)> Callback) const override;

	TSharedRef<FThreadProvider> EditThreadProvider() { return ThreadProvider; }
	virtual void ReadThreadProvider(TFunctionRef<void(const IThreadProvider&)> Callback) const override;

	TSharedRef<FFrameProvider> EditFramesProvider() { return FramesProvider; }
	virtual void ReadFramesProvider(TFunctionRef<void(const IFrameProvider&)> Callback) const override;

	TSharedRef<FTimingProfilerProvider> EditTimingProfilerProvider() { return TimingProfilerProvider; }
	virtual void ReadTimingProfilerProvider(TFunctionRef<void(const ITimingProfilerProvider&)> Callback) const override;

	TSharedRef<FFileActivityProvider> EditFileActivityProvider() { return FileActivityProvider; }
	virtual void ReadFileActivityProvider(TFunctionRef<void(const IFileActivityProvider &)> Callback) const override;

	TSharedRef<FLoadTimeProfilerProvider> EditLoadTimeProfilerProvider() { return LoadTimeProfilerProvider; }
	virtual void ReadLoadTimeProfilerProvider(TFunctionRef<void(const ILoadTimeProfilerProvider &)> Callback) const override;

	TSharedRef<FCountersProvider> EditCountersProvider() { return CountersProvider; }
	virtual void ReadCountersProvider(TFunctionRef<void(const ICountersProvider &)> Callback) const override;

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
	TPagedArray<FClassInfo> ClassInfos;
	TSharedRef<FBookmarkProvider> BookmarkProvider;
	TSharedRef<FLogProvider> LogProvider;
	TSharedRef<FThreadProvider> ThreadProvider;
	TSharedRef<FFrameProvider> FramesProvider;
	TSharedRef<FTimingProfilerProvider> TimingProfilerProvider;
	TSharedRef<FFileActivityProvider> FileActivityProvider;
	TSharedRef<FLoadTimeProfilerProvider> LoadTimeProfilerProvider;
	TSharedRef<FCountersProvider> CountersProvider;
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
	virtual TSharedPtr<const IAnalysisSession> MockAnalysis() override;
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

	class FMockGenerator
	{
	public:
		FMockGenerator(TSharedRef<FAnalysisSession> InAnalysisSession);
		void CreateMockData();

	private:
		void InitTimingEventHierarchy(FRandomStream& InRandomStream, TSharedRef<FTimingProfilerProvider::TimelineInternal> InTimeline, int& InOutEventCount, const double InStartTime, const double InDuration, const int32 InLevel);
		
		TSharedPtr<FAnalysisSession> AnalysisSession;
		TArray<TSharedRef<FTimingProfilerProvider::TimelineInternal>> Timelines;
		TArray<uint32> EventTypes;
	};

	void AnalyzeInternal(TSharedRef<FAnalysisSession> AnalysisSession, Trace::IInDataStream& DataStream);

	FAnalysisStartedEvent AnalysisStartedEvent;
	FAnalysisFinishedEvent AnalysisFinishedEvent;
	TArray<TSharedPtr<FAsyncTask<FAnalysisWorker>>> Tasks;
};

}
