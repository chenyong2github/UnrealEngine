// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CsvProfilerModule.h"
#include "Analyzers/CsvProfilerTraceAnalysis.h"
#include "AnalysisServicePrivate.h"
#include "TraceServices/ModuleService.h"

namespace Trace
{

static const FName CsvProfilerModuleName("TraceModule_CsvProfiler");

void FCsvProfilerModule::GetModuleInfo(FModuleInfo& OutModuleInfo)
{
	OutModuleInfo.Name = CsvProfilerModuleName;
	OutModuleInfo.DisplayName = TEXT("CsvProfiler");
}
	

void FCsvProfilerModule::OnAnalysisBegin(IAnalysisSession& Session)
{
	ICounterProvider& CounterProvider = EditCounterProvider(Session);
	Session.AddAnalyzer(new FCsvProfilerAnalyzer(Session, CounterProvider));
}

void FCsvProfilerModule::GetLoggers(TArray<const TCHAR *>& OutLoggers)
{
	OutLoggers.Add(TEXT("CsvProfiler"));
}

}
