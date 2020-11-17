// Copyright Epic Games, Inc. All Rights Reserved.

#include "SwitchboardMenuEntry.h"

#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "LevelEditor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "SwitchboardEditorSettings.h"
#include "SwitchboardEditorStyle.h"

#define LOCTEXT_NAMESPACE "SwitchboardEditor"

DEFINE_LOG_CATEGORY(LogSwitchboardPlugin);


class FSwitchboardUICommands : public TCommands<FSwitchboardUICommands>
{
public:
	FSwitchboardUICommands()
		: TCommands<FSwitchboardUICommands>("Switchboard", LOCTEXT("SwitchboardCommands", "Switchboard"), NAME_None, FSwitchboardEditorStyle::Get().GetStyleSetName())
	{}

	virtual void RegisterCommands() override
	{
		UI_COMMAND(TriggerToolbarButtonCmd, "Launch Switchboard", "Launch Switchboard", EUserInterfaceActionType::Button, FInputChord());
	}

	TSharedPtr<FUICommandInfo> TriggerToolbarButtonCmd;
};


struct FSwitchboardMenuEntryImpl
{
	FSwitchboardMenuEntryImpl()
	{
		FSwitchboardUICommands::Register();
		TSharedPtr<FUICommandList> Actions = MakeShareable(new FUICommandList);

		Actions->MapAction(FSwitchboardUICommands::Get().TriggerToolbarButtonCmd,
			FExecuteAction::CreateRaw(this, &FSwitchboardMenuEntryImpl::OnLaunchSwitchboardClicked),
			FCanExecuteAction::CreateRaw(this, &FSwitchboardMenuEntryImpl::IsToolbarButtonEnabled));

		ToolBarExtender = MakeShareable(new FExtender);
		ToolBarExtender->AddToolBarExtension("Game", EExtensionHook::After, Actions, FToolBarExtensionDelegate::CreateLambda([this, Actions](FToolBarBuilder& ToolbarBuilder)
		{
			ToolbarBuilder.BeginSection("Switchboard");
			{
				ToolbarBuilder.AddToolBarButton
				(
					FSwitchboardUICommands::Get().TriggerToolbarButtonCmd,
					NAME_None,
					TAttribute<FText>::Create([this]() { return LOCTEXT("LaunchSwitchboard", "Switchboard"); }),
					TAttribute<FText>::Create([this]() { return LOCTEXT("LaunchSwitchboardTooltip", "Launch Switchboard"); }),
					TAttribute<FSlateIcon>::Create([this]() { return GetToolbarButtonIcon(); })
				);
				ToolbarBuilder.AddComboButton
				(
					FUIAction(),
					FOnGetContent::CreateRaw(this, &FSwitchboardMenuEntryImpl::CreateListenerEntries),
					TAttribute<FText>::Create([this]() { return LOCTEXT("LaunchSwitchboard", "Switchboard"); }),
					LOCTEXT("SwitchboardTooltip", "Actions related to the SwitchboardListener"),
					FSlateIcon(),
					true
				);
			}
			ToolbarBuilder.EndSection();
		}));

		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolBarExtender);
	
		PathToListenerInstaller = FPaths::ConvertRelativePathToFull(FPaths::EnginePluginsDir() + FString(TEXT("VirtualProduction")));
		PathToListenerInstaller /= FString(TEXT("Switchboard")) / FString(TEXT("Source")) / FString(TEXT("install_listener.py"));
	}

	~FSwitchboardMenuEntryImpl()
	{
		if (!IsEngineExitRequested() && ToolBarExtender.IsValid())
		{
			FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor");
			if (LevelEditorModule)
			{
				LevelEditorModule->GetToolBarExtensibilityManager()->RemoveExtender(ToolBarExtender);
			}
		}
	}

	TSharedRef<SWidget> CreateListenerEntries()
	{
		const bool bShouldCloseWindowAfterMenuSelection = true;
		FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, nullptr);

		MenuBuilder.BeginSection("Switchboard", LOCTEXT("SwitchboardListener", "Listener"));
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("ListenerLaunchLabel", "Launch SwitchboardListener"),
				LOCTEXT("ListenerLaunchTooltip", "Launches the SwitchboardListener with the settings from Editor Settings."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateRaw(this, &FSwitchboardMenuEntryImpl::LaunchListener))
			);
