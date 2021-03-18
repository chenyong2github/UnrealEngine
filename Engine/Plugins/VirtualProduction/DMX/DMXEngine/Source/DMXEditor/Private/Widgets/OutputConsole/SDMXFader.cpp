// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXFader.h"

#include "DMXEditorLog.h"
#include "DMXProtocolCommon.h"
#include "DMXProtocolSettings.h"
#include "Interfaces/IDMXProtocol.h"
#include "IO/DMXOutputPort.h"
#include "Widgets/OutputConsole/SDMXOutputFaderList.h"

#include "Styling/SlateTypes.h"
#include "Widgets/Common/SSpinBoxVertical.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"


#define LOCTEXT_NAMESPACE "SDMXFader"

namespace DMXFader
{
	const FSlateFontInfo NameFont = FCoreStyle::GetDefaultFontStyle("Regular", 7);
	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);

	const FLinearColor DefaultFillColor = FLinearColor::FromSRGBColor(FColor::FromHex("00aeef"));
	const FLinearColor DefaultBackColor = FLinearColor::FromSRGBColor(FColor::FromHex("ffffff"));
	const FLinearColor DefeaultForeColor = FLinearColor::FromSRGBColor(FColor::FromHex("ffffff"));	
};

void SDMXFader::Construct(const FArguments& InArgs)
{
	OnRequestDelete = InArgs._OnRequestDelete;
	OnRequestSelect = InArgs._OnRequestSelect;
	FaderName = InArgs._FaderName.ToString();
	UniverseID = InArgs._UniverseID;
	MaxValue = InArgs._MaxValue;
	MinValue = InArgs._MinValue;
	StartingAddress = InArgs._StartingAddress;
	EndingAddress = InArgs._EndingAddress;

	SanetizeDMXProperties();

	// Init styles
	FSlateBrush FillBrush;
	FillBrush.TintColor = DMXFader::DefaultFillColor;

	FSlateBrush BackBrush;
	BackBrush.TintColor = DMXFader::DefaultBackColor;

	FSlateBrush ArrowsImage;
	ArrowsImage.TintColor = FLinearColor::Transparent;

	OutputFaderStyle = FSpinBoxStyle(FCoreStyle::Get().GetWidgetStyle<FSpinBoxStyle>("SpinBox"))
		.SetActiveFillBrush(FillBrush)
		.SetInactiveFillBrush(FillBrush)
		.SetBackgroundBrush(BackBrush)
		.SetHoveredBackgroundBrush(BackBrush)
		.SetForegroundColor(DMXFader::DefeaultForeColor)
		.SetArrowsImage(ArrowsImage);
		
	ChildSlot
	.Padding(5.0f, 0.0f)
	[
		SNew(SBox)
		.WidthOverride(80.0f)
		[
			SAssignNew(BackgroundBorder, SBorder)
			.BorderImage(this, &SDMXFader::GetBorderImage)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.VAlign(VAlign_Top)
				.HAlign(HAlign_Fill)
				.Padding(1.0f, 5.0f, 1.0f, 1.0f)
				.AutoHeight()
				[					
					SNew(SHorizontalBox)
		
					// Fader Name
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Fill)					
					.FillWidth(1.0f)
					[
						SNew(SBorder)
						.BorderBackgroundColor(FLinearColor::Black)
						[
							SAssignNew(FaderNameTextBox, SInlineEditableTextBlock)
							.MultiLine(false)
							.Text(InArgs._FaderName)		
							.Font(DMXFader::TitleFont)
							.Justification(ETextJustify::Center)
							.ColorAndOpacity(FLinearColor::White)
							.Style(FCoreStyle::Get(), "InlineEditableTextBlockSmallStyle")
							.OnTextCommitted(this, &SDMXFader::OnFaderNameCommitted)
						]
					]

					// Delete Button
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Top)
					.HAlign(HAlign_Right)					
					.Padding(FMargin(1.0f, 0.0f, 0.0f, 0.0f))
					.AutoWidth()
					[
						SNew(SButton)
						.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
						.OnClicked(this, &SDMXFader::OnDeleteClicked)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("x")))
							.Font(DMXFader::NameFont)
							.ColorAndOpacity(FLinearColor::White)
						]
					]
				]

				+ SVerticalBox::Slot()
				.VAlign(VAlign_Top)
				.HAlign(HAlign_Center)
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.MaxWidth(35.0f)
					[
						// Max Value
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.VAlign(VAlign_Bottom)
						.HAlign(HAlign_Fill)
						.AutoHeight()
						[
							SNew(SBorder)
							.BorderBackgroundColor(FLinearColor::Black)
							.Padding(5.0f)
							[
								SNew(SInlineEditableTextBlock)
								.MultiLine(false)
								.Text(this, &SDMXFader::GetMaxValueAsText)
								.Justification(ETextJustify::Center)
								.OnVerifyTextChanged(this, &SDMXFader::VerifyMaxValue)
								.OnTextCommitted(this, &SDMXFader::OnMaxValueCommitted)
								.Style(FCoreStyle::Get(), "InlineEditableTextBlockSmallStyle")
							]
						]
								
						// Fader Control
						+ SVerticalBox::Slot()
						.VAlign(VAlign_Top)
						.HAlign(HAlign_Center)
						.Padding(0.0f, 1.0f, 0.0f, 1.0f)
						.AutoHeight()
						[
							SNew(SBorder)
							.BorderBackgroundColor(FLinearColor::Black)
							[
								SAssignNew(FaderSpinBox, SSpinBoxVertical<uint8>)
								.Value(Value)
								.MinValue(0)
								.MaxValue(DMX_MAX_VALUE)
								.MinSliderValue(0)
								.MaxSliderValue(255)
								.OnValueChanged(this, &SDMXFader::HandleValueChanged)
								.Style(&OutputFaderStyle)
								.MinDesiredWidth(30.0f)
							]
						]

						// Fader Min Value
						+ SVerticalBox::Slot()
						.VAlign(VAlign_Bottom)
						.HAlign(HAlign_Fill)
						.AutoHeight()
						[
							SNew(SBorder)
							.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 1.0f))
							.Padding(5.0f)
							[
								SNew(SInlineEditableTextBlock)
								.MultiLine(false)
								.Text(this, &SDMXFader::GetMinValueAsText)
								.Justification(ETextJustify::Center)
								.OnVerifyTextChanged(this, &SDMXFader::VerifyMinValue)
								.OnTextCommitted(this, &SDMXFader::OnMinValueCommitted)
								.Style(FCoreStyle::Get(), "InlineEditableTextBlockSmallStyle")
							]
						]
					]
				]

				+ SVerticalBox::Slot()
				.VAlign(VAlign_Top)
				.HAlign(HAlign_Fill)
				.AutoHeight()
				.Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
				[
					GenerateAdressEditWidget()
				]
			]
		]
	];
}

