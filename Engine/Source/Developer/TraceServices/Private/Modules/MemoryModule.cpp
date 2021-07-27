// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemoryModule.h"
#include "Analyzers/AllocationsAnalysis.h"
#include "Analyzers/CallstacksAnalysis.h"
#include "Analyzers/MemoryAnalysis.h"
#include "Analyzers/ModuleAnalysis.h"
#include "Model/AllocationsProvider.h"
#include "Model/CallstacksProvider.h"
#include "TraceServices/Model/AnalysisSession.h"

namespace TraceServices
{

static const FName MemoryModuleName("TraceModule_Memory");

void FMemoryModule::GetModuleInfo(FModuleInfo& OutModuleInfo)
{
	OutModuleInfo.Name = MemoryModuleName;
	OutModuleInfo.DisplayName = TEXT("Memory");
}

void FMemoryModule::OnAnalysisBegin(IAnalysisSession& Session)
{
	// LLM Tag Stats
	FMemoryProvider* MemoryProvider = new FMemoryProvider(Session);
	Session.AddProvider(GetMemoryProviderName(), MemoryProvider);
	Session.AddAnalyzer(new FMemoryAnalyzer(Session, MemoryProvider));

	// Module
	Session.AddAnalyzer(new FModuleAnalyzer(Session));

	// Callstack
	FCallstacksProvider* CallstacksProvider = new FCallstacksProvider(Session);
	Session.AddProvider(GetCallstacksProviderName(), CallstacksProvider);
	Session.AddAnalyzer(new FCallstacksAnalyzer(Session, CallstacksProvider));

	// Allocations
	FAllocationsProvider* AllocationsProvider = new FAllocationsProvider(Session);
	Session.AddProvider(GetAllocationsProviderName(), AllocationsProvider);
	Session.AddAnalyzer(new FAllocationsAnalyzer(Session, *AllocationsProvider));
}

void FMemoryModule::GetLoggers(TArray<const TCHAR*>& OutLoggers)
{
	OutLoggers.Add(TEXT("Memory"));
}

} // namespace TraceServices
