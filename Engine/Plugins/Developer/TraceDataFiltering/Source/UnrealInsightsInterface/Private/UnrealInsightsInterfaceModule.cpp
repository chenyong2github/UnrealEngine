// Copyright Epic Games, Inc. All Rights Reserved.

#include "UnrealInsightsInterfaceModule.h"

#include "HAL/LowLevelMemTracker.h"
#include "Logging/MessageLog.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#include "UnrealInsightsLauncher.h"

DEFINE_LOG_CATEGORY(UnrealInsightsInterface);

IMPLEMENT_MODULE(FUnrealInsightsInterfaceModule, UnrealInsightsInterface);

void FUnrealInsightsInterfaceModule::StartupModule()
{
	LLM_SCOPE_BYNAME(TEXT("Insights"));
	Launcher = MakeShared<FUnrealInsightsLauncher>();
	RegisterStartupCallbackHandle = UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateSP(Launcher.Get(), &FUnrealInsightsLauncher::RegisterMenus));
}

void FUnrealInsightsInterfaceModule::ShutdownModule()
{
	LLM_SCOPE_BYNAME(TEXT("Insights"));
	UToolMenus::UnRegisterStartupCallback(RegisterStartupCallbackHandle);
}
