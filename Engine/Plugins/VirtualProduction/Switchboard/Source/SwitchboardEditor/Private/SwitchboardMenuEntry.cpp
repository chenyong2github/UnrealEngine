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
		UI_COMMAND(TriggerToolbarButtonCmd, "Launch Switchboard", "Launch Switchboard", EUserInterfaceActionType::Button, FInputChord());
	}

	TSharedPtr<FUICommandInfo> TriggerToolbarButtonCmd;
};


struct FSwitchboardMenuEntryImpl
{
	FSwitchboardMenuEntryImpl()
	{
		FSwitchboardUICommands::Register();
		const TSharedPtr<FUICommandList> Actions = MakeShared<FUICommandList>();

		Actions->MapAction(FSwitchboardUICommands::Get().TriggerToolbarButtonCmd,
			FExecuteAction::CreateRaw(this, &FSwitchboardMenuEntryImpl::OnLaunchSwitchboardClicked),
			FCanExecuteAction::CreateRaw(this, &FSwitchboardMenuEntryImpl::IsToolbarButtonEnabled));

		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User");
		FToolMenuSection& Section = Menu->FindOrAddSection("Switchboard");

		FToolMenuEntry SwitchboardButtonEntry = FToolMenuEntry::InitToolBarButton(
			FSwitchboardUICommands::Get().TriggerToolbarButtonCmd,
			TAttribute<FText>::Create([this]() { return LOCTEXT("LaunchSwitchboard", "Switchboard"); }),
			TAttribute<FText>::Create([this]() { return LOCTEXT("LaunchSwitchboardTooltip", "Launch Switchboard"); }),
			TAttribute<FSlateIcon>::Create([this]() { return GetToolbarButtonIcon(); })
		);
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
		FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, nullptr);

		MenuBuilder.BeginSection("Switchboard", LOCTEXT("SwitchboardListener", "Listener"));
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("ListenerLaunchLabel", "Launch SwitchboardListener"),
				LOCTEXT("ListenerLaunchTooltip", "Launches the SwitchboardListener with the settings from Editor Settings."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateRaw(this, &FSwitchboardMenuEntryImpl::OnLaunchListenerClicked))
			);

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

	void OnLaunchListenerClicked()
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

	void OnLaunchSwitchboardClicked()
	{
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

	FSlateIcon GetToolbarButtonIcon() const
	{
		return FSlateIcon(FSwitchboardEditorStyle::Get().GetStyleSetName(), FSwitchboardEditorStyle::NAME_SwitchboardBrush, FSwitchboardEditorStyle::NAME_SwitchboardBrush);
	}

	bool IsToolbarButtonEnabled()
	{
		return true;
	}

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
