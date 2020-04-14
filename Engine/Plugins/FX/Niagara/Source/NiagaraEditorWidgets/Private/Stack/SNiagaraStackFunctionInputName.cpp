// Copyright Epic Games, Inc. All Rights Reserved.

#include "Stack/SNiagaraStackFunctionInputName.h"
#include "NiagaraEditorWidgetsStyle.h"
#include "NiagaraEditorStyle.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SNiagaraParameterName.h"
#include "NiagaraNodeAssignment.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraEditorUtilities.h"
#include "HAL/PlatformApplicationMisc.h"

#define LOCTEXT_NAMESPACE "NiagaraStackFunctionInputName"

void SNiagaraStackFunctionInputName::Construct(const FArguments& InArgs, UNiagaraStackFunctionInput* InFunctionInput, UNiagaraStackViewModel* InStackViewModel)
{
	FunctionInput = InFunctionInput;
	StackViewModel = InStackViewModel;
	StackEntryItem = InFunctionInput;
	IsSelected = InArgs._IsSelected;

	TSharedPtr<SHorizontalBox> NameBox;
	ChildSlot
	[
		SAssignNew(NameBox, SHorizontalBox)
		.IsEnabled_UObject(FunctionInput, &UNiagaraStackEntry::GetOwnerIsEnabled)
	];

	// Edit condition checkbox
	NameBox->AddSlot()
	.AutoWidth()
	.Padding(0, 0, 3, 0)
	[
		SNew(SCheckBox)
		.Visibility(this, &SNiagaraStackFunctionInputName::GetEditConditionCheckBoxVisibility)
		.IsChecked(this, &SNiagaraStackFunctionInputName::GetEditConditionCheckState)
		.OnCheckStateChanged(this, &SNiagaraStackFunctionInputName::OnEditConditionCheckStateChanged)
	];
	
	// Name Label
	if(FunctionInput->GetInputFunctionCallNode().IsA<UNiagaraNodeAssignment>())
	{
		NameBox->AddSlot()
		.VAlign(VAlign_Center)
		[
			SAssignNew(ParameterTextBlock, SNiagaraParameterNameTextBlock)
			.EditableTextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterInlineEditableText")
			.ParameterText_UObject(FunctionInput, &UNiagaraStackEntry::GetDisplayName)
			.IsReadOnly(FunctionInput->SupportsRename() == false)
			.IsEnabled(this, &SNiagaraStackFunctionInputName::GetIsEnabled)
			.IsSelected(this, &SNiagaraStackFunctionInputName::GetIsNameWidgetSelected)
			.OnTextCommitted(this, &SNiagaraStackFunctionInputName::OnNameTextCommitted)
			//.HighlightText_UObject(InStackViewModel, &UNiagaraStackViewModel::GetCurrentSearchText)
			.ToolTipText(this, &SNiagaraStackFunctionInputName::GetToolTipText)
		];
	}
	else
	{
		NameBox->AddSlot()
		.VAlign(VAlign_Center)
		[
			SAssignNew(NameTextBlock, SInlineEditableTextBlock)
			.Style(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterInlineEditableText")
			.Text_UObject(FunctionInput, &UNiagaraStackEntry::GetDisplayName)
			.IsReadOnly(this, &SNiagaraStackFunctionInputName::GetIsNameReadOnly)
			.IsEnabled(this, &SNiagaraStackFunctionInputName::GetIsEnabled)
			.IsSelected(this, &SNiagaraStackFunctionInputName::GetIsNameWidgetSelected)
			.OnTextCommitted(this, &SNiagaraStackFunctionInputName::OnNameTextCommitted)
			.HighlightText_UObject(InStackViewModel, &UNiagaraStackViewModel::GetCurrentSearchText)
			.ToolTipText(this, &SNiagaraStackFunctionInputName::GetToolTipText)
		];
	}
}

void SNiagaraStackFunctionInputName::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (FunctionInput->GetIsRenamePending())
	{
		if (NameTextBlock.IsValid())
		{
			NameTextBlock->EnterEditingMode();
		}
		else if (ParameterTextBlock.IsValid())
		{
			ParameterTextBlock->EnterEditingMode();
		}
		FunctionInput->SetIsRenamePending(false);
	}
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
}

void SNiagaraStackFunctionInputName::FillRowContextMenu(class FMenuBuilder& MenuBuilder)
{
	if (FunctionInput->GetInputFunctionCallNode().IsA<UNiagaraNodeAssignment>())
	{
		MenuBuilder.BeginSection("Parameter", LOCTEXT("ParameterHeader", "Parameter"));
		{
			TAttribute<FText> CopyReferenceToolTip;
			CopyReferenceToolTip.Bind(this, &SNiagaraStackFunctionInputName::GetCopyParameterReferenceToolTip);
			MenuBuilder.AddMenuEntry(
				LOCTEXT("CopyParameterReference", "Copy Reference"),
				CopyReferenceToolTip,
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(this, &SNiagaraStackFunctionInputName::OnCopyParameterReference),
					FCanExecuteAction::CreateSP(this, &SNiagaraStackFunctionInputName::CanCopyParameterReference)));

			MenuBuilder.AddSubMenu(
				LOCTEXT("ChangeNamespace", "Namespace"),
				LOCTEXT("ChangeNamespaceToolTip", "Select a new namespace for the selected parameter."),
				FNewMenuDelegate::CreateSP(this, &SNiagaraStackFunctionInputName::GetChangeNamespaceSubMenu));

			MenuBuilder.AddSubMenu(
				LOCTEXT("ChangeNamespaceModifier", "Namespace Modifier"),
				LOCTEXT("ChangeNamespaceModifierToolTip", "Edit the namespace modifier for the selected parameter."),
				FNewMenuDelegate::CreateSP(this, &SNiagaraStackFunctionInputName::GetChangeNamespaceModifierSubMenu));
		}
		MenuBuilder.EndSection();
	}
}

