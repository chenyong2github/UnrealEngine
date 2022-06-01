// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCActionPanel.h"

#include "SlateOptMacros.h"
#include "UI/SRemoteControlPanel.h"
#include "SRCActionPanelHeader.h"
#include "SRCActionPanelList.h"
#include "SRCActionPanelFooter.h"

#include "UI/Behaviour/RCBehaviourModel.h"

#define LOCTEXT_NAMESPACE "SRCActionPanel"

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCActionPanel::Construct(const FArguments& InArgs, const TSharedRef<SRemoteControlPanel>& InPanel)
{
	SRCLogicPanelBase::Construct(SRCLogicPanelBase::FArguments(), InPanel);
	
	WrappedBoxWidget = SNew(SBox);
	UpdateWrappedWidget();
	
	ChildSlot
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
		WrappedBoxWidget->SetContent(
		SNew(SVerticalBox)
		// Header
		+ SVerticalBox::Slot()
		.Padding(5.0f, 5.0f)
		.AutoHeight()
		[
			SNew(SRCActionPanelHeader, SharedThis(this), InBehaviourItem.ToSharedRef())
		]

		// Border (separating Header and "If Value Is" panel)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor(FColor::Black)
			.Padding(FMargin(0.f, 1.f))
			[
				SNullWidget::NullWidget
			]
		]

		// Behaviour Specific Details Panel
		+ SVerticalBox::Slot()
		.Padding(20.0f, 5.0f)
		.AutoHeight()
		[
			InBehaviourItem->GetBehaviourDetailsWidget()			
		]

		// Actions Mapping List
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(5.f))
		[
			SAssignNew(ActionPanelList, SRCActionPanelList, SharedThis(this), InBehaviourItem)
		]

		// Footer
		+ SVerticalBox::Slot()
		.Padding(5.0f, 5.0f)
		.AutoHeight()
		[
			SNew(SRCActionPanelFooter, SharedThis(this), InBehaviourItem.ToSharedRef())
		]
	);
	}
	else
	{
		WrappedBoxWidget->SetContent(
			SNew(STextBlock).Text(LOCTEXT("Unselected", "No Behaviour Selected"))
		);
	}
}

bool SRCActionPanel::IsListFocused() const
{
	return ActionPanelList.IsValid() && ActionPanelList->IsListFocused();
}

void SRCActionPanel::DeleteSelectedPanelItem()
{
	ActionPanelList->DeleteSelectedPanelItem();
}

#undef LOCTEXT_NAMESPACE