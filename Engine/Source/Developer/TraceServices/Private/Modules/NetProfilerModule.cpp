// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetProfilerModule.h"
#include "Analyzers/NetTraceAnalyzer.h"
#include "AnalysisServicePrivate.h"
#include "Model/NetProfilerProvider.h"

namespace Trace
{

FName FNetProfilerModule::ModuleName("TraceModule_NetProfiler");
static const FName NetProfilerProviderName("NetProfilerProvider");

void FNetProfilerModule::GetModuleInfo(FModuleInfo& OutModuleInfo)
{
	OutModuleInfo.Name = ModuleName;
	OutModuleInfo.DisplayName = TEXT("NetProfiler");
}
	
void FNetProfilerModule::OnAnalysisBegin(IAnalysisSession& InSession)
{
	FAnalysisSession& Session = static_cast<FAnalysisSession&>(InSession);

	FNetProfilerProvider* NetProfilerProvider = new FNetProfilerProvider(Session);
	Session.AddProvider(NetProfilerProviderName, NetProfilerProvider);
	InSession.AddAnalyzer(new FNetTraceAnalyzer(InSession, *NetProfilerProvider));
}

void FNetProfilerModule::GetLoggers(TArray<const TCHAR *>& OutLoggers)
{
	//OutLoggers.Add(TEXT("NetProfiler"));
}

}
