// Copyright Epic Games, Inc. All Rights Reserved.

#include "UnrealInsightsLauncher.h"

#include "EditorStyleSet.h"
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
		FToolMenuSection& Section = ProfileMenu->AddSection("Unreal Insights", FText::FromString(TEXT("Unreal Unsights")));
		Section.AddMenuEntry("OpenUnrealInsights",
			LOCTEXT("OpenUnrealInsights_Label", "Run Unreal Insights"),
			LOCTEXT("OpenUnrealInsights_Desc", "Run the Unreal Insights standalone application."),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "UnrealInsights.MenuIcon"),
			FUIAction(FExecuteAction::CreateRaw(this, &FUnrealInsightsLauncher::OpenUnrealInsights), FCanExecuteAction())
		);
	}
}

void FUnrealInsightsLauncher::OpenUnrealInsights()
{
	FString Path = FPaths::GetPath(FPlatformProcess::ExecutablePath());
	Path = FPaths::Combine(Path, TEXT("UnrealInsights"));
#if PLATFORM_WINDOWS
	Path = FPaths::SetExtension(Path, TEXT(".exe"));
#endif
	FPaths::MakeStandardFilename(Path);

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
		FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
		if (!MessageLogModule.IsRegisteredLogListing(LogListingName))
		{
			MessageLogModule.RegisterLogListing(LogListingName, LOCTEXT("UnrealInsights", "Unreal Insights"));
		}

		FText MessageBoxTextFmt;
		if (!FPaths::FileExists(Path))
		{
			MessageBoxTextFmt = LOCTEXT("ExecutableNotFound_TextFmt", "Unreal Insights executable could not be found at: {0}. Is Unreal Insights built?");
		}
		else
		{
			MessageBoxTextFmt = LOCTEXT("ExecutableNotFound_TextFmt", "Could not start Unreal Insights executable at path: {0}");
		}
		const FText MessageBoxText = FText::Format(MessageBoxTextFmt, FText::FromString(Path));

		FMessageLog ReportMessageLog(LogListingName);
		TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(EMessageSeverity::Error, MessageBoxText);
		ReportMessageLog.AddMessage(Message);
		ReportMessageLog.Notify();
	}
}