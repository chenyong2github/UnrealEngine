// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/AnalysisService.h"
#include "Async/AsyncWork.h"
#include "Containers/HashTable.h"
#include "Misc/ScopeLock.h"
#include "Common/StringStore.h"

namespace Trace
{

class FModuleService;

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
	virtual ~FAnalysisSession();

	virtual const TCHAR* GetName() const { return *Name; }
	virtual bool IsAnalysisComplete() const override { return IsComplete; }
	virtual double GetDurationSeconds() const { Lock.ReadAccessCheck(); return DurationSeconds; }
	virtual void UpdateDurationSeconds(double Duration) override { Lock.WriteAccessCheck(); DurationSeconds = FMath::Max(Duration, DurationSeconds); }
	void SetComplete() { IsComplete = true; }

	virtual ILinearAllocator& GetLinearAllocator() override { return Allocator; }
	virtual const TCHAR* StoreString(const TCHAR* String) override { return StringStore.Store(String); }

	virtual void BeginRead() const override { Lock.BeginRead(); }
	virtual void EndRead() const override { Lock.EndRead(); }

	virtual void BeginEdit() override { Lock.BeginEdit(); }
	virtual void EndEdit() override { Lock.EndEdit(); }

	virtual void ReadAccessCheck() const override { return Lock.ReadAccessCheck(); }
	virtual void WriteAccessCheck() override { return Lock.WriteAccessCheck(); }

	virtual void AddProvider(const FName& Name, IProvider* Provider) override;

private:
	virtual const IProvider* ReadProviderPrivate(const FName& Name) const override;

	mutable FAnalysisSessionLock Lock;

	FString Name;
	bool IsComplete = false;
	double DurationSeconds = 0.0;
	FSlabAllocator Allocator;
	FStringStore StringStore;
	TMap<FName, IProvider*> Providers;
};

class FAnalysisService
	: public IAnalysisService
{
public:
	FAnalysisService(FModuleService& ModuleService);
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

	FModuleService& ModuleService;
	FAnalysisStartedEvent AnalysisStartedEvent;
	FAnalysisFinishedEvent AnalysisFinishedEvent;
	TArray<TSharedPtr<FAsyncTask<FAnalysisWorker>>> Tasks;
};

}
