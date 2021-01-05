// Copyright Epic Games, Inc. All Rights Reserved.

#include "TurnkeySupportModule.h"
#include "TurnkeySupport.h"

#include "SlateOptMacros.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "MessageLogModule.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Misc/MessageDialog.h"
#include "HAL/PlatformFileManager.h"
#include "ITargetDeviceServicesModule.h"
#include "Misc/DataDrivenPlatformInfoRegistry.h"
#include "Async/Async.h"
#include "Misc/FileHelper.h"
#include "Misc/ConfigCacheIni.h"
#include "Interfaces/TargetDeviceId.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/IProjectManager.h"
#include "SourceControlHelpers.h"
#include "ISourceControlModule.h"
#include "ITargetDeviceProxy.h"
#include "ITargetDeviceServicesModule.h"
#include "Settings/ProjectPackagingSettings.h" // can go away once turnkey menu handles Package
#include "IDesktopPlatform.h"
#include "DesktopPlatformModule.h"
#include "PlatformInfo.h"
#include "InstalledPlatformInfo.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Developer/DerivedDataCache/Public/DerivedDataCacheInterface.h"
#include "Misc/MonitoredProcess.h"
#include "EditorStyleSet.h"
#include "CookerSettings.h"
#include "UObject/UObjectIterator.h"
#include "ToolMenus.h"
#include "TurnkeyEditorSupport.h"

#include "Misc/App.h"
#include "Framework/Application/SlateApplication.h"


#if WITH_ENGINE
#include "RenderUtils.h"
#endif

DEFINE_LOG_CATEGORY(LogTurnkeySupport);
#define LOCTEXT_NAMESPACE "FTurnkeySupportModule"

