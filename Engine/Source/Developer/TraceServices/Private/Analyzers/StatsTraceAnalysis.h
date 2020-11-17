// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Analyzer.h"
#include "Templates/SharedPointer.h"

namespace TraceServices
{

class IAnalysisSession;
class IEditableCounter;
class ICounterProvider;

class FStatsAnalyzer
	: public UE::Trace::IAnalyzer
{
public:
	FStatsAnalyzer(IAnalysisSession& Session, ICounterProvider& CounterProvider);
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

	IAnalysisSession& Session;
	ICounterProvider& CounterProvider;
	TMap<uint32, IEditableCounter*> CountersMap;
	TMap<uint32, TSharedRef<FThreadState>> ThreadStatesMap;
};

} // namespace TraceServices
