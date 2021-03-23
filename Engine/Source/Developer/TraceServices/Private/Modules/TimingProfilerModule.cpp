// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimingProfilerModule.h"
#include "Analyzers/CpuProfilerTraceAnalysis.h"
#include "Analyzers/GpuProfilerTraceAnalysis.h"
#include "AnalysisServicePrivate.h"
#include "Model/ThreadsPrivate.h"
#include "Model/TimingProfilerPrivate.h"

namespace Trace
{

static const FName TimingProfilerModuleName("TraceModule_TimingProfiler");
static const FName TimingProfilerProviderName("TimingProfilerProvider");

void FTimingProfilerModule::GetModuleInfo(FModuleInfo& OutModuleInfo)
{
	OutModuleInfo.Name = TimingProfilerModuleName;
	OutModuleInfo.DisplayName = TEXT("Timing");
}
	
void FTimingProfilerModule::OnAnalysisBegin(IAnalysisSession& InSession)
{
	FAnalysisSession& Session = static_cast<FAnalysisSession&>(InSession);
	
	auto* ThreadProvider = Session.EditProvider<FThreadProvider>(FThreadProvider::ProviderName);
	check(ThreadProvider != nullptr);

	FTimingProfilerProvider* TimingProfilerProvider = new FTimingProfilerProvider(Session);
	Session.AddProvider(TimingProfilerProviderName, TimingProfilerProvider);
	Session.AddAnalyzer(new FCpuProfilerAnalyzer(Session, *TimingProfilerProvider, *ThreadProvider));
	Session.AddAnalyzer(new FGpuProfilerAnalyzer(Session, *TimingProfilerProvider));
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