namespace 
{
	FCriticalSection GTurnkeySection;
}


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

			bool bClickedOK = FTurnkeyEditorSupport::ShowOKCancelDialog(WarningText, LOCTEXT("BadSDK_Title", "SDK Not Setup"));
			return bClickedOK;

		}

		return true;
	}


	static bool ShouldBuildProject(UProjectPackagingSettings* PackagingSettings, const ITargetPlatform* TargetPlatform)
	{
		const UProjectPackagingSettings::FConfigurationInfo& ConfigurationInfo = UProjectPackagingSettings::ConfigurationInfo[(int)PackagingSettings->BuildConfiguration];

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
			if (FApp::GetEngineIsPromotedBuild())
			{
				FString BaseDir;

				// Get the target name
				FString TargetName;
				if (Target == nullptr)
				{
					TargetName = TEXT("UnrealGame");
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

					if (TargetPlatform->RequiresTempTarget(FTurnkeyEditorSupport::DoesProjectHaveCode(), ConfigurationInfo.Configuration, false, Reason))
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
		if (GetTargetPlatformManager()->FindTargetPlatform(IniPlatformName.ToString()) == nullptr)
		{
			return false;
		}

// 		// PrepForDebugging needs the platform to specify how
// 		if (Mode == EPrepareContentMode::PrepareForDebugging)
// 		{
// 			return FDataDrivenPlatformInfoRegistry::GetPlatformInfo(IniPlatformName).PrepareForDebuggingOptions != TEXT("");
// 		}

		return true;
	}

	static UProjectPackagingSettings* GetPackagingSettingsForPlatform(FName IniPlatformName)
	{
		FString PlatformString = IniPlatformName.ToString();
		UProjectPackagingSettings* PackagingSettings = nullptr;
		for (TObjectIterator<UProjectPackagingSettings> Itr; Itr; ++Itr)
		{
			if (Itr->GetConfigPlatform() == PlatformString)
			{
				PackagingSettings = *Itr;
				break;
			}
		}
		if (PackagingSettings == nullptr)
		{
			PackagingSettings = NewObject<UProjectPackagingSettings>(GetTransientPackage());
			PackagingSettings->LoadSettingsForPlatform(PlatformString);
		}

		return PackagingSettings;
	}

	static void CookOrPackage(FName IniPlatformName, EPrepareContentMode Mode)
	{
		const PlatformInfo::FTargetPlatformInfo* PlatformInfo = PlatformInfo::FindPlatformInfo(GetDefault<UProjectPackagingSettings>()->GetTargetPlatformForPlatform(IniPlatformName));
		
		// this is unexpected to be able to happen, but it could if there was a bad value saved in the UProjectPackagingSettings - if this trips, we should handle errors
		check(PlatformInfo != nullptr);


		// get all the helper objects
		const FString UBTPlatformString = PlatformInfo->DataDrivenPlatformInfo->UBTPlatformString;
		UProjectPackagingSettings* PackagingSettings = GetPackagingSettingsForPlatform(IniPlatformName);
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

			if (!FTurnkeyEditorSupport::CheckSupportedPlatforms(IniPlatformName))
			{
				return;
			}

			if (ITurnkeySupportModule::Get().GetSdkInfo(IniPlatformName).Status != ETurnkeyPlatformSdkStatus::Valid && ShowBadSDKDialog(IniPlatformName) == false)
			{
				return;
			}
		}


		// basic BuildCookRun params we always want
		FString BuildCookRunParams = FString::Printf(TEXT("-nop4 -utf8output %s -cook "), GetUATCompilationFlags());


		// set locations to engine and project
		{
			BuildCookRunParams += FString::Printf(TEXT(" -project=\"%s\""), *ProjectPath);
		}

		// let the editor add options (-ue4exe in particular)
		{
			BuildCookRunParams += FString::Printf(TEXT(" %s"), *FTurnkeyEditorSupport::GetUATOptions());
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
		if (FDerivedDataCacheInterface* DDC = GetDerivedDataCache())
		{
			BuildCookRunParams += FString::Printf(TEXT(" -ddc=%s"), DDC->GetGraphName());
		}
		if (FApp::IsEngineInstalled())
		{
			BuildCookRunParams += TEXT(" -installed");
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
// 		else if (Mode == EPrepareContentMode::PrepareForDebugging)
// 		{
// 			const ITargetPlatform* TargetPlatform = GetTargetPlatformManager()->FindTargetPlatform(PlatformInfo->Name);
// 			if (ShouldBuildProject(PackagingSettings, TargetPlatform))
// 			{
// 				BuildCookRunParams += TEXT(" -build");
// 			}
// 
// 			BuildCookRunParams += FString::Printf(TEXT(" %s"), *FDataDrivenPlatformInfoRegistry::GetPlatformInfo(IniPlatformName).PrepareForDebuggingOptions);
// 		}
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


		FString TurnkeyParams = FString::Printf(TEXT("-command=VerifySdk -platform=%s -UpdateIfNeeded -EditorIO -project=\"%s\""), *UBTPlatformString, *ProjectPath);

		FString CommandLine = FString::Printf(TEXT("-ScriptsForProject=\"%s\" Turnkey %s BuildCookRun %s"),
			*ProjectPath,
			*TurnkeyParams,
			*BuildCookRunParams
		);

		FTurnkeyEditorSupport::RunUAT(CommandLine, PlatformInfo->DisplayName, ContentPrepDescription, ContentPrepTaskName, ContentPrepIcon);
	}

	static bool CanExecuteCustomBuild(FName IniPlatformName, FProjectBuildSettings Build)
	{
		if (GetTargetPlatformManager()->FindTargetPlatform(IniPlatformName.ToString()) == nullptr)
		{
			return false;
		}

		return true;
	}

	static FString GetIniSetting(FString Spec)
	{
		// handle these cases:
		//  iniif:-option:Engine:/Script/Module.Class:bUseOption
		//  iniif:-option:bUseOption   [convenience for ProjectPackagingSettings setting]
		//  inivalue:Engine:/Script/Module.Class:SomeSetting
		//  inivalue:SomeSetting       [convenience for ProjectPackagingSettings setting]

		TArray<FString> Tokens;
		Spec.ParseIntoArray(Tokens, TEXT(":"));

		FString ConfigName;
		FString SectionName;
		FString Key;
		FString IniIfValue;
		if (Tokens[0] == TEXT("iniif"))
		{
			if (Tokens.Num() == 3)
			{
				ConfigName = TEXT("Game");
				SectionName = TEXT("/Script/UnrealEd.ProjectPackagingSettings");
				Key = Tokens[2];
				IniIfValue = Tokens[1];
			}
			else if (Tokens.Num() == 5)
			{
				ConfigName = Tokens[2];
				SectionName = Tokens[3];
				Key = Tokens[4];
				IniIfValue = Tokens[1];
			}
			else
			{
				UE_LOG(LogTurnkeySupport, Warning, TEXT("Found a bad iniif spec: %s"), *Spec);
				return TEXT("");
			}
		}
		else if (Tokens[0] == TEXT("inivalue"))
		{
			if (Tokens.Num() == 2)
			{
				ConfigName = TEXT("Game");
				SectionName = TEXT("/Script/UnrealEd.ProjectPackagingSettings");
				Key = Tokens[1];
			}
			else if (Tokens.Num() == 4)
			{
				ConfigName = Tokens[1];
				SectionName = Tokens[2];
				Key = Tokens[3];
			}
			else
			{
				UE_LOG(LogTurnkeySupport, Warning, TEXT("Found a bad inivalue spec: %s"), *Spec);
				return TEXT("");
			}
		}
		else
		{
			UE_LOG(LogTurnkeySupport, Warning, TEXT("Found a bad ini spec: %s"), *Spec);
			return TEXT("");
		}

		// get the value, if it exists (or empty string if not)
		FString FoundValue;
		GConfig->GetString(*SectionName, *Key, FoundValue, ConfigName);

		if (Tokens[0] == TEXT("iniif"))
		{
			if (FCString::ToBool(*FoundValue))
			{
				return IniIfValue;
			}
			return TEXT("");
		}

		return FoundValue;

	}

	// @todo since Turnkey can execute builds with ExecuteBuild, we can probably just let it do all of the work so that we don't have the commandline fixup code in two places
	static void ExecuteCustomBuild(FName IniPlatformName, FProjectBuildSettings Build)
	{
		const PlatformInfo::FTargetPlatformInfo* PlatformInfo = PlatformInfo::FindPlatformInfo(GetDefault<UProjectPackagingSettings>()->GetTargetPlatformForPlatform(IniPlatformName));

		FString CommandLine = Build.BuildCookRunParams;

		const FString ProjectPath = FPaths::IsProjectFilePathSet() ? FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()) : FPaths::RootDir() / FApp::GetProjectName() / FApp::GetProjectName() + TEXT(".uproject");
		CommandLine = CommandLine.Replace(TEXT("{Project}"), *ProjectPath);

		CommandLine = CommandLine.Replace(TEXT("{Platform}"), *IniPlatformName.ToString());

		int Offset = 0;
		while (Offset != -1)
		{
			Offset = CommandLine.Find(TEXT("{ini"), ESearchCase::IgnoreCase, ESearchDir::FromStart, Offset);
			if (Offset != -1)
			{
				// skip over the {
				Offset++;

				// look for the close brace, don't need to worry about nested braces
				int CloseBrace = CommandLine.Find(TEXT("}"), ESearchCase::IgnoreCase, ESearchDir::FromStart, Offset);
				if (CloseBrace == -1)
				{
					Offset = -1;
				}
				else
				{
					FString IniSpec = CommandLine.Mid(Offset, CloseBrace - Offset);
					FString ReplacedValue = GetIniSetting(IniSpec);
					CommandLine = CommandLine.Mid(0, Offset - 1) + ReplacedValue + CommandLine.Mid(CloseBrace + 1);
				}
			}
		}



		FTurnkeyEditorSupport::RunUAT(CommandLine, PlatformInfo->DisplayName, LOCTEXT("Turnkey_CustomTaskName", "Executing Custom Build"), LOCTEXT("Turnkey_CustomTaskName", "Custom"), FEditorStyle::GetBrush(TEXT("MainFrame.PackageProject")));
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
		ActionList->MapAction(PackagingSettings, FExecuteAction::CreateLambda([] {} ));

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



static void ShowInstallationHelp(FName IniPlatformName)
{
// 	const ITargetPlatform* Platform = GetTargetPlatformManager()->FindTargetPlatform(IniPlatformName.ToString());
// 	bool bProjectHasCode;
// 	EBuildConfiguration Configuration;
// 	bool bRequiresAssetNativization;
// 	FString TutorialPath, DocumentationPath;
// 	FText LogMessage;
// 
// 	Platform->CheckRequirements(bProjectHasCode, Configuration, bRequiresAssetNativization, TutorialPath, DocumentationPath, LogMessage);

	FTurnkeyEditorSupport::ShowInstallationHelp(IniPlatformName, FDataDrivenPlatformInfoRegistry::GetPlatformInfo(IniPlatformName).SDKTutorial);
}

static void TurnkeyInstallSdk(FString PlatformName, bool bPreferFull, bool bForceInstall, FString DeviceId)
{
	FString OptionalOptions;
	if (bPreferFull)
	{
		OptionalOptions += TEXT(" -PreferFull");
	}
	if (bForceInstall)
	{
		OptionalOptions += DeviceId.Len() > 0 ? TEXT(" -ForceDeviceInstall") : TEXT(" -ForceSdkInstall");
	}
	if (DeviceId.Len() > 0)
	{
		OptionalOptions += FString::Printf(TEXT(" -Device=%s"), *DeviceId);
	}

	const FString ProjectPath = FPaths::IsProjectFilePathSet() ? FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()) : FPaths::RootDir() / FApp::GetProjectName() / FApp::GetProjectName() + TEXT(".uproject");
	FString CommandLine = FString::Printf(TEXT("-ScriptsForProject=\"%s\" Turnkey -command=VerifySdk -UpdateIfNeeded -platform=%s %s -EditorIO -noturnkeyvariables -utf8output -WaitForUATMutex"), *ProjectPath, *PlatformName, *OptionalOptions);

	FText TaskName = LOCTEXT("InstallingSdk", "Installing Sdk");
	FTurnkeyEditorSupport::RunUAT(CommandLine, FText::FromString(PlatformName), TaskName, TaskName, FEditorStyle::GetBrush(TEXT("MainFrame.PackageProject")),
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
#if WITH_ENGINE
			RenderUtilsInit();
#endif

			FTurnkeyEditorSupport::ShowRestartToast();
		});
	}
	);
}

static TAttribute<FText> MakeSdkStatusAttribute(FName IniPlatformName, TSharedPtr< ITargetDeviceProxy> DeviceProxy)
{
	FString DisplayString = DeviceProxy ? DeviceProxy->GetName() : IniPlatformName.ToString();
	FString DeviceId = DeviceProxy ? DeviceProxy->GetTargetDeviceId(NAME_None) : FString();

	return TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateLambda([IniPlatformName, DisplayString, DeviceId]()
	{
		// get the status, or Unknown if it's not there
		ETurnkeyPlatformSdkStatus Status = DeviceId.Len() ? ITurnkeySupportModule::Get().GetSdkInfoForDeviceId(DeviceId).Status : ITurnkeySupportModule::Get().GetSdkInfo(IniPlatformName, false).Status;

		if (Status == ETurnkeyPlatformSdkStatus::Querying)
		{
			FFormatNamedArguments LabelArguments;
			LabelArguments.Add(TEXT("DisplayName"), FText::FromString(DisplayString));
			return FText::Format(LOCTEXT("SDKStatusLabel", "{DisplayName} (Querying...)"), LabelArguments);
		}
		return FText::FromString(DisplayString);
	}));
}

