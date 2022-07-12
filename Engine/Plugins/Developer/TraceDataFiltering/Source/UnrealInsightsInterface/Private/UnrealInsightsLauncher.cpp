// Copyright Epic Games, Inc. All Rights Reserved.

#include "UnrealInsightsLauncher.h"

#include "Styling/AppStyle.h"
#include "IUATHelperModule.h"
#include "Logging/LogMacros.h"
#include "Logging/MessageLog.h"
#include "MessageLog/Public/MessageLogModule.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

#include "UnrealInsightsInterfaceModule.h"

#define LOCTEXT_NAMESPACE "FUnrealInsightsLauncher"

FUnrealInsightsLauncher::FUnrealInsightsLauncher()
	: LogListingName(TEXT("UnrealInsights"))
{

}

FUnrealInsightsLauncher::~FUnrealInsightsLauncher()
{

}
	
void FUnrealInsightsLauncher::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* ProfileMenu = UToolMenus::Get()->ExtendMenu("MainFrame.MainMenu.Tools");
	if (ProfileMenu)
	{
		FToolMenuSection& Section = ProfileMenu->AddSection("Unreal Insights", FText::FromString(TEXT("Unreal Insights")));
		Section.AddMenuEntry("OpenUnrealInsights",
			LOCTEXT("OpenUnrealInsights_Label", "Run Unreal Insights"),
			LOCTEXT("OpenUnrealInsights_Desc", "Run the Unreal Insights standalone application."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "UnrealInsights.MenuIcon"),
			FUIAction(FExecuteAction::CreateRaw(this, &FUnrealInsightsLauncher::RunUnrealInsights_Execute), FCanExecuteAction())
		);
	}
}

void FUnrealInsightsLauncher::RunUnrealInsights_Execute()
{
	FString Path = FPlatformProcess::GenerateApplicationPath(TEXT("UnrealInsights"), EBuildConfiguration::Development);
	Path = FPaths::ConvertRelativePathToFull(Path);
	
	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	if (!MessageLogModule.IsRegisteredLogListing(LogListingName))
	{
		MessageLogModule.RegisterLogListing(LogListingName, LOCTEXT("UnrealInsights", "Unreal Insights"));
	}

	if (!FPaths::FileExists(Path))
	{
#if !PLATFORM_MAC
		UE_LOG(UnrealInsightsInterface, Log, TEXT("Could not find the Unreal Insights executable: %s. Attempting to build UnrealInsights."), *Path);

		FString Arguments;
#if PLATFORM_WINDOWS
		FText PlatformName = LOCTEXT("PlatformName_Windows", "Windows");
		Arguments = TEXT("BuildTarget -Target=UnrealInsights -Platform=Win64");
#elif PLATFORM_MAC
		FText PlatformName = LOCTEXT("PlatformName_Mac", "Mac");
		Arguments = TEXT("BuildTarget -Target=UnrealInsights -Platform=Mac");
#elif PLATFORM_LINUX
		FText PlatformName = LOCTEXT("PlatformName_Linux", "Linux");
		Arguments = TEXT("BuildTarget -Target=UnrealInsights -Platform=Linux");
#endif

		IUATHelperModule::Get().CreateUatTask(Arguments, PlatformName, LOCTEXT("BuildingUnrealInsights", "Building Unreal Insights"),
			LOCTEXT("BuildUnrealInsightsTask", "Build Unreal Insights Task"), FAppStyle::GetBrush(TEXT("MainFrame.CookContent")), [this, Path](FString Result, double Time)
			{
				if (Result.Equals(TEXT("Completed")))
				{
					this->StartUnrealInsights(Path);
				}
			});
#else
		const FText	MessageBoxTextFmt = LOCTEXT("ExecutableNotFoundManualBuild_TextFmt", "Could not find Unreal Insights executable. Have you built Unreal Insights?");
		const FText MessageBoxText = FText::Format(MessageBoxTextFmt, FText::FromString(Path));

		FMessageLog ReportMessageLog(LogListingName);
		TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(EMessageSeverity::Error, MessageBoxText);
		ReportMessageLog.AddMessage(Message);
		ReportMessageLog.Notify();
#endif
	}
	else
	{
		StartUnrealInsights(Path);
	}
}

void FUnrealInsightsLauncher::StartUnrealInsights(const FString& Path)
{
	FString CmdLine;

	constexpr bool bLaunchDetached = true;
	constexpr bool bLaunchHidden = false;
	constexpr bool bLaunchReallyHidden = false;

	uint32 ProcessID = 0;
	const int32 PriorityModifier = 0;
	const TCHAR* OptionalWorkingDirectory = nullptr;

	void* PipeWriteChild = nullptr;
	void* PipeReadChild = nullptr;
	FProcHandle Handle = FPlatformProcess::CreateProc(*Path, *CmdLine, bLaunchDetached, bLaunchHidden, bLaunchReallyHidden, &ProcessID, PriorityModifier, OptionalWorkingDirectory, PipeWriteChild, PipeReadChild);

	if (Handle.IsValid())
	{
		UE_LOG(UnrealInsightsInterface, Log, TEXT("Launched Unreal Insights executable: %s"), *Path);
	}
	else
	{
		const FText	MessageBoxTextFmt = LOCTEXT("ExecutableNotFound_TextFmt", "Could not start Unreal Insights executable at path: {0}");
		const FText MessageBoxText = FText::Format(MessageBoxTextFmt, FText::FromString(Path));

		FMessageLog ReportMessageLog(LogListingName);
		TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(EMessageSeverity::Error, MessageBoxText);
		ReportMessageLog.AddMessage(Message);
		ReportMessageLog.Notify();
	}
}

#undef LOCTEXT_NAMESPACE