EVisibility SNiagaraStackFunctionInputName::GetEditConditionCheckBoxVisibility() const
{
	return FunctionInput->GetHasEditCondition() && FunctionInput->GetShowEditConditionInline() ? EVisibility::Visible : EVisibility::Collapsed;
}

ECheckBoxState SNiagaraStackFunctionInputName::GetEditConditionCheckState() const
{
	return FunctionInput->GetHasEditCondition() && FunctionInput->GetEditConditionEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SNiagaraStackFunctionInputName::OnEditConditionCheckStateChanged(ECheckBoxState InCheckState)
{
	FunctionInput->SetEditConditionEnabled(InCheckState == ECheckBoxState::Checked);
}

bool SNiagaraStackFunctionInputName::GetIsNameReadOnly() const
{
	return FunctionInput->SupportsRename() == false;
}

bool SNiagaraStackFunctionInputName::GetIsNameWidgetSelected() const
{
	return IsSelected.Get();
}

bool SNiagaraStackFunctionInputName::GetIsEnabled() const
{
	return FunctionInput->GetOwnerIsEnabled() && (FunctionInput->GetHasEditCondition() == false || FunctionInput->GetEditConditionEnabled());
}

FText SNiagaraStackFunctionInputName::GetToolTipText() const
{
	// The engine ticks tooltips before widgets so it's possible for the footer to be finalized when
	// the widgets haven't been recreated.
	if (FunctionInput->IsFinalized())
	{
		return FText();
	}
	return FunctionInput->GetTooltipText();
}

void SNiagaraStackFunctionInputName::OnNameTextCommitted(const FText& InText, ETextCommit::Type InCommitType)
{
	FunctionInput->OnRenamed(InText);
}

void SNiagaraStackFunctionInputName::GetChangeNamespaceSubMenu(FMenuBuilder& MenuBuilder)
{
	FName InputParameterName = *FunctionInput->GetDisplayName().ToString();

	TArray<FName> NamespacesForWriteParameters;
	FunctionInput->GetNamespacesForNewWriteParameters(NamespacesForWriteParameters);

	TArray<FNiagaraParameterUtilities::FChangeNamespaceMenuData> MenuData;
	FNiagaraParameterUtilities::GetChangeNamespaceMenuData(*FunctionInput->GetDisplayName().ToString(), FNiagaraParameterUtilities::EParameterContext::System, MenuData);
	for (const FNiagaraParameterUtilities::FChangeNamespaceMenuData& MenuDataItem : MenuData)
	{
		if (MenuDataItem.Metadata.Namespaces.Num() == 1 && NamespacesForWriteParameters.Contains(MenuDataItem.Metadata.Namespaces[0]))
		{
			bool bCanChange = MenuDataItem.bCanChange;
			FUIAction Action = FUIAction(
				FExecuteAction::CreateSP(this, &SNiagaraStackFunctionInputName::OnChangeNamespace, MenuDataItem.Metadata),
				FCanExecuteAction::CreateLambda([bCanChange]() { return bCanChange; }));

			TSharedRef<SWidget> MenuItemWidget = FNiagaraParameterUtilities::CreateNamespaceMenuItemWidget(MenuDataItem.NamespaceParameterName, MenuDataItem.CanChangeToolTip);
			MenuBuilder.AddMenuEntry(Action, MenuItemWidget, NAME_None, MenuDataItem.CanChangeToolTip);
		}
	}
}

void SNiagaraStackFunctionInputName::OnChangeNamespace(FNiagaraNamespaceMetadata Metadata)
{
	FName InputParameterName = *FunctionInput->GetDisplayName().ToString();
	FName NewName = FNiagaraParameterUtilities::ChangeNamespace(InputParameterName, Metadata);
	if (NewName != NAME_None)
	{
		FScopedTransaction Transaction(LOCTEXT("ChangeNamespaceTransaction", "Change namespace"));
		FunctionInput->OnRenamed(FText::FromName(NewName));
	}
}

void SNiagaraStackFunctionInputName::GetChangeNamespaceModifierSubMenu(FMenuBuilder& MenuBuilder)
{
	TAttribute<FText> AddToolTip;
	AddToolTip.Bind(this, &SNiagaraStackFunctionInputName::GetAddNamespaceModifierToolTip);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddNamespaceModifier", "Add"),
		AddToolTip,
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &SNiagaraStackFunctionInputName::OnAddNamespaceModifier),
			FCanExecuteAction::CreateSP(this, &SNiagaraStackFunctionInputName::CanAddNamespaceModifier)));

	TAttribute<FText> RemoveToolTip;
	RemoveToolTip.Bind(this, &SNiagaraStackFunctionInputName::GetRemoveNamespaceModifierToolTip);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("RemoveNamespaceModifier", "Remove"),
		RemoveToolTip,
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &SNiagaraStackFunctionInputName::OnRemoveNamespaceModifier),
			FCanExecuteAction::CreateSP(this, &SNiagaraStackFunctionInputName::CanRemoveNamespaceModifier)));

	TAttribute<FText> EditToolTip;
	EditToolTip.Bind(this, &SNiagaraStackFunctionInputName::GetEditNamespaceModifierToolTip);
	MenuBuilder.AddMenuEntry(
		LOCTEXT("EditNamespaceModifier", "Edit"),
		EditToolTip,
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &SNiagaraStackFunctionInputName::OnEditNamespaceModifier),
			FCanExecuteAction::CreateSP(this, &SNiagaraStackFunctionInputName::CanEditNamespaceModifier)));
}