//static TAttribute<FSlateIcon> MakePlatformSdkIconAttribute(FName IniPlatformName, TSharedPtr< ITargetDeviceProxy> DeviceProxy)
static FSlateIcon MakePlatformSdkIconAttribute(FName IniPlatformName, TSharedPtr< ITargetDeviceProxy> DeviceProxy)
{
	FString DeviceId = DeviceProxy ? DeviceProxy->GetTargetDeviceId(NAME_None) : FString();

//	return TAttribute<FSlateIcon>::Create(TAttribute<FSlateIcon>::FGetter::CreateLambda([IniPlatformName, DeviceId]()
		{
			// get the status, or Unknown if it's not there
			ETurnkeyPlatformSdkStatus Status = DeviceId.Len() ? ITurnkeySupportModule::Get().GetSdkInfoForDeviceId(DeviceId).Status : ITurnkeySupportModule::Get().GetSdkInfo(IniPlatformName, false).Status;

			if (Status == ETurnkeyPlatformSdkStatus::OutOfDate || Status == ETurnkeyPlatformSdkStatus::NoSdk || Status == ETurnkeyPlatformSdkStatus::FlashOutOfDate)
			{
				return FSlateIcon(FEditorStyle::GetStyleSetName(), TEXT("Icons.Warning"));
			}
			else if (Status == ETurnkeyPlatformSdkStatus::Error)
			{
				return FSlateIcon(FEditorStyle::GetStyleSetName(), TEXT("Icons.Error"));
			}
			else if (Status == ETurnkeyPlatformSdkStatus::Unknown)
			{
				return FSlateIcon(FEditorStyle::GetStyleSetName(), TEXT("Icons.Help"));
			}
			else
			{
				return FSlateIcon(FEditorStyle::GetStyleSetName(), FDataDrivenPlatformInfoRegistry::GetPlatformInfo(IniPlatformName).GetIconStyleName(EPlatformIconSize::Normal));
			}
		}
//		));
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
// 
// 		MenuBuilder.AddMenuEntry(
// 			LOCTEXT("Turnkey_PrepareForDebugging", "Prepare For Debugging"),
// 			LOCTEXT("TurnkeyTooltip_PrepareForDebugging", "Prepare this project for debugging"),
// 			FSlateIcon(),
// 			FUIAction(
// 				FExecuteAction::CreateStatic(&FTurnkeySupportCallbacks::CookOrPackage, IniPlatformName, EPrepareContentMode::PrepareForDebugging),
// 				FCanExecuteAction::CreateStatic(&FTurnkeySupportCallbacks::CanCookOrPackage, IniPlatformName, EPrepareContentMode::PrepareForDebugging)
// 			)
// 		);
// 

		FString PlatformString = IniPlatformName.ToString();
		UProjectPackagingSettings* PackagingSettings = FTurnkeySupportCallbacks::GetPackagingSettingsForPlatform(IniPlatformName);

		for (FProjectBuildSettings Build : PackagingSettings->ExtraProjectBuilds)
		{
			if (Build.SpecificPlatforms.Num() == 0 || Build.SpecificPlatforms.Contains(PlatformString))
			{
				MenuBuilder.AddMenuEntry(
					FText::FromString(Build.Name),
					LOCTEXT("TurnkeyTooltip_CustomBuild", "Execute a custom build (this comes from Packaging Settings)"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateStatic(&FTurnkeySupportCallbacks::ExecuteCustomBuild, IniPlatformName, Build),
						FCanExecuteAction::CreateStatic(&FTurnkeySupportCallbacks::CanExecuteCustomBuild, IniPlatformName, Build)
					)
				);
			}
		}

		MenuBuilder.EndSection();


		MenuBuilder.BeginSection("BuildConfig", LOCTEXT("TurnkeySection_BuildConfig", "Binary Configuration"));
			EProjectType ProjectType = FTurnkeyEditorSupport::DoesProjectHaveCode() ? EProjectType::Code : EProjectType::Content;
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

		for (const TSharedPtr<ITargetDeviceProxy>& Proxy : DeviceProxies)
		{
			FString DeviceName = Proxy->GetName();
			FString DeviceId = Proxy->GetTargetDeviceId(NAME_None);
			MenuBuilder.AddSubMenu(
				MakeSdkStatusAttribute(IniPlatformName, Proxy),
				FText(),
				FNewMenuDelegate::CreateLambda([UBTPlatformString, IniPlatformName, DeviceName, DeviceId](FMenuBuilder& SubMenuBuilder)
				{
					FTurnkeySdkInfo SdkInfo = ITurnkeySupportModule::Get().GetSdkInfoForDeviceId(DeviceId);
					FFormatOrderedArguments Args = { FText::FromString(SdkInfo.InstalledVersion), FText::FromString(SdkInfo.MinAllowedVersion),FText::FromString(SdkInfo.MaxAllowedVersion) };
					SubMenuBuilder.AddWidget(
						SNew(STextBlock)
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Text(FText::Format(LOCTEXT("SdkInfo", "Manual Installed SDK: {0}\nAllowedVersions: {1}-{2}"), Args)),
						FText::GetEmpty()
					);

					SubMenuBuilder.AddMenuEntry(
						LOCTEXT("Turnkey_RepairDevice", "Repair Device as Needed"),
						LOCTEXT("TurnkeyTooltip_RepairDevice", "Perform any fixup that may be needed on this device. If up to date already, nothing will be done."),
						FSlateIcon(),
						FExecuteAction::CreateStatic(TurnkeyInstallSdk, UBTPlatformString, false, false, DeviceId)
					);

					SubMenuBuilder.AddMenuEntry(
						LOCTEXT("Turnkey_ForceRepairDevice", "Force Repair Device"),
						LOCTEXT("TurnkeyTooltip_ForceRepairDevice", "Force repairing anything on the device needed (update firmware, etc). Will perform all steps possible, even if not needed."),
						FSlateIcon(),
						FExecuteAction::CreateStatic(TurnkeyInstallSdk, UBTPlatformString, true, false, DeviceId)
					);
				}),
				false, MakePlatformSdkIconAttribute(IniPlatformName, Proxy)
			);
		}


		MenuBuilder.EndSection();
	}

	MenuBuilder.BeginSection("SdkManagement", LOCTEXT("TurnkeySection_Sdks", "Sdk Managment"));

	const FTurnkeySdkInfo& SdkInfo = ITurnkeySupportModule::Get().GetSdkInfo(IniPlatformName, true);
	FFormatOrderedArguments Args = { FText::FromString(SdkInfo.InstalledVersion), FText::FromString(SdkInfo.AutoSDKVersion),FText::FromString(SdkInfo.MinAllowedVersion),FText::FromString(SdkInfo.MaxAllowedVersion) };
	MenuBuilder.AddWidget(
		SNew(STextBlock)
		.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		.Text(FText::Format(LOCTEXT("SdkInfo", "Manual Installed SDK: {0}\nAutoSDK: {1}\nAllowedVersions: {2}-{3}"), Args)),
		FText::GetEmpty()
	);

	FString NoDevice;
	if (SdkInfo.bCanInstallFullSdk || SdkInfo.bCanInstallAutoSdk)
	{
		if (SdkInfo.Status == ETurnkeyPlatformSdkStatus::OutOfDate)
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("Turnkey_InstallSdkMinimal", "Update Sdk"),
				LOCTEXT("TurnkeyTooltip_InstallSdkMinimal", "Attempt to update an Sdk, as hosted by your studio. Will attempt to install a minimal Sdk (useful for building/running only)"),
				FSlateIcon(),
				FExecuteAction::CreateStatic(TurnkeyInstallSdk, UBTPlatformString, false, false, NoDevice)
			);

			if (SdkInfo.bCanInstallFullSdk && SdkInfo.bCanInstallAutoSdk)
			{
				MenuBuilder.AddMenuEntry(
					LOCTEXT("Turnkey_InstallSdkFull", "Update Sdk (Prefer Full)"),
					LOCTEXT("TurnkeyTooltip_InstallSdkMinimal", "Attempt to update an Sdk, as hosted by your studio. Will attempt to install a full Sdk (useful profiling or other use cases)"),
					FSlateIcon(),
					FExecuteAction::CreateStatic(TurnkeyInstallSdk, UBTPlatformString, true, false, NoDevice)
				);
			}
		}
		else if (SdkInfo.Status == ETurnkeyPlatformSdkStatus::Valid)
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
	else
	{
		// if Turnkey can't be used for this platform, then show old-school documentation
		MenuBuilder.AddMenuEntry(
			LOCTEXT("Turnkey_ShowDocumentation", "Installation Help..."),
			LOCTEXT("TurnkeyTooltip_ShowDocumentation", "Show documentation with help installing the SDK for this platform"),
			FSlateIcon(),
			FExecuteAction::CreateStatic(ShowInstallationHelp, IniPlatformName)
		);
	}
}


