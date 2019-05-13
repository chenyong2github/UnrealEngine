// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Trace.h"
#include "Trace/Analyzer.h"
#include "Templates/SharedPointer.h"

namespace Trace
{
	class FAnalysisSession;
	class FCounterInternal;
	class FCountersProvider;
}

class FStatsAnalyzer
	: public Trace::IAnalyzer
{
public:
	FStatsAnalyzer(TSharedRef<Trace::FAnalysisSession> Session);
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual void OnEvent(uint16 RouteId, const FOnEventContext& Context) override;
	virtual void OnAnalysisEnd() override;

private:
	enum : uint16
	{
		RouteId_Spec,
		RouteId_EventBatch,
	};

	struct FThreadState
	{
		uint64 LastCycle = 0;
	};

	TSharedRef<FThreadState> GetThreadState(uint32 ThreadId);

	TSharedRef<Trace::FAnalysisSession> Session;
	TSharedRef<Trace::FCountersProvider> CountersProvider;
	TMap<uint32, Trace::FCounterInternal*> CountersMap;
	TMap<uint32, TSharedRef<FThreadState>> ThreadStatesMap;
};
