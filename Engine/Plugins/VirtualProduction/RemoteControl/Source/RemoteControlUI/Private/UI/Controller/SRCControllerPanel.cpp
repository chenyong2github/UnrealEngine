// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCControllerPanel.h"

#include "RCControllerModel.h"
#include "SlateOptMacros.h"
#include "SRCControllerPanelHeader.h"
#include "SRCControllerPanelList.h"
#include "UI/BaseLogicUI/RCLogicModeBase.h"
#include "UI/SRemoteControlPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Views/SListView.h"

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCControllerPanel::Construct(const FArguments& InArgs, const TSharedRef<SRemoteControlPanel>& InPanel)
{
	SRCLogicPanelBase::Construct(SRCLogicPanelBase::FArguments(), InPanel);
	
	ChildSlot
	[
		SNew(SVerticalBox)
		// Header
		+ SVerticalBox::Slot()
		.Padding(FMargin(5.f))
		.AutoHeight()
		[
			SNew(SRCControllerPanelHeader, SharedThis(this))
		]
		// List
		+ SVerticalBox::Slot()
		.Padding(FMargin(5.f))
		[
			SAssignNew(ControllerPanelList, SRCControllerPanelList, SharedThis(this))
		]
	];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

bool SRCControllerPanel::IsListFocused() const
{
	return ControllerPanelList->IsListFocused();
}

void SRCControllerPanel::DeleteSelectedPanelItem()
{
	ControllerPanelList->DeleteSelectedPanelItem();
}