// Launch On

bool CanLaunchOnDevice(const FString& DeviceName)
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

	return false;
}

static void LaunchOnDevice(const FString& DeviceId, const FString& DeviceName, bool bUseTurnkey)
{
	FTurnkeyEditorSupport::LaunchRunningMap(DeviceId, DeviceName, bUseTurnkey);
}

static void PrepareLaunchOn(FString DeviceId, FString DeviceName)
{
	FTurnkeyEditorSupport::PrepareToLaunchRunningMap(DeviceId, DeviceName);
}

static void HandleLaunchOnDeviceActionExecute(FString DeviceId, FString DeviceName, bool bUseTurnkey)
{
	PrepareLaunchOn(DeviceId, DeviceName);
	LaunchOnDevice(DeviceId, DeviceName, bUseTurnkey);
}


static bool HandleLaunchOnDeviceActionCanExecute(FString DeviceName)
{
	return CanLaunchOnDevice(DeviceName);
}

static void GenerateDeviceProxyMenuParams(TSharedPtr<ITargetDeviceProxy> DeviceProxy, FName PlatformName, FUIAction& OutAction, FText& OutTooltip, FOnQuickLaunchSelected ExternalOnClickDelegate)
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
		FExecuteAction::CreateLambda([DeviceProxy, ExternalOnClickDelegate]()
			{
				FString DeviceId = DeviceProxy->GetTargetDeviceId(NAME_None);
				HandleLaunchOnDeviceActionExecute(DeviceId, DeviceProxy->GetName(), true);
				ExternalOnClickDelegate.ExecuteIfBound(DeviceId);
			}
	//		, FCanExecuteAction::CreateStatic(&HandleLaunchOnDeviceActionCanExecute, DeviceProxy->GetName())
	));

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


void FTurnkeySupportModule::MakeQuickLaunchItems(class UToolMenu* Menu, FOnQuickLaunchSelected ExternalOnClickDelegate) const
{
	FToolMenuSection& MenuSection = Menu->AddSection("QuickLaunchDevices", LOCTEXT("QuickLaunch", "Quick Launch"));

	MenuSection.AddDynamicEntry("PlatformsMenu", FNewToolMenuSectionDelegate::CreateLambda([ExternalOnClickDelegate](FToolMenuSection& DynamicSection)
	{
		TArray<FString> DeviceIdsToQuery;
		ITargetDeviceServicesModule* TargetDeviceServicesModule = static_cast<ITargetDeviceServicesModule*>(FModuleManager::Get().LoadModule(TEXT("TargetDeviceServices")));
		for (const auto& Pair : FDataDrivenPlatformInfoRegistry::GetAllPlatformInfos())
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
				GenerateDeviceProxyMenuParams(DeviceProxies[0], PlatformName, Action, Tooltip, ExternalOnClickDelegate);

				if (DeviceProxies.Num() == 1)
				{
					UE_LOG(LogTurnkeySupport, Display, TEXT("Adding device menu item for %s"), *DeviceProxies[0]->GetName());
					DynamicSection.AddMenuEntry(
						NAME_None,
						MakeSdkStatusAttribute(PlatformName, DeviceProxies[0]),
						Tooltip,
						MakePlatformSdkIconAttribute(PlatformName, DeviceProxies[0]),
						Action
					);
				}
				else
				{
					DynamicSection.AddSubMenu(
						NAME_None,
						MakeSdkStatusAttribute(PlatformName, DeviceProxies[0]),
						Tooltip,
						FNewMenuDelegate::CreateLambda([TargetDeviceServicesModule, PlatformName, &ExternalOnClickDelegate](FMenuBuilder& SubMenuBuilder)
							{
								// re-get the proxies, just in case they changed
								TArray<TSharedPtr<ITargetDeviceProxy>> DeviceProxies;
								TargetDeviceServicesModule->GetDeviceProxyManager()->GetAllProxies(PlatformName, DeviceProxies);
								// for each one, put an entry (even the one that was in the outer menu, for less confusion)
								for (const TSharedPtr<ITargetDeviceProxy>& Proxy : DeviceProxies)
								{
									FUIAction SubAction;
									FText SubTooltip;
									GenerateDeviceProxyMenuParams(Proxy, PlatformName, SubAction, SubTooltip, ExternalOnClickDelegate);
									SubMenuBuilder.AddMenuEntry(
										MakeSdkStatusAttribute(PlatformName, Proxy),
										SubTooltip,
										MakePlatformSdkIconAttribute(PlatformName, Proxy),
										SubAction,
										NAME_None,
										EUserInterfaceActionType::Button
									);
								}
							}),
						Action,
						EUserInterfaceActionType::Check,
						false,
						MakePlatformSdkIconAttribute(PlatformName, nullptr),
						true
						);
				}

				ITurnkeySupportModule& TurnkeySupport = ITurnkeySupportModule::Get();
				// gath	er any unknown status devices to query at the end
				for (const TSharedPtr<ITargetDeviceProxy>& Proxy : DeviceProxies)
				{
					FString DeviceId = Proxy->GetTargetDeviceId(NAME_None);
					if (TurnkeySupport.GetSdkInfoForDeviceId(DeviceId).Status == ETurnkeyPlatformSdkStatus::Unknown)
					{
						DeviceIdsToQuery.Add(DeviceId);
					}
				}
			}
		}

		// if we don't have an external delegate to call, then this is the internally included items in the Platforms menu and we can add the extra option(s)
		if (!ExternalOnClickDelegate.IsBound())
		{
			DynamicSection.AddMenuEntry(
				NAME_None,
				LOCTEXT("CookOnTheFlyOnLaunch", "Enable cooking on the fly"),
				LOCTEXT("CookOnTheFlyOnLaunchDescription", "Cook on the fly instead of cooking upfront when launching"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateStatic(&FTurnkeySupportCallbacks::SetCookOnTheFly),
					FCanExecuteAction::CreateStatic(&FTurnkeySupportCallbacks::CanSetCookOnTheFly),
					FIsActionChecked::CreateStatic(&FTurnkeySupportCallbacks::SetCookOnTheFlyIsChecked)
				),
				EUserInterfaceActionType::ToggleButton
			);
		}


		// now kick-off any devices that need to be updated
		if (DeviceIdsToQuery.Num() > 0)
		{
			ITurnkeySupportModule::Get().UpdateSdkInfoForDevices(DeviceIdsToQuery);
		}
	}
	));
}

