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

class IContextSwitchProvider
	: public IProvider
{
public:
	virtual ~IContextSwitchProvider() = default;
	virtual int32 GetCoreNumber(uint32 ThreadId, double Time) const = 0;
	virtual const TPagedArray<FContextSwitch>* GetContextSwitches(uint32 ThreadId) const = 0;
};

TRACESERVICES_API const IContextSwitchProvider& ReadContextSwitchProvider(const IAnalysisSession& Session);

} // namespace TraceServices
