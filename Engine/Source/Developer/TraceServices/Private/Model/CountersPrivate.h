// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Model/Counters.h"
#include "TraceServices/Model/Frames.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "Templates/SharedPointer.h"
#include "Common/PagedArray.h"

namespace Trace
{

enum ECounterOpType : uint8
{
	CounterOpType_Add,
	CounterOpType_Set,
};

template<typename ValueType>
class TCounterData;

template<typename ValueType>
class TCounterDataIterator
{
public:
	TCounterDataIterator(const TCounterData<ValueType>& Outer, const TArray64<double>& FrameStartTimes)
		: FrameStartTimesIterator(FrameStartTimes.CreateConstIterator())
		, TimestampsIterator(Outer.Timestamps.GetIterator())
		, OpTypesIterator(Outer.OpTypes.GetIterator())
		, OpArgumentsIterator(Outer.OpArguments.GetIterator())
	{
		Current = MakeTuple(0.0, ValueType());
		UpdateValue();
	}

	const TTuple<double, ValueType>& operator*() const
	{
		return Current;
	}

	explicit operator bool() const
	{
		return bool(TimestampsIterator);
	}

	TCounterDataIterator& operator++()
	{
		++TimestampsIterator;
		++OpTypesIterator;
		++OpArgumentsIterator;
		UpdateValue();
		return *this;
	}

	TCounterDataIterator operator++(int)
	{
		TCounterDataIterator Tmp(*this);
		this->operator++();
		return Tmp;
	}

private:
	void UpdateValue()
	{
		const double* Time = TimestampsIterator.GetCurrentItem();
		if (Time)
		{
			bool bIsNewFrame = false;
			while (FrameStartTimesIterator && *FrameStartTimesIterator < *Time)
			{
				bIsNewFrame = true;
				++FrameStartTimesIterator;
			}
			Current.template Get<0>() = *Time;
			switch (*OpTypesIterator)
			{
			case CounterOpType_Add:
				if (bIsNewFrame)
				{
					Current.template Get<1>() = *OpArgumentsIterator;
				}
				else
				{
					Current.template Get<1>() += *OpArgumentsIterator;
				}
				break;
			case CounterOpType_Set:
				Current.template Get<1>() = *OpArgumentsIterator;
				break;
			}
		}
	}

	TArray64<double>::TConstIterator FrameStartTimesIterator;
	TPagedArray<double>::TIterator TimestampsIterator;
	TPagedArray<ECounterOpType>::TIterator OpTypesIterator;
	typename TPagedArray<ValueType>::TIterator OpArgumentsIterator;
	TTuple<double, ValueType> Current;
};

template<typename ValueType>
class TCounterData
{
public:
	typedef TCounterDataIterator<ValueType> TIterator;

	TCounterData(ILinearAllocator& Allocator)
		: Timestamps(Allocator, 1024)
		, OpTypes(Allocator, 1024)
		, OpArguments(Allocator, 1024)
	{

	}

	void InsertOp(double Timestamp, ECounterOpType OpType, ValueType OpArgument)
	{
		uint64 InsertionIndex;
		if (Timestamps.Num() == 0 || Timestamps[Timestamps.Num() - 1] <= Timestamp)
		{
			InsertionIndex = Timestamps.Num();
		}
		else if (Timestamps[0] >= Timestamp)
		{
			InsertionIndex = 0;
		}
		else
		{
			auto TimestampIterator = Timestamps.GetIteratorFromItem(Timestamps.Num() - 1);
			auto CurrentPage = TimestampIterator.GetCurrentPage();
			while (*GetFirstItem(*CurrentPage) > Timestamp)
			{
				CurrentPage = TimestampIterator.PrevPage();
			}
			uint64 PageInsertionIndex = Algo::LowerBound(*CurrentPage, Timestamp);
			InsertionIndex = TimestampIterator.GetCurrentPageIndex() * Timestamps.GetPageSize() + PageInsertionIndex;
		}
		Timestamps.Insert(InsertionIndex) = Timestamp;
		OpTypes.Insert(InsertionIndex) = OpType;
		OpArguments.Insert(InsertionIndex) = OpArgument;
	}

	uint64 Num() const
	{
		return Timestamps.Num();
	}

	TIterator GetIterator(const TArray64<double>& FrameStartTimes) const
	{
		return TIterator(*this, FrameStartTimes);
	}

private:
	template<typename IteratorValueType>
	friend class TCounterDataIterator;

	TPagedArray<double> Timestamps;
	TPagedArray<ECounterOpType> OpTypes;
	TPagedArray<ValueType> OpArguments;
};

class FCounter
	: public IEditableCounter
{
public:
	FCounter(ILinearAllocator& Allocator, const TArray64<double>& FrameStartTimes);
	virtual const TCHAR* GetName() const override { return Name; }
	virtual void SetName(const TCHAR* InName) override { Name = InName; }
	virtual const TCHAR* GetDescription() const override { return Description; }
	virtual void SetDescription(const TCHAR* InDescription) override { Description = InDescription; }
	virtual bool IsFloatingPoint() const override { return bIsFloatingPoint; }
	virtual void SetIsFloatingPoint(bool bInIsFloatingPoint) override;
	virtual ECounterDisplayHint GetDisplayHint() const { return DisplayHint; }
	virtual void SetDisplayHint(ECounterDisplayHint InDisplayHint) override { DisplayHint = InDisplayHint; }
	virtual void SetIsResetEveryFrame(bool bInIsResetEveryFrame) override { bIsResetEveryFrame = bInIsResetEveryFrame; }
	virtual void EnumerateValues(double IntervalStart, double IntervalEnd, bool bIncludeExternalBounds, TFunctionRef<void(double, int64)> Callback) const override;
	virtual void EnumerateFloatValues(double IntervalStart, double IntervalEnd, bool bIncludeExternalBounds, TFunctionRef<void(double, double)> Callback) const override;
	virtual void AddValue(double Time, int64 Value) override;
	virtual void AddValue(double Time, double Value) override;
	virtual void SetValue(double Time, int64 Value) override;
	virtual void SetValue(double Time, double Value) override;

private:
	const TArray64<double>& FrameStartTimes;
	TCounterData<int64> IntCounterData;
	TCounterData<double> DoubleCounterData;
	const TCHAR* Name = nullptr;
	const TCHAR* Description = nullptr;
	uint64 ModCount = 0;
	ECounterDisplayHint DisplayHint = CounterDisplayHint_None;
	bool bIsFloatingPoint = false;
	bool bIsResetEveryFrame = false;
};

class FCounterProvider
	: public ICounterProvider
{
public:
	static const FName ProviderName;

	FCounterProvider(IAnalysisSession& Session, IFrameProvider& FrameProvider);
	virtual ~FCounterProvider();
	virtual uint64 GetCounterCount() const override { return Counters.Num(); }
	virtual void EnumerateCounters(TFunctionRef<void(uint32, const ICounter&)> Callback) const override;
	virtual bool ReadCounter(uint32 CounterId, TFunctionRef<void(const ICounter&)> Callback) const override;
	virtual IEditableCounter* CreateCounter() override;
	virtual void AddCounter(const ICounter* Counter) override;

private:
	IAnalysisSession& Session;
	IFrameProvider& FrameProvider;
	TArray<const ICounter*> Counters;
};

}
