// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Stack/SNiagaraStackModuleItem.h"
#include "NiagaraEditorWidgetsStyle.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraActions.h"
#include "NiagaraConstants.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeAssignment.h"
#include "Stack/SNiagaraStackItemGroupAddMenu.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Images/SImage.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "SDropTarget.h"
#include "NiagaraEditorUtilities.h"
#include "SGraphActionMenu.h"
#include "NiagaraEditorWidgetsUtilities.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "NiagaraStackModuleItem"

void SNiagaraStackModuleItem::Construct(const FArguments& InArgs, UNiagaraStackModuleItem& InModuleItem, UNiagaraStackViewModel* InStackViewModel)
{
	ModuleItem = &InModuleItem;
	StackEntryItem = ModuleItem;
	StackViewModel = InStackViewModel;

	ChildSlot
	[
		SNew(SDropTarget)
		.OnAllowDrop(this, &SNiagaraStackModuleItem::OnModuleItemAllowDrop)
		.OnDrop(this, &SNiagaraStackModuleItem::OnModuleItemDrop)
		.HorizontalImage(FNiagaraEditorWidgetsStyle::Get().GetBrush("NiagaraEditor.Stack.DropTarget.BorderHorizontal"))
		.VerticalImage(FNiagaraEditorWidgetsStyle::Get().GetBrush("NiagaraEditor.Stack.DropTarget.BorderVertical"))
		.BackgroundColor(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.DropTarget.BackgroundColor"))
		.BackgroundColorHover(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.DropTarget.BackgroundColorHover"))
		.Content()
		[
			SNew(SHorizontalBox)
			// Name
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(2, 0, 0, 0)
			[
				SNew(SNiagaraStackDisplayName, InModuleItem, *InStackViewModel, "NiagaraEditor.Stack.ItemText")
			]
			// Raise Action Menu button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SComboButton)
				.HasDownArrow(true)
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.ForegroundColor(FSlateColor::UseForeground())
				.OnGetMenuContent(this, &SNiagaraStackModuleItem::RaiseActionMenuClicked)
				.ContentPadding(FMargin(2))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Visibility(this, &SNiagaraStackModuleItem::GetRaiseActionMenuVisibility)
				.IsEnabled(this, &SNiagaraStackModuleItem::GetButtonsEnabled)
			]
			// Refresh button
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.IsFocusable(false)
				.ForegroundColor(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.FlatButtonColor"))
				.ToolTipText(LOCTEXT("RefreshTooltip", "Refresh this module"))
				.Visibility(this, &SNiagaraStackModuleItem::GetRefreshVisibility)
				.IsEnabled(this, &SNiagaraStackModuleItem::GetButtonsEnabled)
				.OnClicked(this, &SNiagaraStackModuleItem::RefreshClicked)
				.Content()
				[
					SNew(STextBlock)
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
					.Text(FText::FromString(FString(TEXT("\xf021"))))
				]
			]
			// Delete button
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.IsFocusable(false)
				.ForegroundColor(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.FlatButtonColor"))
				.ToolTipText(this, &SNiagaraStackModuleItem::GetDeleteButtonToolTipText)
				.IsEnabled(this, &SNiagaraStackModuleItem::GetDeleteButtonEnabled)
				.OnClicked(this, &SNiagaraStackModuleItem::DeleteClicked)
				.Content()
				[
					SNew(STextBlock)
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
					.Text(FText::FromString(FString(TEXT("\xf1f8"))))
				]
			]
			// Enabled checkbox
			+ SHorizontalBox::Slot()
			.Padding(2, 0, 0, 0)
			.AutoWidth()
			[
				SNew(SCheckBox)
				.IsChecked(this, &SNiagaraStackModuleItem::GetCheckState)
				.OnCheckStateChanged(this, &SNiagaraStackModuleItem::OnCheckStateChanged)
				.IsEnabled(this, &SNiagaraStackModuleItem::GetEnabledCheckBoxEnabled)
			]
		]
	];
}

void SNiagaraStackModuleItem::SetEnabled(bool bInIsEnabled)
{
	ModuleItem->SetIsEnabled(bInIsEnabled);
}

bool SNiagaraStackModuleItem::CheckEnabledStatus(bool bIsEnabled)
{
	return ModuleItem->GetIsEnabled() == bIsEnabled;
}

void SNiagaraStackModuleItem::FillRowContextMenu(FMenuBuilder& MenuBuilder)
{
	FNiagaraStackEditorWidgetsUtilities::AddStackModuleItemContextMenuActions(MenuBuilder, *ModuleItem, this->AsShared());
	FNiagaraStackEditorWidgetsUtilities::AddStackItemContextMenuActions(MenuBuilder, *ModuleItem);
}

FReply SNiagaraStackModuleItem::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	const UNiagaraNodeFunctionCall& ModuleFunctionCall = ModuleItem->GetModuleNode();
	if (ModuleFunctionCall.FunctionScript != nullptr && ModuleFunctionCall.FunctionScript->IsAsset())
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(const_cast<UNiagaraScript*>(ModuleFunctionCall.FunctionScript));
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SNiagaraStackModuleItem::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (ModuleItem->GetIsModuleScriptReassignmentPending())
	{
		ModuleItem->SetIsModuleScriptReassignmentPending(false);
		ShowReassignModuleScriptMenu();
	}
}

