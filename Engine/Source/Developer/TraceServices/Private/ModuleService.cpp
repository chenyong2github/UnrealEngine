// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TraceServices/ModuleService.h"
#include "ModuleServicePrivate.h"
#include "UObject/NameTypes.h"
#include "Features/IModularFeatures.h"

namespace Trace
{

const FName ModuleFeatureName("TraceModuleFeature");

FModuleService::FModuleService()
{

}

void FModuleService::Initialize()
{
	if (bIsInitialized)
	{
		return;
	}
	TArray<IModule*> Modules = IModularFeatures::Get().GetModularFeatureImplementations<IModule>(ModuleFeatureName);
	for (IModule* Module : Modules)
	{
		FModuleInfo ModuleInfo;
		Module->GetModuleInfo(ModuleInfo);
		ModulesMap.Add(ModuleInfo.Name, Module);
	}

	bIsInitialized = true;
}

void FModuleService::GetAvailableModules(TArray<FModuleInfo>& OutModules)
{
	FScopeLock Lock(&CriticalSection);
	Initialize();
	OutModules.Empty(ModulesMap.Num());
	for (const auto& KV : ModulesMap)
	{
		IModule* Module = KV.Value;
		FModuleInfo& ModuleInfo = OutModules.AddDefaulted_GetRef();
		Module->GetModuleInfo(ModuleInfo);
	}
}
	
void FModuleService::SetModuleEnabled(const FName& ModuleName, bool bEnabled)
{
	FScopeLock Lock(&CriticalSection);
	Initialize();
	IModule** FindIt = ModulesMap.Find(ModuleName);
	if (!FindIt)
	{
		return;
	}
	bool bWasEnabled = !!EnabledModules.Find(*FindIt);
	if (bEnabled == bWasEnabled)
	{
		return;
	}
	if (bEnabled)
	{
		EnabledModules.Add(*FindIt);
	}
	else
	{
		EnabledModules.Remove(*FindIt);
	}
}

void FModuleService::OnAnalysisBegin(IAnalysisSession& Session, TArray<IAnalyzer*>& OutAnalyzers)
{
	FScopeLock Lock(&CriticalSection);
	Initialize();
	for (const auto& KV : ModulesMap)
	{
		IModule* Module = KV.Value;
		Module->OnAnalysisBegin(Session, EnabledModules.Contains(Module), OutAnalyzers);
	}
}

bool FModuleService::GetModuleLoggers(const FName& ModuleName, TArray<const TCHAR *>& OutLoggers)
{
	FScopeLock Lock(&CriticalSection);
	Initialize();
	IModule** FindIt = ModulesMap.Find(ModuleName);
	if (!FindIt)
	{
		return false;
	}
	(*FindIt)->GetLoggers(OutLoggers);
	return true;
}

}
