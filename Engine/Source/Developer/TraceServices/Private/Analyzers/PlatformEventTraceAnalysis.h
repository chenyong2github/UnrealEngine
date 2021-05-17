// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Analyzer.h"
#include "ProfilingDebugging/MiscTrace.h"

namespace TraceServices
{

class IAnalysisSession;
class FContextSwitchProvider;
class FStackSampleProvider;

class FPlatformEventTraceAnalyzer
	: public UE::Trace::IAnalyzer
{
public:
	FPlatformEventTraceAnalyzer(IAnalysisSession& Session,
								FContextSwitchProvider& ContextSwitchProvider,
								FStackSampleProvider& StackSampleProvider);
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual bool OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override;

private:
	enum : uint16
	{
		RouteId_ContextSwitch,
		RouteId_StackSample,
	};

	IAnalysisSession& Session;
	FContextSwitchProvider& ContextSwitchProvider;
	FStackSampleProvider& StackSampleProvider;
};

} // namespace TraceServices
