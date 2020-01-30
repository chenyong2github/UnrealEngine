// Copyright Epic Games, Inc. All Rights Reserved.

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
	ICounterProvider& CounterProvider = EditCounterProvider(Session);
	FCsvProfilerProvider* CsvProfilerProvider = new FCsvProfilerProvider(Session);
	Session.AddProvider(CsvProfilerProviderName, CsvProfilerProvider);
	Session.AddAnalyzer(new FCsvProfilerAnalyzer(Session, *CsvProfilerProvider, CounterProvider, FrameProvider, ThreadProvider));
}

void FCsvProfilerModule::GenerateReports(const IAnalysisSession& Session, const TCHAR* CmdLine, const TCHAR* OutputDirectory)
{
	const ICsvProfilerProvider* CsvProfilerProvider = Trace::ReadCsvProfilerProvider(Session);
	if (CsvProfilerProvider)
	{
		CsvProfilerProvider->EnumerateCaptures([CsvProfilerProvider, &OutputDirectory](const FCaptureInfo& CaptureInfo)
		{
			Table2Csv(CsvProfilerProvider->GetTable(CaptureInfo.Id), *(FString(OutputDirectory) / TEXT("CsvProfiler") / CaptureInfo.Filename));
		});
	}
}

const ICsvProfilerProvider* ReadCsvProfilerProvider(const IAnalysisSession& Session)
{
	return Session.ReadProvider<ICsvProfilerProvider>(CsvProfilerProviderName);
}

}
