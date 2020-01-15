// Copyright Epic Games, Inc. All Rights Reserved.

#pragma  once

#include "TraceServices/Model/AnalysisSession.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Templates/Function.h"
#include "Delegates/Delegate.h"
#include "Containers/ContainersFwd.h"

namespace Trace
{

struct FFrame
{
	uint64 Index;
	double StartTime;
	double EndTime;
	ETraceFrameType FrameType;
};

class IFrameProvider
	: public IProvider
{
public:
	virtual ~IFrameProvider() = default;
	virtual uint64 GetFrameCount(ETraceFrameType FrameType) const = 0;
	virtual void EnumerateFrames(ETraceFrameType FrameType, uint64 Start, uint64 End, TFunctionRef<void(const FFrame&)> Callback) const = 0;
	virtual const TArray64<double>& GetFrameStartTimes(ETraceFrameType FrameType) const = 0;
	virtual bool GetFrameFromTime(ETraceFrameType FrameType, double Time, FFrame& OutFrame) const = 0;
	virtual const FFrame* GetFrame(ETraceFrameType FrameType, uint64 Index) const = 0;
};

TRACESERVICES_API const IFrameProvider& ReadFrameProvider(const IAnalysisSession& Session);

}