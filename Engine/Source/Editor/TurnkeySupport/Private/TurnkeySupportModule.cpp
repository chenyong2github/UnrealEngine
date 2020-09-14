// Copyright Epic Games, Inc. All Rights Reserved.

#include "TurnkeySupportModule.h"

#include "SlateOptMacros.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "GameProjectGenerationModule.h"
#include "MessageLogModule.h"
#include "EditorStyleSet.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Misc/MessageDialog.h"
#include "HAL/PlatformFileManager.h"
#include "UnrealEdMisc.h"
#include "ITargetDeviceServicesModule.h"
#include "Interfaces/IProjectTargetPlatformEditorModule.h"
#include "Dialogs/Dialogs.h"
#include "Misc/DataDrivenPlatformInfoRegistry.h"
#include "IUATHelperModule.h"
#include "ISettingsEditorModule.h"
#include "Async/Async.h"
#include "Misc/FileHelper.h"
#include "Interfaces/TargetDeviceId.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/IProjectManager.h"
#include "ISettingsModule.h"
#include "SourceControlHelpers.h"
#include "ISourceControlModule.h"
#include "ITargetDeviceProxy.h"
#include "ITargetDeviceServicesModule.h"
#include "Settings/ProjectPackagingSettings.h" // can go away once turnkey menu handles Package
#include "IDesktopPlatform.h"
#include "DesktopPlatformModule.h"
#include "PlatformInfo.h"
#include "InstalledPlatformInfo.h"
#include "Settings/EditorExperimentalSettings.h"
#include "CookerSettings.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"




DEFINE_LOG_CATEGORY(LogTurnkeySupport);
#define LOCTEXT_NAMESPACE "FTurnkeySupportModule"


enum class EPrepareContentMode : uint8
{
	CookOnly,
	Package,
	PrepareForDebugging,
};

class FTurnkeySupportCallbacks
{
protected:
	static const TCHAR* GetUATCompilationFlags()
	{
		// We never want to compile editor targets when invoking UAT in this context.
		// If we are installed or don't have a compiler, we must assume we have a precompiled UAT.
		return TEXT("-nocompileeditor");
	}

	static bool ShowBadSDKDialog(FName IniPlatformName)
	{
		// Don't show the warning during automation testing; the dlg is modal and blocks
		if (!GIsAutomationTesting)
		{
			FFormatNamedArguments Args;
			//		Args.Add(TEXT("DisplayName"), PlatformInfo->DisplayName);
			Args.Add(TEXT("DisplayName"), FText::FromName(IniPlatformName));
			FText WarningText = FText::Format(LOCTEXT("BadSDK_Message", "The SDK for {DisplayName} is not installed properly, which is needed to generate data. Check the SDK section of the Launch On menu in the main toolbar to update SDK.\n\nWould you like to attempt to continue anyway?"), Args);

			FSuppressableWarningDialog::FSetupInfo Info(
				WarningText,
				LOCTEXT("BadSDK_Title", "SDK Not Setup"),
				TEXT("BadSDKDialog")
			);
			Info.ConfirmText = LOCTEXT("BadSDK_Confirm", "Continue");
			Info.CancelText = LOCTEXT("BadSDK_Cancel", "Cancel");
			FSuppressableWarningDialog BadSDKgDialog(Info);

			return BadSDKgDialog.ShowModal() != FSuppressableWarningDialog::EResult::Cancel;
		}

		return true;
	}


	static bool ShouldBuildProject(UProjectPackagingSettings* PackagingSettings, const ITargetPlatform* TargetPlatform)
	{
		const UProjectPackagingSettings::FConfigurationInfo& ConfigurationInfo = UProjectPackagingSettings::ConfigurationInfo[(int)PackagingSettings->BuildConfiguration];
		bool bAssetNativizationEnabled = (PackagingSettings->BlueprintNativizationMethod != EProjectPackagingBlueprintNativizationMethod::Disabled);

		// Get the target to build
		const FTargetInfo* Target = PackagingSettings->GetBuildTargetInfo();

		// Only build if the user elects to do so
		bool bBuild = false;
		if (PackagingSettings->Build == EProjectPackagingBuild::Always)
		{
			bBuild = true;
		}
		else if (PackagingSettings->Build == EProjectPackagingBuild::Never)
		{
			bBuild = false;
		}
		else if (PackagingSettings->Build == EProjectPackagingBuild::IfProjectHasCode)
		{
			bBuild = true;
			if (FApp::GetEngineIsPromotedBuild() && !bAssetNativizationEnabled)
			{
				FString BaseDir;

				// Get the target name
				FString TargetName;
				if (Target == nullptr)
				{
					TargetName = TEXT("UE4Game");
				}
				else
				{
					TargetName = Target->Name;
				}

				// Get the directory containing the receipt for this target, depending on whether the project needs to be built or not
				FString ProjectDir = FPaths::GetPath(FPaths::GetProjectFilePath());
				if (Target != nullptr && FPaths::IsUnderDirectory(Target->Path, ProjectDir))
				{
					UE_LOG(LogTurnkeySupport, Log, TEXT("Selected target: %s"), *Target->Name);
					BaseDir = ProjectDir;
				}
				else
				{
					FText Reason;

					FGameProjectGenerationModule& GameProjectModule = FModuleManager::LoadModuleChecked<FGameProjectGenerationModule>(TEXT("GameProjectGeneration"));
					bool bProjectHasCode = GameProjectModule.Get().ProjectHasCodeFiles();

					if (TargetPlatform->RequiresTempTarget(bProjectHasCode, ConfigurationInfo.Configuration, false, Reason))
					{
						UE_LOG(LogTurnkeySupport, Log, TEXT("Project requires temp target (%s)"), *Reason.ToString());
						BaseDir = ProjectDir;
					}
					else
					{
						UE_LOG(LogTurnkeySupport, Log, TEXT("Project does not require temp target"));
						BaseDir = FPaths::EngineDir();
					}
				}

				// Check if the receipt is for a matching promoted target
				FString UBTPlatformName = TargetPlatform->GetTargetPlatformInfo().DataDrivenPlatformInfo->UBTPlatformString;

				extern LAUNCHERSERVICES_API bool HasPromotedTarget(const TCHAR * BaseDir, const TCHAR * TargetName, const TCHAR * Platform, EBuildConfiguration Configuration, const TCHAR * Architecture);
				if (HasPromotedTarget(*BaseDir, *TargetName, *UBTPlatformName, ConfigurationInfo.Configuration, nullptr))
				{
					bBuild = false;
				}
			}
		}
		else if (PackagingSettings->Build == EProjectPackagingBuild::IfEditorWasBuiltLocally)
		{
			bBuild = !FApp::GetEngineIsPromotedBuild();
		}

		return bBuild;
	}


public:

