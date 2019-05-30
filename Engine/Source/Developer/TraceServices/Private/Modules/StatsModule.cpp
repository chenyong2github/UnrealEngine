// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "StatsModule.h"
#include "Analyzers/StatsTraceAnalysis.h"
#include "AnalysisServicePrivate.h"
#include "TraceServices/ModuleService.h"
#include "Model/Counters.h"

namespace Trace
{

FName FStatsModule::ModuleName("TraceModule_Stats");

void FStatsModule::GetModuleInfo(FModuleInfo& OutModuleInfo)
{
	OutModuleInfo.Name = ModuleName;
	OutModuleInfo.DisplayName = TEXT("Stats");
}
	
static const FName CounterProviderName("CounterProvider");

void FStatsModule::OnAnalysisBegin(IAnalysisSession& InSession, bool bIsEnabled, TArray<IAnalyzer*>& OutAnalyzers)
{
	FAnalysisSession& Session = static_cast<FAnalysisSession&>(InSession);
	FCounterProvider* CounterProvider = new FCounterProvider(InSession);
	InSession.AddProvider(CounterProviderName, CounterProvider);
	OutAnalyzers.Add(new FStatsAnalyzer(Session, *CounterProvider));
}

void FStatsModule::GetLoggers(TArray<const TCHAR *>& OutLoggers)
{
	OutLoggers.Add(TEXT("Stats"));
}

const ICounterProvider* ReadCounterProvider(const IAnalysisSession& Session)
{
	return Session.ReadProvider<ICounterProvider>(CounterProviderName);
}

}
