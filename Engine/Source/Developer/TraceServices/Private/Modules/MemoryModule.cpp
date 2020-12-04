// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemoryModule.h"
#include "Analyzers/AllocationsAnalysis.h"
#include "Analyzers/CallstacksAnalysis.h"
#include "Analyzers/MemoryAnalysis.h"
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
	FAllocationsProvider* AllocationsProvider = new FAllocationsProvider(Session.GetLinearAllocator());
	Session.AddProvider(AllocationsProvider->GetName(), AllocationsProvider);
	Session.AddAnalyzer(new FAllocationsAnalyzer(Session, *AllocationsProvider));

	FCallstacksProvider* CallstacksProvider = new FCallstacksProvider(Session);
	Session.AddProvider(CallstacksProvider->GetName(), CallstacksProvider);
	Session.AddAnalyzer(new FCallstacksAnalyzer(Session, CallstacksProvider));

	Session.AddAnalyzer(new FMemoryAnalyzer(Session));
}

void FMemoryModule::GetLoggers(TArray<const TCHAR *>& OutLoggers)
{
	OutLoggers.Add(TEXT("Memory"));
}

} // namespace TraceServices