	/** Opens the Packaging settings tab */
	static void PackagingSettings()
	{
		FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "Project", "Packaging");
	}

	static void OpenProjectLauncher()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(FTabId("ProjectLauncher"));
	}

	static void OpenDeviceManager()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(FTabId("DeviceManager"));
	}

	static bool CanCookOrPackage(FName IniPlatformName, EPrepareContentMode Mode)
	{
		// prep for debugging not supported yet
		return Mode != EPrepareContentMode::PrepareForDebugging && GetTargetPlatformManager()->FindTargetPlatform(IniPlatformName.ToString()) != nullptr;
	}

	static void CookOrPackage(FName IniPlatformName, EPrepareContentMode Mode)
	{
		const PlatformInfo::FTargetPlatformInfo* PlatformInfo = PlatformInfo::FindPlatformInfo(GetDefault<UProjectPackagingSettings>()->GetTargetPlatformForPlatform(IniPlatformName));
		
		// this is unexpected to be able to happen, but it could if there was a bad value saved in the UProjectPackagingSettings - if this trips, we should handle errors
		check(PlatformInfo != nullptr);


		// get all the helper objects
		const FString UBTPlatformString = PlatformInfo->DataDrivenPlatformInfo->UBTPlatformString;
		UProjectPackagingSettings* PackagingSettings = Cast<UProjectPackagingSettings>(UProjectPackagingSettings::StaticClass()->GetDefaultObject());
		const FString ProjectPath = FPaths::IsProjectFilePathSet() ? FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()) : FPaths::RootDir() / FApp::GetProjectName() / FApp::GetProjectName() + TEXT(".uproject");


		// check that we can proceed
		{
			if (FInstalledPlatformInfo::Get().IsPlatformMissingRequiredFile(UBTPlatformString))
			{
				if (!FInstalledPlatformInfo::OpenInstallerOptions())
				{
					FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("MissingPlatformFilesCook", "Missing required files to cook for this platform."));
				}
				return;
			}

			if (!FModuleManager::LoadModuleChecked<IProjectTargetPlatformEditorModule>("ProjectTargetPlatformEditor").ShowUnsupportedTargetWarning(IniPlatformName))
			{
				return;
			}

			if (PlatformInfo->DataDrivenPlatformInfo->GetSdkStatus() != DDPIPlatformSdkStatus::Valid && ShowBadSDKDialog(IniPlatformName) == false)
			{
				return;
			}
		}


		// basic BuildCookRun params we always want
		FString BuildCookRunParams = FString::Printf(TEXT("-nop4 -utf8output %s -cook "), GetUATCompilationFlags());


		// set locations to engine and project
		{
			BuildCookRunParams += FString::Printf(TEXT(" -project=\"%s\" -ue4exe=\"%s\""), *ProjectPath, *FUnrealEdMisc::Get().GetExecutableForCommandlets());
		}

		// set the platform we are preparing content for
		{
			BuildCookRunParams += FString::Printf(TEXT(" -platform=%s"), *UBTPlatformString);
		}

		// Append any extra UAT flags specified for this platform flavor
		if (!PlatformInfo->UATCommandLine.IsEmpty())
		{
			BuildCookRunParams += FString::Printf(TEXT(" %s"), *PlatformInfo->UATCommandLine);
		}

		// optional settings
		if (PackagingSettings->bSkipEditorContent)
		{
			BuildCookRunParams += TEXT(" -SkipCookingEditorContent");
		}
		if (FApp::IsEngineInstalled())
		{
			BuildCookRunParams += TEXT(" -installed");
		}
		int32 NumCookers = GetDefault<UEditorExperimentalSettings>()->MultiProcessCooking;
		if (NumCookers > 0)
		{
			BuildCookRunParams += FString::Printf(TEXT(" -NumCookersToSpawn=%d"), NumCookers);
		}



		// per mode settings
		FText ContentPrepDescription;
		FText ContentPrepTaskName;
		const FSlateBrush* ContentPrepIcon = nullptr;
		if (Mode == EPrepareContentMode::Package)
		{
			ContentPrepDescription = LOCTEXT("PackagingProjectTaskName", "Packaging project");
			ContentPrepTaskName = LOCTEXT("PackagingTaskName", "Packaging");
			ContentPrepIcon = FEditorStyle::GetBrush(TEXT("MainFrame.PackageProject"));

			// let the user pick a target directory
			if (PackagingSettings->StagingDirectory.Path.IsEmpty())
			{
				PackagingSettings->StagingDirectory.Path = FPaths::ProjectDir();
			}

			FString OutFolderName;

			if (!FDesktopPlatformModule::Get()->OpenDirectoryDialog(FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr), LOCTEXT("PackageDirectoryDialogTitle", "Package project...").ToString(), PackagingSettings->StagingDirectory.Path, OutFolderName))
			{
				return;
			}

			PackagingSettings->StagingDirectory.Path = OutFolderName;
			PackagingSettings->SaveConfig();


			BuildCookRunParams += TEXT(" -stage -archive -package");

			const ITargetPlatform* TargetPlatform = GetTargetPlatformManager()->FindTargetPlatform(PlatformInfo->Name);
			if (ShouldBuildProject(PackagingSettings, TargetPlatform))
			{
				BuildCookRunParams += TEXT(" -build");
			}

			if (PackagingSettings->FullRebuild)
			{
				BuildCookRunParams += TEXT(" -clean");
			}

			if (PackagingSettings->bCompressed)
			{
				BuildCookRunParams += TEXT(" -compressed");
			}

			if (PackagingSettings->bUseIoStore)
			{
				BuildCookRunParams += TEXT(" -iostore");

				// Pak file(s) must be used when using container file(s)
				PackagingSettings->UsePakFile = true;
			}

			if (PackagingSettings->UsePakFile)
			{
				BuildCookRunParams += TEXT(" -pak");
			}

			if (PackagingSettings->bUseIoStore)
			{
				BuildCookRunParams += TEXT(" -iostore");
			}

			if (PackagingSettings->IncludePrerequisites)
			{
				BuildCookRunParams += TEXT(" -prereqs");
			}

			if (!PackagingSettings->ApplocalPrerequisitesDirectory.Path.IsEmpty())
			{
				BuildCookRunParams += FString::Printf(TEXT(" -applocaldirectory=\"%s\""), *(PackagingSettings->ApplocalPrerequisitesDirectory.Path));
			}
			else if (PackagingSettings->IncludeAppLocalPrerequisites)
			{
				BuildCookRunParams += TEXT(" -applocaldirectory=\"$(EngineDir)/Binaries/ThirdParty/AppLocalDependencies\"");
			}

			BuildCookRunParams += FString::Printf(TEXT(" -archivedirectory=\"%s\""), *PackagingSettings->StagingDirectory.Path);

			if (PackagingSettings->ForDistribution)
			{
				BuildCookRunParams += TEXT(" -distribution");
			}

			if (!PackagingSettings->IncludeDebugFiles)
			{
				BuildCookRunParams += TEXT(" -nodebuginfo");
			}

			if (PackagingSettings->bGenerateChunks)
			{
				BuildCookRunParams += TEXT(" -manifests");
			}

			// Whether to include the crash reporter.
			if (PackagingSettings->IncludeCrashReporter && PlatformInfo->DataDrivenPlatformInfo->bCanUseCrashReporter)
			{
				BuildCookRunParams += TEXT(" -CrashReporter");
			}

			if (PackagingSettings->bBuildHttpChunkInstallData)
			{
				BuildCookRunParams += FString::Printf(TEXT(" -manifests -createchunkinstall -chunkinstalldirectory=\"%s\" -chunkinstallversion=%s"), *(PackagingSettings->HttpChunkInstallDataDirectory.Path), *(PackagingSettings->HttpChunkInstallDataVersion));
			}

			const UProjectPackagingSettings::FConfigurationInfo& ConfigurationInfo = UProjectPackagingSettings::ConfigurationInfo[(int)PackagingSettings->GetBuildConfigurationForPlatform(IniPlatformName)];
			if (PlatformInfo->PlatformType == EBuildTargetType::Server)
			{
				BuildCookRunParams += FString::Printf(TEXT(" -serverconfig=%s"), LexToString(ConfigurationInfo.Configuration));
			}
			else
			{
				BuildCookRunParams += FString::Printf(TEXT(" -clientconfig=%s"), LexToString(ConfigurationInfo.Configuration));
			}
		}
		else if (Mode == EPrepareContentMode::CookOnly)
		{
			ContentPrepDescription = LOCTEXT("CookingContentTaskName", "Cooking content");
			ContentPrepTaskName = LOCTEXT("CookingTaskName", "Cooking");
			ContentPrepIcon = FEditorStyle::GetBrush(TEXT("MainFrame.CookContent"));


			UCookerSettings const* CookerSettings = GetDefault<UCookerSettings>();
			if (CookerSettings->bIterativeCookingForFileCookContent)
			{
				BuildCookRunParams += TEXT(" -iterate");
			}

			BuildCookRunParams += TEXT(" -skipstage");
		}


		FString TurnkeyParams = FString::Printf(TEXT(" -command=VerifySdk -platform=%s -UpdateIfNeeded -EditorIO"), *UBTPlatformString);

		FString CommandLine = FString::Printf(TEXT("-ScriptsForProject=\"%s\" Turnkey %s BuildCookRun %s"),
			*ProjectPath,
			*TurnkeyParams,
			*BuildCookRunParams
		);

		IUATHelperModule::Get().CreateUatTask(CommandLine, PlatformInfo->DisplayName, ContentPrepDescription, ContentPrepTaskName, ContentPrepIcon);
	}

	static void PackageBuildConfiguration(const PlatformInfo::FTargetPlatformInfo* Info, EProjectPackagingBuildConfigurations BuildConfiguration)
	{
		UProjectPackagingSettings* PackagingSettings = GetMutableDefault<UProjectPackagingSettings>();
		PackagingSettings->SetBuildConfigurationForPlatform(Info->IniPlatformName, BuildConfiguration);
		PackagingSettings->SaveConfig();
	}

	static bool CanPackageBuildConfiguration(const PlatformInfo::FTargetPlatformInfo* Info, EProjectPackagingBuildConfigurations BuildConfiguration)
	{
		return true;
	}

	static bool PackageBuildConfigurationIsChecked(const PlatformInfo::FTargetPlatformInfo* Info, EProjectPackagingBuildConfigurations BuildConfiguration)
	{
		return GetDefault<UProjectPackagingSettings>()->GetBuildConfigurationForPlatform(Info->IniPlatformName) == BuildConfiguration;
	}

	static void SetActiveTargetPlatform(const PlatformInfo::FTargetPlatformInfo* Info)
	{
		UProjectPackagingSettings* PackagingSettings = GetMutableDefault<UProjectPackagingSettings>();
		PackagingSettings->SetTargetPlatformForPlatform(Info->IniPlatformName, Info->Name);
		PackagingSettings->SaveConfig();
	}

	static bool CanSetActiveTargetPlatform(const PlatformInfo::FTargetPlatformInfo* Info)
	{
		return true;
	}

	static bool SetActiveTargetPlatformIsChecked(const PlatformInfo::FTargetPlatformInfo* Info)
	{
		return GetDefault<UProjectPackagingSettings>()->GetTargetPlatformForPlatform(Info->IniPlatformName) == Info->Name;
	}

	static void SetCookOnTheFly()
	{
		UCookerSettings* CookerSettings = GetMutableDefault<UCookerSettings>();

		CookerSettings->bCookOnTheFlyForLaunchOn = !CookerSettings->bCookOnTheFlyForLaunchOn;
		CookerSettings->Modify(true);

		// Update source control

		FString ConfigPath = FPaths::ConvertRelativePathToFull(CookerSettings->GetDefaultConfigFilename());

		if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*ConfigPath))
		{
			if (ISourceControlModule::Get().IsEnabled())
			{
				FText ErrorMessage;

				if (!SourceControlHelpers::CheckoutOrMarkForAdd(ConfigPath, FText::FromString(ConfigPath), NULL, ErrorMessage))
				{
					FNotificationInfo Info(ErrorMessage);
					Info.ExpireDuration = 3.0f;
					FSlateNotificationManager::Get().AddNotification(Info);
				}
			}
			else
			{
				if (!FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*ConfigPath, false))
				{
					FNotificationInfo Info(FText::Format(LOCTEXT("FailedToMakeWritable", "Could not make {0} writable."), FText::FromString(ConfigPath)));
					Info.ExpireDuration = 3.0f;
					FSlateNotificationManager::Get().AddNotification(Info);
				}
			}
		}

		// Save settings
		CookerSettings->UpdateSinglePropertyInConfigFile(CookerSettings->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UCookerSettings, bCookOnTheFlyForLaunchOn)), CookerSettings->GetDefaultConfigFilename());

	}

	static bool CanSetCookOnTheFly()
	{
		return true;
	}

	static bool SetCookOnTheFlyIsChecked()
	{
		return GetDefault<UCookerSettings>()->bCookOnTheFlyForLaunchOn;
	}
};

