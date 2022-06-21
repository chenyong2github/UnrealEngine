// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCBehaviourDetails.h"

#include "Action/RCActionContainer.h"
#include "Action/RCFunctionAction.h"
#include "Action/RCPropertyAction.h"
#include "Behaviour/RCBehaviour.h"
#include "Controller/RCController.h"
#include "RemoteControlPreset.h"
#include "SlateOptMacros.h"
#include "SRCActionPanel.h"
#include "Styling/CoreStyle.h"
#include "Styling/RemoteControlStyles.h"
#include "UI/Behaviour/RCBehaviourModel.h"
#include "UI/BaseLogicUI/RCLogicHelpers.h"
#include "UI/RCUIHelpers.h"
#include "UI/RemoteControlPanelStyle.h"
#include "UI/SRemoteControlPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSeparator.h"

#define LOCTEXT_NAMESPACE "SRCBehaviourDetails"

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCBehaviourDetails::Construct(const FArguments& InArgs, TSharedRef<SRCActionPanel> InActionPanel, TSharedRef<FRCBehaviourModel> InBehaviourItem)
{
	RCPanelStyle = &FRemoteControlPanelStyle::Get()->GetWidgetStyle<FRCPanelStyle>("RemoteControlPanel.MinorPanel");

	ActionPanelWeakPtr = InActionPanel;
	BehaviourItemWeakPtr = InBehaviourItem;

	URCBehaviour* Behaviour = BehaviourItemWeakPtr.Pin()->GetBehaviour();
	const FText BehaviourDisplayName = Behaviour->GetDisplayName().ToUpper();
	const FText BehaviourDescription = Behaviour->GetBehaviorDescription();

	const FSlateFontInfo& FontBehaviorDesc = FRemoteControlPanelStyle::Get()->GetFontStyle("RemoteControlPanel.Behaviours.BehaviorDescription");

	FLinearColor TypeColor;
	FString TypeDisplayName;
	if (URCController* Controller = Behaviour->ControllerWeakPtr.Get())
	{
		TypeColor = UE::RCUIHelpers::GetFieldClassTypeColor(Behaviour->ControllerWeakPtr->GetProperty());
		TypeDisplayName = FName::NameToDisplayString(UE::RCUIHelpers::GetFieldClassDisplayName(Behaviour->ControllerWeakPtr->GetProperty()).ToString(), false);
	}

	TSharedRef<SWidget> BehaviourDetailsWidget = InBehaviourItem->GetBehaviourDetailsWidget();
	
	ChildSlot
		.Padding(RCPanelStyle->PanelPadding)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.HAlign(HAlign_Fill)
			.Padding(4.f)
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				// Behaviour Name
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.FillWidth(1.f)
				.Padding(4.f)
				[
					SNew(STextBlock)
					.ColorAndOpacity(FLinearColor::White)
					.Text(BehaviourDisplayName)
					.TextStyle(&RCPanelStyle->SectionHeaderTextStyle)
				]

				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				.Padding(4.f, 0.f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("SettingsEditor.CheckoutWarningBorder"))
					.Padding(FMargin(8.f, 4.f))
					.BorderBackgroundColor(TypeColor)
					[
						SNew(STextBlock)
						.ColorAndOpacity(FLinearColor::Black)
						.Text(FText::FromString(TypeDisplayName))
						.TextStyle(&RCPanelStyle->PanelTextStyle)
					]
				]
			]

			+ SVerticalBox::Slot()
			.Padding(8.f, 4.f)
			.VAlign(VAlign_Top)
			.FillHeight(1.f)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Text(BehaviourDescription)
				.TextStyle(&RCPanelStyle->PanelTextStyle)
				.AutoWrapText(true) // note: use of autowidth on this slot prevents wrapping from working!
			]
			
			// Border (separating Header and "If Value Is" panel)
			+ SVerticalBox::Slot()
			.Padding(2.f, 4.f)
			.AutoHeight()
			[
				SNew(SSeparator)
				.SeparatorImage(FAppStyle::Get().GetBrush("Separator"))
				.Thickness(2.f)
				.Orientation(EOrientation::Orient_Horizontal)
				.Visibility_Lambda([BehaviourDetailsWidget]() { return BehaviourDetailsWidget != SNullWidget::NullWidget ? EVisibility::Visible : EVisibility::Collapsed; })
			]

			// Behaviour Specific Details Panel
			+ SVerticalBox::Slot()
			.Padding(8.f, 4.f)
			.AutoHeight()
			[
				BehaviourDetailsWidget
			]

			// Spacer to fill the gap.
			+ SVerticalBox::Slot()
			.Padding(0)
			.FillHeight(1.f)
			[
				SNew(SSpacer)
			]
		];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

#undef LOCTEXT_NAMESPACE