void SDMXFader::Select()
{
	bSelected = true;

	check(BackgroundBorder.IsValid());
	BackgroundBorder->SetBorderImage(FEditorStyle::GetBrush("DetailsView.CategoryMiddle_Highlighted"));

	FSlateApplication::Get().SetKeyboardFocus(SharedThis(this));
}

void SDMXFader::Unselect()
{
	bSelected = false;

	check(BackgroundBorder.IsValid());
	BackgroundBorder->SetBorderImage(FEditorStyle::GetBrush("DetailsView.CategoryMiddle"));
}

void SDMXFader::SanetizeDMXProperties()
{
	if (UniverseID < 0)
	{
		UniverseID = 0;
	}
	else if (UniverseID > DMX_MAX_UNIVERSE)
	{
		UniverseID = DMX_MAX_UNIVERSE;
	}

	if (MaxValue > DMX_MAX_VALUE)
	{
		MaxValue = DMX_MAX_VALUE;
	}

	if (MinValue > MaxValue)
	{
		MinValue = MaxValue;
	}

	if (EndingAddress < 1)
	{
		EndingAddress = 1;
	}
	else if (EndingAddress > DMX_MAX_ADDRESS)
	{
		EndingAddress = DMX_MAX_ADDRESS;
	}

	if (StartingAddress < 1)
	{
		StartingAddress = 1;
	}
	else if (StartingAddress > EndingAddress)
	{
		StartingAddress = EndingAddress;
	}
}

