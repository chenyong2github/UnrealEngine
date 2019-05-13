// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Trace.h"
#include "Trace/Analyzer.h"
#include "Templates/SharedPointer.h"
#include "Model/TimingProfiler.h"

namespace Trace
{
	class FAnalysisSession;
}

class FGpuProfilerAnalyzer
	: public Trace::IAnalyzer
{
public:
	FGpuProfilerAnalyzer(TSharedRef<Trace::FAnalysisSession> Session);
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual void OnEvent(uint16 RouteId, const FOnEventContext& Context) override;
	virtual void OnAnalysisEnd() override {};

private:
	enum : uint16
	{
		RouteId_EventSpec,
		RouteId_Frame,
	};

	double GpuTimestampToSessionTime(uint64 GpuMicroseconds);

	TSharedRef<Trace::FAnalysisSession> Session;
	TSharedRef<Trace::FTimingProfilerProvider> TimingProfilerProvider;
	TSharedRef<Trace::FTimingProfilerProvider::TimelineInternal> Timeline;
	TMap<uint64, uint32> EventTypeMap;
	bool Calibrated;
	uint64 GpuTimeOffset;
};
