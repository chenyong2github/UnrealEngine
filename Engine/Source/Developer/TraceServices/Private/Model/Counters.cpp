// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TraceServices/Model/Counters.h"
#include "Model/CountersPrivate.h"
#include "AnalysisServicePrivate.h"

namespace Trace
{

const FName FCounterProvider::ProviderName("CounterProvider");

FCounter::FCounter(ILinearAllocator& Allocator, const TPagedArray<double>& InFrameStartTimes, uint32 InId)
	: FrameStartTimes(InFrameStartTimes)
	, IntCounterData(Allocator)
	, DoubleCounterData(Allocator)
	, Id(InId)
{

}

template<typename CounterType, typename EnumerationType>
static void EnumerateCounterValuesInternal(const TCounterData<CounterType>& CounterData, const TPagedArray<double>& FrameStartTimes, bool bResetEveryFrame, double IntervalStart, double IntervalEnd, TFunctionRef<void(double, EnumerationType)> Callback)
{
	auto CounterIterator = bResetEveryFrame ? CounterData.GetIterator(FrameStartTimes) : CounterData.GetIterator();
	while (CounterIterator)
	{
		const TTuple<double, CounterType>& Current = *CounterIterator;
		double Time = Current.template Get<0>();
		CounterType CounterValue = Current.template Get<1>();
		if (Time > IntervalEnd)
		{
			break;
		}
		if (Time >= IntervalStart)
		{
			Callback(Time, CounterValue);
		}
		++CounterIterator;
	}
}

void FCounter::SetIsFloatingPoint(bool bInIsFloatingPoint)
{
	check(!ModCount);
	bIsFloatingPoint = bInIsFloatingPoint;
}

void FCounter::EnumerateValues(double IntervalStart, double IntervalEnd, TFunctionRef<void(double, int64)> Callback) const
{
	if (bIsFloatingPoint)
	{
		EnumerateCounterValuesInternal<double, int64>(DoubleCounterData, FrameStartTimes, bIsResetEveryFrame, IntervalStart, IntervalEnd, Callback);
	}
	else
	{
		EnumerateCounterValuesInternal<int64, int64>(IntCounterData, FrameStartTimes, bIsResetEveryFrame, IntervalStart, IntervalEnd, Callback);
	}
}

void FCounter::EnumerateFloatValues(double IntervalStart, double IntervalEnd, TFunctionRef<void(double, double)> Callback) const
{
	if (bIsFloatingPoint)
	{
		EnumerateCounterValuesInternal<double, double>(DoubleCounterData, FrameStartTimes, bIsResetEveryFrame, IntervalStart, IntervalEnd, Callback);
	}
	else
	{
		EnumerateCounterValuesInternal<int64, double>(IntCounterData, FrameStartTimes, bIsResetEveryFrame, IntervalStart, IntervalEnd, Callback);
	}
}

void FCounter::AddValue(double Time, int64 Value)
{
	if (bIsFloatingPoint)
	{
		DoubleCounterData.InsertOp(Time, CounterOpType_Add, double(Value));
	}
	else
	{
		IntCounterData.InsertOp(Time, CounterOpType_Add, Value);
	}
	++ModCount;
}

void FCounter::AddValue(double Time, double Value)
{
	if (bIsFloatingPoint)
	{
		DoubleCounterData.InsertOp(Time, CounterOpType_Add, Value);
	}
	else
	{
		IntCounterData.InsertOp(Time, CounterOpType_Add, int64(Value));
	}
	++ModCount;
}

void FCounter::SetValue(double Time, int64 Value)
{
	if (bIsFloatingPoint)
	{
		DoubleCounterData.InsertOp(Time, CounterOpType_Set, double(Value));
	}
	else
	{
		IntCounterData.InsertOp(Time, CounterOpType_Set, Value);
	}
	++ModCount;
}

void FCounter::SetValue(double Time, double Value)
{
	if (bIsFloatingPoint)
	{
		DoubleCounterData.InsertOp(Time, CounterOpType_Set, Value);
	}
	else
	{
		IntCounterData.InsertOp(Time, CounterOpType_Set, int64(Value));
	}
	++ModCount;
}

FCounterProvider::FCounterProvider(IAnalysisSession& InSession, IFrameProvider& InFrameProvider)
	: Session(InSession)
	, FrameProvider(InFrameProvider)
	, FrameStartTimes(InSession.GetLinearAllocator(), 1024)
{
	FrameAddedDelegateHandle = FrameProvider.OnFrameAdded().AddRaw(this, &FCounterProvider::FrameAdded);
}

FCounterProvider::~FCounterProvider()
{
	FrameProvider.OnFrameAdded().Remove(FrameAddedDelegateHandle);
	for (FCounter* Counter : Counters)
	{
		delete Counter;
	}
}

void FCounterProvider::EnumerateCounters(TFunctionRef<void(const ICounter &)> Callback) const
{
	uint32 Id = 0;
	for (FCounter* Counter : Counters)
	{
		Callback(*Counter);
	}
}

bool FCounterProvider::ReadCounter(uint32 CounterId, TFunctionRef<void(const ICounter &)> Callback) const
{
	if (CounterId < uint32(Counters.Num()))
	{
		return false;
	}
	Callback(*Counters[CounterId]);
	return true;
}

ICounter* FCounterProvider::CreateCounter()
{
	FCounter* Counter = new FCounter(Session.GetLinearAllocator(), FrameStartTimes, Counters.Num());
	Counters.Add(Counter);
	return Counter;
}

void FCounterProvider::FrameAdded(const FFrame& Frame)
{
	if (Frame.FrameType == TraceFrameType_Game)
	{
		FrameStartTimes.PushBack() = Frame.StartTime;
	}
}

const ICounterProvider& ReadCounterProvider(const IAnalysisSession& Session)
{
	return *Session.ReadProvider<ICounterProvider>(FCounterProvider::ProviderName);
}

ICounterProvider& EditCounterProvider(IAnalysisSession& Session)
{
	return *Session.EditProvider<ICounterProvider>(FCounterProvider::ProviderName);
}

}