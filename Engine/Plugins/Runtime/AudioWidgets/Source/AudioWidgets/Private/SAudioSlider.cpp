// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAudioSlider.h"
#include "Brushes/SlateImageBrush.h"
#include "DSP/Dsp.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/SlateStyle.h"
#include "Styling/StyleColors.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"

// SAudioSliderBase
const FVector2D SAudioSliderBase::LinearRange = FVector2D(0.0f, 1.0f);

SAudioSliderBase::SAudioSliderBase()
{
}

void SAudioSliderBase::Construct(const SAudioSliderBase::FArguments& InArgs)
{
	Style = InArgs._Style;
	OnValueChanged = InArgs._OnValueChanged;
	OnValueCommitted = InArgs._OnValueCommitted;
	ValueAttribute = InArgs._Value;
	SliderBackgroundColor = InArgs._SliderBackgroundColor;
	SliderBarColor = InArgs._SliderBarColor;
	SliderThumbColor = InArgs._SliderThumbColor;
	WidgetBackgroundColor = InArgs._WidgetBackgroundColor;
	Orientation = InArgs._Orientation;
	DesiredSizeOverride = InArgs._DesiredSizeOverride;

	// Get style
	const ISlateStyle* AudioWidgetsStyle = FSlateStyleRegistry::FindSlateStyle("AudioWidgetsStyle");
	if (AudioWidgetsStyle)
	{
		Style = &AudioWidgetsStyle->GetWidgetStyle<FAudioSliderStyle>("AudioSlider.Style");
	}

	// Create components
	SliderBackgroundSize = Style->SliderBackgroundSize;
	SliderBackgroundBrush = FSlateRoundedBoxBrush(SliderBarColor.Get().GetSpecifiedColor(), SliderBackgroundSize.X / 2.0f, SliderBackgroundColor.Get().GetSpecifiedColor(), SliderBackgroundSize.X / 3.0f, SliderBackgroundSize);

	SAssignNew(SliderBackgroundImage, SImage)
		.Image(&SliderBackgroundBrush);
	SAssignNew(WidgetBackgroundImage, SImage)
		.Image(&Style->WidgetBackgroundImage)
		.ColorAndOpacity(WidgetBackgroundColor);

	// Underlying slider widget
	SAssignNew(Slider, SSlider)
		.Value(ValueAttribute.Get())
		.Style(&Style->SliderStyle)
		.Orientation(Orientation.Get())
		.IndentHandle(false)
		.OnValueChanged_Lambda([this](float Value)
		{
			ValueAttribute.Set(Value);
			OnValueChanged.ExecuteIfBound(Value);
			const float OutputValue = GetOutputValueForText(Value);
			Label->SetValueText(OutputValue);
		})
		.OnMouseCaptureEnd_Lambda([this]()
		{
			OnValueCommitted.ExecuteIfBound(ValueAttribute.Get());
		});

	// Text label
	SAssignNew(Label, SAudioTextBox)
		.Style(&Style->TextBoxStyle)
		.OnValueTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type CommitType)
		{
			const float OutputValue = FCString::Atof(*Text.ToString());
			const float LinValue = GetLinValueForText(OutputValue);
			ValueAttribute.Set(LinValue);
			Slider->SetValue(LinValue);
			OnValueChanged.ExecuteIfBound(LinValue);
			OnValueCommitted.ExecuteIfBound(LinValue);
		});

	ChildSlot
	[
		CreateWidgetLayout()
	];
}

void SAudioSliderBase::SetValue(float LinValue)
{
	ValueAttribute.Set(LinValue);
	const float OutputValueForText = GetOutputValueForText(LinValue);
	Label->SetValueText(OutputValueForText);
	Slider->SetValue(LinValue);
}

void SAudioSliderBase::SetOrientation(EOrientation InOrientation)
{
	SetAttribute(Orientation, TAttribute<EOrientation>(InOrientation), EInvalidateWidgetReason::Layout);
	Slider->SetOrientation(InOrientation);
	LayoutWidgetSwitcher->SetActiveWidgetIndex(Orientation.Get());

	// Set widget component orientations
	const FVector2D TextBoxImageSize = Style->TextBoxStyle.BackgroundImage.ImageSize;
	if (Orientation.Get() == Orient_Horizontal)
	{
		const FVector2D DesiredWidgetSizeHorizontal = FVector2D(SliderBackgroundSize.Y + TextBoxImageSize.X + Style->LabelPadding, SliderBackgroundSize.X);

		SliderBackgroundImage->SetDesiredSizeOverride(FVector2D(SliderBackgroundSize.Y, SliderBackgroundSize.X));
		WidgetBackgroundImage->SetDesiredSizeOverride(DesiredWidgetSizeHorizontal);
	}
	else if (Orientation.Get() == Orient_Vertical)
	{
		SliderBackgroundImage->SetDesiredSizeOverride(TOptional<FVector2D>());
		WidgetBackgroundImage->SetDesiredSizeOverride(TOptional<FVector2D>());
	}
}