#if PLATFORM_WINDOWS
			MenuBuilder.AddMenuSeparator();
			MenuBuilder.AddMenuEntry(
				LOCTEXT("ListenerInstallLabel", "Install SwitchboardListener"),
				LOCTEXT("ListenerInstallTooltip", "Configures SwitchboardListener to automatically run on system start."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateRaw(this, &FSwitchboardMenuEntryImpl::InstallListener))
			);
			MenuBuilder.AddMenuEntry(
				LOCTEXT("ListenerUninstallLabel", "Uninstall SwitchboardListener"),
				LOCTEXT("ListenerUninstallTooltip", "Stop SwitchboardListener from automatically running on system start."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateRaw(this, &FSwitchboardMenuEntryImpl::UninstallListener))
			);
#endif
		}
		MenuBuilder.EndSection();

		return MenuBuilder.MakeWidget();
	}

	void LaunchListener()
	{
		const FString ListenerPath = GetDefault<USwitchboardEditorSettings>()->ListenerPath.FilePath;
		if (!FPaths::FileExists(ListenerPath))
		{
			const FString ErrorMsg = TEXT("Could not find SwitchboardListener! Make sure it was compiled.");
			FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *ErrorMsg, TEXT("Error starting listener"));
			return;
		}

		const FString ListenerArgs = GetDefault<USwitchboardEditorSettings>()->ListenerCommandlineArguments;
		if (RunProcess(ListenerPath, ListenerArgs))
		{
			UE_LOG(LogSwitchboardPlugin, Display, TEXT("Successfully started SwitchboardListener"));
		}
		else
		{
			const FString ErrorMsg = TEXT("Unable to start the listener! Make sure it was compiled. Check the log for details.");
			FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *ErrorMsg, TEXT("Error starting listener"));
		}
	}

	void InstallListener()
	{
		FString PythonInterpreter = GetPathToPython();
		if (PythonInterpreter.IsEmpty())
		{
			const FString ErrorMsg = TEXT("Could not find path to python interpreter which is required to run the installation script!");
			UE_LOG(LogSwitchboardPlugin, Error, TEXT("%s"), *ErrorMsg);
			FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *ErrorMsg, TEXT("No Python interpreter"));
			return;
		}

		const FString ListenerPath = GetDefault<USwitchboardEditorSettings>()->ListenerPath.FilePath;
		if (!FPaths::FileExists(ListenerPath))
		{
			const FString ErrorMsg = TEXT("Could not find SwitchboardListener! Make sure it was compiled.");
			FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *ErrorMsg, TEXT("Error installing listener"));
			return;
		}

		const FString ListenerArgs = GetDefault<USwitchboardEditorSettings>()->ListenerCommandlineArguments;
		const FString Args = FString::Printf(TEXT("%s install \"\\\"%s\\\" %s\""), *PathToListenerInstaller, *ListenerPath, *ListenerArgs);
		if (RunProcess(PythonInterpreter, Args))
		{
			const FString Msg = TEXT("Successfully installed the SwitchboardListener. It will automatically run on the next system start.");
			FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *Msg, TEXT("Success"));
		}
		else
		{
			const FString ErrorMsg = TEXT("Unable to install listener! Check the log for details.");
			FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *ErrorMsg, TEXT("Error installing listener"));
		}
	}

	void UninstallListener()
	{
		FString PythonInterpreter = GetPathToPython();
		if (PythonInterpreter.IsEmpty())
		{
			return;
		}

		const FString Args = FString::Printf(TEXT("%s uninstall"), *PathToListenerInstaller);
		if (RunProcess(PythonInterpreter, Args))
		{
			const FString Msg = TEXT("Successfully removed the SwitchboardListener from Autorun.");
			FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *Msg, TEXT("Success"));
		}
		else
		{
			const FString ErrorMsg = TEXT("Unable to uninstall listener! Check the log for details.");
			FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *ErrorMsg, TEXT("Error uninstalling listener"));
		}
	}

	FString GetPathToPython()
	{
		const FFilePath PythonPath = GetDefault<USwitchboardEditorSettings>()->PythonInterpreterPath;
		FString PythonInterpreter = PythonPath.FilePath;
		if (PythonInterpreter.IsEmpty())
		{
			FString PythonExeName;
#if PLATFORM_WINDOWS
			PythonExeName = TEXT("python.exe");
#elif PLATFORM_LINUX
			PythonExeName = TEXT("python");
#endif

			PythonInterpreter = GetDefault<USwitchboardEditorSettings>()->SwitchboardPath.Path;
			PythonInterpreter /= FString(TEXT(".thirdparty")) / FString(TEXT("python"));
			PythonInterpreter /= FString(TEXT("current")) / PythonExeName;
			if (!FPaths::FileExists(PythonInterpreter)) // python will not exist in thirdparty (yet) if SB was never run
			{
				// so we fall back onto the interpreter that ships with the engine
				PythonInterpreter = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() + FString(TEXT("Binaries")));
				PythonInterpreter /= FString(TEXT("ThirdParty")) / FString(TEXT("Python3")) / PythonExeName;
			}
		}

		if (!FPaths::FileExists(PythonInterpreter))
		{
			FString EnginePythonPath = FPaths::EngineDir() / TEXT("Binaries") / TEXT("ThirdParty") / TEXT("Python3");
#if PLATFORM_WINDOWS
			EnginePythonPath = EnginePythonPath / TEXT("Win64") / TEXT("python.exe");
#elif PLATFORM_LINUX
			EnginePythonPath = EnginePythonPath / TEXT("Linux") / TEXT("bin") / TEXT("python");
#endif
			PythonInterpreter = EnginePythonPath;
		}

		return PythonInterpreter;
	}

	void OnLaunchSwitchboardClicked()
	{
		FString SwitchboardPath = GetDefault<USwitchboardEditorSettings>()->SwitchboardPath.Path;
		if (SwitchboardPath.IsEmpty())
		{
			SwitchboardPath = FPaths::ConvertRelativePathToFull(FPaths::EnginePluginsDir() + FString(TEXT("VirtualProduction")));
			SwitchboardPath /= FString(TEXT("Switchboard")) / FString(TEXT("Source")) / FString(TEXT("Switchboard"));
		}

#if PLATFORM_WINDOWS
		FString Executable = SwitchboardPath / TEXT("switchboard.bat");
#elif PLATFORM_LINUX
		FString Executable = SwitchboardPath / TEXT("switchboard.sh");
#endif
		FString Args = GetDefault<USwitchboardEditorSettings>()->CommandlineArguments;

		const FString PythonPath = GetDefault<USwitchboardEditorSettings>()->PythonInterpreterPath.FilePath;
		if (!PythonPath.IsEmpty())
		{
			Executable = PythonPath;
			Args.InsertAt(0, TEXT("-m switchboard "));
		}

		if (RunProcess(Executable, Args, SwitchboardPath))
		{
			UE_LOG(LogSwitchboardPlugin, Display, TEXT("Successfully started Switchboard from %s"), *SwitchboardPath);
		}
		else
		{
			const FString ErrorMsg = TEXT("Unable to start Switchboard! Check the log for details.");
			FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *ErrorMsg, TEXT("Error starting Switchboard"));
		}
	}

	FSlateIcon GetToolbarButtonIcon() const
	{
		return FSlateIcon(FSwitchboardEditorStyle::Get().GetStyleSetName(), FSwitchboardEditorStyle::NAME_SwitchboardBrush, FSwitchboardEditorStyle::NAME_SwitchboardBrush);
	}

	bool RunProcess(const FString& InExe, const FString& InArgs, const FString& InWorkingDirectory = TEXT(""))
	{
		const bool bLaunchDetached = false;
		const bool bLaunchHidden = false;
		const bool bLaunchReallyHidden = false;
		const int32 PriorityModifier = 0;
		const TCHAR* WorkingDirectory = InWorkingDirectory.IsEmpty() ? nullptr : *InWorkingDirectory;
		uint32 PID = 0;
		FProcHandle Handle = FPlatformProcess::CreateProc(*InExe, *InArgs, bLaunchDetached, bLaunchHidden, bLaunchReallyHidden, &PID, PriorityModifier, WorkingDirectory, nullptr);
		int32 RetCode;
		if (FPlatformProcess::GetProcReturnCode(Handle, &RetCode))
		{
			return RetCode == 0;
		}
		return FPlatformProcess::IsProcRunning(Handle);
	}

	bool IsToolbarButtonEnabled()
	{
		return true;
	}

private:
	TSharedPtr<FExtender> ToolBarExtender;
	FString PathToListenerInstaller;

public:
	static TUniquePtr<FSwitchboardMenuEntryImpl> Implementation;
};

TUniquePtr<FSwitchboardMenuEntryImpl> FSwitchboardMenuEntryImpl::Implementation;

void FSwitchboardMenuEntry::Register()
{
	if (!IsRunningCommandlet())
	{
		FSwitchboardMenuEntryImpl::Implementation = MakeUnique<FSwitchboardMenuEntryImpl>();
	}
}

void FSwitchboardMenuEntry::Unregister()
{
	FSwitchboardMenuEntryImpl::Implementation.Reset();
}

#undef LOCTEXT_NAMESPACE
