// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EditorMenuOwner.h"
#include "EditorMenuDelegates.h"
#include "EditorMenuMisc.h"

#include "Misc/Attribute.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Framework/Commands/UICommandInfo.h"
#include "UObject/TextProperty.h"

#include "EditorMenuEntry.generated.h"

class UEditorMenuEntryScript;

struct FEditorMenuEntrySubMenuData
{
public:
	FEditorMenuEntrySubMenuData() :
		bIsSubMenu(false),
		bOpenSubMenuOnClick(false)
	{
	}

	bool bIsSubMenu;
	bool bOpenSubMenuOnClick;
	FNewEditorMenuChoice ConstructMenu;
};

struct FEditorMenuEntryToolBarData
{
public:
	FEditorMenuEntryToolBarData() :
		bSimpleComboBox(false),
		bIsFocusable(false),
		bForceSmallIcons(false)
	{
	}

	TOptional< EVisibility > LabelVisibility;

	/** If true, the icon and label won't be displayed */
	bool bSimpleComboBox;

	/** Whether ToolBar will have Focusable buttons */
	bool bIsFocusable;

	/** Whether this toolbar should always use small icons, regardless of the current settings */
	bool bForceSmallIcons;

	/** Delegate that generates a widget for this combo button's menu content.  Called when the menu is summoned. */
	FNewEditorMenuWidgetChoice ComboButtonContextMenuGenerator;

	/** Legacy delegate that generates a widget for this combo button's menu content.  Called when the menu is summoned. */
	FNewToolBarDelegateLegacy ConstructLegacy;
};


struct FEditorMenuEntryWidgetData
{
public:
	FEditorMenuEntryWidgetData() :
		bNoIndent(false),
		bSearchable(false)
	{
	}

	/** Remove the padding from the left of the widget that lines it up with other menu items */
	bool bNoIndent;

	/** If true, widget will be searchable */
	bool bSearchable;
};

USTRUCT(BlueprintType)
struct EDITORMENUS_API FEditorMenuEntry
{
	GENERATED_BODY()

	FEditorMenuEntry();
	FEditorMenuEntry(const FEditorMenuOwner InOwner, const FName InName, EMultiBlockType InType);

	static FEditorMenuEntry InitMenuEntry(const FName InName, const TAttribute<FText>& InLabel, const TAttribute<FText>& InToolTip, const TAttribute<FSlateIcon>& InIcon, const FEditorUIActionChoice& InAction, const EUserInterfaceActionType UserInterfaceActionType = EUserInterfaceActionType::Button, const FName InTutorialHighlightName = NAME_None);
	static FEditorMenuEntry InitMenuEntry(const TSharedPtr< const FUICommandInfo >& InCommand, const TAttribute<FText>& InLabelOverride = TAttribute<FText>(), const TAttribute<FText>& InToolTipOverride = TAttribute<FText>(), const TAttribute<FSlateIcon>& InIconOverride = TAttribute<FSlateIcon>(), const FName InTutorialHighlightName = NAME_None, const FName InNameOverride = NAME_None);
	static FEditorMenuEntry InitMenuEntry(const FName InName, const FEditorUIActionChoice& InAction, const TSharedRef<SWidget>& Widget);
	static FEditorMenuEntry InitSubMenu(const FName InParentMenu, const FName InName, const TAttribute<FText>& InLabel, const TAttribute<FText>& InToolTip, const FNewEditorMenuChoice& InMakeMenu, bool bInOpenSubMenuOnClick = false, const TAttribute<FSlateIcon>& InIcon = TAttribute<FSlateIcon>(), const bool ShouldCloseWindowAfterMenuSelection = true);

	static FEditorMenuEntry InitToolBarButton(const FName InName, const FEditorUIActionChoice& InAction, const TAttribute<FText>& InLabel = TAttribute<FText>(), const TAttribute<FText>& InToolTip = TAttribute<FText>(), const TAttribute<FSlateIcon>& InIcon = TAttribute<FSlateIcon>(), const EUserInterfaceActionType UserInterfaceActionType = EUserInterfaceActionType::Button, FName InTutorialHighlightName = NAME_None);
	static FEditorMenuEntry InitToolBarButton(const TSharedPtr< const FUICommandInfo >& InCommand, const TAttribute<FText>& InLabelOverride = TAttribute<FText>(), const TAttribute<FText>& InToolTipOverride = TAttribute<FText>(), const TAttribute<FSlateIcon>& InIconOverride = TAttribute<FSlateIcon>(), FName InTutorialHighlightName = NAME_None, const FName InNameOverride = NAME_None);
	static FEditorMenuEntry InitComboButton(const FName InName, const FEditorUIActionChoice& InAction, const FNewEditorMenuWidgetChoice& InMenuContentGenerator, const TAttribute<FText>& InLabelOverride = TAttribute<FText>(), const TAttribute<FText>& InToolTipOverride = TAttribute<FText>(), const TAttribute<FSlateIcon>& InIconOverride = TAttribute<FSlateIcon>(), bool bInSimpleComboBox = false, FName InTutorialHighlightName = NAME_None);

	static FEditorMenuEntry InitMenuSeparator(const FName InName);
	static FEditorMenuEntry InitToolBarSeparator(const FName InName);

	static FEditorMenuEntry InitWidget(const FName InName, const TSharedRef<SWidget>& Widget, const FText& Label, bool bNoIndent = false, bool bSearchable = true);

	bool IsSubMenu() const { return SubMenuData.bIsSubMenu; }

	friend struct FEditorMenuSection;
	friend class UEditorMenuEntryScript;

private:

	void SetCommand(const TSharedPtr< const FUICommandInfo >& InCommand, FName InName, const TAttribute<FText>& InLabel, const TAttribute<FText>& InToolTip, const TAttribute<FSlateIcon>& InIcon);

	void ResetActions();

	bool IsScriptObjectDynamicConstruct() const;

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	FEditorMenuOwner Owner;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	EMultiBlockType Type;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	EUserInterfaceActionType UserInterfaceActionType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	FName TutorialHighlightName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	FEditorMenuInsert InsertPosition;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	bool bShouldCloseWindowAfterMenuSelection;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	UEditorMenuEntryScript* ScriptObject;

	FEditorMenuEntrySubMenuData SubMenuData;

	FEditorMenuEntryToolBarData ToolBarData;

	FEditorMenuEntryWidgetData WidgetData;

	/** Optional delegate that returns a widget to use as this menu entry */
	FNewEditorMenuWidget MakeWidget;

private:

	friend class UEditorMenuSubsystem;
	friend class UEditorMenuEntryExtensions;

	TAttribute<FText> Label;
	TAttribute<FText> ToolTip;
	TAttribute<FSlateIcon> Icon;

	FEditorUIActionChoice Action;

	FEditorMenuStringCommand StringExecuteAction;

	TSharedPtr< const FUICommandInfo > Command;
	TSharedPtr< const FUICommandList > CommandList;

	FNewEditorMenuSectionDelegate Construct;
	FNewEditorMenuDelegateLegacy ConstructLegacy;
};

