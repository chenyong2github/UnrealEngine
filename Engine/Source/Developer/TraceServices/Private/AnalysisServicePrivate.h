// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/AnalysisService.h"
#include "Async/AsyncWork.h"
#include "Containers/HashTable.h"
#include "Misc/ScopeLock.h"
#include "Common/StringStore.h"
#include "Trace/Analysis.h"

namespace Trace
{

class FModuleService;
class IInDataStream;

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
	FRWLock RWLock;
};

class FAnalysisSession
	: public IAnalysisSession
{
public:
	FAnalysisSession(const TCHAR* SessionName, TUniquePtr<Trace::IInDataStream>&& InDataStream);
	virtual ~FAnalysisSession();
	void Start();
	virtual void Stop(bool bAndWait) const override;
	virtual void Wait() const override;

	virtual const TCHAR* GetName() const { return *Name; }
	virtual bool IsAnalysisComplete() const override { return !Processor.IsActive(); }
	virtual double GetDurationSeconds() const { Lock.ReadAccessCheck(); return DurationSeconds; }
	virtual void UpdateDurationSeconds(double Duration) override { Lock.WriteAccessCheck(); DurationSeconds = FMath::Max(Duration, DurationSeconds); }

	virtual ILinearAllocator& GetLinearAllocator() override { return Allocator; }
	virtual const TCHAR* StoreString(const TCHAR* String) override { return StringStore.Store(String); }
	virtual const TCHAR* StoreString(const FStringView& String) override { return StringStore.Store(String); }

	virtual void BeginRead() const override { Lock.BeginRead(); }
	virtual void EndRead() const override { Lock.EndRead(); }

	virtual void BeginEdit() override { Lock.BeginEdit(); }
	virtual void EndEdit() override { Lock.EndEdit(); }

	virtual void ReadAccessCheck() const override { return Lock.ReadAccessCheck(); }
	virtual void WriteAccessCheck() override { return Lock.WriteAccessCheck(); }

	virtual void AddAnalyzer(IAnalyzer* Analyzer) override;
	virtual void AddProvider(const FName& Name, IProvider* Provider) override;

	const TArray<IAnalyzer*> ReadAnalyzers() { return Analyzers; }

private:
	virtual const IProvider* ReadProviderPrivate(const FName& Name) const override;
	virtual IProvider* EditProviderPrivate(const FName& Name) override;

	mutable FAnalysisSessionLock Lock;

	FString Name;
	double DurationSeconds = 0.0;
	FSlabAllocator Allocator;
	FStringStore StringStore;
	TArray<IAnalyzer*> Analyzers;
	TArray<IProvider*> Providers;
	TMap<FName, IProvider*> ProvidersMap;
	mutable TUniquePtr<Trace::IInDataStream> DataStream;
	mutable Trace::FAnalysisProcessor Processor;
};

class FAnalysisService
	: public IAnalysisService
{
public:
	FAnalysisService(FModuleService& ModuleService);
	virtual ~FAnalysisService();
	virtual TSharedPtr<const IAnalysisSession> Analyze(const TCHAR* SessionUri) override;
	virtual TSharedPtr<const IAnalysisSession> StartAnalysis(const TCHAR* SessionUri) override;
	virtual TSharedPtr<const IAnalysisSession> StartAnalysis(const TCHAR* SessionName, TUniquePtr<Trace::IInDataStream>&& DataStream) override;

private:
	FModuleService& ModuleService;
};

}
