// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAudioSlider.h"
#include "Brushes/SlateImageBrush.h"
#include "DSP/Dsp.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"

// SAudioSliderBase
SAudioSliderBase::SAudioSliderBase()
{
}

void SAudioSliderBase::Construct(const SAudioSliderBase::FArguments& InDeclaration)
{
	OnValueChanged = InDeclaration._OnValueChanged;
	ValueAttribute = InDeclaration._Value;
	AlwaysShowLabel = InDeclaration._AlwaysShowLabel;
	LabelBackgroundColor = InDeclaration._LabelBackgroundColor;
	SliderBackgroundColor = InDeclaration._SliderBackgroundColor;
	SliderBarColor = InDeclaration._SliderBarColor;
	SliderThumbColor = InDeclaration._SliderThumbColor;
	WidgetBackgroundColor = InDeclaration._WidgetBackgroundColor;

	const ISlateStyle* AudioSliderStyle = FSlateStyleRegistry::FindSlateStyle("AudioSliderStyle");
	ensure(AudioSliderStyle);

	ChildSlot
	[
		SNew(SOverlay)
		// Widget background image
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Fill)
		[
			SAssignNew(WidgetBackgroundImage, SImage)
			.Image(AudioSliderStyle->GetBrush("AudioSlider.WidgetBackground"))
			.ColorAndOpacity(WidgetBackgroundColor)
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Fill)
		[
			SNew(SVerticalBox)
			 // Text Label
			+ SVerticalBox::Slot()
			.Padding(0.0f, AudioSliderStyle->GetFloat("AudioSlider.LabelVerticalPadding"))
			.AutoHeight()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SConstraintCanvas)
				.Visibility_Lambda([this]() 
				{
					return (AlwaysShowLabel.Get() || this->IsHovered()) ? EVisibility::Visible : EVisibility::Hidden;
				})
				+ SConstraintCanvas::Slot()
				.Anchors(FAnchors(0.0f, 0.5f, 1.0f, 0.5f))
				.Alignment(FVector2D(0.0f, 0.5f))
				.Offset(FMargin(0.0f, 0.0f, 0.0f, 24.0f))
				[
					SAssignNew(LabelBackgroundImage, SImage)
					.Image(AudioSliderStyle->GetBrush("AudioSlider.LabelBackground"))
					.ColorAndOpacity(LabelBackgroundColor)
				]
				+ SConstraintCanvas::Slot()
				.Anchors(FAnchors(0.0f, 0.5f, 1.0f, 0.5f))
				.Alignment(FVector2D(0.0f, 0.5f))
				.Offset(FMargin(0.0f, 0.0f, 0.0f, 32.0f))
				.AutoSize(true)
				//.ZOrder(3)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					//.AutoWidth()
					.FillWidth(0.5)
					.Padding(4.0f, 0.0f)
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					[
						SAssignNew(ValueText, SEditableText)
						.Text(FText::AsNumber(GetOutputValue(ValueAttribute.Get())))
						.Justification(ETextJustify::Right)
						.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type CommitType)
						{
							const float OutputValue = FCString::Atof(*Text.ToString());
							const float LinValue = GetLinValue(OutputValue);
							// Update this widget's value 
							ValueAttribute.Set(LinValue);
							// Update the slider's value attribute
							Slider->SetValue(ValueAttribute);
							// Call delegate if bound
							OnValueChanged.ExecuteIfBound(LinValue);
						})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(2.0f, 0.0f, 6.0f, 0.0f)
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					[
						SAssignNew(UnitsText, SEditableText)
						.Text(FText::FromString("units"))
					]
				]
			]
			// Slider
			+ SVerticalBox::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Fill)
			[
				SNew(SOverlay)
				// Slider background image
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Fill)
				[
					SNew(SVerticalBox)
					+SVerticalBox::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Fill)
					.AutoHeight()
					[
						SAssignNew(SliderBackgroundTopCapImage, SImage)
						.Image(AudioSliderStyle->GetBrush("AudioSlider.SliderBackgroundCap"))
						.ColorAndOpacity(SliderBackgroundColor)
					]
					+ SVerticalBox::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Fill)
					.FillHeight(1.0f)
					[
						SAssignNew(SliderBackgroundRectangleImage, SImage)
						.Image(AudioSliderStyle->GetBrush("AudioSlider.SliderBackgroundRectangle"))
						.ColorAndOpacity(SliderBackgroundColor)
					]
					+ SVerticalBox::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Fill)
					.AutoHeight()
					[
						SAssignNew(SliderBackgroundBottomCapImage, SImage)
						.Image(AudioSliderStyle->GetBrush("AudioSlider.SliderBackgroundCap"))
						.ColorAndOpacity(SliderBackgroundColor)
						.RenderTransform(FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(180.0f))))
						.RenderTransformPivot(FVector2D(0.5f, 0.5f))
					]
				]
				// Slider bar image
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Fill)
				.Padding(0.0f, 10.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Fill)
					.AutoHeight()
					[
						SAssignNew(SliderBarTopCapImage, SImage)
						.Image(AudioSliderStyle->GetBrush("AudioSlider.SliderBarCap"))
						.ColorAndOpacity(SliderBarColor)
					]
					+ SVerticalBox::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Fill)
					.FillHeight(1.0f)
					[
						SAssignNew(SliderBarRectangleImage, SImage)
						.Image(AudioSliderStyle->GetBrush("AudioSlider.SliderBarRectangle"))
						.ColorAndOpacity(SliderBarColor)
					]
					+ SVerticalBox::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Fill)
					.AutoHeight()
					[
						SAssignNew(SliderBarBottomCapImage, SImage)
						.Image(AudioSliderStyle->GetBrush("AudioSlider.SliderBarCap"))
						.ColorAndOpacity(SliderBarColor)
						.RenderTransform(FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(180.0f))))
						.RenderTransformPivot(FVector2D(0.5f, 0.5f))
					]
				]
				+ SOverlay::Slot()
				// Actual SSlider
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Fill)
				[
					SAssignNew(Slider, SSlider)
					.Orientation(Orient_Vertical)
					.Value(InDeclaration._Value)
					.Style(&AudioSliderStyle->GetWidgetStyle<FSliderStyle>("AudioSlider.Slider"))
					.OnValueChanged_Lambda([this](float Value)
					{
						// Update this widget's value 
						ValueAttribute.Set(Value);
						// Call delegate if bound
						OnValueChanged.ExecuteIfBound(Value);
						// Convert lin to output, update this widget's text
						const float OutputValue = GetOutputValue(Value);
						ValueText->SetText(FText::AsNumber(OutputValue));
					})
				]
			]
		]
	];
}

