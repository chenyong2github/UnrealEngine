// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Analyzer.h"
#include "Containers/Map.h"
#include "TraceServices/Model/AnalysisSession.h"

namespace Trace
{
	class FTimeline;
	class FFileActivityProvider;
}

class FPlatformFileTraceAnalyzer
	: public Trace::IAnalyzer
{
public:
	FPlatformFileTraceAnalyzer(Trace::IAnalysisSession& Session, Trace::FFileActivityProvider& FileActivityProvider);
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual bool OnEvent(uint16 RouteId, const FOnEventContext& Context) override;

private:
	enum : uint16
	{
		RouteId_BeginOpen,
		RouteId_EndOpen,
		RouteId_BeginClose,
		RouteId_EndClose,
		RouteId_BeginRead,
		RouteId_EndRead,
		RouteId_BeginWrite,
		RouteId_EndWrite,

		RouteId_Count
	};

	struct FPendingActivity
	{
		uint64 ActivityIndex;
		uint32 FileIndex;
		uint32 ThreadId;
	};

	Trace::IAnalysisSession& Session;
	Trace::FFileActivityProvider& FileActivityProvider;
	TMap<uint64, uint32> OpenFilesMap;
	TMap<uint32, FPendingActivity> PendingOpenMap;
	TMap<uint32, FPendingActivity> PendingCloseMap;
	TMap<uint64, FPendingActivity> ActiveReadsMap;
	TMap<uint64, FPendingActivity> ActiveWritesMap;
};
