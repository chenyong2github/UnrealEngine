// Copyright Epic Games, Inc. All Rights Reserved.

#include "Android/AndroidErrorReport.h"
#include "../CrashReportUtil.h"
#include "CrashReportCoreConfig.h"
#include "Modules/ModuleManager.h"
#include "CrashDebugHelperModule.h"
#include "CrashDebugHelper.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFilemanager.h"

#define LOCTEXT_NAMESPACE "CrashReport"

namespace AndroidErrorReport
{
	/** Pointer to dynamically loaded crash diagnosis module */
	FCrashDebugHelperModule* CrashHelperModule;
}

void FAndroidErrorReport::Init()
{
	AndroidErrorReport::CrashHelperModule = &FModuleManager::LoadModuleChecked<FCrashDebugHelperModule>(FName("CrashDebugHelper"));
}

void FAndroidErrorReport::ShutDown()
{
	AndroidErrorReport::CrashHelperModule->ShutdownModule();
}

 FAndroidErrorReport::FAndroidErrorReport(const FString& Directory)
	: FGenericErrorReport(Directory)
{
	// Check for allthreads.txt, if it exists rename it.
	// This way if anything goes wrong during processing it will not continue to cause issues on subsequent runs.

	const FString StartingThreadContextsFileName(TEXT("AllThreads.txt"));
	const FString StartingThreadContextsFilePath(ReportDirectory / StartingThreadContextsFileName);
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	bool bHasFoundThreadsFile = PlatformFile.FileExists(*StartingThreadContextsFilePath);
	if (bHasFoundThreadsFile)
	{
		FString ThreadContextsFileName(TEXT("AllThreads.tmp"));
		ThreadContextsPathName = ReportDirectory / ThreadContextsFileName;
		PlatformFile.MoveFile(*ThreadContextsPathName, *StartingThreadContextsFilePath);

		// mirror the renaming in ReportFilenames.
		ReportFilenames.Add(ThreadContextsFileName);
		ReportFilenames.RemoveSingle(StartingThreadContextsFileName);
	}
}

static void AddThreadContexts(const FString& ThreadContextsPathName)
{
	// Try to load the callstacks file.
	if (!ThreadContextsPathName.IsEmpty())
	{
		FXmlFile ThreadsNode(ThreadContextsPathName);
		if (ThreadsNode.IsValid())
		{			
			FPrimaryCrashProperties::Get()->Threads = ThreadsNode.GetRootNode();
			// delete the file as it has been added to the primary report.
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			PlatformFile.DeleteFile(*ThreadContextsPathName);
		}
	}
}

FText FAndroidErrorReport::DiagnoseReport() const
{
	AddThreadContexts(ThreadContextsPathName);
	return FText();
}

#undef LOCTEXT_NAMESPACE