class FTurnkeySupportCommands : public TCommands<FTurnkeySupportCommands>
{
public:
	void RegisterCommands()
	{
		UI_COMMAND(PackagingSettings, "Packaging Settings...", "Opens the settings for project packaging", EUserInterfaceActionType::Button, FInputChord());
		ActionList->MapAction(PackagingSettings, FExecuteAction::CreateStatic(&FTurnkeySupportCallbacks::PackagingSettings));

	}

	TSharedPtr< FUICommandInfo > PackagingSettings;

private:

	friend class TCommands<FTurnkeySupportCommands>;

	FTurnkeySupportCommands()
		: TCommands<FTurnkeySupportCommands>("TurnkeySupport", LOCTEXT("TurnkeySupport", "Turnkey and General Platform Options"), "MainFrame", FEditorStyle::GetStyleSetName())
	{

	}

public:
	/** List of all of the main frame commands */
	static TSharedRef<FUICommandList> ActionList;

};

TSharedRef<FUICommandList> FTurnkeySupportCommands::ActionList = MakeShareable(new FUICommandList);




static void TurnkeyInstallSdk(FString PlatformName, bool bPreferFull, bool bForceInstall, const TCHAR* DeviceName = nullptr)
{
	//	FString CommandLine = FString::Printf(TEXT("Turnkey -command=InstallSdk -platform=%s -BestAvailable -AllowAutoSdk -EditorIO -noturnkeyvariables -utf8output -WaitForUATMutex"), *PlatformName);

	FString OptionalOptions;
	if (bPreferFull)
	{
		OptionalOptions += TEXT(" -PreferFull");
	}
	if (bForceInstall)
	{
		OptionalOptions += DeviceName != nullptr ? TEXT(" -ForceDeviceInstall") : TEXT(" -ForceSdkInstall");
	}
	if (DeviceName != nullptr)
	{
		OptionalOptions += FString::Printf(TEXT(" -Device=%s"), DeviceName);
	}

	FString CommandLine = FString::Printf(TEXT("Turnkey -command=VerifySdk -UpdateIfNeeded -platform=%s %s -EditorIO -noturnkeyvariables -utf8output -WaitForUATMutex"), *PlatformName, *OptionalOptions);

	FText TaskName = LOCTEXT("InstallingSdk", "Installing Sdk");
	IUATHelperModule::Get().CreateUatTask(CommandLine, FText::FromString(PlatformName), TaskName, TaskName, FEditorStyle::GetBrush(TEXT("MainFrame.PackageProject")),
		[PlatformName](FString, double)
	{
		AsyncTask(ENamedThreads::GameThread, [PlatformName]()
		{

			// read in env var changes
			// @todo turnkey move this and make it mac/linux aware
			FString TurnkeyEnvVarsFilename = FPaths::Combine(FPaths::EngineIntermediateDir(), TEXT("Turnkey/PostTurnkeyVariables.bat"));

			if (IFileManager::Get().FileExists(*TurnkeyEnvVarsFilename))
			{
				TArray<FString> Contents;
				if (FFileHelper::LoadFileToStringArray(Contents, *TurnkeyEnvVarsFilename))
				{
					for (const FString& Line : Contents)
					{
						if (Line.StartsWith(TEXT("set ")))
						{
							// split the line
							FString VariableLine = Line.Mid(4);
							int Equals;
							if (VariableLine.FindChar('=', Equals))
							{
								// set the key/value
								FString Key = VariableLine.Mid(0, Equals);
								FString Value = VariableLine.Mid(Equals + 1);

								FPlatformMisc::SetEnvironmentVar(*Key, *Value);

								UE_LOG(LogTurnkeySupport, Log, TEXT("Turnkey setting env var: %s = %s"), *Key, *Value);
							}
						}
					}
				}
			}

			// update the Sdk status
//			FDataDrivenPlatformInfoRegistry::UpdateSdkStatus();
			GetTargetPlatformManager()->UpdateAfterSDKInstall(*PlatformName);
			RenderUtilsInit();


			// show restart dialog
			FModuleManager::GetModuleChecked<ISettingsEditorModule>("SettingsEditor").OnApplicationRestartRequired();
		});
	}
	);
}

