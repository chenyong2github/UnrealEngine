// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/ModuleService.h"

namespace Trace
{

class FLoadTimeProfilerModule
	: public IModule
{
public:
	virtual void GetModuleInfo(FModuleInfo& OutModuleInfo) override;
	virtual void OnAnalysisBegin(IAnalysisSession& Session, bool bIsEnabled, TArray<IAnalyzer*>& OutAnalyzers) override;
	virtual void GetLoggers(TArray<const TCHAR *>& OutLoggers) override;

private:
	static FName ModuleName;
};

}
