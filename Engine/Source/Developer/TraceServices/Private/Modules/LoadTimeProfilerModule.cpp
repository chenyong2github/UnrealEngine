// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LoadTimeProfilerModule.h"
#include "Analyzers/PlatformFileTraceAnalysis.h"
#include "Analyzers/LoadTimeTraceAnalysis.h"
#include "AnalysisServicePrivate.h"
#include "Model/FileActivity.h"

namespace Trace
{

static const FName LoadTimeProfilerModuleName("TraceModule_LoadTimeProfiler");
static const FName LoadTimeProfilerProviderName("LoadTimeProfiler");
static const FName FileActivityProviderName("FileActivity");

void FLoadTimeProfilerModule::GetModuleInfo(FModuleInfo& OutModuleInfo)
{
	OutModuleInfo.Name = LoadTimeProfilerModuleName;
	OutModuleInfo.DisplayName = TEXT("Asset Loading");
}

void FLoadTimeProfilerModule::OnAnalysisBegin(IAnalysisSession& Session)
{
	FLoadTimeProfilerProvider* LoadTimeProfilerProvider = new FLoadTimeProfilerProvider(Session);
	Session.AddProvider(LoadTimeProfilerProviderName, LoadTimeProfilerProvider);
	Session.AddAnalyzer(new FAsyncLoadingTraceAnalyzer(Session, *LoadTimeProfilerProvider));
	FFileActivityProvider* FileActivityProvider = new FFileActivityProvider(Session);
	Session.AddProvider(FileActivityProviderName, FileActivityProvider);
	Session.AddAnalyzer(new FPlatformFileTraceAnalyzer(Session, *FileActivityProvider));
}

void FLoadTimeProfilerModule::GetLoggers(TArray<const TCHAR *>& OutLoggers)
{
	OutLoggers.Add(TEXT("LoadTime"));
	OutLoggers.Add(TEXT("PlatformFile"));
}

const ILoadTimeProfilerProvider* ReadLoadTimeProfilerProvider(const IAnalysisSession& Session)
{
	return Session.ReadProvider<ILoadTimeProfilerProvider>(LoadTimeProfilerProviderName);
}

const IFileActivityProvider* ReadFileActivityProvider(const IAnalysisSession& Session)
{
	return Session.ReadProvider<IFileActivityProvider>(FileActivityProviderName);
}

}