FReply SDMXFader::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Delete)
	{
		OnRequestDelete.ExecuteIfBound(SharedThis(this));
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SDMXFader::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnRequestSelect.ExecuteIfBound(SharedThis(this));
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

TSharedRef<SWidget> SDMXFader::GenerateAdressEditWidget()
{
	FMargin LabelPadding = FMargin(4.0f, 0.0f, 20.0f, 1.0f);
	FMargin ValuePadding = FMargin(0.0f, 0.0f, 4.0f, 1.0f);

	return 
		SNew(SBorder)
		.Padding(FMargin(2.0f, 4.0f, 2.0f, 4.0f))
		[
			SNew(SGridPanel)
			.FillColumn(1, 1.0f)

			+ SGridPanel::Slot(0, 0)
			.VAlign(VAlign_Top)
			.HAlign(HAlign_Left)
			.Padding(LabelPadding)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Universe", "Uni:"))
				.Font(DMXFader::NameFont)
			]
	
			+ SGridPanel::Slot(1, 0)
			.VAlign(VAlign_Top)
			.HAlign(HAlign_Fill)
			.Padding(ValuePadding)
			[
				SNew(SBorder)
				.ToolTipText(LOCTEXT("UniverseIDTooltip", "The Universe to which DMX is sent to"))
				.BorderImage(FEditorStyle::GetBrush("EditableTextBox.Background.Focused"))
				.OnMouseDoubleClick(this, &SDMXFader::OnUniverseIDBorderDoubleClicked)
				[
					SAssignNew(UniverseIDEditableTextBlock, SInlineEditableTextBlock)						
					.MultiLine(false)
					.Text(this, &SDMXFader::GetUniverseIDAsText)
					.Justification(ETextJustify::Center)
					.Font(DMXFader::NameFont)
					.ColorAndOpacity(FLinearColor::Black)
					.OnVerifyTextChanged(this, &SDMXFader::VerifyUniverseID)
					.OnTextCommitted(this, &SDMXFader::OnUniverseIDCommitted)
				]
			]
	
			+ SGridPanel::Slot(0, 1)
			.VAlign(VAlign_Top)		
			.HAlign(HAlign_Fill)
			.Padding(LabelPadding)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("StaringAddressLabel", "Adr:"))
				.Font(DMXFader::NameFont)
			]

			+ SGridPanel::Slot(1, 1)
			.VAlign(VAlign_Top)
			.HAlign(HAlign_Fill)
			.Padding(ValuePadding)
			[
				SNew(SBorder)
				.ToolTipText(LOCTEXT("StartingAdressTooltip", "The Starting Adress of the Channel to which DMX is sent to"))
				.BorderImage(FEditorStyle::GetBrush("EditableTextBox.Background.Focused"))
				.OnMouseDoubleClick(this, &SDMXFader::OnStartingAddressBorderDoubleClicked)
				[
					SAssignNew(StartingAddressEditableTextBlock, SInlineEditableTextBlock)
					.MultiLine(false)
					.Text(this, &SDMXFader::GetStartingAddressAsText)
					.Justification(ETextJustify::Center)
					.Font(DMXFader::NameFont)
					.ColorAndOpacity(FLinearColor::Black)
					.OnVerifyTextChanged(this, &SDMXFader::VerifyStartingAddress)
					.OnTextCommitted(this, &SDMXFader::OnStartingAddressCommitted)
				]
			]

			+ SGridPanel::Slot(1, 2)			
			.VAlign(VAlign_Top)	
			.HAlign(HAlign_Fill)
			.Padding(ValuePadding)
			[
				SNew(SBorder)
				.ToolTipText(LOCTEXT("EndingAdressTooltip", "The Ending Adress of the Channel to which DMX is sent to"))
				.BorderImage(FEditorStyle::GetBrush("EditableTextBox.Background.Focused"))
				.OnMouseDoubleClick(this, &SDMXFader::OnEndingAddressBorderDoubleClicked)
				[
					SAssignNew(EndingAddressEditableTextBlock, SInlineEditableTextBlock)					
					.MultiLine(false)
					.Text(this, &SDMXFader::GetEndingAddressAsText)
					.Justification(ETextJustify::Center)
					.Font(DMXFader::NameFont)
					.ColorAndOpacity(FLinearColor::Black)
					.OnVerifyTextChanged(this, &SDMXFader::VerifyEndingAddress)
					.OnTextCommitted(this, &SDMXFader::OnEndingAddressCommitted)
				]
			]
		];
}