static TAttribute<FText> MakeSdkStatusAttribute(FName IniPlatformName, TSharedPtr< ITargetDeviceProxy> DeviceProxy)
{
	// 	FFormatNamedArguments LabelArguments;
	// 	LabelArguments.Add(TEXT("DeviceName"), FText::FromString(DeviceProxy->GetName()));
	// 
	// 	if (!DeviceProxy->IsConnected())
	// 	{
	// 		LabelArguments.Add(TEXT("HostUser"), LOCTEXT("DisconnectedHint", " [Disconnected]"));
	// 	}
	// 	else if (DeviceProxy->GetHostUser() != FPlatformProcess::UserName(false))
	// 	{
	// 		LabelArguments.Add(TEXT("HostUser"), FText::FromString(DeviceProxy->GetHostUser()));
	// 	}
	// 	else
	// 	{
	// 		LabelArguments.Add(TEXT("HostUser"), FText::GetEmpty());
	// 	}
	// 
	// 	OutLabel = FText::Format(LOCTEXT("LaunchDeviceLabel", "{DeviceName}{HostUser}"), LabelArguments);
	// 	TAttribute<FText> = Make


	FString DisplayString = DeviceProxy ? DeviceProxy->GetName() : IniPlatformName.ToString();
	FString DeviceId = DeviceProxy ? DeviceProxy->GetTargetDeviceId(NAME_None) : FString();

	return TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateLambda([IniPlatformName, DisplayString, DeviceId]()
	{
		// get the status, or Unknown if it's not there
		const FDataDrivenPlatformInfo& Info = FDataDrivenPlatformInfoRegistry::GetPlatformInfo(IniPlatformName);
		DDPIPlatformSdkStatus Status = DeviceId.Len() ? Info.GetStatusForDeviceId(DeviceId) : Info.GetSdkStatus(false);

		// @todo turnkey: Have premade FText's by SdkStatus for speed
		const TCHAR* Desc =
			Status == DDPIPlatformSdkStatus::Querying ? TEXT("Querying...") :
			Status == DDPIPlatformSdkStatus::Valid ? TEXT("Valid Sdk") :
			Status == DDPIPlatformSdkStatus::OutOfDate ? TEXT("Outdated Sdk") :
			Status == DDPIPlatformSdkStatus::NoSdk ? TEXT("No Sdk") :
			Status == DDPIPlatformSdkStatus::FlashValid ? TEXT("Valid Flash") :
			Status == DDPIPlatformSdkStatus::FlashOutOfDate ? TEXT("Outdated Flash") :
			TEXT("???");
		return FText::FromString(FString::Printf(TEXT("%s (%s)"), *DisplayString, Desc));
	}));
}






