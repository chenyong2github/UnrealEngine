// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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

static void AddThreadContexts(const FString& ReportDirectory)
{
	// Try to load the callstacks file.
	const FString ThreadContextsFile(ReportDirectory / TEXT("AllThreads.txt"));
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	if (PlatformFile.FileExists(*ThreadContextsFile))
	{
		// rename the threads file, if anything goes wrong during processing it will be skipped on the next run.
		const FString ThreadContextsFileTemp(ReportDirectory / TEXT("AllThreads.tmp"));
		PlatformFile.MoveFile(*ThreadContextsFileTemp, *ThreadContextsFile);

		FXmlFile ThreadsNode(ThreadContextsFileTemp);
		if (ThreadsNode.IsValid())
		{			
			FPrimaryCrashProperties::Get()->Threads = ThreadsNode.GetRootNode();
			// delete the file as it has been added to the primary report.
			PlatformFile.DeleteFile(*ThreadContextsFileTemp);
		}
	}
}

FText FAndroidErrorReport::DiagnoseReport() const
{
	AddThreadContexts(ReportDirectory);
	return FText();
}

#undef LOCTEXT_NAMESPACE
