// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraceServices/ModuleService.h"

class IAnimationProvider;
namespace Insights { class ITimingViewSession; }

namespace UE
{
namespace SlateInsights
{

class FSlateTraceModule : public Trace::IModule
{
public:
	//~ Begin Trace::IModule interface
	virtual void GetModuleInfo(Trace::FModuleInfo& OutModuleInfo) override;
	virtual void OnAnalysisBegin(Trace::IAnalysisSession& Session) override;
	virtual void GetLoggers(TArray<const TCHAR *>& OutLoggers) override;
	virtual void GenerateReports(const Trace::IAnalysisSession& Session, const TCHAR* CmdLine, const TCHAR* OutputDirectory) override;
	virtual const TCHAR* GetCommandLineArgument() override { return TEXT("slatetrace"); }
	//~ End Trace::IModule interface

private:
	static FName ModuleName;
};

} //namespace SlateInsights
} //namespace UE
