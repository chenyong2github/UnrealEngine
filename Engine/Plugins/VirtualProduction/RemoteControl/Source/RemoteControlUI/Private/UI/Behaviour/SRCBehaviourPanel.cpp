// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCBehaviourPanel.h"
#include "SRCBehaviourPanelHeader.h"
#include "SRCBehaviourPanelList.h"

#include "SlateOptMacros.h"
#include "UI/SRemoteControlPanel.h"

#define LOCTEXT_NAMESPACE "SRCBehaviourPanel"

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCBehaviourPanel::Construct(const FArguments& InArgs, const TSharedRef<SRemoteControlPanel>& InPanel)
{
	SRCLogicPanelBase::Construct(SRCLogicPanelBase::FArguments(), InPanel);

	WrappedBoxWidget = SNew(SBox);
	UpdateWrappedWidget();
	
	ChildSlot
	[
		WrappedBoxWidget.ToSharedRef()
	];

	// Register delegates
	InPanel->OnControllerSelectionChanged.AddSP(this, &SRCBehaviourPanel::OnControllerSelectionChanged);
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCBehaviourPanel::OnControllerSelectionChanged(TSharedPtr<FRCControllerModel> InControllerItem)
{
	UpdateWrappedWidget(InControllerItem);
	SelectedControllerItemWeakPtr = InControllerItem;
}

void SRCBehaviourPanel::UpdateWrappedWidget(TSharedPtr<FRCControllerModel> InControllerItem)
{
	if (InControllerItem.IsValid())
	{
		WrappedBoxWidget->SetContent(
			SNew(SVerticalBox)
			// Header
			+ SVerticalBox::Slot()
			.Padding(5.0f, 5.0f)
			.AutoHeight()
			[
				SNew(SRCBehaviourPanelHeader, SharedThis(this), InControllerItem)
			]
			// List
			+ SVerticalBox::Slot()
			[
				SAssignNew(BehaviourPanelList, SRCBehaviourPanelList, SharedThis(this), InControllerItem)
			]
		);
	}
	else
	{
		WrappedBoxWidget->SetContent(
			SNew(STextBlock).Text(LOCTEXT("Unselected", "No Controller Selected"))
		);
	}
}

bool SRCBehaviourPanel::IsListFocused() const
{
	return BehaviourPanelList.IsValid() && BehaviourPanelList->IsListFocused();
}

void SRCBehaviourPanel::DeleteSelectedPanelItem()
{
	BehaviourPanelList->DeleteSelectedPanelItem();
}

#undef LOCTEXT_NAMESPACE