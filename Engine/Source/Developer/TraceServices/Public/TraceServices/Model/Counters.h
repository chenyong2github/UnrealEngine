// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Model/AnalysisSession.h"
#include "Templates/Function.h"

namespace Trace
{

enum ECounterDisplayHint
{
	CounterDisplayHint_None,
	CounterDisplayHint_Memory,
};

class ICounter
{
public:
	virtual ~ICounter() = default;

	virtual const TCHAR* GetName() const = 0;
	virtual const TCHAR* GetDescription() const = 0;
	virtual bool IsFloatingPoint() const = 0;
	virtual ECounterDisplayHint GetDisplayHint() const = 0;
	virtual void EnumerateValues(double IntervalStart, double IntervalEnd, bool bIncludeExternalBounds, TFunctionRef<void(double, int64)> Callback) const = 0;
	virtual void EnumerateFloatValues(double IntervalStart, double IntervalEnd, bool bIncludeExternalBounds, TFunctionRef<void(double, double)> Callback) const = 0;
};

class IEditableCounter
	: public ICounter
{
public:
	virtual ~IEditableCounter() = default;

	virtual void SetName(const TCHAR* Name) = 0;
	virtual void SetDescription(const TCHAR* Description) = 0;
	virtual void SetIsFloatingPoint(bool bIsFloatingPoint) = 0;
	virtual void SetDisplayHint(ECounterDisplayHint DisplayHint) = 0;
	virtual void SetIsResetEveryFrame(bool bInIsResetEveryFrame) = 0;
	virtual void AddValue(double Time, int64 Value) = 0;
	virtual void AddValue(double Time, double Value) = 0;
	virtual void SetValue(double Time, int64 Value) = 0;
	virtual void SetValue(double Time, double Value) = 0;
};

class ICounterProvider
	: public IProvider
{
public:
	virtual ~ICounterProvider() = default;
	virtual uint64 GetCounterCount() const = 0;
	virtual bool ReadCounter(uint32 CounterId, TFunctionRef<void(const ICounter&)> Callback) const = 0;
	virtual void EnumerateCounters(TFunctionRef<void(uint32, const ICounter&)> Callback) const = 0;
	virtual IEditableCounter* CreateCounter() = 0;
	virtual void AddCounter(const ICounter* Counter) = 0;
};

TRACESERVICES_API const ICounterProvider& ReadCounterProvider(const IAnalysisSession& Session);
TRACESERVICES_API ICounterProvider& EditCounterProvider(IAnalysisSession& Session);

}