FVector2D SAudioSliderBase::ComputeDesiredSize(float) const
{
	if (DesiredSizeOverride.Get().IsSet())
	{
		return DesiredSizeOverride.Get().GetValue();
	}
	const FVector2D TextBoxImageSize = Style->TextBoxStyle.BackgroundImage.ImageSize;

	const FVector2D DesiredWidgetSizeVertical = FVector2D(TextBoxImageSize.X, TextBoxImageSize.Y + Style->LabelPadding + SliderBackgroundSize.Y);
	const FVector2D DesiredWidgetSizeHorizontal = FVector2D(SliderBackgroundSize.Y + TextBoxImageSize.X + Style->LabelPadding, SliderBackgroundSize.X);

	return Orientation.Get() == Orient_Vertical ? 
		DesiredWidgetSizeVertical : DesiredWidgetSizeHorizontal;
}

void SAudioSliderBase::SetDesiredSizeOverride(const FVector2D Size)
{
	SetAttribute(DesiredSizeOverride, TAttribute<TOptional<FVector2D>>(Size), EInvalidateWidgetReason::Layout);
}

const float SAudioSliderBase::GetOutputValue(const float LinValue)
{
	return FMath::Clamp(LinValue, OutputRange.X, OutputRange.Y);
}

const float SAudioSliderBase::GetOutputValueForText(const float LinValue)
{
	return GetOutputValue(LinValue);
}

const float SAudioSliderBase::GetLinValueForText(const float OutputValue)
{
	return GetLinValue(OutputValue);
}

const float SAudioSliderBase::GetLinValue(const float OutputValue)
{
	return OutputValue;
}

void SAudioSliderBase::SetSliderBackgroundColor(FSlateColor InSliderBackgroundColor)
{
	SetAttribute(SliderBackgroundColor, TAttribute<FSlateColor>(InSliderBackgroundColor), EInvalidateWidgetReason::Paint);
	SliderBackgroundBrush = FSlateRoundedBoxBrush(SliderBarColor.Get().GetSpecifiedColor(), SliderBackgroundSize.X / 2.0f, SliderBackgroundColor.Get().GetSpecifiedColor(), SliderBackgroundSize.X / 3.0f, SliderBackgroundSize);
	SliderBackgroundImage->SetImage(&SliderBackgroundBrush);
}

void SAudioSliderBase::SetSliderBarColor(FSlateColor InSliderBarColor)
{
	SetAttribute(SliderBarColor, TAttribute<FSlateColor>(InSliderBarColor), EInvalidateWidgetReason::Paint);
	SliderBackgroundBrush = FSlateRoundedBoxBrush(SliderBarColor.Get().GetSpecifiedColor(), SliderBackgroundSize.X / 2.0f, SliderBackgroundColor.Get().GetSpecifiedColor(), SliderBackgroundSize.X / 3.0f, SliderBackgroundSize);
	SliderBackgroundImage->SetImage(&SliderBackgroundBrush);
}

void SAudioSliderBase::SetSliderThumbColor(FSlateColor InSliderThumbColor)
{
	SetAttribute(SliderThumbColor, TAttribute<FSlateColor>(InSliderThumbColor), EInvalidateWidgetReason::Paint);
	Slider->SetSliderHandleColor(SliderThumbColor.Get());
}

void SAudioSliderBase::SetWidgetBackgroundColor(FSlateColor InWidgetBackgroundColor)
{
	SetAttribute(WidgetBackgroundColor, TAttribute<FSlateColor>(InWidgetBackgroundColor), EInvalidateWidgetReason::Paint);
	WidgetBackgroundImage->SetColorAndOpacity(WidgetBackgroundColor);
}

void SAudioSliderBase::SetOutputRange(const FVector2D Range)
{
	OutputRange = Range;
	// if Range.Y < Range.X, set Range.X to Range.Y
	OutputRange.X = FMath::Min(Range.X, Range.Y);

	const float OutputValue = GetOutputValue(ValueAttribute.Get());
	const float ClampedOutputValue = FMath::Clamp(OutputValue, OutputRange.X, OutputRange.Y);
	const float LinValue = GetLinValue(ClampedOutputValue);
	SetValue(LinValue);

	Label->UpdateValueTextWidth(OutputRange);
}

void SAudioSliderBase::SetLabelBackgroundColor(FSlateColor InColor)
{
	Label->SetLabelBackgroundColor(InColor.GetSpecifiedColor());
}

void SAudioSliderBase::SetUnitsText(const FText Units)
{
	Label->SetUnitsText(Units);
}

void SAudioSliderBase::SetUnitsTextReadOnly(const bool bIsReadOnly)
{
	Label->SetUnitsTextReadOnly(bIsReadOnly);
}

void SAudioSliderBase::SetValueTextReadOnly(const bool bIsReadOnly)
{
	Label->SetValueTextReadOnly(bIsReadOnly);
}

void SAudioSliderBase::SetShowLabelOnlyOnHover(const bool bShowLabelOnlyOnHover)
{
	Label->SetShowLabelOnlyOnHover(bShowLabelOnlyOnHover);
}

void SAudioSliderBase::SetShowUnitsText(const bool bShowUnitsText)
{
	Label->SetShowUnitsText(bShowUnitsText);
}

TSharedRef<SWidgetSwitcher> SAudioSliderBase::CreateWidgetLayout()
{
	SAssignNew(LayoutWidgetSwitcher, SWidgetSwitcher);
	// Create overall layout
	// Horizontal orientation
	LayoutWidgetSwitcher->AddSlot(EOrientation::Orient_Horizontal)
	[
		SNew(SOverlay)
		// Widget background image
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		[
			WidgetBackgroundImage.ToSharedRef()
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			// Slider
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
			[
				SNew(SOverlay)
				// Slider background image
				+ SOverlay::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Center)
				[
					SliderBackgroundImage.ToSharedRef()
				]
				+ SOverlay::Slot()
				// Actual SSlider
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Center)
				.Padding(Style->LabelPadding, 0.0f)
				[
					Slider.ToSharedRef()
				]
			]
			// Text Label
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
			.Padding(Style->LabelPadding, 0.0f, 0.0f, 0.0f)
			[
				Label.ToSharedRef()
			]
		]	
	];
	// Vertical orientation
	LayoutWidgetSwitcher->AddSlot(EOrientation::Orient_Vertical)
	[
		SNew(SOverlay)
		// Widget background image
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Fill)
		[
			WidgetBackgroundImage.ToSharedRef()
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Fill)
		[
			SNew(SVerticalBox)
			// Text Label
			+ SVerticalBox::Slot()
			.Padding(0.0f, 0.0f, 0.0f, Style->LabelPadding)
			.AutoHeight()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				Label.ToSharedRef()
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
					SliderBackgroundImage.ToSharedRef()
				]
				+ SOverlay::Slot()
				// Actual SSlider
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Fill)
				.Padding(0.0f, Style->LabelPadding)
				[
					Slider.ToSharedRef()
				]
			]
		]		
	];
	LayoutWidgetSwitcher->SetActiveWidgetIndex(Orientation.Get());
	SetOrientation(Orientation.Get());

	return LayoutWidgetSwitcher.ToSharedRef();
}

// SAudioSlider
SAudioSlider::SAudioSlider()
{
}

void SAudioSlider::Construct(const SAudioSliderBase::FArguments& InArgs)
{
	SAudioSliderBase::Construct(InArgs);
}

void SAudioSlider::SetLinToOutputCurve(const TWeakObjectPtr<const UCurveFloat> InLinToOutputCurve)
{
	LinToOutputCurve = InLinToOutputCurve;
	Label->SetValueText(GetOutputValueForText(ValueAttribute.Get()));
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
	if (LinToOutputCurve.IsValid())
	{
		const float CurveOutputValue = LinToOutputCurve->GetFloatValue(LinValue);
		return FMath::Clamp(CurveOutputValue, OutputRange.X, OutputRange.Y);
	}
	return FMath::GetMappedRangeValueClamped(LinearRange, OutputRange, LinValue);
}

const float SAudioSlider::GetLinValue(const float OutputValue)
{
	if (OutputToLinCurve.IsValid())
	{
		const float CurveLinValue = OutputToLinCurve->GetFloatValue(OutputValue);
		return FMath::Clamp(CurveLinValue, OutputRange.X, OutputRange.Y);
	}
	return FMath::GetMappedRangeValueClamped(OutputRange, LinearRange, OutputValue);
}

