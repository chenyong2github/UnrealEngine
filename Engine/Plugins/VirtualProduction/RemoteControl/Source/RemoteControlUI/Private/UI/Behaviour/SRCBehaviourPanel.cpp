// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCBehaviourPanel.h"
#include "SRCBehaviourPanelList.h"

#include "Behaviour/RCBehaviourBlueprintNode.h"
#include "Behaviour/RCBehaviour.h"
#include "Behaviour/RCBehaviourNode.h"
#include "Controller/RCController.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/MessageDialog.h"
#include "RemoteControlPreset.h"

#include "SlateOptMacros.h"
#include "Styling/RemoteControlStyles.h"

#include "UI/Panels/SRCDockPanel.h"
#include "UI/RemoteControlPanelStyle.h"
#include "UI/SRemoteControlPanel.h"
#include "UI/Controller/RCControllerModel.h"
#include "Widgets/Input/SComboButton.h"

#define LOCTEXT_NAMESPACE "SRCBehaviourPanel"

TSharedPtr<SBox> SRCBehaviourPanel::NoneSelectedWidget = SNew(SBox)
			.Padding(0.f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoneSelected", "Select a controller to view its behaviors."))
				.TextStyle(&FAppStyle::GetWidgetStyle<FTextBlockStyle>("NormalText"))
				.Justification(ETextJustify::Center)
			];

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCBehaviourPanel::Construct(const FArguments& InArgs, const TSharedRef<SRemoteControlPanel>& InPanel)
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
	InPanel->OnControllerSelectionChanged.AddSP(this, &SRCBehaviourPanel::OnControllerSelectionChanged);
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCBehaviourPanel::Shutdown()
{
	NoneSelectedWidget.Reset();
}

void SRCBehaviourPanel::OnControllerSelectionChanged(TSharedPtr<FRCControllerModel> InControllerItem)
{
	SelectedControllerItemWeakPtr = InControllerItem;
	UpdateWrappedWidget(InControllerItem);
}

void SRCBehaviourPanel::UpdateWrappedWidget(TSharedPtr<FRCControllerModel> InControllerItem)
{
	if (InControllerItem.IsValid())
	{
		// Behaviour Dock Panel
		TSharedPtr<SRCMinorPanel> BehaviourDockPanel = SNew(SRCMinorPanel)
			.HeaderLabel(LOCTEXT("BehavioursLabel", "Behavior"))
			[
				SAssignNew(BehaviourPanelList, SRCBehaviourPanelList, SharedThis(this), InControllerItem)
			];

		// Add New Behaviour Button
		const TSharedRef<SWidget> AddNewBehaviourButton = SNew(SComboButton)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Add Behavior")))
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
				GetBehaviourMenuContentWidget()
			];

		// Empty All Button
		TSharedRef<SWidget> EmptyAllButton = SNew(SButton)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Empty Behaviors")))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ForegroundColor(FSlateColor::UseForeground())
			.ButtonStyle(&RCPanelStyle->FlatButtonStyle)
			.ToolTipText(LOCTEXT("EmptyAllToolTip", "Deletes all the behaviors."))
			.OnClicked(this, &SRCBehaviourPanel::RequestDeleteAllItems)
			.Visibility_Lambda([this]() { return BehaviourPanelList.IsValid() && !BehaviourPanelList->IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
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

		BehaviourDockPanel->AddHeaderToolbarItem(EToolbar::Left, AddNewBehaviourButton);
		BehaviourDockPanel->AddHeaderToolbarItem(EToolbar::Right, EmptyAllButton);

		WrappedBoxWidget->SetContent(BehaviourDockPanel.ToSharedRef());
	}
	else
	{
		WrappedBoxWidget->SetContent(NoneSelectedWidget.ToSharedRef());
	}
}

TSharedRef<SWidget> SRCBehaviourPanel::GetBehaviourMenuContentWidget()
{
	constexpr bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, nullptr);

	if (const TSharedPtr<FRCControllerModel> ControllerItem = SelectedControllerItemWeakPtr.Pin())
	{
		if (URCController* Controller = Cast<URCController>(ControllerItem->GetVirtualProperty()))
		{
			for (TObjectIterator<UClass> It; It; ++It)
			{
				UClass* Class = *It;

				if (Class->IsChildOf(URCBehaviourNode::StaticClass()))
				{
					const bool bIsClassInstantiatable = Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Abstract) || FKismetEditorUtilities::IsClassABlueprintSkeleton(Class);
					if (bIsClassInstantiatable)
					{
						continue;
					}

					if (Class->IsInBlueprint())
					{
						if (Class->GetSuperClass() != URCBehaviourBlueprintNode::StaticClass())
						{
							continue;
						}
					}

					URCBehaviour* Behaviour = Controller->CreateBehaviour(Class);
					if (!Behaviour)
					{
						continue;
					}

					FUIAction Action(FExecuteAction::CreateSP(this, &SRCBehaviourPanel::OnAddBehaviourClicked, Class));
					MenuBuilder.AddMenuEntry(
						FText::Format(LOCTEXT("AddBehaviourNode", "{0}"), Behaviour->GetDisplayName()),
						FText::Format(LOCTEXT("AddBehaviourNodeTooltip", "{0}"), Behaviour->GetBehaviorDescription()),
						FSlateIcon(),
						Action);
				}
			}
		}
	}

	return MenuBuilder.MakeWidget();
}

void SRCBehaviourPanel::OnAddBehaviourClicked(UClass* InClass)
{
	if (const TSharedPtr<FRCControllerModel> ControllerItem = SelectedControllerItemWeakPtr.Pin())
	{
		if (URCController* Controller = Cast<URCController>(ControllerItem->GetVirtualProperty()))
		{
			URCBehaviour* NewBehaviour = Controller->AddBehaviour(InClass);

			if (const TSharedPtr<SRemoteControlPanel> RemoteControlPanel = GetRemoteControlPanel())
			{
				RemoteControlPanel->OnBehaviourAdded.Broadcast(NewBehaviour);
			}
		}
	}
}

FReply SRCBehaviourPanel::OnClickEmptyButton()
{
	if (const TSharedPtr<FRCControllerModel> ControllerItem = SelectedControllerItemWeakPtr.Pin())
	{
		if (URCController* Controller = Cast<URCController>(ControllerItem->GetVirtualProperty()))
		{
			Controller->EmptyBehaviours();
		}
	}

	if (const TSharedPtr<SRemoteControlPanel> RemoteControlPanel = GetRemoteControlPanel())
	{
		RemoteControlPanel->OnEmptyBehaviours.Broadcast();
	}

	return FReply::Handled();
}

bool SRCBehaviourPanel::IsListFocused() const
{
	return BehaviourPanelList.IsValid() && BehaviourPanelList->IsListFocused();
}

void SRCBehaviourPanel::DeleteSelectedPanelItem()
{
	BehaviourPanelList->DeleteSelectedPanelItem();
}

FReply SRCBehaviourPanel::RequestDeleteAllItems()
{
	if (!BehaviourPanelList.IsValid())
	{
		return FReply::Unhandled();
	}

	const FText WarningMessage = FText::Format(LOCTEXT("DeleteAllWarning", "You are about to delete '{0}' behaviors. This action might not be undone.\nAre you sure you want to proceed?"), BehaviourPanelList->Num());

	EAppReturnType::Type UserResponse = FMessageDialog::Open(EAppMsgType::YesNo, WarningMessage);

	if (UserResponse == EAppReturnType::Yes)
	{
		return OnClickEmptyButton();
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE