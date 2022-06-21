// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCActionPanel.h"

#include "RemoteControlField.h"
#include "RemoteControlPreset.h"

#include "Action/RCActionContainer.h"
#include "Action/RCFunctionAction.h"
#include "Action/RCPropertyAction.h"

#include "Behaviour/RCBehaviour.h"
#include "Controller/RCController.h"

#include "EdGraphSchema_K2.h"
#include "EdGraph/EdGraphPin.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/MessageDialog.h"

#include "SlateOptMacros.h"
#include "SRCActionPanelList.h"
#include "SRCBehaviourDetails.h"
#include "Styling/RemoteControlStyles.h"

#include "UI/Behaviour/RCBehaviourModel.h"
#include "UI/Panels/SRCDockPanel.h"
#include "UI/RemoteControlPanelStyle.h"
#include "UI/SRemoteControlPanel.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "SRCActionPanel"

TSharedRef<SBox> SRCActionPanel::NoneSelectedWidget = SNew(SBox)
			.Padding(0.f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoneSelected", "Select a behavior to view its actions."))
				.TextStyle(&FAppStyle::GetWidgetStyle<FTextBlockStyle>("NormalText"))
				.Justification(ETextJustify::Center)
			];

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCActionPanel::Construct(const FArguments& InArgs, const TSharedRef<SRemoteControlPanel>& InPanel)
{
	SRCLogicPanelBase::Construct(SRCLogicPanelBase::FArguments(), InPanel);
	
	RCPanelStyle = &FRemoteControlPanelStyle::Get()->GetWidgetStyle<FRCPanelStyle>("RemoteControlPanel.MinorPanel");

	WrappedBoxWidget = SNew(SBox);
	UpdateWrappedWidget();
	
	ChildSlot
		.Padding(RCPanelStyle->PanelPadding)
		[
			WrappedBoxWidget.ToSharedRef()
		];

	// Register delegates
	InPanel->OnBehaviourSelectionChanged.AddSP(this, &SRCActionPanel::OnBehaviourSelectionChanged);
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCActionPanel::OnBehaviourSelectionChanged(TSharedPtr<FRCBehaviourModel> InBehaviourItem)
{
	SelectedBehaviourItemWeakPtr = InBehaviourItem;
	UpdateWrappedWidget(InBehaviourItem);
}

void SRCActionPanel::UpdateWrappedWidget(TSharedPtr<FRCBehaviourModel> InBehaviourItem)
{
	if (InBehaviourItem.IsValid())
	{
		// Action Dock Panel
		TSharedPtr<SRCMinorPanel> ActionDockPanel = SNew(SRCMinorPanel)
			.HeaderLabel(LOCTEXT("ActionsLabel", "Actions"))
			[
				SAssignNew(ActionPanelList, SRCActionPanelList, SharedThis(this), InBehaviourItem)
			];

		// Add New Action Button
		const TSharedRef<SWidget> AddNewActionButton = SNew(SComboButton)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Add Action")))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ButtonStyle(&RCPanelStyle->FlatButtonStyle)
			.ForegroundColor(FSlateColor::UseForeground())
			.CollapseMenuOnParentFocus(true)
			.HasDownArrow(false)
			.ContentPadding(FMargin(4.f, 2.f))
			.ButtonContent()
			[
				SNew(SBox)
				.WidthOverride(RCPanelStyle->IconSize.X)
				.HeightOverride(RCPanelStyle->IconSize.Y)
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(FAppStyle::GetBrush("Icons.PlusCircle"))
				]
			]
			.MenuContent()
			[
				GetActionMenuContentWidget()
			];
		
		// Add All Button
		TSharedRef<SWidget> AddAllActionsButton = SNew(SButton)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Add All Actions")))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ForegroundColor(FSlateColor::UseForeground())
			.ButtonStyle(&RCPanelStyle->FlatButtonStyle)
			.ToolTipText(LOCTEXT("AddAllToolTip", "Adds all the available actions."))
			.OnClicked(this, &SRCActionPanel::OnAddAllFields)
			.Visibility(this, &SRCActionPanel::HandleAddAllButtonVisibility)
			[
				SNew(SBox)
				.WidthOverride(RCPanelStyle->IconSize.X)
				.HeightOverride(RCPanelStyle->IconSize.Y)
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(FAppStyle::GetBrush("Icons.Duplicate"))
				]
			];
		
		// Empty All Button
		TSharedRef<SWidget> EmptyAllButton = SNew(SButton)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Empty All Actions")))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ForegroundColor(FSlateColor::UseForeground())
			.ButtonStyle(&RCPanelStyle->FlatButtonStyle)
			.ToolTipText(LOCTEXT("EmptyAllToolTip", "Deletes all the actions."))
			.OnClicked(this, &SRCActionPanel::RequestDeleteAllItems)
			.Visibility_Lambda([this]() { return ActionPanelList.IsValid() && !ActionPanelList->IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SBox)
				.WidthOverride(RCPanelStyle->IconSize.X)
				.HeightOverride(RCPanelStyle->IconSize.Y)
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(FAppStyle::GetBrush("Icons.Delete"))
				]
			];

		ActionDockPanel->AddHeaderToolbarItem(EToolbar::Left, AddNewActionButton);
		ActionDockPanel->AddHeaderToolbarItem(EToolbar::Right, AddAllActionsButton);
		ActionDockPanel->AddHeaderToolbarItem(EToolbar::Right, EmptyAllButton);

		// Duplicate Dock Panel
		TSharedPtr<SRCMinorPanel> DuplicateDockPanel = SNew(SRCMinorPanel)
			.HeaderLabel(LOCTEXT("DuplicateLabel", "Duplicate"))
			[
				SNew(SRCBehaviourDetails, SharedThis(this), InBehaviourItem.ToSharedRef())
			];

		// Duplicate Behaviour Button
		const TSharedRef<SWidget> DuplicateBehaviourButton = SNew(SButton)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Duplicate Behavior")))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ButtonStyle(&RCPanelStyle->FlatButtonStyle)
			.ForegroundColor(FSlateColor::UseForeground())
			.ContentPadding(FMargin(4.f, 2.f))
			.OnClicked(this, &SRCActionPanel::OnClickOverrideBlueprintButton)
			[
				SNew(SBox)
				.WidthOverride(RCPanelStyle->IconSize.X)
				.HeightOverride(RCPanelStyle->IconSize.Y)
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(FAppStyle::GetBrush("Icons.PlusCircle"))
				]
			];
		
		// Toggle Behaviour Button
		// TODO : Replace this with the actual algorithm to disable or enable behaviour.
		const bool bIsChecked = FMath::RandBool();
		const TSharedRef<SWidget> ToggleBehaviourButton = SNew(SCheckBox)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Duplicate Behavior")))
			.HAlign(HAlign_Center)
			.Style(&RCPanelStyle->ToggleButtonStyle)
			.ForegroundColor(FSlateColor::UseForeground())
			.IsChecked_Lambda([bIsChecked]() { return bIsChecked ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; });

		DuplicateDockPanel->AddHeaderToolbarItem(EToolbar::Left, DuplicateBehaviourButton);
		DuplicateDockPanel->AddHeaderToolbarItem(EToolbar::Right, ToggleBehaviourButton);

		TSharedRef<SRCMajorPanel> ActionsPanel = SNew(SRCMajorPanel)
			.EnableFooter(false)
			.EnableHeader(false)
			.ChildOrientation(Orient_Vertical);

		ActionsPanel->AddPanel(DuplicateDockPanel.ToSharedRef(), 0.5f);
		ActionsPanel->AddPanel(ActionDockPanel.ToSharedRef(), 0.5f);

		WrappedBoxWidget->SetContent(ActionsPanel);
	}
	else
	{
		WrappedBoxWidget->SetContent(NoneSelectedWidget);
	}
}