TSharedRef<SWidget> FTurnkeySupportModule::MakeTurnkeyMenuWidget() const
{
	FTurnkeySupportCommands::Register();
	const FTurnkeySupportCommands& Commands = FTurnkeySupportCommands::Get();

	const bool bShouldCloseWindowAfterMenuSelection = true;
//	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, FTurnkeySupportCommands::ActionList);

	static const FName MenuName("UnrealEd.PlayWorldCommands.PlatformsMenu");

	if (!UToolMenus::Get()->IsMenuRegistered(MenuName))
	{
		UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(MenuName);

		FOnQuickLaunchSelected EmptyFunc;
		MakeQuickLaunchItems(Menu, EmptyFunc);

		// need to make this dyamic so icons, etc can update with SDK 
		// shared devices section

		FToolMenuSection& ManagePlatformsSection = Menu->AddSection("AllPlatforms", LOCTEXT("TurnkeyMenu_ManagePlatforms", "Content/Sdk/Device Management"));
		ManagePlatformsSection.AddDynamicEntry(NAME_None, FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& PlatformsSection)
		{
			ITargetDeviceServicesModule* TargetDeviceServicesModule = static_cast<ITargetDeviceServicesModule*>(FModuleManager::Get().LoadModule(TEXT("TargetDeviceServices")));

			TMap<FName, const FDataDrivenPlatformInfo*> UncompiledPlatforms;
			TMap<FName, const FDataDrivenPlatformInfo*> UnsupportedPlatforms;

			FProjectStatus ProjectStatus;
			bool bProjectStatusIsValid = IProjectManager::Get().QueryStatusForCurrentProject(ProjectStatus);

			for (const auto& Pair : FDataDrivenPlatformInfoRegistry::GetAllPlatformInfos())
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

				if (bProjectStatusIsValid && !ProjectStatus.IsTargetPlatformSupported(PlatformName))
				{
					UnsupportedPlatforms.Add(PlatformName, &Info);
					continue;
				}

				PlatformsSection.AddSubMenu(
					NAME_None,
					MakeSdkStatusAttribute(PlatformName, nullptr),
					FText::FromString(PlatformName.ToString()),
					FNewMenuDelegate::CreateStatic(&MakeTurnkeyPlatformMenu, PlatformName, TargetDeviceServicesModule),
					false,
					MakePlatformSdkIconAttribute(PlatformName, nullptr),
					true
				);
			}

			if (UnsupportedPlatforms.Num() != 0)
			{
				PlatformsSection.AddSeparator(NAME_None);

				PlatformsSection.AddSubMenu(
					NAME_None,
					LOCTEXT("Turnkey_UnsupportedPlatforms", "Platforms Not Supported by Project"),
					LOCTEXT("Turnkey_UnsupportedPlatformsToolTip", "List of platforms that are not marked as supported by this platform. Use the \"Supported Platforms...\""),
					FNewMenuDelegate::CreateLambda([UnsupportedPlatforms, TargetDeviceServicesModule](FMenuBuilder& SubMenuBuilder)
						{
							for (const auto It : UnsupportedPlatforms)
							{
								SubMenuBuilder.AddSubMenu(
									MakeSdkStatusAttribute(It.Key, nullptr),
									FText::FromString(It.Key.ToString()),
									FNewMenuDelegate::CreateStatic(&MakeTurnkeyPlatformMenu, It.Key, TargetDeviceServicesModule),
									false,
									MakePlatformSdkIconAttribute(It.Key, nullptr),
									true
								);
							}
						})
				);
			}

			if (UncompiledPlatforms.Num() != 0)
			{
				PlatformsSection.AddSeparator(NAME_None);

				PlatformsSection.AddSubMenu(
					NAME_None,
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
									MakePlatformSdkIconAttribute(It.Key, nullptr),
									true
								);
							}
						})
				);
			}
		}));


		// options section
		FToolMenuSection& OptionsSection = Menu->AddSection("TurnkeyOptions", LOCTEXT("TurnkeySection_Options", "Options and Settings"));
		{
			OptionsSection.AddMenuEntry(
				NAME_None,
				LOCTEXT("OpenProjectLauncher", "Project Launcher..."),
				LOCTEXT("OpenProjectLauncher_ToolTip", "Open the Project Launcher for advanced packaging, deploying and launching of your projects"),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "Launcher.TabIcon"),
				FUIAction(FExecuteAction::CreateStatic(&FTurnkeySupportCallbacks::OpenProjectLauncher))
			);

			OptionsSection.AddMenuEntry(
				NAME_None,
				LOCTEXT("OpenDeviceManager", "Device Manager..."),
				LOCTEXT("OpenDeviceManager_ToolTip", "View and manage connected devices."),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "DeviceDetails.TabIcon"),
				FUIAction(FExecuteAction::CreateStatic(&FTurnkeySupportCallbacks::OpenDeviceManager))
			);

			FTurnkeyEditorSupport::AddEditorOptions(OptionsSection);
		}
	}

	FToolMenuContext MenuContext(FTurnkeySupportCommands::ActionList);
	return UToolMenus::Get()->GenerateWidget(MenuName, MenuContext);

}

void FTurnkeySupportModule::MakeTurnkeyMenu(FToolMenuSection& MenuSection) const
{
	// make sure the DeviceProxyManager is going _before_ we create the menu contents dynamically, so that devices will show up
	ITargetDeviceServicesModule* TargetDeviceServicesModule = static_cast<ITargetDeviceServicesModule*>(FModuleManager::Get().LoadModule(TEXT("TargetDeviceServices")));
	TargetDeviceServicesModule->GetDeviceProxyManager();

	// hide during PIE
	FUIAction PlatformMenuShownDelegate;
	PlatformMenuShownDelegate.IsActionVisibleDelegate = FIsActionButtonVisible::CreateLambda([]() 
	{
		return !FTurnkeyEditorSupport::IsPIERunning();
	});

	FToolMenuEntry Entry = FToolMenuEntry::InitComboButton(
		"PlatformsMenu",
		PlatformMenuShownDelegate,
		FOnGetContent::CreateLambda([this] { return MakeTurnkeyMenuWidget(); }),
		LOCTEXT("PlatformMenu", "Platforms"),
		LOCTEXT("PlatformMenu_Tooltip", "Platform related actions and settings (Launching, Packaging, custom builds, etc)"),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "PlayWorld.RepeatLastLaunch"), // not great name for a good "platforms" icon
		false,
		"PlatformsMenu");
	Entry.StyleNameOverride = "CalloutToolbar";

	MenuSection.AddEntry(Entry);
}




// some shared functionality
static void PrepForTurnkeyReport(FString& Command, FString& BaseCommandline, FString& ReportFilename)
{
	static int ReportIndex = 0;

	FString LogFilename = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectIntermediateDir(), *FString::Printf(TEXT("TurnkeyLog_%d.log"), ReportIndex)));
	ReportFilename = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectIntermediateDir(), *FString::Printf(TEXT("TurnkeyReport_%d.log"), ReportIndex++)));

	// make sure intermediate directory exists
	IFileManager::Get().MakeDirectory(*FPaths::ProjectIntermediateDir());

	Command = TEXT("{EngineDir}Build/BatchFiles/RunuAT");
	//	Command = TEXT("{EngineDir}/Binaries/DotNET/AutomationTool.exe");

	const FString ProjectPath = FPaths::IsProjectFilePathSet() ? FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()) : FPaths::RootDir() / FApp::GetProjectName() / FApp::GetProjectName() + TEXT(".uproject");
	BaseCommandline = FString::Printf(TEXT("-ScriptsForProject=\"%s\" Turnkey -utf8output -WaitForUATMutex -command=VerifySdk -ReportFilename=\"%s\" -log=\"%s\""), *ProjectPath, *ReportFilename, *LogFilename);

	// convert into appropriate calls for the current platform
	FPlatformProcess::ModifyCreateProcParams(Command, BaseCommandline, FGenericPlatformProcess::ECreateProcHelperFlags::AppendPlatformScriptExtension | FGenericPlatformProcess::ECreateProcHelperFlags::RunThroughShell);
}

