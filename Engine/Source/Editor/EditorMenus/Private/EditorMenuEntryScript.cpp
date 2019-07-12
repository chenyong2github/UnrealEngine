// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EditorMenuEntryScript.h"
#include "EditorMenuEntry.h"
#include "EditorMenuSubsystem.h"
#include "IEditorMenusModule.h"

#include "Textures/SlateIcon.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Internationalization/Internationalization.h"

#include "Editor.h"

FScriptSlateIcon::FScriptSlateIcon()
{

}

FScriptSlateIcon::FScriptSlateIcon(const FName InStyleSetName, const FName InStyleName) :
	StyleSetName(InStyleSetName),
	StyleName(InStyleName),
	SmallStyleName(ISlateStyle::Join(InStyleName, ".Small"))
{
}

FScriptSlateIcon::FScriptSlateIcon(const FName InStyleSetName, const FName InStyleName, const FName InSmallStyleName) :
	StyleSetName(InStyleSetName),
	StyleName(InStyleName),
	SmallStyleName(InSmallStyleName)
{
}

FSlateIcon FScriptSlateIcon::GetSlateIcon() const
{
	if (SmallStyleName == NAME_None)
	{
		if (StyleSetName == NAME_None && StyleName == NAME_None)
		{
			return FSlateIcon();
		}

		return FSlateIcon(StyleSetName, StyleName);
	}

	return FSlateIcon(StyleSetName, StyleName, SmallStyleName);
}

TAttribute<FText> UEditorMenuEntryScript::CreateLabelAttribute(FEditorMenuContext& Context)
{
	static const FName FunctionName = GET_FUNCTION_NAME_CHECKED(UEditorMenuEntryScript, GetLabel);
	if (GetClass()->IsFunctionImplementedInScript(FunctionName))
	{
		return TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateUFunction(this, FunctionName, Context));
	}

	return Data.Label;
}

TAttribute<FText> UEditorMenuEntryScript::CreateToolTipAttribute(FEditorMenuContext& Context)
{
	static const FName FunctionName = GET_FUNCTION_NAME_CHECKED(UEditorMenuEntryScript, GetToolTip);
	if (GetClass()->IsFunctionImplementedInScript(FunctionName))
	{
		return TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateUFunction(this, FunctionName, Context));
	}

	return Data.ToolTip;
}

TAttribute<FSlateIcon> UEditorMenuEntryScript::CreateIconAttribute(FEditorMenuContext& Context)
{
	static const FName FunctionName = GET_FUNCTION_NAME_CHECKED(UEditorMenuEntryScript, GetIcon);
	if (GetClass()->IsFunctionImplementedInScript(FunctionName))
	{
		TWeakObjectPtr<UEditorMenuEntryScript> WeakThis(this);
		TAttribute<FSlateIcon>::FGetter Getter;
		Getter.BindLambda([=]()
		{
			if (UEditorMenuEntryScript* Object = WeakThis.Get())
			{
				return Object->GetIcon(Context).GetSlateIcon();
			}
			else
			{
				return FSlateIcon();
			}
		});

		return TAttribute<FSlateIcon>::Create(Getter);
	}

	return Data.Icon.GetSlateIcon();
}

FSlateIcon UEditorMenuEntryScript::GetSlateIcon(const FEditorMenuContext& Context) const
{
	return GetIcon(Context).GetSlateIcon();
}

void UEditorMenuEntryScript::RegisterMenuEntry()
{
	UEditorMenuSubsystem::AddMenuEntryObject(this);
}

void UEditorMenuEntryScript::InitEntry(const FName OwnerName, const FName Menu, const FName Section, const FName Name, const FText& Label, const FText& ToolTip)
{
	Data.OwnerName = OwnerName;
	Data.Menu = Menu;
	Data.Section = Section;
	Data.Name = Name;
	Data.Label = Label;
	Data.ToolTip = ToolTip;
}

void UEditorMenuEntryScript::ToMenuEntry(FEditorMenuEntry& Output)
{
	if (Data.Advanced.bIsSubMenu)
	{
		Output = FEditorMenuEntry::InitSubMenu(
			Data.Menu,
			Data.Name,
			Data.Label,
			Data.ToolTip,
			FNewEditorMenuChoice(), // Menu will be opened by string: 'Menu' + '.' + 'Name'
			Data.Advanced.bOpenSubMenuOnClick,
			Data.Icon,
			Data.Advanced.bShouldCloseWindowAfterMenuSelection);
	}
	else
	{
		if (Data.Advanced.EntryType == EMultiBlockType::ToolBarButton)
		{
			Output = FEditorMenuEntry::InitToolBarButton(
				Data.Name,
				FEditorUIActionChoice(), // Action will be handled by 'ScriptObject'
				Data.Label,
				Data.ToolTip,
				Data.Icon,
				Data.Advanced.UserInterfaceActionType,
				Data.Advanced.TutorialHighlight
			);
		}
		else
		{
			Output = FEditorMenuEntry::InitMenuEntry(Data.Name, Data.Label, Data.ToolTip, Data.Icon, FUIAction());
			Output.UserInterfaceActionType = Data.Advanced.UserInterfaceActionType;
			Output.TutorialHighlightName = Data.Advanced.TutorialHighlight;
		}
	}

	if (!Data.InsertPosition.IsDefault())
	{
		Output.InsertPosition = Data.InsertPosition;
	}

	Output.ScriptObject = this;

	Output.Owner = Data.OwnerName;
}

FEditorMenuEntryScriptDataAdvanced::FEditorMenuEntryScriptDataAdvanced() :
	EntryType(EMultiBlockType::MenuEntry),
	UserInterfaceActionType(EUserInterfaceActionType::Button),
	bIsSubMenu(false),
	bOpenSubMenuOnClick(false),
	bShouldCloseWindowAfterMenuSelection(true),
	bSimpleComboBox(false)
{

}