static void MakeTurnkeyPlatformMenu(FMenuBuilder& MenuBuilder, FName IniPlatformName, ITargetDeviceServicesModule* TargetDeviceServicesModule)
{
	const FDataDrivenPlatformInfo& DDPI = FDataDrivenPlatformInfoRegistry::GetPlatformInfo(IniPlatformName);
	FString UBTPlatformString = DDPI.UBTPlatformString;

	const PlatformInfo::FTargetPlatformInfo* VanillaInfo = PlatformInfo::FindVanillaPlatformInfo(IniPlatformName);

	if (VanillaInfo != nullptr)
	{

		MenuBuilder.BeginSection("ContentManagement", LOCTEXT("TurnkeySection_Content", "Content Management"));

		MenuBuilder.AddMenuEntry(
			LOCTEXT("Turnkey_PackageProject", "Package Project"),
			LOCTEXT("TurnkeyTooltip_PackageProject", "Package this project and archive it to a user-selected directory. This can then be used to install and run."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateStatic(&FTurnkeySupportCallbacks::CookOrPackage, IniPlatformName, EPrepareContentMode::Package),
				FCanExecuteAction::CreateStatic(&FTurnkeySupportCallbacks::CanCookOrPackage, IniPlatformName, EPrepareContentMode::Package)
			)
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("Turnkey_CookContent", "Cook Content"),
			LOCTEXT("TurnkeyTooltip_CookContent", "Cook this project for the selected configuration and target"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateStatic(&FTurnkeySupportCallbacks::CookOrPackage, IniPlatformName, EPrepareContentMode::CookOnly),
				FCanExecuteAction::CreateStatic(&FTurnkeySupportCallbacks::CanCookOrPackage, IniPlatformName, EPrepareContentMode::CookOnly)
			)
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("Turnkey_PrepareForDebugging", "Prepare For Debugging"),
			LOCTEXT("TurnkeyTooltip_PrepareForDebugging", "Prepare this project for debugging"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateStatic(&FTurnkeySupportCallbacks::CookOrPackage, IniPlatformName, EPrepareContentMode::PrepareForDebugging),
				FCanExecuteAction::CreateStatic(&FTurnkeySupportCallbacks::CanCookOrPackage, IniPlatformName, EPrepareContentMode::PrepareForDebugging)
			)
		);

		MenuBuilder.EndSection();


		MenuBuilder.BeginSection("BuildConfig", LOCTEXT("TurnkeySection_BuildConfig", "Binary Configuration"));
			EProjectType ProjectType = FGameProjectGenerationModule::Get().ProjectHasCodeFiles() ? EProjectType::Code : EProjectType::Content;
			TArray<EProjectPackagingBuildConfigurations> PackagingConfigurations = UProjectPackagingSettings::GetValidPackageConfigurations();

			for (EProjectPackagingBuildConfigurations PackagingConfiguration : PackagingConfigurations)
			{
				const UProjectPackagingSettings::FConfigurationInfo& ConfigurationInfo = UProjectPackagingSettings::ConfigurationInfo[(int)PackagingConfiguration];
				if (FInstalledPlatformInfo::Get().IsValid(TOptional<EBuildTargetType>(), TOptional<FString>(), ConfigurationInfo.Configuration, ProjectType, EInstalledPlatformState::Downloaded))
				{
					MenuBuilder.AddMenuEntry(
						ConfigurationInfo.Name,
						ConfigurationInfo.ToolTip,
						FSlateIcon(),
						FUIAction(
							FExecuteAction::CreateStatic(&FTurnkeySupportCallbacks::PackageBuildConfiguration, VanillaInfo, PackagingConfiguration),
							FCanExecuteAction::CreateStatic(&FTurnkeySupportCallbacks::CanPackageBuildConfiguration, VanillaInfo, PackagingConfiguration),
							FIsActionChecked::CreateStatic(&FTurnkeySupportCallbacks::PackageBuildConfigurationIsChecked, VanillaInfo, PackagingConfiguration)
						),
						NAME_None,
						EUserInterfaceActionType::RadioButton
					);
				}
			}
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection("TargetSelection", LOCTEXT("TurnkeySection_TargetSelection", "Target Selection"));

			// gather all platform infos
			TArray<const PlatformInfo::FTargetPlatformInfo*> AllTargets = { VanillaInfo };
			AllTargets.Append(VanillaInfo->Flavors);

			for (const PlatformInfo::FTargetPlatformInfo* Info : AllTargets)
			{
				MenuBuilder.AddMenuEntry(
					Info->DisplayName,
					FText(),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateStatic(&FTurnkeySupportCallbacks::SetActiveTargetPlatform, Info),
						FCanExecuteAction::CreateStatic(&FTurnkeySupportCallbacks::CanSetActiveTargetPlatform, Info),
						FIsActionChecked::CreateStatic(&FTurnkeySupportCallbacks::SetActiveTargetPlatformIsChecked, Info)
					),
					NAME_None,
					EUserInterfaceActionType::RadioButton
				);
			}
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection("AllDevices", LOCTEXT("TurnkeySection_AllDevices", "All Devices"));

		TArray<TSharedPtr<ITargetDeviceProxy>> DeviceProxies;
		TargetDeviceServicesModule->GetDeviceProxyManager()->GetAllProxies(IniPlatformName, DeviceProxies);

		for (const TSharedPtr<ITargetDeviceProxy> Proxy : DeviceProxies)
		{
			FString DeviceName = Proxy->GetName();
			MenuBuilder.AddSubMenu(
				MakeSdkStatusAttribute(IniPlatformName, Proxy),
				FText(),
				FNewMenuDelegate::CreateLambda([UBTPlatformString, DeviceName](FMenuBuilder& SubMenuBuilder) {
				SubMenuBuilder.AddMenuEntry(
					LOCTEXT("Turnkey_RepairDevice", "Repair Device as Needed"),
					LOCTEXT("TurnkeyTooltip_RepairDevice", "Perform any fixup that may be needed on this device. If up to date already, nothing will be done."),
					FSlateIcon(),
					FExecuteAction::CreateStatic(TurnkeyInstallSdk, UBTPlatformString, false, false, *DeviceName)
				);

				SubMenuBuilder.AddMenuEntry(
					LOCTEXT("Turnkey_ForceRepairDevice", "Force Repair Device"),
					LOCTEXT("TurnkeyTooltip_ForceRepairDevice", "Force repairing anything on the device needed (update firmware, etc). Will perform all steps possible, even if not needed."),
					FSlateIcon(),
					FExecuteAction::CreateStatic(TurnkeyInstallSdk, UBTPlatformString, true, false, *DeviceName)
				);
			})
			);
		}

		MenuBuilder.EndSection();
		}

	MenuBuilder.BeginSection("AllDevices", LOCTEXT("TurnkeySection_Sdks", "Sdk Managment"));

	const TCHAR* NoDevice = nullptr;
	if (DDPI.GetSdkStatus() == DDPIPlatformSdkStatus::OutOfDate)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("Turnkey_InstallSdkMinimal", "Update Sdk (Prefer Minimal)"),
			LOCTEXT("TurnkeyTooltip_InstallSdkMinimal", "Attempt to update an Sdk, as hosted by your studio. Will attempt to install a minimal Sdk (useful for building/running only)"),
			FSlateIcon(),
			FExecuteAction::CreateStatic(TurnkeyInstallSdk, UBTPlatformString, false, false, NoDevice)
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("Turnkey_InstallSdkFull", "Update Sdk (Prefer Full)"),
			LOCTEXT("TurnkeyTooltip_InstallSdkMinimal", "Attempt to update an Sdk, as hosted by your studio. Will attempt to install a full Sdk (useful profiling or other use cases)"),
			FSlateIcon(),
			FExecuteAction::CreateStatic(TurnkeyInstallSdk, UBTPlatformString, true, false, NoDevice)
		);
	}
	else if (DDPI.GetSdkStatus() == DDPIPlatformSdkStatus::Valid)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("Turnkey_InstallSdkMinimal", "Force Reinstall Sdk (Prefer Minimal)"),
			LOCTEXT("TurnkeyTooltip_InstallSdkMinimal", "Attempt to force re-install an Sdk, as hosted by your studio. Will attempt to install a minimal Sdk (useful for building/running only)"),
			FSlateIcon(),
			FExecuteAction::CreateStatic(TurnkeyInstallSdk, UBTPlatformString, false, true, NoDevice)
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("Turnkey_InstallSdkFull", "Force Reinstall (Prefer Full)"),
			LOCTEXT("TurnkeyTooltip_InstallSdkMinimal", "Attempt to force re-install an Sdk, as hosted by your studio. Will attempt to install a full Sdk (useful profiling or other use cases)"),
			FSlateIcon(),
			FExecuteAction::CreateStatic(TurnkeyInstallSdk, UBTPlatformString, true, true, NoDevice)
		);
	}
	else
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("Turnkey_InstallSdkMinimal", "Install Sdk (Prefer Minimal)"),
			LOCTEXT("TurnkeyTooltip_InstallSdkMinimal", "Attempt to install an Sdk, as hosted by your studio. Will attempt to install a minimal Sdk (useful for building/running only)"),
			FSlateIcon(),
			FExecuteAction::CreateStatic(TurnkeyInstallSdk, UBTPlatformString, false, false, NoDevice)
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("Turnkey_InstallSdkFull", "Install Sdk (Prefer Full)"),
			LOCTEXT("TurnkeyTooltip_InstallSdkMinimal", "Attempt to install an Sdk, as hosted by your studio. Will attempt to install a full Sdk (useful profiling or other use cases)"),
			FSlateIcon(),
			FExecuteAction::CreateStatic(TurnkeyInstallSdk, UBTPlatformString, true, false, NoDevice)
		);
	}
}


// Launch On

bool CanLaunchOnDevice(const FString& DeviceName)
{
	if (!GUnrealEd->IsPlayingViaLauncher())
	{
		static TWeakPtr<ITargetDeviceProxyManager> DeviceProxyManagerPtr;

		if (!DeviceProxyManagerPtr.IsValid())
		{
			ITargetDeviceServicesModule* TargetDeviceServicesModule = FModuleManager::Get().LoadModulePtr<ITargetDeviceServicesModule>(TEXT("TargetDeviceServices"));
			if (TargetDeviceServicesModule)
			{
				DeviceProxyManagerPtr = TargetDeviceServicesModule->GetDeviceProxyManager();
			}
		}

		TSharedPtr<ITargetDeviceProxyManager> DeviceProxyManager = DeviceProxyManagerPtr.Pin();
		if (DeviceProxyManager.IsValid())
		{
			TSharedPtr<ITargetDeviceProxy> DeviceProxy = DeviceProxyManager->FindProxy(DeviceName);
			if (DeviceProxy.IsValid() && DeviceProxy->IsConnected() && DeviceProxy->IsAuthorized())
			{
				return true;
			}

			// check if this is an aggregate proxy
			TArray<TSharedPtr<ITargetDeviceProxy>> Devices;
			DeviceProxyManager->GetProxies(FName(*DeviceName), false, Devices);

			// returns true if the game can be launched al least on 1 device
			for (auto DevicesIt = Devices.CreateIterator(); DevicesIt; ++DevicesIt)
			{
				TSharedPtr<ITargetDeviceProxy> DeviceAggregateProxy = *DevicesIt;
				if (DeviceAggregateProxy.IsValid() && DeviceAggregateProxy->IsConnected() && DeviceAggregateProxy->IsAuthorized())
				{
					return true;
				}
			}

		}
	}

	return false;
}


