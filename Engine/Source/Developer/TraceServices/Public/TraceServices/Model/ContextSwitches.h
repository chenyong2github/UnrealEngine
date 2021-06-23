// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Model/AnalysisSession.h"
#include "Common/PagedArray.h"

namespace TraceServices
{

struct FContextSwitch
{
	double Start;
	double End;
	uint32 CoreNumber;
};

enum class EContextSwitchEnumerationResult
{
	Continue,
	Stop,
};

class IContextSwitchProvider
	: public IProvider
{
public:
	typedef TFunctionRef<EContextSwitchEnumerationResult(const FContextSwitch&)> ContextSwitchCallback;

	virtual ~IContextSwitchProvider() = default;
	virtual int32 GetCoreNumber(uint32 ThreadId, double Time) const = 0;
	virtual const void EnumerateContextSwitches(uint32 ThreadId, double StartTime, double EndTime, ContextSwitchCallback Callback) const = 0;
	virtual bool HasData() const = 0;
};

TRACESERVICES_API const IContextSwitchProvider* ReadContextSwitchProvider(const IAnalysisSession& Session);

} // namespace TraceServices