bool GetSdkInfoFromTurnkey(FString Line, FName& PlatformName, FString& DeviceId, FTurnkeySdkInfo& SdkInfo)
{
	int32 Colon = Line.Find(TEXT(": "));

	if (Colon < 0)
	{
		return false;
	}

	// break up the string
	FString PlatformString = Line.Mid(0, Colon);
	FString Info = Line.Mid(Colon + 2);

	int32 AtSign = PlatformString.Find(TEXT("@"));
	if (AtSign > 0)
	{
		// return the platform@name as the deviceId, then remove the @name part for the platform
		DeviceId = ConvertToDDPIDeviceId(PlatformString);
		PlatformString = PlatformString.Mid(0, AtSign);
	}

	// get the DDPI name
	PlatformName = FName(*ConvertToDDPIPlatform(PlatformString));

	// parse out the results from the (key=val, key=val) result from turnkey
	FString StatusString;
	FString FlagsString;
	FParse::Value(*Info, TEXT("Status="), StatusString);
	FParse::Value(*Info, TEXT("Flags="), FlagsString);
	FParse::Value(*Info, TEXT("Installed="), SdkInfo.InstalledVersion);
	FParse::Value(*Info, TEXT("AutoSDK="), SdkInfo.AutoSDKVersion);
	FParse::Value(*Info, TEXT("MinAllowed="), SdkInfo.MinAllowedVersion);
	FParse::Value(*Info, TEXT("MaxAllowed="), SdkInfo.MaxAllowedVersion);

	SdkInfo.Status = ETurnkeyPlatformSdkStatus::Unknown;
	if (StatusString == TEXT("Valid"))
	{
		SdkInfo.Status = ETurnkeyPlatformSdkStatus::Valid;
	}
	else
	{
		if (FlagsString.Contains(TEXT("AutoSdk_InvalidVersionExists")) || FlagsString.Contains(TEXT("InstalledSdk_InvalidVersionExists")))
		{
			SdkInfo.Status = ETurnkeyPlatformSdkStatus::OutOfDate;
		}
		else
		{
			SdkInfo.Status = ETurnkeyPlatformSdkStatus::NoSdk;
		}
	}
	SdkInfo.bCanInstallFullSdk = FlagsString.Contains(TEXT("Support_FullSdk"));
	SdkInfo.bCanInstallAutoSdk = FlagsString.Contains(TEXT("Support_AutoSdk"));

	return true;
}

// FDataDrivenPlatformInfo& DeviceIdToInfo(FString DeviceId, FString* OutDeviceName)
// {
// 	TArray<FString> PlatformAndDevice;
// 	DeviceId.ParseIntoArray(PlatformAndDevice, TEXT("@"), true);
// 
// 	if (OutDeviceName)
// 	{
// 		*OutDeviceName = PlatformAndDevice[1];
// 	}
// 
// 	FString DDPIPlatformName = ConvertToDDPIPlatform(PlatformAndDevice[0]);
// 
// 	checkf(DataDrivenPlatforms.Contains(*DDPIPlatformName), TEXT("DataDrivenPlatforms map did not contain the DDPI Platform %s"), *DDPIPlatformName);
// 
// 	// have to convert back to Windows from Win64
// 	return DataDrivenPlatforms[*DDPIPlatformName];
// 
// }


void FTurnkeySupportModule::UpdateSdkInfo()
{
	// make sure all known platforms are in the map
	if (PerPlatformSdkInfo.Num() == 0)
	{
		for (auto& It : FDataDrivenPlatformInfoRegistry::GetAllPlatformInfos())
		{
			PerPlatformSdkInfo.Add(It.Key, FTurnkeySdkInfo());
		}
	}

	// don't run UAT from commandlets (like the cooker) that are often launched from UAT and this will go poorly
	if (IsRunningCommandlet())
	{
		return;
	}


	FString Command, BaseCommandline, ReportFilename;
	PrepForTurnkeyReport(Command, BaseCommandline, ReportFilename);
	// get status for all platforms
	FString Commandline = BaseCommandline + TEXT(" -platform=all");

	UE_LOG(LogTurnkeySupport, Log, TEXT("Running Turnkey SDK detection: '%s %s'"), *Command, *Commandline);

	{
		FScopeLock Lock(&GTurnkeySection);

		// reset status to unknown
		for (auto& It : PerPlatformSdkInfo)
		{
			It.Value.Status = ETurnkeyPlatformSdkStatus::Querying;
		}

		// reset the per-device status when querying general Sdk status
		ClearDeviceStatus();
	}

	FMonitoredProcess* TurnkeyProcess = new FMonitoredProcess(Command, Commandline, true, false);
	TurnkeyProcess->OnCompleted().BindLambda([this, ReportFilename, TurnkeyProcess](int32 ExitCode)
	{
		AsyncTask(ENamedThreads::GameThread, [this, ReportFilename, TurnkeyProcess, ExitCode]()
		{
			FScopeLock Lock(&GTurnkeySection);

			if (ExitCode == 0 || ExitCode == 10)
			{
				TArray<FString> Contents;
				if (FFileHelper::LoadFileToStringArray(Contents, *ReportFilename))
				{
					for (FString& Line : Contents)
					{
 						UE_LOG(LogTurnkeySupport, Log, TEXT("Turnkey Platform: %s"), *Line);

						// parse a Turnkey line
						FName PlatformName;
						FString Unused;
						FTurnkeySdkInfo SdkInfo;
						if (GetSdkInfoFromTurnkey(Line, PlatformName, Unused, SdkInfo) == false)
						{
							continue;
						}

						// we received a platform from UAT that we don't know about in the editor. this can happen if you have a UBT/UAT that was compiled with platform access
						// but then you are running without that platform synced. skip this platform and move on
						if (!PerPlatformSdkInfo.Contains(PlatformName))
						{
							UE_LOG(LogTurnkeySupport, Log, TEXT("Received platform %s from Turnkey, but the engine doesn't know about it. Skipping..."), *PlatformName.ToString());
							continue;
						}

						// check if we had already set a ManualSDK - and don't set it again. Because of the way AutoSDKs are activated in the editor after the first call to Turnkey,
						// future calls to Turnkey will inherit the AutoSDK env vars, and it won't be able to determine the manual SDK versions anymore. If we use the editor to
						// install an SDK via Turnkey, it will directly update the installed version based on the result of that command, not this Update operation

						FString OriginalManualInstallValue = PerPlatformSdkInfo[PlatformName].InstalledVersion;

						// set it into the platform
						PerPlatformSdkInfo[PlatformName] = SdkInfo;

						// restore the original installed version if it set after the first time
						if (OriginalManualInstallValue.Len() > 0)
						{
							PerPlatformSdkInfo[PlatformName].InstalledVersion = OriginalManualInstallValue;
						}


// 						UE_LOG(LogTurnkeySupport, Log, TEXT("[TEST] Turnkey Platform: %s - %d, Installed: %s, AudoSDK: %s, Allowed: %s-%s"), *PlatformName.ToString(), (int)SdkInfo.Status, *SdkInfo.InstalledVersion,
// 							*SdkInfo.AutoSDKVersion, *SdkInfo.MinAllowedVersion, *SdkInfo.MaxAllowedVersion);
					}
				}
			}
			else
			{
				for (auto& It : PerPlatformSdkInfo)
				{
					It.Value.Status = ETurnkeyPlatformSdkStatus::Error;
					It.Value.SdkErrorInformation = FText::Format(NSLOCTEXT("Turnkey", "TurnkeyError_ReturnedError", "Turnkey returned an error, code {0}"), { ExitCode });

					// @todo turnkey error description!
				}
			}


			for (auto& It : PerPlatformSdkInfo)
			{
				if (It.Value.Status == ETurnkeyPlatformSdkStatus::Querying)
				{
					// fake platforms won't come back, just skip it
					if (FDataDrivenPlatformInfoRegistry::GetPlatformInfo(It.Key).bIsFakePlatform)
					{
						It.Value.Status = ETurnkeyPlatformSdkStatus::Unknown;
					}
					else
					{
						It.Value.Status = ETurnkeyPlatformSdkStatus::Error;
						It.Value.SdkErrorInformation = NSLOCTEXT("Turnkey", "TurnkeyError_NotReturned", "The platform's Sdk status was not returned from Turnkey");
					}
				}
			}

			// cleanup
			delete TurnkeyProcess;
			IFileManager::Get().Delete(*ReportFilename);
		});
	});

	// run it
	TurnkeyProcess->Launch();
}