void LaunchOnDevice(const FString& DeviceId, const FString& DeviceName, bool bUseTurnkey)
{
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
				FString CommandLine = FString::Printf(TEXT("Turnkey -command=VerifySdk -UpdateIfNeeded -platform=%s -EditorIO -noturnkeyvariables -device=%s -utf8output -WaitForUATMutex"), *UBTPlatformName, *DeviceName);
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
}

static void PrepareLaunchOn(FString DeviceId, FString DeviceName)
{
	ULevelEditorPlaySettings* PlaySettings = GetMutableDefault<ULevelEditorPlaySettings>();

	PlaySettings->LastExecutedLaunchModeType = LaunchMode_OnDevice;
	PlaySettings->LastExecutedLaunchDevice = DeviceId;
	PlaySettings->LastExecutedLaunchName = DeviceName;

	PlaySettings->PostEditChange();

	PlaySettings->SaveConfig();
}

void HandleLaunchOnDeviceActionExecute(FString DeviceId, FString DeviceName, bool bUseTurnkey)
{
	PrepareLaunchOn(DeviceId, DeviceName);
	LaunchOnDevice(DeviceId, DeviceName, bUseTurnkey);
}


bool HandleLaunchOnDeviceActionCanExecute(FString DeviceName)
{
	return CanLaunchOnDevice(DeviceName);
}

bool HandleLaunchOnDeviceActionIsChecked(FString DeviceName)
{
	return (DeviceName == GetDefault<ULevelEditorPlaySettings>()->LastExecutedLaunchName);
}

static void GenerateDeviceProxyMenuParams(TSharedPtr<ITargetDeviceProxy> DeviceProxy, FName PlatformName, FUIAction& OutAction, FText& OutTooltip)
{
	// 	// create an All_<platform>_devices_on_<host> submenu
	// 	if (DeviceProxy->IsAggregated())
	// 	{
	// 		FString AggregateDevicedName(FString::Printf(TEXT("  %s"), *DeviceProxy->GetName())); //align with the other menu entries
	// 		FSlateIcon AggregateDeviceIcon(FEditorStyle::GetStyleSetName(), EditorPlatformInfo->GetIconStyleName(PlatformInfo::EPlatformIconSize::Normal));
	// 
	// 		MenuBuilder.AddSubMenu(
	// 			FText::FromString(AggregateDevicedName),
	// 			FText::FromString(AggregateDevicedName),
	// 			FNewMenuDelegate::CreateStatic(&MakeAllDevicesSubMenu, EditorPlatformInfo, DeviceProxy),
	// 			false, AggregateDeviceIcon, true
	// 		);
	// 		continue;
	// 	}

		// ... create an action...
	OutAction = FUIAction(
		FExecuteAction::CreateStatic(&HandleLaunchOnDeviceActionExecute, DeviceProxy->GetTargetDeviceId(NAME_None), DeviceProxy->GetName(), true)
		//		, FCanExecuteAction::CreateStatic(&HandleLaunchOnDeviceActionCanExecute, DeviceProxy->GetName())
		//		, FIsActionChecked::CreateStatic(&HandleLaunchOnDeviceActionIsChecked, DeviceProxy->GetName())
	);

	// ... generate tooltip text
	FFormatNamedArguments TooltipArguments;
	TooltipArguments.Add(TEXT("DeviceID"), FText::FromString(DeviceProxy->GetName()));
	TooltipArguments.Add(TEXT("DisplayName"), FText::FromName(PlatformName));
	OutTooltip = FText::Format(LOCTEXT("LaunchDeviceToolTipText_ThisDevice", "Launch the game on this {DisplayName} device ({DeviceID})"), TooltipArguments);
	if (!DeviceProxy->IsAuthorized())
	{
		OutTooltip = FText::Format(LOCTEXT("LaunchDeviceToolTipText_UnauthorizedOrLocked", "{DisplayName} device ({DeviceID}) is unauthorized or locked"), TooltipArguments);
	}

	FProjectStatus ProjectStatus;
	if (IProjectManager::Get().QueryStatusForCurrentProject(ProjectStatus) && !ProjectStatus.IsTargetPlatformSupported(PlatformName))
	{
		FText TooltipLine2 = FText::Format(LOCTEXT("LaunchDevicePlatformWarning", "{DisplayName} is not listed as a target platform for this project, so may not run as expected."), TooltipArguments);
		OutTooltip = FText::Format(FText::FromString(TEXT("{0}\n\n{1}")), OutTooltip, TooltipLine2);
	}

}










