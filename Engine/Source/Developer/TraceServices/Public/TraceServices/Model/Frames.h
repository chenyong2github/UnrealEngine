// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma  once

#include "TraceServices/Model/AnalysisSession.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Templates/Function.h"

namespace Trace
{

struct FFrame
{
	uint64 Index;
	double StartTime;
	double EndTime;
};

class IFrameProvider
	: public IProvider
{
public:
	virtual ~IFrameProvider() = default;
	virtual uint64 GetFrameCount(ETraceFrameType FrameType) const = 0;
	virtual void EnumerateFrames(ETraceFrameType FrameType, uint64 Start, uint64 End, TFunctionRef<void(const FFrame&)> Callback) const = 0;
};

TRACESERVICES_API const IFrameProvider& ReadFrameProvider(const IAnalysisSession& Session);

}