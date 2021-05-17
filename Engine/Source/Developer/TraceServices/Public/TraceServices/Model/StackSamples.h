// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/Model/AnalysisSession.h"
#include "Common/PagedArray.h"

namespace TraceServices
{

struct FStackSample
{
	double Time;
	uint32 Count;
	uint64* Addresses;
};

class IStackSampleProvider
	: public IProvider
{
public:
	virtual ~IStackSampleProvider() = default;
	virtual const TPagedArray<FStackSample>* GetStackSamples(uint32 ThreadId) const = 0;
};

TRACESERVICES_API const IStackSampleProvider& ReadStackSampleProvider(const IAnalysisSession& Session);

} // namespace TraceServices
