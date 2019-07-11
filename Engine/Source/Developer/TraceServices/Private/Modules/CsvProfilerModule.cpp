// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CsvProfilerModule.h"
#include "Analyzers/CsvProfilerTraceAnalysis.h"
#include "AnalysisServicePrivate.h"
#include "TraceServices/ModuleService.h"

namespace Trace
{

static const FName CsvProfilerModuleName("TraceModule_CsvProfiler");
static const FName CsvProfilerProviderName("CsvProfilerProvider");

void FCsvProfilerModule::GetModuleInfo(FModuleInfo& OutModuleInfo)
{
	OutModuleInfo.Name = CsvProfilerModuleName;
	OutModuleInfo.DisplayName = TEXT("CsvProfiler");
}
	

void FCsvProfilerModule::OnAnalysisBegin(IAnalysisSession& Session)
{
	const IFrameProvider& FrameProvider = ReadFrameProvider(Session);
	const IThreadProvider& ThreadProvider = ReadThreadProvider(Session);
	FCsvProfilerProvider* CsvProfilerProvider = new FCsvProfilerProvider(Session);
	Session.AddProvider(CsvProfilerProviderName, CsvProfilerProvider);
	Session.AddAnalyzer(new FCsvProfilerAnalyzer(Session, *CsvProfilerProvider, FrameProvider, ThreadProvider));
}

void FCsvProfilerModule::GetLoggers(TArray<const TCHAR *>& OutLoggers)
{
}

const ICsvProfilerProvider* ReadCsvProfilerProvider(const IAnalysisSession& Session)
{
	return Session.ReadProvider<ICsvProfilerProvider>(CsvProfilerProviderName);
}

}
