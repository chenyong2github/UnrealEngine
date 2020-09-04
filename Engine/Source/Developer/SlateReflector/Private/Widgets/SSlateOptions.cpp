// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SSlateOptions.h"

#include "Framework/Application/SlateApplication.h"
#include "SlateGlobals.h"
#include "SlateReflectorModule.h"
#include "Styling/CoreStyle.h"
#include "Styling/WidgetReflectorStyle.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SInvalidationPanel.h"
#include "Widgets/SWidgetReflector.h"

#define LOCTEXT_NAMESPACE "SSlateOptions"

void SSlateOptions::Construct( const FArguments& InArgs )
{
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
		.Padding(2.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 4.f, 0.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AppScale", "Application Scale: "))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.MinDesiredWidth(100)
				.MaxDesiredWidth(250)
				[
					SNew(SSpinBox<float>)
					.Value(this, &SSlateOptions::HandleAppScaleSliderValue)
					.MinValue(0.50f)
					.MaxValue(3.0f)
					.Delta(0.01f)
					.OnValueChanged(this, &SSlateOptions::HandleAppScaleSliderChanged)
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(20.f, 0.f, 4.f, 0.f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Flags", "Flags: "))
			]

#if WITH_SLATE_DEBUGGING
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(5.0f, 0.0f))
			[
				SNew(SCheckBox)
				.Style(FWidgetReflectorStyle::Get(), "CheckBox")
				.ForegroundColor(FSlateColor::UseForeground())
				.ToolTipText(LOCTEXT("EnableWidgetCachingTooltip", "Whether to attempt to cache any widgets through invalidation panels."))
				.IsChecked_Lambda([]()
				{
					return SInvalidationPanel::AreInvalidationPanelsEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;

				})
				.OnCheckStateChanged_Lambda([&](const ECheckBoxState NewState)
				{
					SInvalidationPanel::EnableInvalidationPanels((NewState == ECheckBoxState::Checked) ? true : false);
				})
				[
					SNew(SBox)
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Center)
					.Padding(FMargin(4.0, 2.0))
					[
						SNew(STextBlock)
						.Text(LOCTEXT("EnableWidgetCaching", "Widget Caching"))
					]
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(5.0f, 0.0f))
			[
				SNew(SCheckBox)
				.Style(FWidgetReflectorStyle::Get(), "CheckBox")
				.ForegroundColor(FSlateColor::UseForeground())
				.ToolTipText(LOCTEXT("InvalidationDebuggingTooltip", "Whether to show invalidation debugging visualization."))
				.IsChecked_Lambda([&]()
				{
					return GSlateInvalidationDebugging ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([&](const ECheckBoxState NewState)
				{
					GSlateInvalidationDebugging = (NewState == ECheckBoxState::Checked) ? true : false;
				})
				[
					SNew(SBox)
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Center)
					.Padding(FMargin(4.0, 2.0))
					[
						SNew(STextBlock)
						.Text(LOCTEXT("InvalidationDebugging", "Invalidation Debugging"))
					]
				]
			]
#endif //WITH_SLATE_DEBUGGING
		]
	];
}

void SSlateOptions::HandleAppScaleSliderChanged(float NewValue)
{
	FSlateApplication::Get().SetApplicationScale(NewValue);
}

float SSlateOptions::HandleAppScaleSliderValue() const
{
	return FSlateApplication::Get().GetApplicationScale();
}

#undef LOCTEXT_NAMESPACE
