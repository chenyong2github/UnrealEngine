// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TraceServices/ModuleService.h"
#include "UObject/NameTypes.h"

namespace Trace
{

class FStatsModule
	: public IModule
{
public:
	virtual void GetModuleInfo(FModuleInfo& OutModuleInfo) override;
	virtual void OnAnalysisBegin(IAnalysisSession& Session) override;
	virtual void GetLoggers(TArray<const TCHAR *>& OutLoggers) override;
	virtual void GenerateReports(const IAnalysisSession& Session, const TCHAR* CmdLine, const TCHAR* OutputDirectory) override {}
};

}
