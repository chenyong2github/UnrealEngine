// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Trace.h"
#include "Trace/Analyzer.h"
#include "Containers/Map.h"

namespace Trace
{
	class IAnalysisSession;
	class ICounter;
	class ICounterProvider;
}

class FCsvProfilerAnalyzer
	: public Trace::IAnalyzer
{
public:
	FCsvProfilerAnalyzer(Trace::IAnalysisSession& Session, Trace::ICounterProvider& CounterProvider);
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual void OnEvent(uint16 RouteId, const FOnEventContext& Context) override;
	virtual void OnAnalysisEnd() override;

private:
	enum : uint16
	{
		RouteId_InlineStat,
		RouteId_DeclaredStat,
		RouteId_CustomStatInlineInt,
		RouteId_CustomStatInlineFloat,
		RouteId_CustomStatDeclaredInt,
		RouteId_CustomStatDeclaredFloat,
	};

	enum : uint8
	{
		OpType_Set,
		OpType_Min,
		OpType_Max,
		OpType_Accumulate,
	};

	void HandleIntCounterEvent(Trace::ICounter* Counter, const FOnEventContext& Context);
	void HandleFloatCounterEvent(Trace::ICounter* Counter, const FOnEventContext& Context);
	Trace::ICounter* GetDeclaredStatCounter(uint32 StatId, bool bIsFloatingPoint);
	Trace::ICounter* GetInlineStatCounter(uint64 StatNamePointer, bool bIsFloatingPoint);

	Trace::IAnalysisSession& Session;
	Trace::ICounterProvider& CounterProvider;
	TMap<uint32, const TCHAR*> InlinePendingCountersMap;
	TMap<uint32, Trace::ICounter*> InlineCountersMap;
	TMap<uint64, const TCHAR*> DeclaredPendingCountersMap;
	TMap<uint64, Trace::ICounter*> DeclaredCountersMap;
};
