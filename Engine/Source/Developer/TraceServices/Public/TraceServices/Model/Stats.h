// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Model/AnalysisSession.h"

namespace Trace
{

enum ECounterDisplayHint
{
	CounterDisplayHint_None,
	CounterDisplayHint_Memory,
	CounterDisplayHint_FloatingPoint,
};

class ICounter
{
public:
	virtual ~ICounter() = default;

	virtual const TCHAR* GetName() const = 0;
	virtual uint32 GetId() const = 0;
	virtual ECounterDisplayHint GetDisplayHint() const = 0;
	virtual void EnumerateValues(double IntervalStart, double IntervalEnd, TFunctionRef<void(double, int64)> Callback) const = 0;
	virtual void EnumerateFloatValues(double IntervalStart, double IntervalEnd, TFunctionRef<void(double, double)> Callback) const = 0;
};

class ICounterProvider
	: public IProvider
{
public:
	static const FName ProviderName;
	virtual ~ICounterProvider() = default;
	virtual uint64 GetCounterCount() const = 0;
	virtual void EnumerateCounters(TFunctionRef<void(const ICounter&)> Callback) const = 0;
};

TRACESERVICES_API const ICounterProvider* ReadCounterProvider(const IAnalysisSession& Session);

}
