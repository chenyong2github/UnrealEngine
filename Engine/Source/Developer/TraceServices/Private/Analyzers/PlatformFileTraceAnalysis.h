// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Trace.h"
#include "Trace/Analyzer.h"
#include "Templates/SharedPointer.h"

namespace Trace
{
	class FAnalysisSession;
	class FTimeline;
	class FFileActivityProvider;
}

class FPlatformFileTraceAnalyzer
	: public Trace::IAnalyzer
{
public:
	FPlatformFileTraceAnalyzer(TSharedRef<Trace::FAnalysisSession> Session);
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual void OnEvent(uint16 RouteId, const FOnEventContext& Context) override;
	virtual void OnAnalysisEnd() override;

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
	};

	TSharedRef<Trace::FAnalysisSession> Session;
	TSharedRef<Trace::FFileActivityProvider> FileActivityProvider;
	TMap<uint64, uint32> OpenFilesMap;
	TMap<uint64, FPendingActivity> PendingOpenMap;
	TMap<uint64, FPendingActivity> PendingCloseMap;
	TMap<uint64, FPendingActivity> ActiveReadsMap;
	TMap<uint64, FPendingActivity> ActiveWritesMap;
};
