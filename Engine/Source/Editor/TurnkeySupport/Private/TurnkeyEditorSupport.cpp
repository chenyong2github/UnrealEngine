// Copyright Epic Games, Inc. All Rights Reserved.

#include "TurnkeyEditorSupport.h"
#include "Misc/AssertionMacros.h"

#if WITH_EDITOR
#include "UnrealEdMisc.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "PlatformInfo.h"
#include "InstalledPlatformInfo.h"
#include "Misc/MessageDialog.h"
#include "IUATHelperModule.h"
#include "Interfaces/IProjectTargetPlatformEditorModule.h"
#include "Dialogs/Dialogs.h"
#include "Async/Async.h"
#include "GameProjectGenerationModule.h"
#include "ISettingsModule.h"
#include "ISettingsEditorModule.h"
#include "Settings/EditorExperimentalSettings.h"

#endif

#define LOCTEXT_NAMESPACE "FTurnkeyEditorSupport"

FString FTurnkeyEditorSupport::GetUATOptions()
{
#if WITH_EDITOR
	FString Options;
	Options += FString::Printf(TEXT(" -ue4exe=%s"), *FUnrealEdMisc::Get().GetExecutableForCommandlets());

	int32 NumCookers = GetDefault<UEditorExperimentalSettings>()->MultiProcessCooking;
	if (NumCookers > 0)
	{
		Options += FString::Printf(TEXT(" -NumCookersToSpawn=%d"), NumCookers);
	}
	return Options;
#else
	return TEXT("");
#endif
}


void FTurnkeyEditorSupport::AddEditorOptions(FMenuBuilder& MenuBuilder)
{
#if WITH_EDITOR
	MenuBuilder.AddSeparator();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("OpenPackagingSettings", "Packaging Settings..."),
		LOCTEXT("OpenPackagingSettings_ToolTip", "Opens the settings for project packaging."),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "DeviceDetails.TabIcon"),
		FUIAction(FExecuteAction::CreateLambda([] { FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "Project", "Packaging"); }))
	);

	FModuleManager::LoadModuleChecked<IProjectTargetPlatformEditorModule>("ProjectTargetPlatformEditor").AddOpenProjectTargetPlatformEditorMenuItem(MenuBuilder);
#endif
}

void FTurnkeyEditorSupport::PrepareToLaunchRunningMap(const FString& DeviceId, const FString& DeviceName)
{
#if WITH_EDITOR
	ULevelEditorPlaySettings* PlaySettings = GetMutableDefault<ULevelEditorPlaySettings>();

	PlaySettings->LastExecutedLaunchModeType = LaunchMode_OnDevice;
	PlaySettings->LastExecutedLaunchDevice = DeviceId;
	PlaySettings->LastExecutedLaunchName = DeviceName;

	PlaySettings->PostEditChange();

	PlaySettings->SaveConfig();
#endif
}