FVector2D SAudioSliderBase::ComputeDesiredSize(float) const
{
	const ISlateStyle* AudioSliderStyle = FSlateStyleRegistry::FindSlateStyle("AudioSliderStyle");
	ensure(AudioSliderStyle);

	return AudioSliderStyle->GetVector("AudioSlider.DesiredWidgetSize");
}

void SAudioSliderBase::SetUnitsText(const FText Units)
{
	UnitsText->SetText(Units);
}

void SAudioSliderBase::SetTextReadOnly(const bool bIsReadOnly)
{
	UnitsText->SetIsReadOnly(bIsReadOnly);
	ValueText->SetIsReadOnly(bIsReadOnly);
}

void SAudioSliderBase::SetAlwaysShowLabel(const bool bAlwaysShowLabel)
{
	AlwaysShowLabel = bAlwaysShowLabel;
}

const float SAudioSliderBase::GetOutputValue(const float LinValue)
{
	return LinValue;
}

const float SAudioSliderBase::GetLinValue(const float OutputValue)
{
	return OutputValue;
}

void SAudioSliderBase::SetSliderBackgroundColor(FSlateColor InSliderBackgroundColor)
{
	SetAttribute(SliderBackgroundColor, TAttribute<FSlateColor>(InSliderBackgroundColor), EInvalidateWidgetReason::Paint);
	SliderBackgroundRectangleImage->SetColorAndOpacity(SliderBackgroundColor);
	SliderBackgroundTopCapImage->SetColorAndOpacity(SliderBackgroundColor);
	SliderBackgroundBottomCapImage->SetColorAndOpacity(SliderBackgroundColor);
}

void SAudioSliderBase::SetSliderBarColor(FSlateColor InSliderBarColor)
{
	SetAttribute(SliderBarColor, TAttribute<FSlateColor>(InSliderBarColor), EInvalidateWidgetReason::Paint);
	SliderBarRectangleImage->SetColorAndOpacity(SliderBarColor);
	SliderBarTopCapImage->SetColorAndOpacity(SliderBarColor);
	SliderBarBottomCapImage->SetColorAndOpacity(SliderBarColor);
}