FReply SRCActionPanel::OnClickOverrideBlueprintButton()
{
	if (!SelectedBehaviourItemWeakPtr.IsValid())
	{
		return FReply::Unhandled();
	}

	if (TSharedPtr<FRCBehaviourModel> Behaviour = SelectedBehaviourItemWeakPtr.Pin())
	{
		Behaviour->OnOverrideBlueprint();
	}

	return FReply::Handled();
}

TSharedRef<SWidget> SRCActionPanel::GetActionMenuContentWidget()
{
	constexpr bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, nullptr);

	if (!SelectedBehaviourItemWeakPtr.IsValid())
	{
		return MenuBuilder.MakeWidget();
	}

	// List of exposed entities
	if (URemoteControlPreset* Preset = GetPreset())
	{
		const TArray<TWeakPtr<FRemoteControlField>>& RemoteControlFields = Preset->GetExposedEntities<FRemoteControlField>();
		for (const TWeakPtr<FRemoteControlField>& RemoteControlFieldWeakPtr : RemoteControlFields)
		{
			if (const TSharedPtr<FRemoteControlField> RemoteControlField = RemoteControlFieldWeakPtr.Pin())
			{
				if (const URCBehaviour* Behaviour = SelectedBehaviourItemWeakPtr.Pin()->GetBehaviour())
				{
					// Skip if we already have an Action created for this exposed entity
					if (Behaviour->ActionContainer->FindActionByFieldId(RemoteControlField->GetId()))
					{
						continue;
					}

					// Create menu entry
					FUIAction Action(FExecuteAction::CreateSP(this, &SRCActionPanel::OnAddActionClicked, RemoteControlField));
					MenuBuilder.AddMenuEntry(
						FText::Format(LOCTEXT("AddAction", "{0}"), FText::FromName(RemoteControlField->GetLabel())),
						FText::Format(LOCTEXT("AddActionTooltip", "Add {0}"), FText::FromName(RemoteControlField->GetLabel())),
						FSlateIcon(),
						MoveTemp(Action));
				}
			}
		}
	}

	return MenuBuilder.MakeWidget();
}

