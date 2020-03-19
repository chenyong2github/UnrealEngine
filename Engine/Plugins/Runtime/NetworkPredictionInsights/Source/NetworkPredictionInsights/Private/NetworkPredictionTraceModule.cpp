// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkPredictionTraceModule.h"
#include "NetworkPredictionProvider.h"
#include "NetworkPredictionAnalyzer.h"

FName FNetworkPredictionTraceModule::ModuleName("NetworkPredictionTrace");

void FNetworkPredictionTraceModule::GetModuleInfo(Trace::FModuleInfo& OutModuleInfo)
{
	OutModuleInfo.Name = ModuleName;
	OutModuleInfo.DisplayName = TEXT("NetworkPrediction");
}

void FNetworkPredictionTraceModule::OnAnalysisBegin(Trace::IAnalysisSession& InSession)
{
	FNetworkPredictionProvider* NetworkPredictionProvider = new FNetworkPredictionProvider(InSession);
	InSession.AddProvider(FNetworkPredictionProvider::ProviderName, NetworkPredictionProvider);
	
	InSession.AddAnalyzer(new FNetworkPredictionAnalyzer(InSession, *NetworkPredictionProvider));
}

void FNetworkPredictionTraceModule::GetLoggers(TArray<const TCHAR *>& OutLoggers)
{
	OutLoggers.Add(TEXT("NetworkPrediction"));
}

void FNetworkPredictionTraceModule::GenerateReports(const Trace::IAnalysisSession& Session, const TCHAR* CmdLine, const TCHAR* OutputDirectory)
{

}

