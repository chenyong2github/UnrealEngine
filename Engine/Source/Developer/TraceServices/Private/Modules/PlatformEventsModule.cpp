// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlatformEventsModule.h"
#include "Analyzers/PlatformEventTraceAnalysis.h"
#include "AnalysisServicePrivate.h"
#include "Model/ContextSwitchesPrivate.h"
#include "Model/StackSamplesPrivate.h"

namespace TraceServices
{

static const FName PlatformEventsModuleName("TraceModule_PlatformEvents");

void FPlatformEventsModule::GetModuleInfo(FModuleInfo& OutModuleInfo)
{
	OutModuleInfo.Name = PlatformEventsModuleName;
	OutModuleInfo.DisplayName = TEXT("PlatformEvents");
}
	
void FPlatformEventsModule::OnAnalysisBegin(IAnalysisSession& InSession)
{
	FAnalysisSession& Session = static_cast<FAnalysisSession&>(InSession);

	FContextSwitchProvider* ContextSwitchProvider = new FContextSwitchProvider(Session);
	FStackSampleProvider* StackSampleProvider = new FStackSampleProvider(Session);
	
	Session.AddProvider(FContextSwitchProvider::ProviderName, ContextSwitchProvider);
	Session.AddProvider(FStackSampleProvider::ProviderName, StackSampleProvider);
	Session.AddAnalyzer(new FPlatformEventTraceAnalyzer(Session, *ContextSwitchProvider, *StackSampleProvider));
}

} // namespace TraceServices