void FTurnkeyEditorSupport::LaunchRunningMap(const FString& DeviceId, const FString& DeviceName, bool bUseTurnkey)
{
#if WITH_EDITOR
	FTargetDeviceId TargetDeviceId;
	if (FTargetDeviceId::Parse(DeviceId, TargetDeviceId))
	{
		const PlatformInfo::FTargetPlatformInfo* const PlatformInfo = PlatformInfo::FindPlatformInfo(*TargetDeviceId.GetPlatformName());
		FString UBTPlatformName = PlatformInfo->DataDrivenPlatformInfo->UBTPlatformString;
		FString IniPlatformName = PlatformInfo->IniPlatformName.ToString();

		check(PlatformInfo);

		if (FInstalledPlatformInfo::Get().IsPlatformMissingRequiredFile(UBTPlatformName))
		{
			if (!FInstalledPlatformInfo::OpenInstallerOptions())
			{
				FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("MissingPlatformFilesLaunch", "Missing required files to launch on this platform."));
			}
			return;
		}

		if (FModuleManager::LoadModuleChecked<IProjectTargetPlatformEditorModule>("ProjectTargetPlatformEditor").ShowUnsupportedTargetWarning(*TargetDeviceId.GetPlatformName()))
		{
			GUnrealEd->CancelPlayingViaLauncher();

			FRequestPlaySessionParams::FLauncherDeviceInfo DeviceInfo;
			DeviceInfo.DeviceId = DeviceId;
			DeviceInfo.DeviceName = DeviceName;
			// @todo turnkey: we set this to false because we will kick off a Turnkey run before cooking, etc, to get an early warning. however, if it's too difficult
			// to get an error back from CreateUatTask, then we should set this to bUseTurnkey and remove the block below, and let the code in FLauncherWorker::CreateAndExecuteTasks handle it
			DeviceInfo.bUpdateDeviceFlash = false;

			FRequestPlaySessionParams SessionParams;
			SessionParams.SessionDestination = EPlaySessionDestinationType::Launcher;
			SessionParams.LauncherTargetDevice = DeviceInfo;

			// if we want to check device flash before we start cooking, kick it off now. we could delay this 
			if (bUseTurnkey)
			{
				FString CommandLine = FString::Printf(TEXT("Turnkey -command=VerifySdk -UpdateIfNeeded -platform=%s -EditorIO -noturnkeyvariables -device=%s -utf8output -WaitForUATMutex"), *UBTPlatformName, *TargetDeviceId.GetDeviceName());
				FText TaskName = LOCTEXT("VerifyingSDK", "Verifying SDK and Device");

				IUATHelperModule::Get().CreateUatTask(CommandLine, FText::FromString(IniPlatformName), TaskName, TaskName, FEditorStyle::GetBrush(TEXT("MainFrame.PackageProject")),
					[SessionParams](FString Result, double)
					{
						// unfortunate string comparison for success
						bool bWasSuccessful = Result == TEXT("Completed");
						AsyncTask(ENamedThreads::GameThread, [SessionParams, bWasSuccessful]()
							{
								if (bWasSuccessful)
								{
									GUnrealEd->RequestPlaySession(SessionParams);
								}
								else
								{
									TSharedRef<SWindow> Win = OpenMsgDlgInt_NonModal(EAppMsgType::YesNo, LOCTEXT("SDKCheckFailed", "SDK Verification failed. Would you like to attempt the Launch On anyway?"), LOCTEXT("SDKCheckFailedTitle", "SDK Verification"),
										FOnMsgDlgResult::CreateLambda([SessionParams](const TSharedRef<SWindow>&, EAppReturnType::Type Choice)
											{
												if (Choice == EAppReturnType::Yes)
												{
													GUnrealEd->RequestPlaySession(SessionParams);
												}
											}));
									Win->ShowWindow();
								}
							});
					});
			}
			else
			{
				GUnrealEd->RequestPlaySession(SessionParams);
			}
		}
	}
#endif
}

bool FTurnkeyEditorSupport::DoesProjectHaveCode()
{
#if WITH_EDITOR
	FGameProjectGenerationModule& GameProjectModule = FModuleManager::LoadModuleChecked<FGameProjectGenerationModule>(TEXT("GameProjectGeneration"));
	return GameProjectModule.Get().ProjectHasCodeFiles();
#else
	unimplemented();
	return false;
#endif
}

void FTurnkeyEditorSupport::RunUAT(const FString& CommandLine, const FText& PlatformDisplayName, const FText& TaskName, const FText& TaskShortName, const FSlateBrush* TaskIcon, TFunction<void(FString, double)> ResultCallback)
{
#if WITH_EDITOR
	IUATHelperModule::Get().CreateUatTask(CommandLine, PlatformDisplayName, TaskName, TaskShortName, TaskIcon, ResultCallback);
#else
	unimplemented();
#endif
}




bool FTurnkeyEditorSupport::ShowOKCancelDialog(FText Message, FText Title)
{
#if WITH_EDITOR
	FSuppressableWarningDialog::FSetupInfo Info(Message, Title, TEXT("TurkeyEditorDialog"));

	Info.ConfirmText = LOCTEXT("TurnkeyDialog_Confirm", "Continue");
	Info.CancelText = LOCTEXT("TurnkeyDialogK_Cancel", "Cancel");
	FSuppressableWarningDialog Dialog(Info);

	return Dialog.ShowModal() != FSuppressableWarningDialog::EResult::Cancel;
#else
	unimplemented();
	return false;
#endif
}

void FTurnkeyEditorSupport::ShowRestartToast()
{
#if WITH_EDITOR
	// show restart dialog
	FModuleManager::GetModuleChecked<ISettingsEditorModule>("SettingsEditor").OnApplicationRestartRequired();
#endif
}

bool FTurnkeyEditorSupport::CheckSupportedPlatforms(FName IniPlatformName)
{
#if WITH_EDITOR
	return FModuleManager::LoadModuleChecked<IProjectTargetPlatformEditorModule>("ProjectTargetPlatformEditor").ShowUnsupportedTargetWarning(IniPlatformName);
#endif

	return true;
}


#undef LOCTEXT_NAMESPACE
