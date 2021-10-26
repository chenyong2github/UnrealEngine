// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAudioTextBox.h"

const FVariablePrecisionNumericInterface SAudioTextBox::NumericInterface = FVariablePrecisionNumericInterface();

void SAudioTextBox::Construct(const SAudioTextBox::FArguments& InArgs)
{
	ShowLabelOnlyOnHover = InArgs._ShowLabelOnlyOnHover;
	ShowUnitsText = InArgs._ShowUnitsText;
	OnValueTextCommitted = InArgs._OnValueTextCommitted;
	LabelBackgroundColor = InArgs._LabelBackgroundColor;

	const ISlateStyle* AudioWidgetsStyle = FSlateStyleRegistry::FindSlateStyle("AudioWidgetsStyle");
	if (AudioWidgetsStyle)
	{
		SAssignNew(LabelBackgroundImage, SImage)
			.Image(AudioWidgetsStyle->GetBrush("AudioTextBox.LabelBackground"))
			.ColorAndOpacity(LabelBackgroundColor);
		SAssignNew(ValueText, SEditableText)
			.Text(FText::AsNumber(0.0f))
			.Justification(ETextJustify::Right)
			.OnTextCommitted(OnValueTextCommitted)
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis);
		SAssignNew(UnitsText, SEditableText)
			.Text(FText::FromString("units"))
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis);

		TextWidgetSwitcher = CreateTextWidgetSwitcher();
		SAssignNew(TextLabel, SOverlay)
			.Visibility_Lambda([this]()
			{
				return (!ShowLabelOnlyOnHover.Get() || this->IsHovered()) ? EVisibility::Visible : EVisibility::Hidden;
			})
			+ SOverlay::Slot()
			[
				LabelBackgroundImage.ToSharedRef()
			]
			+ SOverlay::Slot()
			[
				TextWidgetSwitcher.ToSharedRef()
			];
	}

	ChildSlot
	[
		TextLabel.ToSharedRef()
	];
}

void SAudioTextBox::SetLabelBackgroundColor(FSlateColor InColor)
{
	SetAttribute(LabelBackgroundColor, TAttribute<FSlateColor>(InColor), EInvalidateWidgetReason::Paint);
	LabelBackgroundImage->SetColorAndOpacity(InColor);
}

void SAudioTextBox::SetValueText(const float OutputValue)
{
	ValueText->SetText(FText::FromString(NumericInterface.ToString(OutputValue)));
}

void SAudioTextBox::SetUnitsText(const FText Units)
{
	UnitsText->SetText(Units);
}

void SAudioTextBox::SetUnitsTextReadOnly(const bool bIsReadOnly)
{
	UnitsText->SetIsReadOnly(bIsReadOnly);
}

void SAudioTextBox::SetValueTextReadOnly(const bool bIsReadOnly)
{
	ValueText->SetIsReadOnly(bIsReadOnly);
}

void SAudioTextBox::SetShowLabelOnlyOnHover(const bool bShowLabelOnlyOnHover)
{
	SetAttribute(ShowLabelOnlyOnHover, TAttribute<bool>(bShowLabelOnlyOnHover), EInvalidateWidgetReason::Visibility);
}

void SAudioTextBox::SetShowUnitsText(const bool bShowUnitsText)
{
	SetAttribute(ShowUnitsText, TAttribute<bool>(bShowUnitsText), EInvalidateWidgetReason::Layout);

	if (bShowUnitsText)
	{
		TextWidgetSwitcher->SetActiveWidgetIndex(0);
		ValueText->SetJustification(ETextJustify::Right);
	}
	else
	{
		TextWidgetSwitcher->SetActiveWidgetIndex(1);
		ValueText->SetJustification(ETextJustify::Center);
	}
	UpdateValueTextWidth(OutputRange);
}

TSharedRef<SWidgetSwitcher> SAudioTextBox::CreateTextWidgetSwitcher()
{
	SAssignNew(TextWidgetSwitcher, SWidgetSwitcher);
	// Slot 0 is show both value and units text
	TextWidgetSwitcher->AddSlot(0)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		[
			ValueText.ToSharedRef()
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f, 0.0f, 4.0f, 0.0f)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		[
			UnitsText.ToSharedRef()
		]
	];
	// Slot 1 is show only value, not units text
	TextWidgetSwitcher->AddSlot(1)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()	
		.AutoWidth()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		[
			ValueText.ToSharedRef()
		]
	];
	return TextWidgetSwitcher.ToSharedRef();
}

// Update value text size to accommodate the largest numbers possible within the output range
void SAudioTextBox::UpdateValueTextWidth(const FVector2D InOutputRange)
{
	OutputRange = InOutputRange;

	const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FText OutputRangeXText = FText::FromString(SAudioTextBox::NumericInterface.ToString(OutputRange.X));
	const FText OutputRangeYText = FText::FromString(SAudioTextBox::NumericInterface.ToString(OutputRange.Y));
	const FSlateFontInfo Font = FTextBlockStyle::GetDefault().Font;
	const float OutputRangeXTextWidth = FontMeasureService->Measure(OutputRangeXText, Font).X;
	const float OutputRangeYTextWidth = FontMeasureService->Measure(OutputRangeYText, Font).X;
	// add 1 digit of padding
	const float Padding = FontMeasureService->Measure(FText::FromString("-"), Font).X;
	float MaxValueLabelWidth = FMath::Max(OutputRangeXTextWidth, OutputRangeYTextWidth) + Padding;

	if (!ShowUnitsText.Get())
	{
		// set to max of label background width and calculated max width
		const ISlateStyle* AudioWidgetsStyle = FSlateStyleRegistry::FindSlateStyle("AudioWidgetsStyle");
		if (AudioWidgetsStyle)
		{
			MaxValueLabelWidth = FMath::Max(MaxValueLabelWidth, AudioWidgetsStyle->GetVector("AudioTextBox.LabelBackgroundSize").X);
		}
	}
	ValueText->SetMinDesiredWidth(MaxValueLabelWidth);
	// slot index 0 is value text
	TSharedPtr<SHorizontalBox> HorizontalTextBox = StaticCastSharedPtr<SHorizontalBox>(TextWidgetSwitcher->GetActiveWidget());
	HorizontalTextBox->GetSlot(0).SetMaxWidth(MaxValueLabelWidth);
}
