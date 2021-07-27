// Copyright Epic Games, Inc. All Rights Reserved.

#include "DiagnosticsModule.h"
#include "Analyzers/DiagnosticsAnalysis.h"
#include "TraceServices/Model/AnalysisSession.h"

namespace TraceServices
{

static const FName DiagnosticsModuleName("TraceModule_Diagnostics");

void FDiagnosticsModule::GetModuleInfo(FModuleInfo& OutModuleInfo)
{
	OutModuleInfo.Name = DiagnosticsModuleName;
	OutModuleInfo.DisplayName = TEXT("Diagnostics");
}

void FDiagnosticsModule::OnAnalysisBegin(IAnalysisSession& Session)
{
	FDiagnosticsProvider* DiagnosticsProvider = new FDiagnosticsProvider(Session);
	Session.AddProvider(GetDiagnosticsProviderName(), DiagnosticsProvider);

	Session.AddAnalyzer(new FDiagnosticsAnalyzer(Session, DiagnosticsProvider));
}

void FDiagnosticsModule::GetLoggers(TArray<const TCHAR *>& OutLoggers)
{
	OutLoggers.Add(TEXT("Diagnostics"));
}

} // namespace TraceServices
