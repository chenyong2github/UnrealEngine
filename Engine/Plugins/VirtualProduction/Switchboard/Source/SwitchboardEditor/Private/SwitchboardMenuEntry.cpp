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
#include "SwitchboardSetupWizard.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "SwitchboardEditor"


class FSwitchboardUICommands : public TCommands<FSwitchboardUICommands>
{
public:
	FSwitchboardUICommands()
		: TCommands<FSwitchboardUICommands>("Switchboard", LOCTEXT("SwitchboardCommands", "Switchboard"), NAME_None, FSwitchboardEditorStyle::Get().GetStyleSetName())
	{}

	virtual void RegisterCommands() override
	{
		UI_COMMAND(LaunchSwitchboard, "Launch Switchboard", "Launch Switchboard", EUserInterfaceActionType::Button, FInputChord());
		UI_COMMAND(LaunchSwitchboardListener, "Launch Switchboard Listener", "Launch Switchboard Listener", EUserInterfaceActionType::Button, FInputChord());
	}

	TSharedPtr<FUICommandInfo> LaunchSwitchboard;
	TSharedPtr<FUICommandInfo> LaunchSwitchboardListener;
};


struct FSwitchboardMenuEntryImpl
{
	FSwitchboardMenuEntryImpl()
	{
		FSwitchboardUICommands::Register();

		Actions->MapAction(FSwitchboardUICommands::Get().LaunchSwitchboard,
			FExecuteAction::CreateRaw(this, &FSwitchboardMenuEntryImpl::OnLaunchSwitchboard),
			FCanExecuteAction());

		Actions->MapAction(FSwitchboardUICommands::Get().LaunchSwitchboardListener,
			FExecuteAction::CreateRaw(this, &FSwitchboardMenuEntryImpl::OnLaunchSwitchboardListener),
			FCanExecuteAction());

		AddMenu();
	}

	void AddMenu()
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User");
		if (Menu->FindSection("Switchboard"))
		{
			return;
		}

		FToolMenuSection& Section = Menu->AddSection("Switchboard");

		FToolMenuEntry SwitchboardButtonEntry = FToolMenuEntry::InitToolBarButton(FSwitchboardUICommands::Get().LaunchSwitchboard);
		SwitchboardButtonEntry.SetCommandList(Actions);

		const FToolMenuEntry SwitchboardComboEntry = FToolMenuEntry::InitComboButton(
			"SwitchboardMenu",
			FUIAction(),
			FOnGetContent::CreateRaw(this, &FSwitchboardMenuEntryImpl::CreateListenerEntries),
			TAttribute<FText>::Create([this]() { return LOCTEXT("LaunchSwitchboard", "Switchboard"); }),
			LOCTEXT("SwitchboardTooltip", "Actions related to the SwitchboardListener"),
			FSlateIcon(),
			true //bInSimpleComboBox
		);

