// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraceServices/ModuleService.h"

class IAnimationProvider;
namespace Insights { class ITimingViewSession; }

class FGameplayTraceModule : public Trace::IModule
{
public:
	// Trace::IModule interface
	virtual void GetModuleInfo(Trace::FModuleInfo& OutModuleInfo) override;
	virtual void OnAnalysisBegin(Trace::IAnalysisSession& Session) override;
	virtual void GetLoggers(TArray<const TCHAR *>& OutLoggers) override;
	virtual void GenerateReports(const Trace::IAnalysisSession& Session, const TCHAR* CmdLine, const TCHAR* OutputDirectory) override;
	virtual const TCHAR* GetCommandLineArgument() override { return TEXT("objecttrace"); }

private:
	static FName ModuleName;
};

const IAnimationProvider* ReadAnimationProvider(const Trace::IAnalysisSession& Session);