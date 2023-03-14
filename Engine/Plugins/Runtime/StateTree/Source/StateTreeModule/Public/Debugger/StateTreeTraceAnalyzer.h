// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_STATETREE_DEBUGGER

#include "Trace/Analyzer.h"

class FStateTreeTraceProvider;
namespace TraceServices { class IAnalysisSession; }

class FStateTreeTraceAnalyzer : public UE::Trace::IAnalyzer
{
public:
	FStateTreeTraceAnalyzer(TraceServices::IAnalysisSession& InSession, FStateTreeTraceProvider& InProvider);

	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual void OnAnalysisEnd() override {}
	virtual bool OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override;

private:
	enum : uint16
	{
		RouteId_Instance,
		RouteId_LogMessage,
		RouteId_State,
		RouteId_Task,
		RouteId_ActiveStates
	};

	TraceServices::IAnalysisSession& Session;
	FStateTreeTraceProvider& Provider;
};
#endif // WITH_STATETREE_DEBUGGER