// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TimingProfilerModule.h"
#include "Analyzers/CpuProfilerTraceAnalysis.h"
#include "Analyzers/GpuProfilerTraceAnalysis.h"
#include "AnalysisServicePrivate.h"
#include "Model/TimingProfilerPrivate.h"

namespace Trace
{

FName FTimingProfilerModule::ModuleName("TraceModule_TimingProfiler");

void FTimingProfilerModule::GetModuleInfo(FModuleInfo& OutModuleInfo)
{
	OutModuleInfo.Name = ModuleName;
	OutModuleInfo.DisplayName = TEXT("Timing");
}
	
static const FName TimingProfilerProviderName("TimingProfilerProvider");

void FTimingProfilerModule::OnAnalysisBegin(IAnalysisSession& InSession, bool bIsEnabled, TArray<IAnalyzer*>& OutAnalyzers)
{
	FAnalysisSession& Session = static_cast<FAnalysisSession&>(InSession);
	
	FTimingProfilerProvider* TimingProfilerProvider = new FTimingProfilerProvider(Session);
	Session.AddProvider(TimingProfilerProviderName, TimingProfilerProvider);
	OutAnalyzers.Add(new FCpuProfilerAnalyzer(Session, *TimingProfilerProvider));
	OutAnalyzers.Add(new FGpuProfilerAnalyzer(Session, *TimingProfilerProvider));
}

void FTimingProfilerModule::GetLoggers(TArray<const TCHAR *>& OutLoggers)
{
	OutLoggers.Add(TEXT("CpuProfiler"));
	OutLoggers.Add(TEXT("GpuProfiler"));
}

const ITimingProfilerProvider* ReadTimingProfilerProvider(const IAnalysisSession& Session)
{
	return Session.ReadProvider<ITimingProfilerProvider>(TimingProfilerProviderName);
}

}