void SAudioSliderBase::SetSliderThumbColor(FSlateColor InSliderThumbColor)
{
	SetAttribute(SliderThumbColor, TAttribute<FSlateColor>(InSliderThumbColor), EInvalidateWidgetReason::Paint);
	Slider->SetSliderHandleColor(SliderThumbColor.Get());
}

void SAudioSliderBase::SetLabelBackgroundColor(FSlateColor InLabelBackgroundColor)
{
	SetAttribute(LabelBackgroundColor, TAttribute<FSlateColor>(InLabelBackgroundColor), EInvalidateWidgetReason::Paint);
	LabelBackgroundImage->SetColorAndOpacity(LabelBackgroundColor);
}

void SAudioSliderBase::SetWidgetBackgroundColor(FSlateColor InWidgetBackgroundColor)
{
	SetAttribute(WidgetBackgroundColor, TAttribute<FSlateColor>(InWidgetBackgroundColor), EInvalidateWidgetReason::Paint);
	WidgetBackgroundImage->SetColorAndOpacity(WidgetBackgroundColor);
}

// SAudioSlider
SAudioSlider::SAudioSlider()
{
}

void SAudioSlider::Construct(const SAudioSliderBase::FArguments& InDeclaration)
{
	SAudioSliderBase::Construct(InDeclaration);
}

void SAudioSlider::SetLinToOutputCurve(const TWeakObjectPtr<const UCurveFloat> InLinToOutputCurve)
{
	LinToOutputCurve = InLinToOutputCurve;
	ValueText->SetText(FText::AsNumber(GetOutputValue(ValueAttribute.Get())));
}

void SAudioSlider::SetOutputToLinCurve(const TWeakObjectPtr<const UCurveFloat> InOutputToLinCurve)
{
	OutputToLinCurve = InOutputToLinCurve;
}

const TWeakObjectPtr<const UCurveFloat> SAudioSlider::GetOutputToLinCurve()
{
	return OutputToLinCurve;
}

const TWeakObjectPtr<const UCurveFloat> SAudioSlider::GetLinToOutputCurve()
{
	return LinToOutputCurve;
}

const float SAudioSlider::GetOutputValue(const float LinValue)
{
	return LinToOutputCurve->IsValidLowLevel() ? LinToOutputCurve->GetFloatValue(LinValue) : LinValue;
}

const float SAudioSlider::GetLinValue(const float OutputValue)
{
	return OutputToLinCurve->IsValidLowLevel() ? OutputToLinCurve->GetFloatValue(OutputValue) : OutputValue;
}

// SAudioVolumeSlider
SAudioVolumeSlider::SAudioVolumeSlider()
{
}

void SAudioVolumeSlider::Construct(const SAudioSlider::FArguments& InDeclaration)
{
	SAudioSlider::Construct(InDeclaration);

	SetLinToOutputCurve(LoadObject<UCurveFloat>(NULL, TEXT("/AudioWidgets/AudioControlCurves/AudioControl_LinDbCurveDefault"), NULL, LOAD_None, NULL));
	SetOutputToLinCurve(LoadObject<UCurveFloat>(NULL, TEXT("/AudioWidgets/AudioControlCurves/AudioControl_DbLinCurveDefault"), NULL, LOAD_None, NULL));
	SetUnitsText(FText::FromString("dB"));
}

// SAudioFrequencySlider
const FVector2D SAudioFrequencySlider::LinearRange = FVector2D(0.0f, 1.0f);

SAudioFrequencySlider::SAudioFrequencySlider()
{
}

void SAudioFrequencySlider::Construct(const SAudioSlider::FArguments& InDeclaration)
{
	SAudioSliderBase::Construct(InDeclaration);
	SetUnitsText(FText::FromString("Hz"));
}

void SAudioFrequencySlider::SetOutputRange(const FVector2D Range)
{
	OutputRange = Range;
}

const float SAudioFrequencySlider::GetOutputValue(const float LinValue)
{
	return Audio::GetLogFrequencyClamped(LinValue, LinearRange, OutputRange);
}

const float SAudioFrequencySlider::GetLinValue(const float OutputValue)
{
	return Audio::GetLinearFrequencyClamped(OutputValue, LinearRange, OutputRange);
}
