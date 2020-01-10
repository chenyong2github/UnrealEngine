// Copyright Epic Games, Inc. All Rights Reserved.

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
		TArray<const TCHAR*> ModuleLoggers;
		Module->GetLoggers(ModuleLoggers);
		if (ModuleLoggers.Num())
		{
			FModuleInfo& ModuleInfo = OutModules.AddDefaulted_GetRef();
			Module->GetModuleInfo(ModuleInfo);
		}
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

void FModuleService::OnAnalysisBegin(IAnalysisSession& Session)
{
	FScopeLock Lock(&CriticalSection);
	Initialize();
	for (const auto& KV : ModulesMap)
	{
		IModule* Module = KV.Value;
		Module->OnAnalysisBegin(Session);
	}
}

TArray<const TCHAR*> FModuleService::GetModuleLoggers(const FName& ModuleName)
{
	TArray<const TCHAR*> Loggers;

	FScopeLock Lock(&CriticalSection);
	Initialize();
	IModule* FindIt = ModulesMap.FindRef(ModuleName);
	if (FindIt)
	{
		FindIt->GetLoggers(Loggers);
	}
	return Loggers;
}

TSet<FName> FModuleService::GetEnabledModulesFromCommandLine(const TCHAR* CommandLine)
{
	TSet<FName> EnabledModulesFromCommandLine;

	if (!CommandLine)
	{
		return EnabledModulesFromCommandLine;
	}

	FScopeLock Lock(&CriticalSection);
	Initialize();
	for (const auto& KV : ModulesMap)
	{
		IModule* Module = KV.Value;
		const TCHAR* ModuleCommandLineArgument = Module->GetCommandLineArgument();
		if (ModuleCommandLineArgument && FParse::Param(CommandLine, ModuleCommandLineArgument))
		{
			EnabledModulesFromCommandLine.Add(KV.Key);
		}
	}
	return EnabledModulesFromCommandLine;
}

void FModuleService::GenerateReports(const IAnalysisSession& Session, const TCHAR* CmdLine, const TCHAR* OutputDirectory)
{
	FScopeLock Lock(&CriticalSection);
	Initialize();
	for (const auto& KV : ModulesMap)
	{
		IModule* Module = KV.Value;
		Module->GenerateReports(Session, CmdLine, OutputDirectory);
	}
}

}
