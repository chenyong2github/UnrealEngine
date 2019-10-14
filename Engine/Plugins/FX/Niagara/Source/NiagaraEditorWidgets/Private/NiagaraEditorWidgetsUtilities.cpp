// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraEditorWidgetsUtilities.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "ViewModels/Stack/NiagaraStackItem.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "Stack/SNiagaraStackItemGroupAddMenu.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "Modules/ModuleManager.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "NiagaraStackEditorWidgetsUtilities"

FName FNiagaraStackEditorWidgetsUtilities::GetColorNameForExecutionCategory(FName ExecutionCategoryName)
{
	if (ExecutionCategoryName == UNiagaraStackEntry::FExecutionCategoryNames::System)
	{
		return "NiagaraEditor.Stack.AccentColor.System";
	}
	else if (ExecutionCategoryName == UNiagaraStackEntry::FExecutionCategoryNames::Emitter)
	{
		return "NiagaraEditor.Stack.AccentColor.Emitter";
	}
	else if (ExecutionCategoryName == UNiagaraStackEntry::FExecutionCategoryNames::Particle)
	{
		return "NiagaraEditor.Stack.AccentColor.Particle";
	}
	else if (ExecutionCategoryName == UNiagaraStackEntry::FExecutionCategoryNames::Render)
	{
		return "NiagaraEditor.Stack.AccentColor.Render";
	}
	else
	{
		return  "NiagaraEditor.Stack.AccentColor.None";
	}
}

FName FNiagaraStackEditorWidgetsUtilities::GetIconNameForExecutionSubcategory(FName ExecutionSubcategoryName, bool bIsHighlighted)
{
	if (bIsHighlighted)
	{
		if (ExecutionSubcategoryName == UNiagaraStackEntry::FExecutionSubcategoryNames::Settings)
		{
			return "NiagaraEditor.Stack.ParametersIconHighlighted";
		}
		if (ExecutionSubcategoryName == UNiagaraStackEntry::FExecutionSubcategoryNames::Spawn)
		{
			return "NiagaraEditor.Stack.SpawnIconHighlighted";
		}
		else if (ExecutionSubcategoryName == UNiagaraStackEntry::FExecutionSubcategoryNames::Update)
		{
			return "NiagaraEditor.Stack.UpdateIconHighlighted";
		}
		else if (ExecutionSubcategoryName == UNiagaraStackEntry::FExecutionSubcategoryNames::Event)
		{
			return "NiagaraEditor.Stack.EventIconHighlighted";
		}
		else if (ExecutionSubcategoryName == UNiagaraStackEntry::FExecutionSubcategoryNames::Render)
		{
			return "NiagaraEditor.Stack.RenderIconHighlighted";
		}
	}
	else
	{
		if (ExecutionSubcategoryName == UNiagaraStackEntry::FExecutionSubcategoryNames::Settings)
		{
			return "NiagaraEditor.Stack.ParametersIcon";
		}
		if (ExecutionSubcategoryName == UNiagaraStackEntry::FExecutionSubcategoryNames::Spawn)
		{
			return "NiagaraEditor.Stack.SpawnIcon";
		}
		else if (ExecutionSubcategoryName == UNiagaraStackEntry::FExecutionSubcategoryNames::Update)
		{
			return "NiagaraEditor.Stack.UpdateIcon";
		}
		else if (ExecutionSubcategoryName == UNiagaraStackEntry::FExecutionSubcategoryNames::Event)
		{
			return "NiagaraEditor.Stack.EventIcon";
		}
		else if (ExecutionSubcategoryName == UNiagaraStackEntry::FExecutionSubcategoryNames::Render)
		{
			return "NiagaraEditor.Stack.RenderIcon";
		}
	}

	return NAME_None;
}

FName FNiagaraStackEditorWidgetsUtilities::GetIconColorNameForExecutionCategory(FName ExecutionCategoryName)
{
	if (ExecutionCategoryName == UNiagaraStackEntry::FExecutionCategoryNames::System)
	{
		return "NiagaraEditor.Stack.IconColor.System";
	}
	else if (ExecutionCategoryName == UNiagaraStackEntry::FExecutionCategoryNames::Emitter)
	{
		return "NiagaraEditor.Stack.IconColor.Emitter";
	}
	else if (ExecutionCategoryName == UNiagaraStackEntry::FExecutionCategoryNames::Particle)
	{
		return "NiagaraEditor.Stack.IconColor.Particle";
	}
	else if (ExecutionCategoryName == UNiagaraStackEntry::FExecutionCategoryNames::Render)
	{
		return "NiagaraEditor.Stack.IconColor.Render";
	}
	else
	{
		return NAME_None;
	}
}

void OpenSourceAsset(TWeakObjectPtr<UNiagaraStackEntry> StackEntryWeak)
{
	UNiagaraStackEntry* StackEntry = StackEntryWeak.Get();
	if (StackEntry != nullptr)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(StackEntry->GetExternalAsset());
	}
}

void ShowAssetInContentBrowser(TWeakObjectPtr<UNiagaraStackEntry> StackEntryWeak)
{
	UNiagaraStackEntry* StackEntry = StackEntryWeak.Get();
	if (StackEntry != nullptr)
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		TArray<FAssetData> Assets;
		Assets.Add(FAssetData(StackEntry->GetExternalAsset()));
		ContentBrowserModule.Get().SyncBrowserToAssets(Assets);
	}
}

