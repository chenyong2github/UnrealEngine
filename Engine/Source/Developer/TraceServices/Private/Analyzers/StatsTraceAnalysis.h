// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Analyzer.h"
#include "Templates/SharedPointer.h"

namespace Trace
{
	class IAnalysisSession;
	class IEditableCounter;
	class ICounterProvider;
}

class FStatsAnalyzer
	: public Trace::IAnalyzer
{
public:
	FStatsAnalyzer(Trace::IAnalysisSession& Session, Trace::ICounterProvider& CounterProvider);
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual bool OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override;

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

	Trace::IAnalysisSession& Session;
	Trace::ICounterProvider& CounterProvider;
	TMap<uint32, Trace::IEditableCounter*> CountersMap;
	TMap<uint32, TSharedRef<FThreadState>> ThreadStatesMap;
};