// SAudioVolumeSlider
const float SAudioVolumeSlider::MinDbValue = -160.0f;
const float SAudioVolumeSlider::MaxDbValue = 770.0f;
SAudioVolumeSlider::SAudioVolumeSlider()
{
}

void SAudioVolumeSlider::Construct(const SAudioSlider::FArguments& InArgs)
{
	SAudioSliderBase::Construct(InArgs);
	SAudioSliderBase::SetOutputRange(FVector2D(-100.0f, 0.0f));
	Label->SetUnitsText(FText::FromString("dB"));
}

const float SAudioVolumeSlider::GetDbValueFromLin(const float LinValue)
{
	// convert from linear 0-1 space to decibel OutputRange that has been converted to linear 
	const FVector2D LinearSliderRange = FVector2D(Audio::ConvertToLinear(OutputRange.X), Audio::ConvertToLinear(OutputRange.Y));
	const float LinearSliderValue = FMath::GetMappedRangeValueClamped(LinearRange, LinearSliderRange, LinValue);
	// convert from linear to decibels 
	float OutputValue = Audio::ConvertToDecibels(LinearSliderValue);
	return FMath::Clamp(OutputValue, OutputRange.X, OutputRange.Y);
}

const float SAudioVolumeSlider::GetLinValueFromDb(const float DbValue)
{
	float ClampedValue = FMath::Clamp(DbValue, OutputRange.X, OutputRange.Y);
	// convert from decibels to linear
	float LinearSliderValue = Audio::ConvertToLinear(ClampedValue);
	// convert from decibel OutputRange that has been converted to linear to linear 0-1 space 
	const FVector2D LinearSliderRange = FVector2D(Audio::ConvertToLinear(OutputRange.X), Audio::ConvertToLinear(OutputRange.Y));
	return FMath::GetMappedRangeValueClamped(LinearSliderRange, LinearRange, LinearSliderValue);
}

const float SAudioVolumeSlider::GetOutputValue(const float LinValue)
{
	if (bUseLinearOutput)
	{
		return FMath::Clamp(LinValue, LinearRange.X, LinearRange.Y);
	}
	else
	{
		return GetDbValueFromLin(LinValue);
	}
}

const float SAudioVolumeSlider::GetLinValue(const float OutputValue)
{
	if (bUseLinearOutput)
	{
		return FMath::Clamp(OutputValue, LinearRange.X, LinearRange.Y);
	}
	else
	{
		return GetLinValueFromDb(OutputValue);
	}
}

const float SAudioVolumeSlider::GetOutputValueForText(const float LinValue)
{
	return GetDbValueFromLin(LinValue);
}

const float SAudioVolumeSlider::GetLinValueForText(const float OutputValue)
{
	return GetLinValueFromDb(OutputValue);
}

void SAudioVolumeSlider::SetUseLinearOutput(bool InUseLinearOutput)
{
	bUseLinearOutput = InUseLinearOutput;
}

void SAudioVolumeSlider::SetOutputRange(const FVector2D Range)
{
	// if using linear output, output range cannot be changed 
	if (!bUseLinearOutput)
	{
		SAudioSliderBase::SetOutputRange(FVector2D(FMath::Max(MinDbValue, Range.X), FMath::Min(MaxDbValue, Range.Y)));
	}
}

// SAudioFrequencySlider
SAudioFrequencySlider::SAudioFrequencySlider()
{
}

void SAudioFrequencySlider::Construct(const SAudioSlider::FArguments& InArgs)
{
	SAudioSliderBase::Construct(InArgs);
	SetOutputRange(FVector2D(MIN_FILTER_FREQUENCY, MAX_FILTER_FREQUENCY));
	Label->SetUnitsText(FText::FromString("Hz"));
}

const float SAudioFrequencySlider::GetOutputValue(const float LinValue)
{
	return Audio::GetLogFrequencyClamped(LinValue, LinearRange, OutputRange);
}

const float SAudioFrequencySlider::GetLinValue(const float OutputValue)
{
	// edge case to avoid audio function returning negative value
	if (FMath::IsNearlyEqual(OutputValue, OutputRange.X))
	{
		return LinearRange.X;
	}
	if (FMath::IsNearlyEqual(OutputValue, OutputRange.Y))
	{
		return LinearRange.Y;
	}
	return Audio::GetLinearFrequencyClamped(OutputValue, LinearRange, OutputRange);
}
