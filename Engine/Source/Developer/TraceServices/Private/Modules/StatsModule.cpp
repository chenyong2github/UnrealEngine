// Copyright Epic Games, Inc. All Rights Reserved.

#include "StatsModule.h"
#include "Analyzers/StatsTraceAnalysis.h"
#include "AnalysisServicePrivate.h"
#include "TraceServices/ModuleService.h"

namespace Trace
{

static const FName StatsModuleName("TraceModule_Stats");

void FStatsModule::GetModuleInfo(FModuleInfo& OutModuleInfo)
{
	OutModuleInfo.Name = StatsModuleName;
	OutModuleInfo.DisplayName = TEXT("Stats");
}
	
void FStatsModule::OnAnalysisBegin(IAnalysisSession& Session)
{
	ICounterProvider& CounterProvider = EditCounterProvider(Session);
	Session.AddAnalyzer(new FStatsAnalyzer(Session, CounterProvider));
}

void FStatsModule::GetLoggers(TArray<const TCHAR *>& OutLoggers)
{
	//OutLoggers.Add(TEXT("Stats"));
}

}