FText SNiagaraStackFunctionInputName::GetAddNamespaceModifierToolTip() const
{
	FName InputParameterName = *FunctionInput->GetDisplayName().ToString();
	FText AddMessage;
	FNiagaraParameterUtilities::TestCanAddNamespaceModifierWithMessage(InputParameterName, AddMessage);
	return AddMessage;
}

bool SNiagaraStackFunctionInputName::CanAddNamespaceModifier() const
{
	FName InputParameterName = *FunctionInput->GetDisplayName().ToString();
	FText Unused;
	return FNiagaraParameterUtilities::TestCanAddNamespaceModifierWithMessage(InputParameterName, Unused);
}

void SNiagaraStackFunctionInputName::OnAddNamespaceModifier()
{
	FName InputParameterName = *FunctionInput->GetDisplayName().ToString();
	FName NewName = FNiagaraParameterUtilities::AddNamespaceModifier(InputParameterName);
	if (NewName != NAME_None)
	{
		FScopedTransaction Transaction(LOCTEXT("AddNamespaceModifierTransaction", "Add namespace modifier."));
		FunctionInput->OnRenamed(FText::FromName(NewName));
	}
}

FText SNiagaraStackFunctionInputName::GetRemoveNamespaceModifierToolTip() const
{
	FName InputParameterName = *FunctionInput->GetDisplayName().ToString();
	FText RemoveMessage;
	FNiagaraParameterUtilities::TestCanRemoveNamespaceModifierWithMessage(InputParameterName, RemoveMessage);
	return RemoveMessage;
}

bool SNiagaraStackFunctionInputName::CanRemoveNamespaceModifier() const
{
	FName InputParameterName = *FunctionInput->GetDisplayName().ToString();
	FText Unused;
	return FNiagaraParameterUtilities::TestCanRemoveNamespaceModifierWithMessage(InputParameterName, Unused);
}

void SNiagaraStackFunctionInputName::OnRemoveNamespaceModifier()
{
	FName InputParameterName = *FunctionInput->GetDisplayName().ToString();
	FText Unused;
	FName NewName = FNiagaraParameterUtilities::RemoveNamespaceModifier(InputParameterName);
	if (NewName != NAME_None)
	{
		FScopedTransaction Transaction(LOCTEXT("RemoveNamespaceModifierTransaction", "Remove namespace modifier."));
		FunctionInput->OnRenamed(FText::FromName(NewName));
	}
}

FText SNiagaraStackFunctionInputName::GetEditNamespaceModifierToolTip() const
{
	FName InputParameterName = *FunctionInput->GetDisplayName().ToString();
	FText EditMessage;
	FNiagaraParameterUtilities::TestCanEditNamespaceModifierWithMessage(InputParameterName, EditMessage);
	return EditMessage;
}

bool SNiagaraStackFunctionInputName::CanEditNamespaceModifier() const
{
	FName InputParameterName = *FunctionInput->GetDisplayName().ToString();
	FText Unused;
	return FNiagaraParameterUtilities::TestCanEditNamespaceModifierWithMessage(InputParameterName, Unused);
}

void SNiagaraStackFunctionInputName::OnEditNamespaceModifier()
{
	if (ParameterTextBlock.IsValid())
	{
		ParameterTextBlock->EnterNamespaceModifierEditingMode();
	}
}

FText SNiagaraStackFunctionInputName::GetCopyParameterReferenceToolTip() const
{
	return LOCTEXT("CopyReferenceToolTip", "Copy a string reference for this parameter to the clipboard.\nThis reference can be used in expressions and custom HLSL nodes.");
}

bool SNiagaraStackFunctionInputName::CanCopyParameterReference() const
{
	return true;
}

void SNiagaraStackFunctionInputName::OnCopyParameterReference()
{
	FPlatformApplicationMisc::ClipboardCopy(*FunctionInput->GetDisplayName().ToString());
}

#undef LOCTEXT_NAMESPACE