uint8 SDMXFader::GetValue() const
{
	check(FaderSpinBox.IsValid());
	return FaderSpinBox->GetValue();
}

void SDMXFader::SetValueByPercentage(float InNewPercentage)
{
	check(FaderSpinBox.IsValid());
	float Range = MaxValue - MinValue;
	FaderSpinBox->SetValue(static_cast<uint8>(Range * InNewPercentage / 100.0f) + MinValue);
}

FReply SDMXFader::OnDeleteClicked()
{
	OnRequestDelete.ExecuteIfBound(SharedThis(this));

	return FReply::Handled();
}

void SDMXFader::HandleValueChanged(uint8 NewValue)
{
	Value = NewValue;
}

void SDMXFader::OnFaderNameCommitted(const FText& NewFaderName, ETextCommit::Type InCommit)
{
	check(FaderNameTextBox.IsValid());

	FaderNameTextBox->SetText(FaderName);

	FaderName = NewFaderName.ToString();
}

FReply SDMXFader::OnUniverseIDBorderDoubleClicked(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		check(UniverseIDEditableTextBlock.IsValid());
		UniverseIDEditableTextBlock->EnterEditingMode();
		
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SDMXFader::OnStartingAddressBorderDoubleClicked(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		check(StartingAddressEditableTextBlock.IsValid());
		StartingAddressEditableTextBlock->EnterEditingMode();

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SDMXFader::OnEndingAddressBorderDoubleClicked(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		check(EndingAddressEditableTextBlock.IsValid());
		EndingAddressEditableTextBlock->EnterEditingMode();

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

const FSlateBrush* SDMXFader::GetBorderImage() const
{
	if (IsHovered())
	{
		return FEditorStyle::GetBrush("DetailsView.CategoryMiddle_Hovered");
	}
	else
	{
		return FEditorStyle::GetBrush("DetailsView.CategoryMiddle");
	}
}

bool SDMXFader::VerifyUniverseID(const FText& UniverseIDText, FText& OutErrorText)
{
	FString Str = UniverseIDText.ToString();
	if (Str.Len() >= 0 && Str.IsNumeric())
	{
		int32 StrValue;
		if (LexTryParseString<int32>(StrValue, *Str))
		{
			if (StrValue >= 0 && StrValue <= DMX_MAX_UNIVERSE)
			{
				return true;
			}
		}
	}
	OutErrorText = FText::Format(LOCTEXT("InvalidUniverseID", "Universe must be a number between 0 and {0}"), DMX_MAX_UNIVERSE);
	return false;
}

void SDMXFader::OnUniverseIDCommitted(const FText& UniverseIDText, ETextCommit::Type InCommit)
{
	FString Str = UniverseIDText.ToString();

	int32 NewUniverseID;
	if (LexTryParseString<int32>(NewUniverseID, *Str))
	{
		UniverseID = NewUniverseID;
		SanetizeDMXProperties();
	}
}

bool SDMXFader::VerifyStartingAddress(const FText& StartingAddressText, FText& OutErrorText)
{
	FString Str = StartingAddressText.ToString();
	if (Str.Len() > 0 && Str.IsNumeric())
	{
		int32 StrValue;
		if (LexTryParseString<int32>(StrValue, *Str))
		{
			if (StrValue > 0 && StrValue <= DMX_MAX_UNIVERSE)
			{
				return true;
			}
		}
	}
	OutErrorText = FText::Format(LOCTEXT("InvalidStartingAddress", "Address must be a number between 1 and {0}"), DMX_MAX_ADDRESS);
	return false;
}

void SDMXFader::OnStartingAddressCommitted(const FText& StartingAddressText, ETextCommit::Type InCommit)
{
	FString Str = StartingAddressText.ToString();
	int32 StrValue;
	if (LexTryParseString<int32>(StrValue, *Str))
	{
		if (StartingAddress != StrValue)
		{
			StartingAddress = StrValue;
			SanetizeDMXProperties();
		}
	}
}

bool SDMXFader::VerifyEndingAddress(const FText& EndingAddressText, FText& OutErrorText)
{
	FString Str = EndingAddressText.ToString();
	if (Str.Len() > 0 && Str.IsNumeric())
	{
		int32 StrValue;
		if (LexTryParseString<int32>(StrValue, *Str))
		{
			if (StrValue > 0 && StrValue <= DMX_MAX_ADDRESS)
			{
				return true;
			}
		}
	}
	OutErrorText = FText::Format(LOCTEXT("InvalidEndingAddress", "Address must be a number between 1 and {0}"), DMX_MAX_ADDRESS);
	return false;
}

void SDMXFader::OnEndingAddressCommitted(const FText& EndingAddressText, ETextCommit::Type InCommit)
{
	FString Str = EndingAddressText.ToString();
	int32 StrValue;
	if (LexTryParseString<int32>(StrValue, *Str))
	{
		if (EndingAddress != StrValue)
		{
			EndingAddress = StrValue;
			SanetizeDMXProperties();
		}
	}
}

bool SDMXFader::VerifyMaxValue(const FText& MaxValueText, FText& OutErrorText)
{
	FString Str = MaxValueText.ToString();
	if (Str.Len() >= 1 && Str.Len() <= 3 && Str.IsNumeric())
	{
		int32 StrValue;
		if (LexTryParseString<int32>(StrValue, *Str))
		{
			if (StrValue >= 0 && StrValue <= DMX_MAX_VALUE)
			{
				if (StrValue >= MinValue)
				{
					return true;
				}
			}
		}
	}
	OutErrorText = LOCTEXT("InvalidRangeValue", "Must be a number between 0 and 255");
	return false;
}

void SDMXFader::OnMaxValueCommitted(const FText& MaxValueText, ETextCommit::Type InCommit)
{
	check(FaderSpinBox.IsValid());

	FString Str = MaxValueText.ToString();
	int32 StrValue;
	if (LexTryParseString<int32>(StrValue, *Str))
	{
		MaxValue = StrValue;
		SanetizeDMXProperties();

		FaderSpinBox->SetMaxValue(MaxValue);

		if (Value > MaxValue)
		{
			Value = MaxValue;
			FaderSpinBox->SetValue(Value);
		}
	}
}

bool SDMXFader::VerifyMinValue(const FText& MinValueText, FText& OutErrorText)
{
	check(FaderSpinBox.IsValid());

	FString Str = MinValueText.ToString();
	if (Str.Len() >= 1 && Str.Len() <= 3 && Str.IsNumeric())
	{
		int32 StrValue;
		if (LexTryParseString<int32>(StrValue, *Str))
		{
			if (StrValue >= 0 && StrValue <= DMX_MAX_VALUE)
			{
				if (StrValue <= MaxValue)
				{
					return true;
				}
			}
		}
	}
	OutErrorText = LOCTEXT("InvalidRangeValue", "Must be a number between 0 and 255");
	return false;
}

void SDMXFader::OnMinValueCommitted(const FText& MinValueText, ETextCommit::Type InCommit)
{
	check(FaderSpinBox.IsValid());

	FString Str = MinValueText.ToString();
	int32 StrValue;
	if (LexTryParseString<int32>(StrValue, *Str))
	{
		MinValue = StrValue;
		SanetizeDMXProperties();

		FaderSpinBox->SetMinValue(MinValue);

		if (Value < MinValue)
		{
			Value = MinValue;
			FaderSpinBox->SetValue(Value);
		}
	}
}

#undef LOCTEXT_NAMESPACE