bool FNiagaraStackEditorWidgetsUtilities::AddStackEntryAssetContextMenuActions(FMenuBuilder& MenuBuilder, UNiagaraStackEntry& StackEntry)
{
	if (StackEntry.GetExternalAsset() != nullptr)
	{
		MenuBuilder.BeginSection("AssetActions", LOCTEXT("AssetActions", "Asset Actions"));
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("OpenAndFocusAsset", "Open and Focus Asset"),
				FText::Format(LOCTEXT("OpenAndFocusAssetTooltip", "Open {0} in separate editor"), StackEntry.GetDisplayName()),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&OpenSourceAsset, TWeakObjectPtr<UNiagaraStackEntry>(&StackEntry))));
			MenuBuilder.AddMenuEntry(
				LOCTEXT("ShowAssetInContentBrowser", "Show in Content Browser"),
				FText::Format(LOCTEXT("ShowAssetInContentBrowserToolTip", "Navigate to {0} in the Content Browser window"), StackEntry.GetDisplayName()),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&ShowAssetInContentBrowser, TWeakObjectPtr<UNiagaraStackEntry>(&StackEntry))));
		}
		MenuBuilder.EndSection();
		return true;
	}
	return false;
}

void DeleteItem(TWeakObjectPtr<UNiagaraStackItem> StackItemWeak)
{
	UNiagaraStackItem* StackItem = StackItemWeak.Get();
	FText Unused;
	if (StackItem != nullptr && StackItem->TestCanDeleteWithMessage(Unused))
	{
		StackItem->Delete();
	}
}

void ToggleEnabledState(TWeakObjectPtr<UNiagaraStackItem> StackItemWeak)
{
	UNiagaraStackItem* StackItem = StackItemWeak.Get();
	if (StackItem != nullptr)
	{
		StackItem->SetIsEnabled(!StackItem->GetIsEnabled());
	}
}

bool FNiagaraStackEditorWidgetsUtilities::AddStackItemContextMenuActions(FMenuBuilder& MenuBuilder, UNiagaraStackItem& StackItem)
{
	if (StackItem.SupportsDelete() || StackItem.SupportsChangeEnabled())
	{
		MenuBuilder.BeginSection("ItemActions", LOCTEXT("ItemActions", "Item Actions"));
		{
			if (StackItem.SupportsChangeEnabled())
			{
				FUIAction Action(FExecuteAction::CreateStatic(&ToggleEnabledState, TWeakObjectPtr<UNiagaraStackItem>(&StackItem)),
					FCanExecuteAction(),
					FIsActionChecked::CreateUObject(&StackItem, &UNiagaraStackItem::GetIsEnabled));
				MenuBuilder.AddMenuEntry(
					LOCTEXT("IsEnabled", "Is Enabled"),
					LOCTEXT("ToggleEnabledToolTip", "Toggle enabled/disabled state"),
					FSlateIcon(),
					Action,
					NAME_None,
					EUserInterfaceActionType::Check);
			}

			if (StackItem.SupportsDelete())
			{
				FText CanDeleteMessage;
				bool bCanDelete = StackItem.TestCanDeleteWithMessage(CanDeleteMessage);
				MenuBuilder.AddMenuEntry(
					LOCTEXT("DeleteModule", "Delete Item"),
					CanDeleteMessage,
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateStatic(&DeleteItem, TWeakObjectPtr<UNiagaraStackItem>(&StackItem)),
						FCanExecuteAction::CreateLambda([=]() { return bCanDelete; })));

			}
		}
		MenuBuilder.EndSection();
		return true;
	}
	return false;
}

void ShowInsertModuleMenu(TWeakObjectPtr<UNiagaraStackModuleItem> StackModuleItemWeak, int32 InsertOffset, TWeakPtr<SWidget> TargetWidgetWeak)
{
	UNiagaraStackModuleItem* StackModuleItem = StackModuleItemWeak.Get();
	TSharedPtr<SWidget> TargetWidget = TargetWidgetWeak.Pin();
	if (StackModuleItem != nullptr && TargetWidget.IsValid())
	{
		TSharedRef<SWidget> MenuContent = SNew(SNiagaraStackItemGroupAddMenu, StackModuleItem->GetGroupAddUtilities(), StackModuleItem->GetModuleIndex() + InsertOffset);
		FGeometry ThisGeometry = TargetWidget->GetCachedGeometry();
		bool bAutoAdjustForDpiScale = false; // Don't adjust for dpi scale because the push menu command is expecting an unscaled position.
		FVector2D MenuPosition = FSlateApplication::Get().CalculatePopupWindowPosition(ThisGeometry.GetLayoutBoundingRect(), MenuContent->GetDesiredSize(), bAutoAdjustForDpiScale);
		FSlateApplication::Get().PushMenu(TargetWidget.ToSharedRef(), FWidgetPath(), MenuContent, MenuPosition, FPopupTransitionEffect::ContextMenu);
	}
}

bool FNiagaraStackEditorWidgetsUtilities::AddStackModuleItemContextMenuActions(FMenuBuilder& MenuBuilder, UNiagaraStackModuleItem& StackModuleItem, TSharedRef<SWidget> TargetWidget)
{
	MenuBuilder.BeginSection("ModuleActions", LOCTEXT("ModuleActions", "Module Actions"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("InsertModuleAbove", "Insert Above"),
			LOCTEXT("InsertModuleAboveToolTip", "Insert a new module above this module in the stack."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&ShowInsertModuleMenu, TWeakObjectPtr<UNiagaraStackModuleItem>(&StackModuleItem), 0, TWeakPtr<SWidget>(TargetWidget))));

		MenuBuilder.AddMenuEntry(
			LOCTEXT("InsertModuleBelow", "Insert Below"),
			LOCTEXT("InsertModuleBelowToolTip", "Insert a new module below this module in the stack."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&ShowInsertModuleMenu, TWeakObjectPtr<UNiagaraStackModuleItem>(&StackModuleItem), 1, TWeakPtr<SWidget>(TargetWidget))));
	}
	MenuBuilder.EndSection();
	return true;
}

#undef LOCTEXT_NAMESPACE