TSharedRef<SWidget> FTurnkeySupportModule::MakeTurnkeyMenu() const
{
	const FTurnkeySupportCommands& Commands = FTurnkeySupportCommands::Get();

	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, FTurnkeySupportCommands::ActionList);

	// shared devices section
	ITargetDeviceServicesModule* TargetDeviceServicesModule = static_cast<ITargetDeviceServicesModule*>(FModuleManager::Get().LoadModule(TEXT("TargetDeviceServices")));
	IProjectTargetPlatformEditorModule& ProjectTargetPlatformEditorModule = FModuleManager::LoadModuleChecked<IProjectTargetPlatformEditorModule>("ProjectTargetPlatformEditor");

	TArray<FString> DeviceIdsToQuery;

	MenuBuilder.BeginSection("LevelEditorLaunchDevices", LOCTEXT("TurnkeySection_LaunchButtonDevices", "Quick Launch"));
	{
		for (const auto Pair : FDataDrivenPlatformInfoRegistry::GetAllPlatformInfos())
		{
			if (Pair.Value.bIsFakePlatform)
			{
				continue;
			}

			FName PlatformName = Pair.Key;
			const FDataDrivenPlatformInfo& Info = Pair.Value;

			// look for devices for all platforms, even if the platform isn't installed - Turnkey can install Sdk after selecting LaunchOn
			TArray<TSharedPtr<ITargetDeviceProxy>> DeviceProxies;
			TargetDeviceServicesModule->GetDeviceProxyManager()->GetAllProxies(PlatformName, DeviceProxies);

			if (DeviceProxies.Num() > 0)
			{
				// 					Algo::Sort(DeviceProxies, [](TSharedPtr<ITargetDeviceProxy> A, TSharedPtr<ITargetDeviceProxy> B)
				// 					{ return A->Name == LastChosen || B->Name != LastChosen && A->IsDefault(); });


				// always use the first one, after sorting
				FUIAction Action;
				FText Tooltip;
				GenerateDeviceProxyMenuParams(DeviceProxies[0], PlatformName, Action, Tooltip);

				if (DeviceProxies.Num() == 1)
				{
					MenuBuilder.AddMenuEntry(
						MakeSdkStatusAttribute(PlatformName, DeviceProxies[0]),
						Tooltip,
						FSlateIcon(FEditorStyle::GetStyleSetName(), Pair.Value.GetIconStyleName(EPlatformIconSize::Normal)),
						Action,
						NAME_None,
						EUserInterfaceActionType::Button
					);
				}
				else
				{
					MenuBuilder.AddSubMenu(
						MakeSdkStatusAttribute(PlatformName, DeviceProxies[0]),
						Tooltip,
						FNewMenuDelegate::CreateLambda([TargetDeviceServicesModule, PlatformName, IconStyle = Pair.Value.GetIconStyleName(EPlatformIconSize::Normal)](FMenuBuilder& SubMenuBuilder) 
						{
							// re-get the proxies, just in case they changed
							TArray<TSharedPtr<ITargetDeviceProxy>> DeviceProxies;
							TargetDeviceServicesModule->GetDeviceProxyManager()->GetAllProxies(PlatformName, DeviceProxies);
							// for each one, put an entry (even the one that was in the outer menu, for less confusion)
							for (const TSharedPtr<ITargetDeviceProxy> Proxy : DeviceProxies)
							{
								FUIAction SubAction;
								FText SubTooltip;
								GenerateDeviceProxyMenuParams(Proxy, PlatformName, SubAction, SubTooltip);
								SubMenuBuilder.AddMenuEntry(
									MakeSdkStatusAttribute(PlatformName, Proxy),
									SubTooltip,
									FSlateIcon(FEditorStyle::GetStyleSetName(), IconStyle),
									SubAction,
									NAME_None,
									EUserInterfaceActionType::Button
								);
							}
						}),
						Action,
						NAME_None,
						EUserInterfaceActionType::Check,
						false,
						FSlateIcon(FEditorStyle::GetStyleSetName(), Pair.Value.GetIconStyleName(EPlatformIconSize::Normal)),
						true
						);
				}

				// gather any unknown status devices to query at the end
				for (const TSharedPtr<ITargetDeviceProxy> Proxy : DeviceProxies)
				{
					FString DeviceId = Proxy->GetTargetDeviceId(NAME_None);
					if (Pair.Value.GetStatusForDeviceId(DeviceId) == DDPIPlatformSdkStatus::Unknown)
					{
						DeviceIdsToQuery.Add(DeviceId);
					}
				}
			}
		}
	}
	MenuBuilder.EndSection();


	MenuBuilder.BeginSection("CookerSettings");

	MenuBuilder.AddMenuEntry(
		LOCTEXT("CookOnTheFlyOnLaunch", "Enable cooking on the fly"),
		LOCTEXT("CookOnTheFlyOnLaunchDescription", "Cook on the fly instead of cooking upfront when launching"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateStatic(&FTurnkeySupportCallbacks::SetCookOnTheFly),
			FCanExecuteAction::CreateStatic(&FTurnkeySupportCallbacks::CanSetCookOnTheFly),
			FIsActionChecked::CreateStatic(&FTurnkeySupportCallbacks::SetCookOnTheFlyIsChecked)
		),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);

	{
		MenuBuilder.AddWidget(
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("ZoomToFitHorizontal", "Launching a game on a different device will change your default 'Launch' device in the toolbar"))
			.WrapTextAt(300),
			FText::GetEmpty()
		);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("AllPlatforms", LOCTEXT("TurnkeyMenu_ManagePlatforms", "Content/Sdk/Device Management"));
	TMap<FName, const FDataDrivenPlatformInfo*> UncompiledPlatforms;
	for (const auto Pair : FDataDrivenPlatformInfoRegistry::GetAllPlatformInfos())
	{
		if (Pair.Value.bIsFakePlatform || Pair.Value.bEnabledForUse == false)
		{
			continue;
		}

		FName PlatformName = Pair.Key;
		const FDataDrivenPlatformInfo& Info = Pair.Value;

		if (!FDataDrivenPlatformInfoRegistry::HasCompiledSupportForPlatform(PlatformName, FDataDrivenPlatformInfoRegistry::EPlatformNameType::Ini))
		{
			UncompiledPlatforms.Add(PlatformName, &Info);
			continue;
		}

		MenuBuilder.AddSubMenu(
			MakeSdkStatusAttribute(PlatformName, nullptr),
			FText::FromString(PlatformName.ToString()),
			FNewMenuDelegate::CreateStatic(&MakeTurnkeyPlatformMenu, PlatformName, TargetDeviceServicesModule),
			false,
			FSlateIcon(FEditorStyle::GetStyleSetName(), Pair.Value.GetIconStyleName(EPlatformIconSize::Normal)),
			true
		);
	}

	if (UncompiledPlatforms.Num() != 0)
	{
		MenuBuilder.AddSeparator(NAME_None);

		MenuBuilder.AddSubMenu(
			LOCTEXT("Turnkey_UncompiledPlatforms", "Platforms With No Compiled Support"),
			LOCTEXT("Turnkey_UncompiledPlatformsToolTip", "List of platforms that you have access to, but support is not compiled in to the editor. It may be caused by missing an SDK, so you attempt to install an SDK here."),
			FNewMenuDelegate::CreateLambda([UncompiledPlatforms, TargetDeviceServicesModule](FMenuBuilder& SubMenuBuilder)
			{
				for (const auto It : UncompiledPlatforms)
				{
					SubMenuBuilder.AddSubMenu(
						MakeSdkStatusAttribute(It.Key, nullptr),
						FText::FromString(It.Key.ToString()),
						FNewMenuDelegate::CreateStatic(&MakeTurnkeyPlatformMenu, It.Key, TargetDeviceServicesModule),
						false,
						FSlateIcon(FEditorStyle::GetStyleSetName(), It.Value->GetIconStyleName(EPlatformIconSize::Normal)),
						true
					);
				}
			})
		);
	}

	MenuBuilder.EndSection();


	// options section
	MenuBuilder.BeginSection("TurnkeyOptions", LOCTEXT("TurnkeySection_Options", "Options and Settings"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("OpenProjectLauncher", "Project Launcher..."),
			LOCTEXT("OpenProjectLauncher_ToolTip", "Open the Project Launcher for advanced packaging, deploying and launching of your projects"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Launcher.TabIcon"),
			FUIAction(FExecuteAction::CreateStatic(&FTurnkeySupportCallbacks::OpenProjectLauncher))
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("OpenDeviceManager", "Device Manager..."),
			LOCTEXT("OpenDeviceManager_ToolTip", "View and manage connected devices."),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "DeviceDetails.TabIcon"),
			FUIAction(FExecuteAction::CreateStatic(&FTurnkeySupportCallbacks::OpenDeviceManager))
		);

		MenuBuilder.AddSeparator();

		MenuBuilder.AddMenuEntry(
			LOCTEXT("PackagingSettings", "Packaging Settings..."),
			LOCTEXT("PackagingSettings_ToolTip", "Opens the settings for project packaging"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&FTurnkeySupportCallbacks::PackagingSettings))
		);

// 		MenuBuilder.AddMenuEntry(Commands.ZipUpProject,
// 			NAME_None,
// 			TAttribute<FText>(),
// 			TAttribute<FText>(),
// 			FSlateIcon()
// 		);
// 		MenuBuilder.AddMenuEntry(Commands.PackagingSettings,
// 			NAME_None,
// 			TAttribute<FText>(),
// 			TAttribute<FText>(),
// 			FSlateIcon()
// 		);

		ProjectTargetPlatformEditorModule.AddOpenProjectTargetPlatformEditorMenuItem(MenuBuilder);
	}
	MenuBuilder.EndSection();

	// now kick-off any devices that need to be updated
	if (DeviceIdsToQuery.Num() > 0)
	{
		FDataDrivenPlatformInfoRegistry::UpdateDeviceSdkStatus(DeviceIdsToQuery);
	}

	return MenuBuilder.MakeWidget();
}

/* IModuleInterface implementation
 *****************************************************************************/

void FTurnkeySupportModule::StartupModule( )
{
	FTurnkeySupportCommands::Register();
}


void FTurnkeySupportModule::ShutdownModule( )
{
}



#undef LOCTEXT_NAMESPACE










#if 0

// PACKAGING STUFF THAT HASN'T COME OVER


GUnrealEd->CancelPlayingViaLauncher();
SaveAll();

