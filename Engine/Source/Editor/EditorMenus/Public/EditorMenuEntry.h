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

USTRUCT(BlueprintType)
struct EDITORMENUS_API FSubMenuEntryData
{
	GENERATED_USTRUCT_BODY()

	FSubMenuEntryData() :
		bIsSubMenu(false),
		bOpenSubMenuOnClick(false)
	{
	}

	FSubMenuEntryData(const FNewEditorMenuChoice& InConstruct, bool bInOpenSubMenuOnClick = false) :
		bIsSubMenu(true),
		bOpenSubMenuOnClick(bInOpenSubMenuOnClick),
		Construct(InConstruct)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	bool bIsSubMenu;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	bool bOpenSubMenuOnClick;

	FNewEditorMenuChoice Construct;
};

USTRUCT(BlueprintType)
struct EDITORMENUS_API FToolBarData
{
	GENERATED_USTRUCT_BODY()

	FToolBarData() : bSimpleComboBox(false) {}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	bool bSimpleComboBox;

	FOnGetContent ComboButtonContextMenuGenerator;
	FNewToolBarDelegateLegacy ConstructLegacy;
};

USTRUCT(BlueprintType)
struct EDITORMENUS_API FEditorMenuStringCommand
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	FName TypeName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	FString String;
};

USTRUCT(BlueprintType)
struct EDITORMENUS_API FEditorMenuEntry
{
	GENERATED_USTRUCT_BODY()

	FEditorMenuEntry();
	FEditorMenuEntry(const FEditorMenuOwner InOwner, const FName InName, EMultiBlockType InType);

	static FEditorMenuEntry InitMenuEntry(const FName InName, const TAttribute<FText>& InLabel, const TAttribute<FText>& InToolTip, const TAttribute<FSlateIcon>& InIcon, const FEditorUIActionChoice& InAction, const EUserInterfaceActionType UserInterfaceActionType = EUserInterfaceActionType::Button, const FName InTutorialHighlightName = NAME_None);
	static FEditorMenuEntry InitMenuEntry(const TSharedPtr< const FUICommandInfo >& InCommand, const TAttribute<FText>& InLabelOverride = TAttribute<FText>(), const TAttribute<FText>& InToolTipOverride = TAttribute<FText>(), const TAttribute<FSlateIcon>& InIconOverride = TAttribute<FSlateIcon>(), const FName InTutorialHighlightName = NAME_None, const FName InNameOverride = NAME_None);
	static FEditorMenuEntry InitSubMenu(const FName InParentMenu, const FName InName, const TAttribute<FText>& InLabel, const TAttribute<FText>& InToolTip, const FNewEditorMenuChoice& InMakeMenu, bool bInOpenSubMenuOnClick = false, const TAttribute<FSlateIcon>& InIcon = TAttribute<FSlateIcon>(), const bool ShouldCloseWindowAfterMenuSelection = true);

	static FEditorMenuEntry InitToolBarButton(const FName InName, const FEditorUIActionChoice& InAction, const TAttribute<FText>& InLabel = TAttribute<FText>(), const TAttribute<FText>& InToolTip = TAttribute<FText>(), const TAttribute<FSlateIcon>& InIcon = TAttribute<FSlateIcon>(), const EUserInterfaceActionType UserInterfaceActionType = EUserInterfaceActionType::Button, FName InTutorialHighlightName = NAME_None);
	static FEditorMenuEntry InitToolBarButton(const TSharedPtr< const FUICommandInfo >& InCommand, const TAttribute<FText>& InLabelOverride = TAttribute<FText>(), const TAttribute<FText>& InToolTipOverride = TAttribute<FText>(), const TAttribute<FSlateIcon>& InIconOverride = TAttribute<FSlateIcon>(), FName InTutorialHighlightName = NAME_None, const FName InNameOverride = NAME_None);
	static FEditorMenuEntry InitComboButton(const FName InName, const FEditorUIActionChoice& InAction, const FOnGetContent& InMenuContentGenerator, const TAttribute<FText>& InLabelOverride = TAttribute<FText>(), const TAttribute<FText>& InToolTipOverride = TAttribute<FText>(), const TAttribute<FSlateIcon>& InIconOverride = TAttribute<FSlateIcon>(), bool bInSimpleComboBox = false, FName InTutorialHighlightName = NAME_None);

	static FEditorMenuEntry InitMenuSeparator(const FName InName);
	static FEditorMenuEntry InitToolBarSeparator(const FName InName);

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
	FSubMenuEntryData SubMenu;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	FToolBarData ToolBar;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	UEditorMenuEntryScript* ScriptObject;

private:

	friend class UEditorMenuSubsystem;
	friend class UEditorMenuEntryExtensions;

	TAttribute<FText> Label;
	TAttribute<FText> ToolTip;
	TAttribute<FSlateIcon> Icon;

	FEditorUIActionChoice Action;

	FEditorMenuStringCommand StringCommand;

	TSharedPtr< const FUICommandInfo > Command;
	TSharedPtr< const FUICommandList > CommandList;

	FNewEditorMenuSectionDelegate Construct;
	FNewEditorMenuDelegateLegacy ConstructLegacy;
};

