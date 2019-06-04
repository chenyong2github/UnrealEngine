// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Model/AnalysisSession.h"
#include "Templates/SharedPointer.h"
#include "Common/PagedArray.h"
#include "TraceServices/Model/Stats.h"

namespace Trace
{

class FAnalysisSessionLock;

enum ECounterOpType : uint8
{
	CounterOpType_Add,
	CounterOpType_Set,
};

class FCounterInternal
	: public ICounter
{
public:
	FCounterInternal(ILinearAllocator& Allocator, uint32 Id, const TCHAR* Name, const TCHAR* Description, ECounterDisplayHint DisplayHint);
	virtual const TCHAR* GetName() const override { return *Name; }
	virtual uint32 GetId() const override { return Id; }
	virtual ECounterDisplayHint GetDisplayHint() const { return DisplayHint; }
	virtual void EnumerateValues(double IntervalStart, double IntervalEnd, TFunctionRef<void(double, int64)> Callback) const override;
	virtual void EnumerateFloatValues(double IntervalStart, double IntervalEnd, TFunctionRef<void(double, double)> Callback) const override;

	const TPagedArray<int64>& ReadIntOpArguments() const { return IntOpArguments; }
	const TPagedArray<double>& ReadDoubleOpArguments() const { return DoubleOpArguments; }
	TPagedArray<double>& EditTimestamps() { return Timestamps; }
	TPagedArray<ECounterOpType>& EditOps() { return Ops; }
	TPagedArray<int64>& EditIntOpArguments() { return IntOpArguments; }
	TPagedArray<double>& EditDoubleOpArguments() { return DoubleOpArguments; }

private:
	friend class FCountersProvider;

	FString Name;
	FString Description;
	uint32 Id;
	ECounterDisplayHint DisplayHint;
	TPagedArray<double> Timestamps;
	TPagedArray<ECounterOpType> Ops;
	TPagedArray<int64> IntOpArguments;
	TPagedArray<double> DoubleOpArguments;
};

class FCounterProvider
	: public ICounterProvider
{
public:
	FCounterProvider(IAnalysisSession& Session);
	virtual ~FCounterProvider();
	virtual uint64 GetCounterCount() const override { return Counters.Num(); }
	virtual void EnumerateCounters(TFunctionRef<void(const ICounter&)> Callback) const override;

	FCounterInternal* CreateCounter(const TCHAR* Name, const TCHAR* Description, ECounterDisplayHint DisplayHint);
	void Add(FCounterInternal& Counter, double Time, int64 Value);
	void Add(FCounterInternal& Counter, double Time, double Value);
	void Set(FCounterInternal& Counter, double Time, int64 Value);
	void Set(FCounterInternal& Counter, double Time, double Value);

private:
	IAnalysisSession& Session;
	TArray<FCounterInternal*> Counters;
};

}