// does the project have any code?
FGameProjectGenerationModule& GameProjectModule = FModuleManager::LoadModuleChecked<FGameProjectGenerationModule>(TEXT("GameProjectGeneration"));
bool bProjectHasCode = GameProjectModule.Get().ProjectHasCodeFiles();

const PlatformInfo::FTargetPlatformInfo* const PlatformInfo = PlatformInfo::FindPlatformInfo(InPlatformInfoName);
check(PlatformInfo);

if (FInstalledPlatformInfo::Get().IsPlatformMissingRequiredFile(PlatformInfo->DataDrivenPlatformInfo->UBTPlatformString))
{
	if (!FInstalledPlatformInfo::OpenInstallerOptions())
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("MissingPlatformFilesPackage", "Missing required files to package this platform."));
	}
	return;
}

if (UGameMapsSettings::GetGameDefaultMap().IsEmpty())
{
	FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("MissingGameDefaultMap", "No Game Default Map specified in Project Settings > Maps & Modes."));
	return;
}

if (/*PlatformInfo->DataDrivenPlatformInfo->GetSdkStatus() != DDPIPlatformSdkStatus::Valid ||*/ (bProjectHasCode && PlatformInfo->DataDrivenPlatformInfo->bUsesHostCompiler && !FSourceCodeNavigation::IsCompilerAvailable()))
{
	if (!ShowBadSDKDialog(PlatformInfo->IniPlatformName))
	{
		// 			IMainFrameModule& MainFrameModule = FModuleManager::GetModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
		// 			MainFrameModule.BroadcastMainFrameSDKNotInstalled(PlatformInfo->Name.ToString(), PlatformInfo->DataDrivenPlatformInfo->SDKTutorial);
		TArray<FAnalyticsEventAttribute> ParamArray;
		ParamArray.Add(FAnalyticsEventAttribute(TEXT("Time"), 0.0));
		FEditorAnalytics::ReportEvent(TEXT("Editor.Package.Failed"), PlatformInfo->Name.ToString(), bProjectHasCode, EAnalyticsErrorCodes::SDKNotFound, ParamArray);
		return;
	}
}

UProjectPackagingSettings* PackagingSettings = Cast<UProjectPackagingSettings>(UProjectPackagingSettings::StaticClass()->GetDefaultObject());
const UProjectPackagingSettings::FConfigurationInfo& ConfigurationInfo = UProjectPackagingSettings::ConfigurationInfo[(int)PackagingSettings->BuildConfiguration];
bool bAssetNativizationEnabled = (PackagingSettings->BlueprintNativizationMethod != EProjectPackagingBlueprintNativizationMethod::Disabled);

const ITargetPlatform* const Platform = GetTargetPlatformManager()->FindTargetPlatform(PlatformInfo->Name.ToString());
{
	if (Platform)
	{
		FString NotInstalledTutorialLink;
		FString DocumentationLink;
		FText CustomizedLogMessage;

		int32 Result = Platform->CheckRequirements(bProjectHasCode, ConfigurationInfo.Configuration, bAssetNativizationEnabled, NotInstalledTutorialLink, DocumentationLink, CustomizedLogMessage);

		// report to analytics
		FEditorAnalytics::ReportBuildRequirementsFailure(TEXT("Editor.Package.Failed"), PlatformInfo->Name.ToString(), bProjectHasCode, Result);

		// report to main frame
		bool UnrecoverableError = false;

		// report to message log
		if ((Result & ETargetPlatformReadyStatus::SDKNotFound) != 0)
		{
			AddMessageLog(
				LOCTEXT("SdkNotFoundMessage", "Software Development Kit (SDK) not found."),
				CustomizedLogMessage.IsEmpty() ? FText::Format(LOCTEXT("SdkNotFoundMessageDetail", "Please install the SDK for the {0} target platform!"), Platform->DisplayName()) : CustomizedLogMessage,
				NotInstalledTutorialLink,
				DocumentationLink
			);
			UnrecoverableError = true;
		}

		if ((Result & ETargetPlatformReadyStatus::LicenseNotAccepted) != 0)
		{
			AddMessageLog(
				LOCTEXT("LicenseNotAcceptedMessage", "License not accepted."),
				CustomizedLogMessage.IsEmpty() ? LOCTEXT("LicenseNotAcceptedMessageDetail", "License must be accepted in project settings to deploy your app to the device.") : CustomizedLogMessage,
				NotInstalledTutorialLink,
				DocumentationLink
			);

			UnrecoverableError = true;
		}

		if ((Result & ETargetPlatformReadyStatus::ProvisionNotFound) != 0)
		{
			AddMessageLog(
				LOCTEXT("ProvisionNotFoundMessage", "Provision not found."),
				CustomizedLogMessage.IsEmpty() ? LOCTEXT("ProvisionNotFoundMessageDetail", "A provision is required for deploying your app to the device.") : CustomizedLogMessage,
				NotInstalledTutorialLink,
				DocumentationLink
			);
			UnrecoverableError = true;
		}

		if ((Result & ETargetPlatformReadyStatus::SigningKeyNotFound) != 0)
		{
			AddMessageLog(
				LOCTEXT("SigningKeyNotFoundMessage", "Signing key not found."),
				CustomizedLogMessage.IsEmpty() ? LOCTEXT("SigningKeyNotFoundMessageDetail", "The app could not be digitally signed, because the signing key is not configured.") : CustomizedLogMessage,
				NotInstalledTutorialLink,
				DocumentationLink
			);
			UnrecoverableError = true;
		}

		if ((Result & ETargetPlatformReadyStatus::ManifestNotFound) != 0)
		{
			AddMessageLog(
				LOCTEXT("ManifestNotFound", "Manifest not found."),
				CustomizedLogMessage.IsEmpty() ? LOCTEXT("ManifestNotFoundMessageDetail", "The generated application manifest could not be found.") : CustomizedLogMessage,
				NotInstalledTutorialLink,
				DocumentationLink
			);
			UnrecoverableError = true;
		}

		if ((Result & ETargetPlatformReadyStatus::RemoveServerNameEmpty) != 0
			&& (bProjectHasCode || (Result & ETargetPlatformReadyStatus::CodeBuildRequired)
				|| (!FApp::GetEngineIsPromotedBuild() && !FApp::IsEngineInstalled())))
		{
			AddMessageLog(
				LOCTEXT("RemoveServerNameNotFound", "Remote compiling requires a server name. "),
				CustomizedLogMessage.IsEmpty() ? LOCTEXT("RemoveServerNameNotFoundDetail", "Please specify one in the Remote Server Name settings field.") : CustomizedLogMessage,
				NotInstalledTutorialLink,
				DocumentationLink
			);
			UnrecoverableError = true;
		}

		if ((Result & ETargetPlatformReadyStatus::CodeUnsupported) != 0)
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NotSupported_SelectedPlatform", "Sorry, packaging a code-based project for the selected platform is currently not supported. This feature may be available in a future release."));
			UnrecoverableError = true;
		}
		else if ((Result & ETargetPlatformReadyStatus::PluginsUnsupported) != 0)
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NotSupported_ThirdPartyPlugins", "Sorry, packaging a project with third-party plugins is currently not supported for the selected platform. This feature may be available in a future release."));
			UnrecoverableError = true;
		}

		if (UnrecoverableError)
		{
			return;
		}
	}
}

if (!FModuleManager::LoadModuleChecked<IProjectTargetPlatformEditorModule>("ProjectTargetPlatformEditor").ShowUnsupportedTargetWarning(PlatformInfo->VanillaInfo->Name))
{
	return;
}


#endif



















IMPLEMENT_MODULE(FTurnkeySupportModule, TurnkeySupport);
