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
#include "SwitchboardEditorModule.h"
#include "SwitchboardEditorSettings.h"
#include "SwitchboardEditorStyle.h"

#define LOCTEXT_NAMESPACE "SwitchboardEditor"


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

			// The FGetActionCheckState delegate is called continually while the menu is open.
			// Cache the state now as the combo is initially expanded.
			CachedAutolaunchCheckState = GetAutolaunchCheckState();

			MenuBuilder.AddMenuEntry(
				LOCTEXT("ListenerAutolaunchLabel", "Launch Switchboard Listener on Login"),
				LOCTEXT("ListenerAutolaunchTooltip", "Controls whether SwitchboardListener runs automatically when you log into Windows."),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateRaw(this, &FSwitchboardMenuEntryImpl::ToggleAutolaunch),
					FCanExecuteAction(),
					FGetActionCheckState::CreateLambda([&](){ return CachedAutolaunchCheckState; })
				),
				NAME_None,
				EUserInterfaceActionType::ToggleButton
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

#if PLATFORM_WINDOWS
	ECheckBoxState GetAutolaunchCheckState()
	{
		return FSwitchboardEditorModule::Get().IsListenerAutolaunchEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}

	void ToggleAutolaunch()
	{
		if (CachedAutolaunchCheckState == ECheckBoxState::Checked)
		{
			FSwitchboardEditorModule::Get().SetListenerAutolaunchEnabled(false);
		}
		else
		{
			if (!FPaths::FileExists(GetDefault<USwitchboardEditorSettings>()->ListenerPath.FilePath))
			{
				const FString ErrorMsg = TEXT("Could not find SwitchboardListener! Make sure it has been compiled.");
				FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *ErrorMsg, TEXT("Error enabling SwitchboardListener auto-launch"));
				return;
			}

			FSwitchboardEditorModule::Get().SetListenerAutolaunchEnabled(true);
		}

		CachedAutolaunchCheckState = GetAutolaunchCheckState();
	}
#endif // #if PLATFORM_WINDOWS

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
	ECheckBoxState CachedAutolaunchCheckState;

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
