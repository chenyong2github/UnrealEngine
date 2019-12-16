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

namespace
{
	/** Pointer to dynamically loaded crash diagnosis module */
	FCrashDebugHelperModule* CrashHelperModule;
}

void FAndroidErrorReport::Init()
{
	CrashHelperModule = &FModuleManager::LoadModuleChecked<FCrashDebugHelperModule>(FName("CrashDebugHelper"));
}

void FAndroidErrorReport::ShutDown()
{
	CrashHelperModule->ShutdownModule();
}

#undef LOCTEXT_NAMESPACE
