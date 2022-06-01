// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCBehaviourIsEqual.h"

#include "SlateOptMacros.h"
#include "SPositiveActionButton.h"
#include "Styling/CoreStyle.h"
#include "UI/Behaviour/Builtin/RCBehaviourIsEqualModel.h"
#include "UI/RemoteControlPanelStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SSeparator.h"

#define LOCTEXT_NAMESPACE "SRCBehaviourIsEqual"

class SPositiveActionButton;

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCBehaviourIsEqual::Construct(const FArguments& InArgs, TSharedRef<const FRCIsEqualBehaviourModel> InBehaviourItem)
{
	IsEqualBehaviourItemWeakPtr = InBehaviourItem;

	ChildSlot
	[
		SNew(SHorizontalBox)
		// Header
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("BehaviorLogicSummary", "If Value Is"))
			.Font(FRemoteControlPanelStyle::Get()->GetFontStyle("RemoteControlPanel.Actions.ValuePanelHeader"))
		]

		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		.Padding(FMargin(5.f))
		[
			SNew(SHorizontalBox)
			// Value Widget
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				InBehaviourItem->GetPropertyWidget()
			]

			// Bind button  (currently purely cosmetic)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "FlatButton.DarkGrey")
				.ContentPadding(FMargin(0.f, 3.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Padding(FMargin(3.f))
					[
						SNew(SImage)
						.Image(FAppStyle::Get().GetBrush("EditableComboBox.Add"))
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("BindButton", "Bind"))
						.Font(FRemoteControlPanelStyle::Get()->GetFontStyle("RemoteControlPanel.Behaviours.BehaviourDescription"))
					]
				]
			]
		]
	];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

#undef LOCTEXT_NAMESPACE