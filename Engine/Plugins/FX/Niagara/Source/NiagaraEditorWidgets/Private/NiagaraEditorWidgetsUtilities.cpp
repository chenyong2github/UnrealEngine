// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraEditorWidgetsUtilities.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "ViewModels/Stack/NiagaraStackItem.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "Stack/SNiagaraStackItemGroupAddMenu.h"
#include "NiagaraEditorWidgetsStyle.h"
#include "NiagaraEditorCommon.h"
#include "NiagaraClipboard.h"
#include "NiagaraEditorModule.h"

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
		else if (ExecutionSubcategoryName == UNiagaraStackEntry::FExecutionSubcategoryNames::ShaderStage)
		{
			return "NiagaraEditor.Stack.ShaderStageIconHighlighted";
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
		else if (ExecutionSubcategoryName == UNiagaraStackEntry::FExecutionSubcategoryNames::ShaderStage)
		{
			return "NiagaraEditor.Stack.ShaderStageIcon";
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
	if (StackItem.SupportsChangeEnabled())
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

TSharedRef<FDragDropOperation> FNiagaraStackEditorWidgetsUtilities::ConstructDragDropOperationForStackEntries(const TArray<UNiagaraStackEntry*>& DraggedEntries)
{
	TSharedRef<FNiagaraStackEntryDragDropOp> DragDropOp = MakeShared<FNiagaraStackEntryDragDropOp>(DraggedEntries);
	DragDropOp->CurrentHoverText = DraggedEntries.Num() == 1 
		? DraggedEntries[0]->GetDisplayName()
		: FText::Format(LOCTEXT("MultipleEntryDragFormat", "{0} (and {1} others)"), DraggedEntries[0]->GetDisplayName(), FText::AsNumber(DraggedEntries.Num() - 1));
	DragDropOp->CurrentIconBrush = FNiagaraEditorWidgetsStyle::Get().GetBrush(
		GetIconNameForExecutionSubcategory(DraggedEntries[0]->GetExecutionSubcategoryName(), true));
	DragDropOp->CurrentIconColorAndOpacity = FNiagaraEditorWidgetsStyle::Get().GetColor(
		GetIconColorNameForExecutionCategory(DraggedEntries[0]->GetExecutionCategoryName()));
	DragDropOp->SetupDefaults();
	DragDropOp->Construct();
	return DragDropOp;
}

void FNiagaraStackEditorWidgetsUtilities::HandleDragLeave(const FDragDropEvent& InDragDropEvent)
{
	if (InDragDropEvent.GetOperation().IsValid())
	{
		TSharedPtr<FDecoratedDragDropOp> DecoratedDragDropOp = InDragDropEvent.GetOperationAs<FDecoratedDragDropOp>();
		if (DecoratedDragDropOp.IsValid())
		{
			DecoratedDragDropOp->ResetToDefaultToolTip();
		}
	}
}

TOptional<EItemDropZone> FNiagaraStackEditorWidgetsUtilities::RequestDropForStackEntry(const FDragDropEvent& InDragDropEvent, EItemDropZone InDropZone, UNiagaraStackEntry* InTargetEntry, UNiagaraStackEntry::EDropOptions DropOptions)
{
	TOptional<EItemDropZone> DropZone;
	if (InDragDropEvent.GetOperation().IsValid())
	{
		TSharedPtr<FDecoratedDragDropOp> DecoratedDragDropOp = InDragDropEvent.GetOperationAs<FDecoratedDragDropOp>();
		if (DecoratedDragDropOp.IsValid())
		{
			DecoratedDragDropOp->ResetToDefaultToolTip();
		}

		UNiagaraStackEntry::EDragOptions DragOptions = UNiagaraStackEntry::EDragOptions::None;
		if (InDragDropEvent.IsAltDown() &&
			InDragDropEvent.IsShiftDown() == false &&
			InDragDropEvent.IsControlDown() == false &&
			InDragDropEvent.IsCommandDown() == false)
		{
			DragOptions = UNiagaraStackEntry::EDragOptions::Copy;
		}

		TOptional<UNiagaraStackEntry::FDropRequestResponse> Response = InTargetEntry->CanDrop(UNiagaraStackEntry::FDropRequest(InDragDropEvent.GetOperation().ToSharedRef(), InDropZone, DragOptions, DropOptions));
		if (Response.IsSet())
		{
			if (DecoratedDragDropOp.IsValid() && Response.GetValue().DropMessage.IsEmptyOrWhitespace() == false)
			{
				DecoratedDragDropOp->CurrentHoverText = FText::Format(LOCTEXT("DropFormat", "{0} - {1}"), DecoratedDragDropOp->GetDefaultHoverText(), Response.GetValue().DropMessage);
			}

			if (Response.GetValue().DropZone.IsSet())
			{
				DropZone = Response.GetValue().DropZone.GetValue();
			}
			else
			{
				if (DecoratedDragDropOp.IsValid())
				{
					DecoratedDragDropOp->CurrentIconBrush = FEditorStyle::GetBrush("Icons.Error");
					DecoratedDragDropOp->CurrentIconColorAndOpacity = FLinearColor::White;
				}
			}
		}
	}
	return DropZone;
}

bool FNiagaraStackEditorWidgetsUtilities::HandleDropForStackEntry(const FDragDropEvent& InDragDropEvent, EItemDropZone InDropZone, UNiagaraStackEntry* InTargetEntry, UNiagaraStackEntry::EDropOptions DropOptions)
{
	bool bHandled = false;
	if (InDragDropEvent.GetOperation().IsValid())
	{
		UNiagaraStackEntry::EDragOptions DragOptions = UNiagaraStackEntry::EDragOptions::None;
		if (InDragDropEvent.IsAltDown() &&
			InDragDropEvent.IsShiftDown() == false &&
			InDragDropEvent.IsControlDown() == false &&
			InDragDropEvent.IsCommandDown() == false)
		{
			DragOptions = UNiagaraStackEntry::EDragOptions::Copy;
		}

		UNiagaraStackEntry::FDropRequest DropRequest(InDragDropEvent.GetOperation().ToSharedRef(), InDropZone, DragOptions, DropOptions);
		bHandled = ensureMsgf(InTargetEntry->Drop(DropRequest).IsSet(),
			TEXT("Failed to drop stack entry when it was requested"));
	}
	return bHandled;
}

FString FNiagaraStackEditorWidgetsUtilities::StackEntryToStringForListDebug(UNiagaraStackEntry* StackEntry)
{
	return FString::Printf(TEXT("0x%08x - %s - %s"), StackEntry, *StackEntry->GetClass()->GetName(), *StackEntry->GetDisplayName().ToString());
}

#undef LOCTEXT_NAMESPACE