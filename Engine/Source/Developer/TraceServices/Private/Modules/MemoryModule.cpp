// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemoryModule.h"
#include "Analyzers/MemoryAnalysis.h"
#include "TraceServices/Model/AnalysisSession.h"

namespace Trace
{

static const FName MemoryModuleName("TraceModule_Memory");

void FMemoryModule::GetModuleInfo(FModuleInfo& OutModuleInfo)
{
	OutModuleInfo.Name = MemoryModuleName;
	OutModuleInfo.DisplayName = TEXT("Memory");
}

void FMemoryModule::OnAnalysisBegin(IAnalysisSession& Session)
{
	Session.AddAnalyzer(new FMemoryAnalyzer(Session));
}

void FMemoryModule::GetLoggers(TArray<const TCHAR *>& OutLoggers)
{
	OutLoggers.Add(TEXT("Memory"));
}

}
