// Copyright Epic Games, Inc. All Rights Reserved.

#include "SlateTraceModule.h"
#include "SlateProvider.h"
#include "SlateAnalyzer.h"

namespace UE
{
namespace SlateInsights
{
	
FName FSlateTraceModule::ModuleName("TraceModule_Slate");

void FSlateTraceModule::GetModuleInfo(Trace::FModuleInfo& OutModuleInfo)
{
	OutModuleInfo.Name = ModuleName;
	OutModuleInfo.DisplayName = TEXT("Slate");
}

void FSlateTraceModule::OnAnalysisBegin(Trace::IAnalysisSession& InSession)
{
	FSlateProvider* SlateProvider = new FSlateProvider(InSession);
	InSession.AddProvider(FSlateProvider::ProviderName, SlateProvider);

	InSession.AddAnalyzer(new FSlateAnalyzer(InSession, *SlateProvider));
}

void FSlateTraceModule::GetLoggers(TArray<const TCHAR *>& OutLoggers)
{
	OutLoggers.Add(TEXT("Slate"));
}

void FSlateTraceModule::GenerateReports(const Trace::IAnalysisSession& Session, const TCHAR* CmdLine, const TCHAR* OutputDirectory)
{

}

} //namespace SlateInsights
} //namespace UE