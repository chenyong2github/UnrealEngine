// Copyright Epic Games, Inc. All Rights Reserved.

#include "CountersModule.h"
#include "Analyzers/CountersTraceAnalysis.h"
#include "TraceServices/ModuleService.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/Counters.h"

namespace Trace
{

static const FName CountersModuleName("TraceModule_Counters");

void FCountersModule::GetModuleInfo(FModuleInfo& OutModuleInfo)
{
	OutModuleInfo.Name = CountersModuleName;
	OutModuleInfo.DisplayName = TEXT("Counters");
}
	
void FCountersModule::OnAnalysisBegin(IAnalysisSession& Session)
{
	ICounterProvider& CounterProvider = EditCounterProvider(Session);
	Session.AddAnalyzer(new FCountersAnalyzer(Session, CounterProvider));
}

void FCountersModule::GetLoggers(TArray<const TCHAR *>& OutLoggers)
{
	OutLoggers.Add(TEXT("Counters"));
}

}
