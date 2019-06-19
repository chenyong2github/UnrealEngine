// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EditorMenuContext.h"
#include "EditorMenuMisc.h"

#include "Framework/Commands/UIAction.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "EditorMenuDelegates.generated.h"

class UEditorMenu;
struct FEditorMenuSection;



DECLARE_DELEGATE_OneParam(FNewEditorMenuSectionDelegate, FEditorMenuSection&);
DECLARE_DELEGATE_OneParam(FNewEditorMenuDelegate, UEditorMenu*);
DECLARE_DELEGATE_TwoParams(FNewEditorMenuDelegateLegacy, class FMenuBuilder&, UEditorMenu*);
DECLARE_DELEGATE_TwoParams(FNewToolBarDelegateLegacy, class FToolBarBuilder&, UEditorMenu*);
DECLARE_DELEGATE_RetVal_OneParam(TSharedRef<SWidget>, FNewEditorMenuWidget, const FEditorMenuContext&);

DECLARE_DELEGATE_OneParam(FEditorMenuExecuteAction, const FEditorMenuContext&);
DECLARE_DELEGATE_RetVal_OneParam(bool, FEditorMenuCanExecuteAction, const FEditorMenuContext&);
DECLARE_DELEGATE_RetVal_OneParam(bool, FEditorMenuIsActionChecked, const FEditorMenuContext&);
DECLARE_DELEGATE_RetVal_OneParam(ECheckBoxState, FEditorMenuGetActionCheckState, const FEditorMenuContext&);
DECLARE_DELEGATE_RetVal_OneParam(bool, FEditorMenuIsActionButtonVisible, const FEditorMenuContext&);

DECLARE_DELEGATE_TwoParams(FEditorMenuExecuteString, const FString&, const FEditorMenuContext&);

DECLARE_DYNAMIC_DELEGATE_OneParam(FEditorMenuDynamicExecuteAction, const FEditorMenuContext&, Context);
DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(bool, FEditorMenuDynamicCanExecuteAction, const FEditorMenuContext&, Context);
DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(bool, FEditorMenuDynamicIsActionChecked, const FEditorMenuContext&, Context);
DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(ECheckBoxState, FEditorMenuDynamicGetActionCheckState, const FEditorMenuContext&, Context);
DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(bool, FEditorMenuDynamicIsActionButtonVisible, const FEditorMenuContext&, Context);

struct EDITORMENUS_API FEditorUIAction
{
	FEditorUIAction() {}
	FEditorUIAction(const FEditorMenuExecuteAction& InExecuteAction) : ExecuteAction(InExecuteAction) {}

	FEditorMenuExecuteAction ExecuteAction;
	FEditorMenuCanExecuteAction CanExecuteAction;
	FEditorMenuGetActionCheckState GetActionCheckState;
	FEditorMenuIsActionButtonVisible IsActionVisibleDelegate;
};

USTRUCT(BlueprintType)
struct EDITORMENUS_API FEditorDynamicUIAction
{
	GENERATED_BODY()

	FEditorDynamicUIAction() {}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	FEditorMenuDynamicExecuteAction ExecuteAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	FEditorMenuDynamicCanExecuteAction CanExecuteAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	FEditorMenuDynamicGetActionCheckState GetActionCheckState;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Editor UI")
	FEditorMenuDynamicIsActionButtonVisible IsActionVisibleDelegate;
};

struct EDITORMENUS_API FNewEditorMenuWidgetChoice
{
public:
	FNewEditorMenuWidgetChoice() {}
	FNewEditorMenuWidgetChoice(const FOnGetContent& InOnGetContent) : OnGetContent(InOnGetContent) {}
	FNewEditorMenuWidgetChoice(const FNewEditorMenuWidget& InNewEditorMenuWidget) : NewEditorMenuWidget(InNewEditorMenuWidget) {}
	FNewEditorMenuWidgetChoice(const FNewEditorMenuDelegate& InNewEditorMenu) : NewEditorMenu(InNewEditorMenu) {}

	FOnGetContent OnGetContent;
	FNewEditorMenuWidget NewEditorMenuWidget;
	FNewEditorMenuDelegate NewEditorMenu;
};

struct EDITORMENUS_API FEditorUIActionChoice
{
public:
	FEditorUIActionChoice() {}
	FEditorUIActionChoice(const FUIAction& InAction) : Action(InAction) {}
	FEditorUIActionChoice(const FExecuteAction& InExecuteAction) : Action(InExecuteAction) {}
	FEditorUIActionChoice(const FEditorUIAction& InAction) : EditorAction(InAction) {}
	FEditorUIActionChoice(const FEditorDynamicUIAction& InAction) : DynamicEditorAction(InAction) {}
	FEditorUIActionChoice(const FEditorMenuExecuteAction& InExecuteAction) : EditorAction(InExecuteAction) {}
	FEditorUIActionChoice(const TSharedPtr< const FUICommandInfo >& InCommand, const FUICommandList& InCommandList);

	const FUIAction* GetUIAction() const
	{
		return Action.IsSet() ? &Action.GetValue() : nullptr;
	}

	const FEditorUIAction* GetEditorUIAction() const
	{
		return EditorAction.IsSet() ? &EditorAction.GetValue() : nullptr;
	}

	const FEditorDynamicUIAction* GetEditorDynamicUIAction() const
	{
		return DynamicEditorAction.IsSet() ? &DynamicEditorAction.GetValue() : nullptr;
	}

private:
	TOptional<FUIAction> Action;
	TOptional<FEditorUIAction> EditorAction;
	TOptional<FEditorDynamicUIAction> DynamicEditorAction;
};

struct EDITORMENUS_API FNewEditorMenuChoice
{
	FNewEditorMenuChoice() {};
	FNewEditorMenuChoice(const FNewEditorMenuDelegate& InNewEditorMenuDelegate) : NewEditorMenuDelegate(InNewEditorMenuDelegate) {}
	FNewEditorMenuChoice(const FNewMenuDelegate& InNewMenuDelegate) : NewMenuDelegate(InNewMenuDelegate) {}

	FNewEditorMenuDelegate NewEditorMenuDelegate;
	FNewMenuDelegate NewMenuDelegate;
};

struct EDITORMENUS_API FNewSectionConstructChoice
{
	FNewSectionConstructChoice() {};
	FNewSectionConstructChoice(const FNewEditorMenuDelegate& InNewEditorMenuDelegate) : NewEditorMenuDelegate(InNewEditorMenuDelegate) {}
	FNewSectionConstructChoice(const FNewEditorMenuDelegateLegacy& InNewEditorMenuDelegateLegacy) : NewEditorMenuDelegateLegacy(InNewEditorMenuDelegateLegacy) {}
	FNewSectionConstructChoice(const FNewToolBarDelegateLegacy& InNewToolBarDelegateLegacy) : NewToolBarDelegateLegacy(InNewToolBarDelegateLegacy) {}

	FNewEditorMenuDelegate NewEditorMenuDelegate;
	FNewEditorMenuDelegateLegacy NewEditorMenuDelegateLegacy;
	FNewToolBarDelegateLegacy NewToolBarDelegateLegacy;
};
