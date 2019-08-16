// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/ModuleService.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Misc/ScopeLock.h"

namespace Trace
{

class IAnalysisSession;

class FModuleService
	: public IModuleService
{
public:
	FModuleService();
	virtual void GetAvailableModules(TArray<FModuleInfo>& OutModules) override;
	virtual void SetModuleEnabled(const FName& ModuleName, bool bEnabled) override;
	void OnAnalysisBegin(IAnalysisSession& Session, TArray<IAnalyzer*>& OutAnalyzers);
	bool GetModuleLoggers(const FName& ModuleName, TArray<const TCHAR*>& OutLoggers);

private:
	void Initialize();

	mutable FCriticalSection CriticalSection;
	bool bIsInitialized = false;
	TSet<IModule*> EnabledModules;
	TMap<FName, IModule*> ModulesMap;
};

}
