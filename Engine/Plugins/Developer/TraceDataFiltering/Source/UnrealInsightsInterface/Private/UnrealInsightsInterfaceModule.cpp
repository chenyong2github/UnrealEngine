// Copyright Epic Games, Inc. All Rights Reserved.

#include "UnrealInsightsInterfaceModule.h"

#include "EditorStyleSet.h"
#include "Logging/MessageLog.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#include "UnrealInsightsLauncher.h"

DEFINE_LOG_CATEGORY(UnrealInsightsInterface);

IMPLEMENT_MODULE(FUnrealInsightsInterfaceModule, UnrealInsightsInterface);

void FUnrealInsightsInterfaceModule::StartupModule()
{
	Launcher = MakeShared<FUnrealInsightsLauncher>();
	RegisterStartupCallbackHandle = UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateSP(Launcher.Get(), &FUnrealInsightsLauncher::RegisterMenus));
}

void FUnrealInsightsInterfaceModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(RegisterStartupCallbackHandle);
}
