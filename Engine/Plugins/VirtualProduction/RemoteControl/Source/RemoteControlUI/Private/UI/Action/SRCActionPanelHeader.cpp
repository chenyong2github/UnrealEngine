// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCActionPanelHeader.h"

#include "Action/RCActionContainer.h"
#include "Action/RCFunctionAction.h"
#include "Action/RCPropertyAction.h"
#include "Behaviour/RCBehaviour.h"
#include "Controller/RCController.h"
#include "RemoteControlPreset.h"
#include "SlateOptMacros.h"
#include "SRCActionPanel.h"
#include "Styling/CoreStyle.h"
#include "UI/Behaviour/RCBehaviourModel.h"
#include "UI/BaseLogicUI/RCLogicHelpers.h"
#include "UI/RCUIHelpers.h"
#include "UI/RemoteControlPanelStyle.h"
#include "UI/SRemoteControlPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SSeparator.h"

#define LOCTEXT_NAMESPACE "SRCActionPanelHeader"

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCActionPanelHeader::Construct(const FArguments& InArgs, TSharedRef<SRCActionPanel> InActionPanel, TSharedRef<FRCBehaviourModel> InBehaviourItem)
{
	SRCLogicPanelHeaderBase::Construct(SRCLogicPanelHeaderBase::FArguments());

	ActionPanelWeakPtr = InActionPanel;
	BehaviourItemWeakPtr = InBehaviourItem;

	URCBehaviour* Behaviour = BehaviourItemWeakPtr.Pin()->GetBehaviour();
	const FText BehaviourDisplayName = Behaviour->GetDisplayName().ToUpper();
	const FText BehaviourDescription = Behaviour->GetBehaviorDescription();

	const FSlateFontInfo& FontBehaviorDesc = FRemoteControlPanelStyle::Get()->GetFontStyle("RemoteControlPanel.Behaviours.BehaviorDescription");

	FLinearColor TypeColor;
	FText TypeDisplayName;
	if (URCController* Controller = Behaviour->ControllerWeakPtr.Get())
	{
		TypeColor = UE::RCUIHelpers::GetFieldClassTypeColor(Behaviour->ControllerWeakPtr->GetProperty());
		TypeDisplayName = UE::RCUIHelpers::GetFieldClassDisplayName(Behaviour->ControllerWeakPtr->GetProperty());
	}
	
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.HAlign(HAlign_Fill)
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.AutoWidth()
			.Padding(FMargin(5.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.HAlign(HAlign_Center)
				.AutoHeight()
				[
					SNew(STextBlock)
					.ColorAndOpacity(FLinearColor(1, 1, 1, 0.4f))
					.Text(BehaviourDisplayName)
					.Font(FRemoteControlPanelStyle::Get()->GetFontStyle("RemoteControlPanel.Behaviours.Title"))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(3.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.AutoWidth()
					.Padding(FMargin(5.f))
					[
						SNew(STextBlock)
						.ColorAndOpacity(FLinearColor(1, 1, 1, 0.4f))
						.Text(LOCTEXT("InputTitle", "Input"))
						.Font(FontBehaviorDesc)
					]

					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.AutoWidth()
					.Padding(FMargin(5.f, 0.f))
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::Get().GetBrush("SettingsEditor.CheckoutWarningBorder"))
						.Padding(FMargin(10.f, 0.f))
						.BorderBackgroundColor(TypeColor)
						[
							SNew(STextBlock)
							.Text(TypeDisplayName)
							.Font(FontBehaviorDesc)
						]
					]
				]
			]

			+ SHorizontalBox::Slot()
			.Padding(FMargin(10.f))
			.VAlign(VAlign_Top)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FLinearColor(1, 1, 1, 0.4f))
				.Text(BehaviourDescription)
				.Font(FontBehaviorDesc)
				.AutoWrapText(true) // note: use of autowidth on this slot prevents wrapping from working!
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			.AutoWidth()
			.Padding(FMargin(10.f))
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.DarkGrey")
				.OnClicked(this, &SRCActionPanelHeader::OnClickOverrideBlueprintButton)
				.ContentPadding(FMargin(0.f))
				[
					SNew(SImage)
					.Image(FRemoteControlPanelStyle::Get()->GetBrush("RemoteControlPanel.Behaviours.CustomBlueprint"))
				]
			]
		]
	];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

FReply SRCActionPanelHeader::OnClickOverrideBlueprintButton()
{
	TSharedPtr<FRCBehaviourModel> Behaviour = BehaviourItemWeakPtr.Pin();
	if (!ensure(Behaviour))
		return FReply::Unhandled();

	Behaviour->OnOverrideBlueprint();

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE