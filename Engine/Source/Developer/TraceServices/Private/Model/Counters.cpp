// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Model/Counters.h"
#include "AnalysisServicePrivate.h"

namespace Trace
{

FCounterInternal::FCounterInternal(ILinearAllocator& Allocator, uint32 InId, const TCHAR* InName, const TCHAR* InDescription, ECounterDisplayHint InDisplayHint)
	: Name(InName)
	, Description(InDescription)
	, Id(InId)
	, DisplayHint(InDisplayHint)
	, Timestamps(Allocator, 1024)
	, Ops(Allocator, 1024)
	, IntOpArguments(Allocator, 1024)
	, DoubleOpArguments(Allocator, 1024)
{

}

template<typename CounterType, typename EnumerationType>
static void EnumerateCounterValuesInternal(const TPagedArray<double>& Timestamps, const TPagedArray<ECounterOpType>& Ops, const TPagedArray<CounterType>& OpArgs, double IntervalStart, double IntervalEnd, TFunctionRef<void(double, EnumerationType)> Callback)
{
	CounterType Value = 0;
	auto TimeIterator = Timestamps.GetIteratorFromItem(0);
	auto OpIterator = Ops.GetIteratorFromItem(0);
	auto OpArgIterator = OpArgs.GetIteratorFromItem(0);
	const double* Time = TimeIterator.GetCurrentItem();
	const ECounterOpType* Op = OpIterator.GetCurrentItem();
	const CounterType* OpArg = OpArgIterator.GetCurrentItem();
	
	double PrevTime = 0.0;
	CounterType PrevValue = CounterType();
	bool IsFirstValue = true;
	bool IsFirstIncludedValue = true;
	while (Time)
	{
		switch (*Op)
		{
		case CounterOpType_Add:
			Value += *OpArg;
			break;
		case CounterOpType_Set:
			Value = *OpArg;
			break;
		}

		if (*Time >= IntervalStart)
		{
			if (IsFirstIncludedValue && !IsFirstValue)
			{
				Callback(PrevTime, PrevValue);
			}
			PrevTime = *Time;
			PrevValue = EnumerationType(Value);
			Callback(PrevTime, PrevValue);
			IsFirstIncludedValue = false;
		}
		if (*Time > IntervalEnd)
		{
			break;
		}

		IsFirstValue = false;

		Time = TimeIterator.NextItem();
		Op = OpIterator.NextItem();
		OpArg = OpArgIterator.NextItem();
	}
}

void FCounterInternal::EnumerateValues(double IntervalStart, double IntervalEnd, TFunctionRef<void(double, int64)> Callback) const
{
	if (DisplayHint == CounterDisplayHint_FloatingPoint)
	{
		EnumerateCounterValuesInternal<double, int64>(Timestamps, Ops, DoubleOpArguments, IntervalStart, IntervalEnd, Callback);
	}
	else
	{
		EnumerateCounterValuesInternal<int64, int64>(Timestamps, Ops, IntOpArguments, IntervalStart, IntervalEnd, Callback);
	}
}

void FCounterInternal::EnumerateFloatValues(double IntervalStart, double IntervalEnd, TFunctionRef<void(double, double)> Callback) const
{
	if (DisplayHint == CounterDisplayHint_FloatingPoint)
	{
		EnumerateCounterValuesInternal<double, double>(Timestamps, Ops, DoubleOpArguments, IntervalStart, IntervalEnd, Callback);
	}
	else
	{
		EnumerateCounterValuesInternal<int64, double>(Timestamps, Ops, IntOpArguments, IntervalStart, IntervalEnd, Callback);
	}
}

FCounterProvider::FCounterProvider(IAnalysisSession& InSession)
	: Session(InSession)
{

}

FCounterProvider::~FCounterProvider()
{
	for (FCounterInternal* Counter : Counters)
	{
		delete Counter;
	}
}

void FCounterProvider::EnumerateCounters(TFunctionRef<void(const ICounter &)> Callback) const
{
	uint32 Id = 0;
	for (FCounterInternal* Counter : Counters)
	{
		Callback(*Counter);
	}
}

FCounterInternal* FCounterProvider::CreateCounter(const TCHAR* Name, const TCHAR* Description, ECounterDisplayHint DisplayHint)
{
	FCounterInternal* Counter = new FCounterInternal(Session.GetLinearAllocator(), Counters.Num(), Name, Description, DisplayHint);
	Counters.Add(Counter);
	return Counter;
}

template<typename T>
static void InsertOp(FCounterInternal& Counter, double Time, ECounterOpType Type, T Arg)
{
	TPagedArray<double>& Timestamps = Counter.EditTimestamps();
	uint64 InsertionIndex;
	if (Timestamps.Num() == 0 || Timestamps[Timestamps.Num() - 1] <= Time)
	{
		InsertionIndex = Timestamps.Num();
	}
	else
	{
		auto TimestampIterator = Timestamps.GetIteratorFromItem(Timestamps.Num() - 1);
		auto CurrentPage = TimestampIterator.GetCurrentPage();
		while (CurrentPage && *GetFirstItem(*CurrentPage) > Time)
		{
			CurrentPage = TimestampIterator.PrevPage();
		}
		if (!CurrentPage)
		{
			InsertionIndex = 0;
		}
		else
		{
			uint64 PageInsertionIndex = Algo::LowerBound(*CurrentPage, Time);
			InsertionIndex = TimestampIterator.GetCurrentPageIndex() * Timestamps.GetPageSize() + PageInsertionIndex;
		}
	}
	Timestamps.Insert(InsertionIndex) = Time;
	Counter.EditOps().Insert(InsertionIndex) = Type;
	if (Counter.GetDisplayHint() == CounterDisplayHint_FloatingPoint)
	{
		Counter.EditDoubleOpArguments().Insert(InsertionIndex) = double(Arg);
	}
	else
	{
		Counter.EditIntOpArguments().Insert(InsertionIndex) = int64(Arg);
	}
}

void FCounterProvider::Add(FCounterInternal& Counter, double Time, int64 Value)
{
	InsertOp(Counter, Time, CounterOpType_Add, Value);
}

void FCounterProvider::Add(FCounterInternal& Counter, double Time, double Value)
{
	InsertOp(Counter, Time, CounterOpType_Add, Value);
}

void FCounterProvider::Set(FCounterInternal& Counter, double Time, int64 Value)
{
	InsertOp(Counter, Time, CounterOpType_Set, Value);
}

void FCounterProvider::Set(FCounterInternal& Counter, double Time, double Value)
{
	InsertOp(Counter, Time, CounterOpType_Set, Value);
}



}