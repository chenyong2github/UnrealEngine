// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/AnalysisService.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Common/PagedArray.h"

namespace Trace
{

class FFrameProvider
	: public IFrameProvider
{
public:
	static const FName ProviderName;

	FFrameProvider(IAnalysisSession& Session);

	virtual uint64 GetFrameCount(ETraceFrameType FrameType) const override;
	virtual void EnumerateFrames(ETraceFrameType FrameType, uint64 Start, uint64 End, TFunctionRef<void(const FFrame&)> Callback) const override;
	virtual const TArray64<double>& GetFrameStartTimes(ETraceFrameType FrameType) const override { return FrameStartTimes[FrameType]; }
	virtual bool GetFrameFromTime(ETraceFrameType FrameType, double Time, FFrame& OutFrame) const override;
	virtual const FFrame* GetFrame(ETraceFrameType FrameType, uint64 Index) const override;
	void BeginFrame(ETraceFrameType FrameType, double Time);
	void EndFrame(ETraceFrameType FrameType, double Time);

private:
	IAnalysisSession& Session;
	TArray<TPagedArray<FFrame>> Frames;
	TArray64<double> FrameStartTimes[TraceFrameType_Count];
};

}