		Section.AddEntry(SwitchboardButtonEntry);
		Section.AddEntry(SwitchboardComboEntry);
	}

	void RemoveMenu()
	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User");
		if (Menu->FindSection("Switchboard"))
		{
			Menu->RemoveSection("Switchboard");
		}
	}

	~FSwitchboardMenuEntryImpl()
	{
		if (!IsEngineExitRequested() && UObjectInitialized())
		{
			UToolMenus::Get()->RemoveSection("LevelEditor.LevelEditorToolBar.User", "Switchboard");
		}
	}

	TSharedRef<SWidget> CreateListenerEntries()
	{
		const bool bShouldCloseWindowAfterMenuSelection = true;
		FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, Actions);

		MenuBuilder.BeginSection("Switchboard", LOCTEXT("SwitchboardListener", "Listener"));
		{
			MenuBuilder.AddMenuEntry(FSwitchboardUICommands::Get().LaunchSwitchboardListener);

#if SB_LISTENER_AUTOLAUNCH
			MenuBuilder.AddMenuSeparator();

			MenuBuilder.AddMenuEntry(
				LOCTEXT("ListenerAutolaunchLabel", "Launch Switchboard Listener on Login"),
				LOCTEXT("ListenerAutolaunchTooltip", "Controls whether SwitchboardListener runs automatically when you log into Windows."),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateRaw(this, &FSwitchboardMenuEntryImpl::ToggleAutolaunch),
					FCanExecuteAction(),
					FGetActionCheckState::CreateLambda([&]()
					{
						return FSwitchboardEditorModule::Get().IsListenerAutolaunchEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
				),
				NAME_None,
				EUserInterfaceActionType::ToggleButton
			);
#endif // #if SB_LISTENER_AUTOLAUNCH
		}
		MenuBuilder.EndSection();

		return MenuBuilder.MakeWidget();
	}

	void OnLaunchSwitchboardListener()
	{
		const FString ListenerPath = GetDefault<USwitchboardEditorSettings>()->GetListenerPlatformPath();
		if (!FPaths::FileExists(ListenerPath))
		{
			const FString ErrorMsg = TEXT("Could not find SwitchboardListener! Make sure it was compiled.");
			FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *ErrorMsg, TEXT("Error starting listener"));
			return;
		}

		if (FSwitchboardEditorModule::Get().LaunchListener())
		{
			UE_LOG(LogSwitchboardPlugin, Display, TEXT("Successfully started SwitchboardListener"));
		}
		else
		{
			const FString ErrorMsg = TEXT("Unable to start the listener! Make sure it was compiled. Check the log for details.");
			FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *ErrorMsg, TEXT("Error starting listener"));
		}
	}

#if SB_LISTENER_AUTOLAUNCH
	void ToggleAutolaunch()
	{
		if (FSwitchboardEditorModule::Get().IsListenerAutolaunchEnabled())
		{
			FSwitchboardEditorModule::Get().SetListenerAutolaunchEnabled(false);
		}
		else
		{
			if (!FPaths::FileExists(GetDefault<USwitchboardEditorSettings>()->GetListenerPlatformPath()))
			{
				const FString ErrorMsg = TEXT("Could not find SwitchboardListener! Make sure it has been compiled.");
				FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *ErrorMsg, TEXT("Error enabling SwitchboardListener auto-launch"));
				return;
			}

			FSwitchboardEditorModule::Get().SetListenerAutolaunchEnabled(true);
		}
	}
#endif // #if SB_LISTENER_AUTOLAUNCH

	void OnLaunchSwitchboard()
	{
		const FSwitchboardVerifyResult& VerifyResult = FSwitchboardEditorModule::Get().GetVerifyResult().Get();
		if (VerifyResult.Summary != FSwitchboardVerifyResult::ESummary::Success)
		{
			SSwitchboardSetupWizard::OpenWindow();
			return;
		}

		if (FSwitchboardEditorModule::Get().LaunchSwitchboard())
		{
			UE_LOG(LogSwitchboardPlugin, Display, TEXT("Successfully started Switchboard"));
		}
		else
		{
			const FString ErrorMsg = TEXT("Unable to start Switchboard! Check the log for details.");
			FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *ErrorMsg, TEXT("Error starting Switchboard"));
		}
	}

public:
	static TUniquePtr<FSwitchboardMenuEntryImpl> Implementation;

	TSharedPtr<FUICommandList> Actions = MakeShared<FUICommandList>();
};

TUniquePtr<FSwitchboardMenuEntryImpl> FSwitchboardMenuEntryImpl::Implementation;

void FSwitchboardMenuEntry::Register()
{
	if (!IsRunningCommandlet())
	{
		FSwitchboardMenuEntryImpl::Implementation = MakeUnique<FSwitchboardMenuEntryImpl>();
	}
}

void FSwitchboardMenuEntry::AddMenu()
{
	if (FSwitchboardMenuEntryImpl::Implementation)
	{
		FSwitchboardMenuEntryImpl::Implementation->AddMenu();
	}
}

void FSwitchboardMenuEntry::RemoveMenu()
{
	if (FSwitchboardMenuEntryImpl::Implementation)
	{
		FSwitchboardMenuEntryImpl::Implementation->RemoveMenu();
	}
}

void FSwitchboardMenuEntry::Unregister()
{
	FSwitchboardMenuEntryImpl::Implementation.Reset();
}

#undef LOCTEXT_NAMESPACE
