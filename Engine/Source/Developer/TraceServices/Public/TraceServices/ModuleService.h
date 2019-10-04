// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeature.h"

class FName;

namespace Trace
{

class IAnalyzer;
class IAnalysisSession;
extern const FName ModuleFeatureName;

struct FModuleInfo
{
	FName Name;
	const TCHAR* DisplayName = nullptr;
};

class IModule
	: public IModularFeature
{
public:
	virtual void GetModuleInfo(FModuleInfo& OutModuleInfo) = 0;
	virtual void OnAnalysisBegin(IAnalysisSession& Session) = 0;
	virtual void GetLoggers(TArray<const TCHAR*>& OutLoggers) = 0;
	virtual void GenerateReports(const IAnalysisSession& Session, const TCHAR* CmdLine, const TCHAR* OutputDirectory) = 0;
};

class IModuleService
{
public:
	virtual ~IModuleService() = default;
	virtual void GetAvailableModules(TArray<FModuleInfo>& OutModules) = 0;
	virtual void SetModuleEnabled(const FName& ModuleName, bool bEnabled) = 0;
	virtual void GenerateReports(const IAnalysisSession& Session, const TCHAR* CmdLine, const TCHAR* OutputDirectory) = 0;
};

}