void FTurnkeySupportModule::UpdateSdkInfoForDevices(TArray<FString> PlatformDeviceIds)
{
	FString Command, BaseCommandline, ReportFilename;
	PrepForTurnkeyReport(Command, BaseCommandline, ReportFilename);

	// the platform part of the Id may need to be converted to be turnkey (ie UBT) proper

	FString Commandline = BaseCommandline + FString(TEXT(" -Device=")) + FString::JoinBy(PlatformDeviceIds, TEXT("+"), [](FString Id) { return ConvertToUATDeviceId(Id); });

	UE_LOG(LogTurnkeySupport, Log, TEXT("Running Turnkey SDK detection: '%s %s'"), *Command, *Commandline);

	{
		FScopeLock Lock(&GTurnkeySection);

		// set status to querying
		FTurnkeySdkInfo DefaultInfo;
		DefaultInfo.Status = ETurnkeyPlatformSdkStatus::Querying;
		for (const FString& Id : PlatformDeviceIds)
		{
			PerDeviceSdkInfo.Add(ConvertToDDPIDeviceId(Id), DefaultInfo);
		}
	}

	FMonitoredProcess* TurnkeyProcess = new FMonitoredProcess(Command, Commandline, true, false);
	TurnkeyProcess->OnCompleted().BindLambda([this, ReportFilename, TurnkeyProcess, PlatformDeviceIds](int32 ExitCode)
	{
		AsyncTask(ENamedThreads::GameThread, [this, ReportFilename, TurnkeyProcess, PlatformDeviceIds, ExitCode]()
		{
			FScopeLock Lock(&GTurnkeySection);

			if (ExitCode == 0 || ExitCode == 10)
			{
				TArray<FString> Contents;
				if (FFileHelper::LoadFileToStringArray(Contents, *ReportFilename))
				{
					for (FString& Line : Contents)
					{
						FName PlatformName;
						FString DDPIDeviceId;
						FTurnkeySdkInfo SdkInfo;
						if (GetSdkInfoFromTurnkey(Line, PlatformName, DDPIDeviceId, SdkInfo) == false)
						{
							continue;
						}

						// skip over non-device lines
						if (DDPIDeviceId.Len() == 0)
						{
							continue;
						}

						// we received a device from UAT that we don't know about in the editor. this should never happen since we pass a list of devices to Turnkey, 
						// so this is a logic error
						if (!PerDeviceSdkInfo.Contains(DDPIDeviceId))
						{
							UE_LOG(LogTurnkeySupport, Error, TEXT("Received DeviceId %s from Turnkey, but the engine doesn't know about it."), *DDPIDeviceId);
						}

						UE_LOG(LogTurnkeySupport, Log, TEXT("Turnkey Device: %s"), *Line);

						PerDeviceSdkInfo[DDPIDeviceId] = SdkInfo;

						UE_LOG(LogTurnkeySupport, Log, TEXT("[TEST] Turnkey Device: %s - %d, Installed: %s, Allowed: %s-%s"), *DDPIDeviceId, (int)SdkInfo.Status, *SdkInfo.InstalledVersion,
							*SdkInfo.MinAllowedVersion, *SdkInfo.MaxAllowedVersion);
					}
				}
			}

			for (const FString& Id : PlatformDeviceIds)
			{
				FTurnkeySdkInfo& SdkInfo = PerDeviceSdkInfo[ConvertToDDPIDeviceId(Id)];
				if (SdkInfo.Status == ETurnkeyPlatformSdkStatus::Querying)
				{
					SdkInfo.Status = ETurnkeyPlatformSdkStatus::Error;
					SdkInfo.SdkErrorInformation = NSLOCTEXT("Turnkey", "TurnkeyError_DeviceNotReturned", "A device's Sdk status was not returned from Turnkey");
				}
			}

			// cleanup
			delete TurnkeyProcess;
			IFileManager::Get().Delete(*ReportFilename);
		});
	});

	// run it
	TurnkeyProcess->Launch();
}

/**
 * Runs Turnkey to get the Sdk information for all known platforms
 */
void FTurnkeySupportModule::RepeatQuickLaunch(FString DeviceId)
{
	UE_LOG(LogTurnkeySupport, Display, TEXT("Launching on %s"), *DeviceId);

	ITargetDeviceServicesModule* TargetDeviceServicesModule = static_cast<ITargetDeviceServicesModule*>(FModuleManager::Get().LoadModule(TEXT("TargetDeviceServices")));
	TSharedPtr<ITargetDeviceProxy> Proxy = TargetDeviceServicesModule->GetDeviceProxyManager()->FindProxyDeviceForTargetDevice(DeviceId);

	if (Proxy.IsValid())
	{
		HandleLaunchOnDeviceActionExecute(DeviceId, Proxy->GetName(), true);
	}
	else
	{
		// @todo show error toast
	}
}


FTurnkeySdkInfo FTurnkeySupportModule::GetSdkInfo(FName PlatformName, bool bBlockIfQuerying) const
{
	FScopeLock Lock(&GTurnkeySection);

	// return the status, or Unknown info if not known
	return PerPlatformSdkInfo.FindRef(ConvertToDDPIPlatform(PlatformName));
}

FTurnkeySdkInfo FTurnkeySupportModule::GetSdkInfoForDeviceId(const FString& DeviceId) const
{
	FScopeLock Lock(&GTurnkeySection);

	// return the status, or Unknown info if not known
	return PerDeviceSdkInfo.FindRef(ConvertToDDPIDeviceId(DeviceId));
}

void FTurnkeySupportModule::ClearDeviceStatus(FName PlatformName)
{
	FScopeLock Lock(&GTurnkeySection);

	FString Prefix = ConvertToDDPIPlatform(PlatformName.ToString()) + "@";
	for (auto& Pair : PerDeviceSdkInfo)
	{
		if (PlatformName == NAME_None || Pair.Key.StartsWith(Prefix))
		{
			Pair.Value.Status = ETurnkeyPlatformSdkStatus::Unknown;
		}
	}
}



