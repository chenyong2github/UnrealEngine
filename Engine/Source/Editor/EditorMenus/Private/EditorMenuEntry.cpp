// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EditorMenuEntry.h"
#include "EditorMenuSubsystem.h"
#include "IEditorMenusModule.h"

#include "Textures/SlateIcon.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Internationalization/Internationalization.h"

#include "Editor.h"


FEditorMenuEntry::FEditorMenuEntry() :
	Type(EMultiBlockType::None),
	UserInterfaceActionType(EUserInterfaceActionType::Button),
	bShouldCloseWindowAfterMenuSelection(true),
	ScriptObject(nullptr)
{
}

FEditorMenuEntry::FEditorMenuEntry(const FEditorMenuOwner InOwner, const FName InName, EMultiBlockType InType) :
	Name(InName),
	Owner(InOwner),
	Type(InType),
	UserInterfaceActionType(EUserInterfaceActionType::Button),
	bShouldCloseWindowAfterMenuSelection(true)	,
	ScriptObject(nullptr)
{
}

void FEditorMenuEntry::SetCommand(const TSharedPtr< const FUICommandInfo >& InCommand, FName InName, const TAttribute<FText>& InLabel, const TAttribute<FText>& InToolTip, const TAttribute<FSlateIcon>& InIcon)
{
	//Action = FEditorUIActionChoice(InCommand, InCommandList);

	Command = InCommand;
	Name = InName != NAME_None ? InName : InCommand->GetCommandName();
	Label = InLabel.IsSet() ? InLabel : InCommand->GetLabel();
	ToolTip = InToolTip.IsSet() ? InToolTip : InCommand->GetDescription();
	Icon = InIcon.IsSet() ? InIcon : InCommand->GetIcon();
}

FEditorMenuEntry FEditorMenuEntry::InitMenuEntry(const FName InName, const TAttribute<FText>& InLabel, const TAttribute<FText>& InToolTip, const TAttribute<FSlateIcon>& InIcon, const FEditorUIActionChoice& InAction, const EUserInterfaceActionType InUserInterfaceActionType, const FName InTutorialHighlightName)
{
	FEditorMenuEntry Entry(UEditorMenuSubsystem::Get()->CurrentOwner(), InName, EMultiBlockType::MenuEntry);
	Entry.TutorialHighlightName = InTutorialHighlightName;
	Entry.Label = InLabel;
	Entry.ToolTip = InToolTip;
	Entry.Icon = InIcon;
	Entry.UserInterfaceActionType = InUserInterfaceActionType;
	Entry.Action = InAction;
	return Entry;
}

FEditorMenuEntry FEditorMenuEntry::InitMenuEntry(const TSharedPtr< const FUICommandInfo >& InCommand, const TAttribute<FText>& InLabel, const TAttribute<FText>& InToolTip, const TAttribute<FSlateIcon>& InIcon, const FName InTutorialHighlightName, const FName InName)
{
	FEditorMenuEntry Entry(UEditorMenuSubsystem::Get()->CurrentOwner(), InName, EMultiBlockType::MenuEntry);
	Entry.TutorialHighlightName = InTutorialHighlightName;
	Entry.SetCommand(InCommand, InName, InLabel, InToolTip, InIcon);
	return Entry;
}

FEditorMenuEntry FEditorMenuEntry::InitSubMenu(const FName InParentMenu, const FName InName, const TAttribute<FText>& InLabel, const TAttribute<FText>& InToolTip, const FNewEditorMenuChoice& InMakeMenu, bool bInOpenSubMenuOnClick, const TAttribute<FSlateIcon>& InIcon, const bool bInShouldCloseWindowAfterMenuSelection)
{
	FEditorMenuEntry Entry(UEditorMenuSubsystem::Get()->CurrentOwner(), InName, EMultiBlockType::MenuEntry);
	Entry.Label = InLabel;
	Entry.ToolTip = InToolTip;
	Entry.Icon = InIcon;

	Entry.SubMenu = FSubMenuEntryData(InMakeMenu, bInOpenSubMenuOnClick);
	Entry.bShouldCloseWindowAfterMenuSelection = bInShouldCloseWindowAfterMenuSelection;
	return Entry;
}

FEditorMenuEntry FEditorMenuEntry::InitToolBarButton(const FName InName, const FEditorUIActionChoice& InAction, const TAttribute<FText>& InLabel, const TAttribute<FText>& InToolTip, const TAttribute<FSlateIcon>& InIcon, const EUserInterfaceActionType InUserInterfaceActionType, FName InTutorialHighlightName)
{
	FEditorMenuEntry Entry(UEditorMenuSubsystem::Get()->CurrentOwner(), InName, EMultiBlockType::ToolBarButton);
	Entry.TutorialHighlightName = InTutorialHighlightName;
	Entry.Label = InLabel;
	Entry.ToolTip = InToolTip;
	Entry.Icon = InIcon;
	Entry.UserInterfaceActionType = InUserInterfaceActionType;
	Entry.Action = InAction;
	return Entry;
}

FEditorMenuEntry FEditorMenuEntry::InitToolBarButton(const TSharedPtr< const FUICommandInfo >& InCommand, const TAttribute<FText>& InLabel, const TAttribute<FText>& InToolTip, const TAttribute<FSlateIcon>& InIcon, FName InTutorialHighlightName, const FName InName)
{
	FEditorMenuEntry Entry(UEditorMenuSubsystem::Get()->CurrentOwner(), InName, EMultiBlockType::ToolBarButton);
	Entry.TutorialHighlightName = InTutorialHighlightName;
	Entry.SetCommand(InCommand, InName, InLabel, InToolTip, InIcon);
	return Entry;
}

FEditorMenuEntry FEditorMenuEntry::InitComboButton(const FName InName, const FEditorUIActionChoice& InAction, const FOnGetContent& InMenuContentGenerator, const TAttribute<FText>& InLabel, const TAttribute<FText>& InToolTip, const TAttribute<FSlateIcon>& InIcon, bool bInSimpleComboBox, FName InTutorialHighlightName)
{
	FEditorMenuEntry Entry(UEditorMenuSubsystem::Get()->CurrentOwner(), InName, EMultiBlockType::ToolBarComboButton);
	Entry.TutorialHighlightName = InTutorialHighlightName;
	Entry.Label = InLabel;
	Entry.ToolTip = InToolTip;
	Entry.Icon = InIcon;
	Entry.Action = InAction;

	Entry.ToolBar.ComboButtonContextMenuGenerator = InMenuContentGenerator;
	Entry.ToolBar.bSimpleComboBox = bInSimpleComboBox;
	return Entry;
}

FEditorMenuEntry FEditorMenuEntry::InitMenuSeparator(const FName InName)
{
	return FEditorMenuEntry(UEditorMenuSubsystem::Get()->CurrentOwner(), InName, EMultiBlockType::MenuSeparator);
}

FEditorMenuEntry FEditorMenuEntry::InitToolBarSeparator(const FName InName)
{
	return FEditorMenuEntry(UEditorMenuSubsystem::Get()->CurrentOwner(), InName, EMultiBlockType::ToolBarSeparator);
}

void FEditorMenuEntry::ResetActions()
{
	Action = FEditorUIActionChoice();
	//ScriptObject = nullptr;
	StringCommand = FEditorMenuStringCommand();
	Command.Reset();
	CommandList.Reset();
}
