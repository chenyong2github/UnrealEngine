// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Analyzer.h"
#include "Containers/Map.h"

namespace Trace
{
	class IAnalysisSession;
	class IEditableCounter;
	class ICounterProvider;
}

class FCountersAnalyzer
	: public Trace::IAnalyzer
{
public:
	FCountersAnalyzer(Trace::IAnalysisSession& Session, Trace::ICounterProvider& CounterProvider);
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual bool OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override;

private:
	enum : uint16
	{
		RouteId_Spec,
		RouteId_SetValueInt,
		RouteId_SetValueFloat,
	};

	Trace::IAnalysisSession& Session;
	Trace::ICounterProvider& CounterProvider;
	TMap<uint16, Trace::IEditableCounter*> CountersMap;
};
