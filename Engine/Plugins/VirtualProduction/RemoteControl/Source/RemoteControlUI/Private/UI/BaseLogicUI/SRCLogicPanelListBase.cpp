// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCLogicPanelListBase.h"

#include "Commands/RemoteControlCommands.h"
#include "Interfaces/IMainFrameModule.h"
#include "SRCLogicPanelBase.h"
#include "UI/SRemoteControlPanel.h"

#define LOCTEXT_NAMESPACE "SRCLogicPanelListBase"

void SRCLogicPanelListBase::Construct(const FArguments& InArgs, const TSharedRef<SRemoteControlPanel>& InPanel)
{
	RemoteControlPanelWeakPtr = InPanel;
}

TSharedPtr<SWidget> SRCLogicPanelListBase::GetContextMenuWidget()
{
	IMainFrameModule& MainFrame = FModuleManager::Get().LoadModuleChecked<IMainFrameModule>("MainFrame");

	FMenuBuilder MenuBuilder(true, MainFrame.GetMainFrameCommandBindings());

	// Special menu options
	MenuBuilder.BeginSection("Advanced");
	AddSpecialContextMenuOptions(MenuBuilder);
	MenuBuilder.EndSection();

	// Generic options (based on UI Commands)
	MenuBuilder.BeginSection("Common");

	const FRemoteControlCommands& Commands = FRemoteControlCommands::Get();

	// 1. Copy
	MenuBuilder.AddMenuEntry(Commands.CopyItem);

	// 2. Paste
	FText PasteItemLabel = LOCTEXT("Paste", "Paste");
	if (TSharedPtr<SRemoteControlPanel> RemoteControlPanel = RemoteControlPanelWeakPtr.Pin())
	{
		if (TSharedPtr<SRCLogicPanelBase> LogicPanel = RemoteControlPanel->LogicClipboardItemSource)
		{
			const FText Suffix = LogicPanel->GetPasteItemMenuEntrySuffix();
			if (!Suffix.IsEmpty())
			{
				PasteItemLabel = FText::Format(FText::FromString("{0} ({1})"), PasteItemLabel, Suffix);
			}
		}
	}
	MenuBuilder.AddMenuEntry(Commands.PasteItem, NAME_None, PasteItemLabel);

	// 2. Duplicate
	MenuBuilder.AddMenuEntry(Commands.DuplicateItem);

	// 3. Delete
	MenuBuilder.AddMenuEntry(Commands.DeleteEntity);

	MenuBuilder.EndSection();

	TSharedPtr<SWidget> MenuWidget = MenuBuilder.MakeWidget();
	ContextMenuWidgetCached = MenuWidget;

	return MenuWidget;
}

#undef LOCTEXT_NAMESPACE