ECheckBoxState SNiagaraStackModuleItem::GetCheckState() const
{
	return ModuleItem->GetIsEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SNiagaraStackModuleItem::OnCheckStateChanged(ECheckBoxState InCheckState)
{
	ModuleItem->SetIsEnabled(InCheckState == ECheckBoxState::Checked);
}

bool SNiagaraStackModuleItem::GetButtonsEnabled() const
{
	return ModuleItem->GetOwnerIsEnabled() && ModuleItem->GetIsEnabled();
}

FText SNiagaraStackModuleItem::GetDeleteButtonToolTipText() const
{
	FText CanDeleteMessage;
	ModuleItem->TestCanDeleteWithMessage(CanDeleteMessage);
	return CanDeleteMessage;
}

bool SNiagaraStackModuleItem::GetDeleteButtonEnabled() const
{
	FText CanDeleteMessage;
	return ModuleItem->TestCanDeleteWithMessage(CanDeleteMessage);
}

bool SNiagaraStackModuleItem::GetEnabledCheckBoxEnabled() const
{
	return ModuleItem->GetOwnerIsEnabled();
}

EVisibility SNiagaraStackModuleItem::GetRaiseActionMenuVisibility() const
{
	return CanRaiseActionMenu() ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SNiagaraStackModuleItem::GetRefreshVisibility() const
{
	return ModuleItem->CanRefresh() ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SNiagaraStackModuleItem::DeleteClicked()
{
	ModuleItem->Delete();
	return FReply::Handled();
}

TSharedRef<SWidget> SNiagaraStackModuleItem::RaiseActionMenuClicked()
{
	if (CanRaiseActionMenu())
	{
		UNiagaraNodeAssignment* AssignmentNode = Cast<UNiagaraNodeAssignment>(&ModuleItem->GetModuleNode());
		if (AssignmentNode != nullptr)
		{
			UNiagaraNodeOutput* OutputNode = ModuleItem->GetOutputNode();
			if (OutputNode)
			{
				FMenuBuilder MenuBuilder(true, nullptr);
				/*MenuBuilder.AddMenuEntry(
					LOCTEXT("MergeLabel", "Merge Up"),
					LOCTEXT("MergeToolTip", "If a Set Variables node precedes this one in the stack, merge this node (and all variable binding logic) into that stack."),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateUObject(AssignmentNode, &UNiagaraNodeAssignment::MergeUp)));*/
				MenuBuilder.AddSubMenu(LOCTEXT("AddVariables", "Add Variable"), 
					LOCTEXT("AddVariablesTooltip", "Add another variable to the end of the list"),
					FNewMenuDelegate::CreateLambda([OutputNode, AssignmentNode](FMenuBuilder& SubMenuBuilder)
				{
					AssignmentNode->BuildAddParameterMenu(SubMenuBuilder, OutputNode->GetUsage(), OutputNode);
				}));
				MenuBuilder.AddSubMenu(LOCTEXT("CreateVariables", "Create New Variable"),
					LOCTEXT("CreateVariablesTooltip", "Create a new variable and set its value"),
					FNewMenuDelegate::CreateLambda([OutputNode, AssignmentNode](FMenuBuilder& SubMenuBuilder)
				{
					AssignmentNode->BuildCreateParameterMenu(SubMenuBuilder, OutputNode->GetUsage(), OutputNode);
				}));

				return MenuBuilder.MakeWidget();
			}
		}
	}

	return SNullWidget::NullWidget;
}

bool SNiagaraStackModuleItem::CanRaiseActionMenu() const
{
	return Cast<UNiagaraNodeAssignment>(&ModuleItem->GetModuleNode()) != nullptr;
}

FReply SNiagaraStackModuleItem::RefreshClicked()
{
	ModuleItem->Refresh();
	return FReply::Handled();
}

FReply SNiagaraStackModuleItem::OnModuleItemDrop(TSharedPtr<FDragDropOperation> DragDropOperation)
{
	if (DragDropOperation->IsOfType<FNiagaraParameterDragOperation>())
	{
		TSharedPtr<FNiagaraParameterDragOperation> InputDragDropOperation = StaticCastSharedPtr<FNiagaraParameterDragOperation>(DragDropOperation);
		TSharedPtr<FNiagaraParameterAction> Action = StaticCastSharedPtr<FNiagaraParameterAction>(InputDragDropOperation->GetSourceAction());
		if (Action.IsValid() && ModuleItem->CanAddInput(Action->GetParameter()))
		{
			ModuleItem->AddInput(Action->GetParameter());
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

bool SNiagaraStackModuleItem::OnModuleItemAllowDrop(TSharedPtr<FDragDropOperation> DragDropOperation)
{
	if (DragDropOperation->IsOfType<FNiagaraParameterDragOperation>())
	{
		TSharedPtr<FNiagaraParameterDragOperation> InputDragDropOperation = StaticCastSharedPtr<FNiagaraParameterDragOperation>(DragDropOperation);
		TSharedPtr<FNiagaraParameterAction> Action = StaticCastSharedPtr<FNiagaraParameterAction>(InputDragDropOperation->GetSourceAction());
		return Action.IsValid() && ModuleItem->CanAddInput(Action->GetParameter());
	}

	return false;
}

void ReassignModuleScript(UNiagaraStackModuleItem* ModuleItem, FAssetData NewModuleScriptAsset)
{
	UNiagaraScript* NewModuleScript = Cast<UNiagaraScript>(NewModuleScriptAsset.GetAsset());
	if (NewModuleScript != nullptr)
	{
		ModuleItem->ReassignModuleScript(NewModuleScript);
	}
}

void OnActionSelected(const TArray<TSharedPtr<FEdGraphSchemaAction>>& SelectedActions, ESelectInfo::Type InSelectionType)
{
	if (SelectedActions.Num() == 1 && (InSelectionType == ESelectInfo::OnKeyPress || InSelectionType == ESelectInfo::OnMouseClick))
	{
		TSharedPtr<FNiagaraMenuAction> Action = StaticCastSharedPtr<FNiagaraMenuAction>(SelectedActions[0]);
		if (Action.IsValid())
		{
			FSlateApplication::Get().DismissAllMenus();
			Action->ExecuteAction();
		}
	}
}

void CollectModuleActions(FGraphActionListBuilderBase& ModuleActions, UNiagaraStackModuleItem* ModuleItem)
{
	TArray<FAssetData> ModuleAssets;
	FNiagaraEditorUtilities::FGetFilteredScriptAssetsOptions ModuleScriptFilterOptions;
	ModuleScriptFilterOptions.ScriptUsageToInclude = ENiagaraScriptUsage::Module;
	ModuleScriptFilterOptions.TargetUsageToMatch = ModuleItem->GetOutputNode()->GetUsage();
	FNiagaraEditorUtilities::GetFilteredScriptAssets(ModuleScriptFilterOptions, ModuleAssets);
	for (const FAssetData& ModuleAsset : ModuleAssets)
	{
		FText Category;
		ModuleAsset.GetTagValue(GET_MEMBER_NAME_CHECKED(UNiagaraScript, Category), Category);
		if (Category.IsEmptyOrWhitespace())
		{
			Category = LOCTEXT("ModuleNotCategorized", "Uncategorized Modules");
		}

		FString DisplayNameString = FName::NameToDisplayString(ModuleAsset.AssetName.ToString(), false);
		FText DisplayName = FText::FromString(DisplayNameString);

		FText AssetDescription;
		ModuleAsset.GetTagValue(GET_MEMBER_NAME_CHECKED(UNiagaraScript, Description), AssetDescription);
		FText Description = FNiagaraEditorUtilities::FormatScriptAssetDescription(AssetDescription, ModuleAsset.ObjectPath);

		FText Keywords;
		ModuleAsset.GetTagValue(GET_MEMBER_NAME_CHECKED(UNiagaraScript, Keywords), Keywords);

		TSharedPtr<FNiagaraMenuAction> ModuleAction(new FNiagaraMenuAction(Category, DisplayName, Description, 0, Keywords,
			FNiagaraMenuAction::FOnExecuteStackAction::CreateStatic(&ReassignModuleScript, ModuleItem, ModuleAsset)));
		ModuleActions.AddAction(ModuleAction);
	}
}

void SNiagaraStackModuleItem::ShowReassignModuleScriptMenu()
{
	TSharedRef<SBorder> MenuWidget = SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		.Padding(5)
		[
			SNew(SBox)
			.WidthOverride(300)
			.HeightOverride(400)
			[
				SNew(SGraphActionMenu)
				.OnActionSelected_Static(OnActionSelected)
				.OnCollectAllActions_Static(CollectModuleActions, ModuleItem)
				.ShowFilterTextBox(true)
			]
		];

	FGeometry ThisGeometry = GetCachedGeometry();
	bool bAutoAdjustForDpiScale = false; // Don't adjust for dpi scale because the push menu command is expecting an unscaled position.
	FVector2D MenuPosition = FSlateApplication::Get().CalculatePopupWindowPosition(ThisGeometry.GetLayoutBoundingRect(), MenuWidget->GetDesiredSize(), bAutoAdjustForDpiScale);
	FSlateApplication::Get().PushMenu(AsShared(), FWidgetPath(), MenuWidget, MenuPosition, FPopupTransitionEffect::ContextMenu);
}

#undef LOCTEXT_NAMESPACE