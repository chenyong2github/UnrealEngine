// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchTraceModule.h"
#include "PoseSearchTraceAnalyzer.h"
#include "PoseSearchTraceProvider.h"

namespace UE { namespace PoseSearch {

const FName FPoseSearchTraceModule::ModuleName("PoseSearchTrace");

void FPoseSearchTraceModule::GetModuleInfo(TraceServices::FModuleInfo& OutModuleInfo)
{
	OutModuleInfo.Name = ModuleName;
	OutModuleInfo.DisplayName = TEXT("PoseSearch");
}

void FPoseSearchTraceModule::OnAnalysisBegin(TraceServices::IAnalysisSession& InSession)
{
	// Add our provider and analyzer, starting our systems
	FTraceProvider* PoseSearchProvider = new FTraceProvider(InSession);
	InSession.AddProvider(FTraceProvider::ProviderName, PoseSearchProvider);
	InSession.AddAnalyzer(new FTraceAnalyzer(InSession, *PoseSearchProvider));
}

void FPoseSearchTraceModule::GetLoggers(TArray<const TCHAR*>& OutLoggers)
{
	OutLoggers.Add(TEXT("PoseSearch"));
}

void FPoseSearchTraceModule::GenerateReports(const TraceServices::IAnalysisSession& Session, const TCHAR* CmdLine, const TCHAR* OutputDirectory)
{
}

}}