void SRCActionPanel::OnAddActionClicked(TSharedPtr<FRemoteControlField> InRemoteControlField)
{
	if (!SelectedBehaviourItemWeakPtr.IsValid())
	{
		return;
	}

	if (const URCBehaviour* Behaviour = SelectedBehaviourItemWeakPtr.Pin()->GetBehaviour())
	{
		URCAction* NewAction = nullptr;

		if (InRemoteControlField->FieldType == EExposedFieldType::Property)
		{
			NewAction = Behaviour->ActionContainer->AddAction(StaticCastSharedPtr<FRemoteControlProperty>(InRemoteControlField));
		}
		else if (InRemoteControlField->FieldType == EExposedFieldType::Function)
		{
			NewAction = Behaviour->ActionContainer->AddAction(StaticCastSharedPtr<FRemoteControlFunction>(InRemoteControlField));
		}

		if (const TSharedPtr<SRemoteControlPanel> RemoteControlPanel = GetRemoteControlPanel())
		{
			RemoteControlPanel->OnActionAdded.Broadcast(NewAction);
		}
	}
}

FReply SRCActionPanel::OnClickEmptyButton()
{
	if (const TSharedPtr<FRCBehaviourModel> BehaviourItem = SelectedBehaviourItemWeakPtr.Pin())
	{
		if (const URCBehaviour* Behaviour = BehaviourItem->GetBehaviour())
		{
			Behaviour->ActionContainer->EmptyActions();
		}
	}

	if (const TSharedPtr<SRemoteControlPanel> RemoteControlPanel = GetRemoteControlPanel())
	{
		RemoteControlPanel->OnEmptyActions.Broadcast();
	}

	return FReply::Handled();
}

FReply SRCActionPanel::OnAddAllFields()
{
	if (!SelectedBehaviourItemWeakPtr.IsValid())
	{
		return FReply::Handled();
	}

	if (URemoteControlPreset* Preset = GetPreset())
	{
		if (const URCBehaviour* Behaviour = SelectedBehaviourItemWeakPtr.Pin()->GetBehaviour())
		{
			const TArray<TWeakPtr<FRemoteControlField>>& RemoteControlFields = Preset->GetExposedEntities<FRemoteControlField>();

			// Enumerate the list of Exposed Entities and Functions available in this Preset for our use as Actions
			for (const TWeakPtr<FRemoteControlField>& RemoteControlFieldWeakPtr : RemoteControlFields)
			{
				if (const TSharedPtr<FRemoteControlField> RemoteControlField = RemoteControlFieldWeakPtr.Pin())
				{
					URCAction* NewAction = nullptr;

					// Property Action
					if (RemoteControlField->FieldType == EExposedFieldType::Property)
					{
						NewAction = Behaviour->ActionContainer->AddAction(StaticCastSharedPtr<FRemoteControlProperty>(RemoteControlField));
					}
					// Function Action
					else if (RemoteControlField->FieldType == EExposedFieldType::Function)
					{
						NewAction = Behaviour->ActionContainer->AddAction(StaticCastSharedPtr<FRemoteControlFunction>(RemoteControlField));
					}

					// Broadcast New Action
					if (const TSharedPtr<SRemoteControlPanel> RemoteControlPanel = GetRemoteControlPanel())
					{
						RemoteControlPanel->OnActionAdded.Broadcast(NewAction);
					}
				}
			}
		}
	}

	return FReply::Handled();
}

bool SRCActionPanel::IsListFocused() const
{
	return ActionPanelList.IsValid() && ActionPanelList->IsListFocused();
}

void SRCActionPanel::DeleteSelectedPanelItem()
{
	ActionPanelList->DeleteSelectedPanelItem();
}

FReply SRCActionPanel::RequestDeleteAllItems()
{
	if (!ActionPanelList.IsValid())
	{
		return FReply::Unhandled();
	}

	const FText WarningMessage = FText::Format(LOCTEXT("DeleteAllWarning", "You are about to delete '{0}' actions. This action might not be undone.\nAre you sure you want to proceed?"), ActionPanelList->Num());

	EAppReturnType::Type UserResponse = FMessageDialog::Open(EAppMsgType::YesNo, WarningMessage);

	if (UserResponse == EAppReturnType::Yes)
	{
		return OnClickEmptyButton();
	}

	return FReply::Handled();
}

EVisibility SRCActionPanel::HandleAddAllButtonVisibility() const
{
	if (URemoteControlPreset* Preset = GetPreset())
	{
		return Preset->HasEntities() ? EVisibility::Visible : EVisibility::Collapsed;
	}

	return EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE