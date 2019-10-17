// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LoadTimeProfilerModule.h"
#include "Analyzers/PlatformFileTraceAnalysis.h"
#include "Analyzers/LoadTimeTraceAnalysis.h"
#include "AnalysisServicePrivate.h"
#include "Model/FileActivity.h"
#include "HAL/FileManager.h"

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

void FLoadTimeProfilerModule::GenerateReports(const IAnalysisSession& Session, const TCHAR* CmdLine, const TCHAR* OutputDirectory)
{
	double CaptureStartTime = -DBL_MAX;
	double CaptureEndTime = DBL_MAX;
	const IBookmarkProvider& BookmarkProvider = Trace::ReadBookmarkProvider(Session);
	FString BeginCaptureBookmarkName;
	FParse::Value(CmdLine, TEXT("-BeginCaptureBookmark="), BeginCaptureBookmarkName);
	FString EndCaptureBookmarkName;
	FParse::Value(CmdLine, TEXT("-EndCaptureBookmark="), EndCaptureBookmarkName);
	BookmarkProvider.EnumerateBookmarks(0.0, DBL_MAX, [BeginCaptureBookmarkName, EndCaptureBookmarkName, &CaptureStartTime, &CaptureEndTime](const FBookmark& Bookmark)
	{
		if (CaptureStartTime == -DBL_MAX && !BeginCaptureBookmarkName.IsEmpty() && BeginCaptureBookmarkName == Bookmark.Text)
		{
			CaptureStartTime = Bookmark.Time;
		}
		if (CaptureEndTime == DBL_MAX && !EndCaptureBookmarkName.IsEmpty() && EndCaptureBookmarkName == Bookmark.Text)
		{
			CaptureEndTime = Bookmark.Time;
		}
	});
	if (CaptureStartTime == -DBL_MAX)
	{
		CaptureStartTime = 0.0;
	}
	if (CaptureEndTime == DBL_MAX)
	{
		CaptureEndTime = Session.GetDurationSeconds();
	}
	const ILoadTimeProfilerProvider* LoadTimeProfiler = Trace::ReadLoadTimeProfilerProvider(Session);
	FString ReportDirectory = FString(OutputDirectory) / TEXT("LoadTimeProfiler");
	if (LoadTimeProfiler)
	{
		TUniquePtr<ITable<FPackagesTableRow>> PackagesTable(LoadTimeProfiler->CreatePackageDetailsTable(CaptureStartTime, CaptureEndTime));
		Table2Csv(*PackagesTable.Get(), *(ReportDirectory / TEXT("Packages.csv")));
		TUniquePtr<ITable<FExportsTableRow>> ExportsTable(LoadTimeProfiler->CreateExportDetailsTable(CaptureStartTime, CaptureEndTime));
		Table2Csv(*ExportsTable.Get(), *(ReportDirectory / TEXT("Exports.csv")));
		Table2Csv(LoadTimeProfiler->GetRequestsTable(), *(ReportDirectory / TEXT("Requests.csv")));
	}
	const IFileActivityProvider* FileActivityProvider = Trace::ReadFileActivityProvider(Session);
	if (FileActivityProvider)
	{
		Table2Csv(FileActivityProvider->GetFileActivityTable(), *(FString(ReportDirectory) / TEXT("FileActivity.csv")));
	}
	if (CaptureStartTime > 0.0 || CaptureEndTime < Session.GetDurationSeconds())
	{
		TSharedPtr<FArchive> CaptureSummaryOutputFile = MakeShareable(IFileManager::Get().CreateFileWriter(*(ReportDirectory / TEXT("CaptureSummary.txt"))));
		FString SummaryLine = FString::Printf(TEXT("Capture start: %f\r\nCapture end: %f\r\nCapture duration: %f"), CaptureStartTime, CaptureEndTime, CaptureEndTime - CaptureStartTime);
		CaptureSummaryOutputFile->Serialize(TCHAR_TO_ANSI(*SummaryLine), SummaryLine.Len());
	}
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
