// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayTraceModule.h"
#include "AnimationAnalyzer.h"
#include "AnimationProvider.h"
#include "GameplayProvider.h"
#include "GameplayAnalyzer.h"

FName FGameplayTraceModule::ModuleName("GameplayTrace");

void FGameplayTraceModule::GetModuleInfo(Trace::FModuleInfo& OutModuleInfo)
{
	OutModuleInfo.Name = ModuleName;
	OutModuleInfo.DisplayName = TEXT("Gameplay");
}

void FGameplayTraceModule::OnAnalysisBegin(Trace::IAnalysisSession& InSession)
{
	FGameplayProvider* GameplayProvider = new FGameplayProvider(InSession);
	InSession.AddProvider(FGameplayProvider::ProviderName, GameplayProvider);
	FAnimationProvider* AnimationProvider = new FAnimationProvider(InSession, *GameplayProvider);
	InSession.AddProvider(FAnimationProvider::ProviderName, AnimationProvider);

	InSession.AddAnalyzer(new FAnimationAnalyzer(InSession, *AnimationProvider));
	InSession.AddAnalyzer(new FGameplayAnalyzer(InSession, *GameplayProvider));
}

void FGameplayTraceModule::GetLoggers(TArray<const TCHAR *>& OutLoggers)
{
	OutLoggers.Add(TEXT("Object"));
	OutLoggers.Add(TEXT("Animation"));
}

void FGameplayTraceModule::GenerateReports(const Trace::IAnalysisSession& Session, const TCHAR* CmdLine, const TCHAR* OutputDirectory)
{

}

const IAnimationProvider* ReadAnimationProvider(const Trace::IAnalysisSession& Session)
{
	return Session.ReadProvider<IAnimationProvider>(FAnimationProvider::ProviderName);
}