void FTurnkeySupportModule::StartupModule( )
{
	

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

if (/*PlatformInfo->DataDrivenPlatformInfo->GetSdkStatus() != ETurnkeyPlatformSdkStatus::Valid ||*/ (bProjectHasCode && PlatformInfo->DataDrivenPlatformInfo->bUsesHostCompiler && !FSourceCodeNavigation::IsCompilerAvailable()))
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

// Note: Blueprint nativization has been removed as a feature.
const bool bAssetNativizationEnabled = false;

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



void FInternalPlayWorldCommandCallbacks::HandleShowSDKTutorial(FString PlatformName, FString NotInstalledDocLink)
{
	// broadcast this, and assume someone will pick it up
	IMainFrameModule& MainFrameModule = FModuleManager::GetModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
	MainFrameModule.BroadcastMainFrameSDKNotInstalled(PlatformName, NotInstalledDocLink);
}



#endif


// another round of junk, some duplicates with above
#if 0



/////// Will move these into TurnkeySupportModule soon:



bool FInternalPlayWorldCommandCallbacks::CanLaunchOnDevice(const FString& DeviceName)
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


void FInternalPlayWorldCommandCallbacks::LaunchOnDevice(const FString& DeviceId, const FString& DeviceName, bool bUseTurnkey)
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


void FInternalPlayWorldCommandCallbacks::RepeatLastLaunch_Clicked()
{
	const ULevelEditorPlaySettings* PlaySettings = GetDefault<ULevelEditorPlaySettings>();

	switch (PlaySettings->LastExecutedLaunchModeType)
	{
	case LaunchMode_OnDevice:
		if (FParse::Param(FCommandLine::Get(), TEXT("OldLaunchOn")))
		{
			if (IsReadyToLaunchOnDevice(PlaySettings->LastExecutedLaunchDevice))
			{
				LaunchOnDevice(PlaySettings->LastExecutedLaunchDevice, PlaySettings->LastExecutedLaunchName, false);
			}
		}
		else
		{
			// @todo turnkey - remove this function completely and all the platform support, and put it into Turnkey
//			if (IsReadyToLaunchOnDevice(PlaySettings->LastExecutedLaunchDevice))
			{
				LaunchOnDevice(PlaySettings->LastExecutedLaunchDevice, PlaySettings->LastExecutedLaunchName, true);
			}
		}


		break;

	default:
		break;
	}
}


bool FInternalPlayWorldCommandCallbacks::RepeatLastLaunch_CanExecute()
{
	const ULevelEditorPlaySettings* PlaySettings = GetDefault<ULevelEditorPlaySettings>();

	switch (PlaySettings->LastExecutedLaunchModeType)
	{
	case LaunchMode_OnDevice:
		return CanLaunchOnDevice(PlaySettings->LastExecutedLaunchName);

	default:
		return false;
	}
}


FText FInternalPlayWorldCommandCallbacks::GetRepeatLastLaunchToolTip()
{
	const ULevelEditorPlaySettings* PlaySettings = GetDefault<ULevelEditorPlaySettings>();

	switch (PlaySettings->LastExecutedLaunchModeType)
	{
	case LaunchMode_OnDevice:
		if (CanLaunchOnDevice(PlaySettings->LastExecutedLaunchName))
		{
			FFormatNamedArguments Arguments;
			Arguments.Add(TEXT("DeviceName"), FText::FromString(PlaySettings->LastExecutedLaunchName));

			return FText::Format(LOCTEXT("RepeatLaunchTooltip", "Launch this level on {DeviceName}"), Arguments);
		}

		break;

	default:
		break;
	}

	return LOCTEXT("RepeatLaunchSelectOptionToolTip", "Select a play-on target from the combo menu");
}


FSlateIcon FInternalPlayWorldCommandCallbacks::GetRepeatLastLaunchIcon()
{
	const ULevelEditorPlaySettings* PlaySettings = GetDefault<ULevelEditorPlaySettings>();

	// @todo gmp: add play mode specific icons
	switch (PlaySettings->LastExecutedLaunchModeType)
	{
	case LaunchMode_OnDevice:
		break;
	}

	static FName RepeatLastLaunchIcon("PlayWorld.RepeatLastLaunch");

	return FSlateIcon(FEditorStyle::GetStyleSetName(), RepeatLastLaunchIcon);
}

bool FInternalPlayWorldCommandCallbacks::IsReadyToLaunchOnDevice(FString DeviceId)
{
	int32 Index = 0;
	DeviceId.FindChar(TEXT('@'), Index);
	FName PlatformName = FName(*DeviceId.Left(Index));

	const PlatformInfo::FTargetPlatformInfo* PlatformInfo = PlatformInfo::FindPlatformInfo(PlatformName);
	checkf(PlatformInfo, TEXT("Unable to find PlatformInfo for %s"), *PlatformName.ToString());

	FGameProjectGenerationModule& GameProjectModule = FModuleManager::LoadModuleChecked<FGameProjectGenerationModule>(TEXT("GameProjectGeneration"));
	bool bHasCode = GameProjectModule.Get().ProjectHasCodeFiles();

	if (ITurnkeySupportModule::Get().GetSdkInfo(PlatformName).Status != ETurnkeyPlatformSdkStatus::Valid)
	{
		IMainFrameModule& MainFrameModule = FModuleManager::GetModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
		MainFrameModule.BroadcastMainFrameSDKNotInstalled(PlatformInfo->Name.ToString(), PlatformInfo->DataDrivenPlatformInfo->SDKTutorial);
		TArray<FAnalyticsEventAttribute> ParamArray;
		ParamArray.Add(FAnalyticsEventAttribute(TEXT("Time"), 0.0));
		FEditorAnalytics::ReportEvent(TEXT("Editor.LaunchOn.Failed"), PlatformInfo->Name.ToString(), bHasCode, EAnalyticsErrorCodes::SDKNotFound, ParamArray);
		return false;
	}

	const ITargetPlatform* Platform = GetTargetPlatformManager()->FindTargetPlatform(PlatformName);
	if (Platform)
	{
		FString NotInstalledTutorialLink;
		FString DocumentationLink;
		FText CustomizedLogMessage;

		EBuildConfiguration BuildConfiguration = GetDefault<ULevelEditorPlaySettings>()->GetLaunchBuildConfiguration();
		bool bEnableAssetNativization = false;
		int32 Result = Platform->CheckRequirements(bHasCode, BuildConfiguration, bEnableAssetNativization, NotInstalledTutorialLink, DocumentationLink, CustomizedLogMessage);

		// report to analytics
		FEditorAnalytics::ReportBuildRequirementsFailure(TEXT("Editor.LaunchOn.Failed"), PlatformName.ToString(), bHasCode, Result);

		// report to message log
		bool UnrecoverableError = false;

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
			&& (bHasCode || (Result & ETargetPlatformReadyStatus::CodeBuildRequired)
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

		if (UnrecoverableError)
		{
			return false;
		}

		// report to main frame
		if ((Result & ETargetPlatformReadyStatus::CodeUnsupported) != 0)
		{
			// show the message
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NotSupported_CodeBased", "Sorry, launching a code-based project for the selected platform is currently not supported. This feature may be available in a future release."));
			return false;
		}
		if ((Result & ETargetPlatformReadyStatus::PluginsUnsupported) != 0)
		{
			// show the message
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NotSupported_Plugins", "Sorry, launching a project with third-party plugins is currently not supported for the selected platform. This feature may be available in a future release."));
			return false;
		}
	}
	else
	{
		IMainFrameModule& MainFrameModule = FModuleManager::GetModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
		MainFrameModule.BroadcastMainFrameSDKNotInstalled(PlatformInfo->Name.ToString(), PlatformInfo->DataDrivenPlatformInfo->SDKTutorial);
		return false;
	}

	return true;
}



#endif

















IMPLEMENT_MODULE(FTurnkeySupportModule, TurnkeySupport);
