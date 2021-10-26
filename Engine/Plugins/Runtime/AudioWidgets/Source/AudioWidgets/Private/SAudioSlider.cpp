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
const FVector2D SAudioSliderBase::LinearRange = FVector2D(0.0f, 1.0f);

SAudioSliderBase::SAudioSliderBase()
{
}

void SAudioSliderBase::Construct(const SAudioSliderBase::FArguments& InArgs)
{
	OnValueChanged = InArgs._OnValueChanged;
	ValueAttribute = InArgs._Value;
	SliderBackgroundColor = InArgs._SliderBackgroundColor;
	LabelBackgroundColor = InArgs._LabelBackgroundColor;
	SliderBarColor = InArgs._SliderBarColor;
	SliderThumbColor = InArgs._SliderThumbColor;
	WidgetBackgroundColor = InArgs._WidgetBackgroundColor;
	Orientation = InArgs._Orientation;
	DesiredSizeOverride = InArgs._DesiredSizeOverride;

	// Get style
	const ISlateStyle* AudioWidgetsStyle = FSlateStyleRegistry::FindSlateStyle("AudioWidgetsStyle");
	if (AudioWidgetsStyle)
	{
		// Create components
		// Images 
		SAssignNew(WidgetBackgroundImage, SImage)
			.Image(AudioWidgetsStyle->GetBrush("AudioSlider.WidgetBackground"))
			.ColorAndOpacity(WidgetBackgroundColor);

		SAssignNew(SliderBackgroundTopCapImage, SImage)
			.Image(AudioWidgetsStyle->GetBrush("AudioSlider.SliderBackgroundCap"))
			.ColorAndOpacity(SliderBackgroundColor)
			.RenderTransformPivot(FVector2D(0.5f, 0.5f));
		SAssignNew(SliderBackgroundLeftCapImage, SImage)
			.Image(AudioWidgetsStyle->GetBrush("AudioSlider.SliderBackgroundCapHorizontal"))
			.ColorAndOpacity(SliderBackgroundColor)
			.RenderTransformPivot(FVector2D(0.5f, 0.5f));
		SAssignNew(SliderBackgroundRectangleImage, SImage)
			.Image(AudioWidgetsStyle->GetBrush("AudioSlider.SliderBackgroundRectangle"))
			.ColorAndOpacity(SliderBackgroundColor);
		SAssignNew(SliderBackgroundBottomCapImage, SImage)
			.Image(AudioWidgetsStyle->GetBrush("AudioSlider.SliderBackgroundCap"))
			.ColorAndOpacity(SliderBackgroundColor)
			.RenderTransformPivot(FVector2D(0.5f, 0.5f))
			.RenderTransform(FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(180.0f))));
		SAssignNew(SliderBackgroundRightCapImage, SImage)
			.Image(AudioWidgetsStyle->GetBrush("AudioSlider.SliderBackgroundCapHorizontal"))
			.ColorAndOpacity(SliderBackgroundColor)
			.RenderTransformPivot(FVector2D(0.5f, 0.5f))
			.RenderTransform(FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(180.0f))));

		SAssignNew(SliderBarTopCapImage, SImage)
			.Image(AudioWidgetsStyle->GetBrush("AudioSlider.SliderBarCap"))
			.ColorAndOpacity(SliderBarColor)
			.RenderTransformPivot(FVector2D(0.5f, 0.5f));
		SAssignNew(SliderBarRectangleImage, SImage)
			.Image(AudioWidgetsStyle->GetBrush("AudioSlider.SliderBarRectangle"))
			.ColorAndOpacity(SliderBarColor);
		SAssignNew(SliderBarBottomCapImage, SImage)
			.Image(AudioWidgetsStyle->GetBrush("AudioSlider.SliderBarCap"))
			.ColorAndOpacity(SliderBarColor)
			.RenderTransformPivot(FVector2D(0.5f, 0.5f));

		// Underlying slider widget
		SAssignNew(Slider, SSlider)
			.Value(ValueAttribute.Get())
			.Style(&AudioWidgetsStyle->GetWidgetStyle<FSliderStyle>("AudioSlider.Slider"))
			.Orientation(Orientation.Get())
			.OnValueChanged_Lambda([this](float Value)
			{
				ValueAttribute.Set(Value);
				OnValueChanged.ExecuteIfBound(Value);
				const float OutputValue = GetOutputValue(Value);
				Label->SetValueText(OutputValue);
			});

		// Text label
		SAssignNew(Label, SAudioTextBox)
			.OnValueTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type CommitType)
			{
				const float OutputValue = FCString::Atof(*Text.ToString());
				const float LinValue = GetLinValue(OutputValue);
				ValueAttribute.Set(LinValue);
				Slider->SetValue(LinValue);
				OnValueChanged.ExecuteIfBound(LinValue);
				})
			.LabelBackgroundColor(LabelBackgroundColor);
	}

	ChildSlot
	[
		CreateWidgetLayout()
	];
}

void SAudioSliderBase::SetValue(float LinValue)
{
	ValueAttribute.Set(LinValue);
	const float OutputValue = GetOutputValue(LinValue);
	Label->SetValueText(OutputValue);
	Slider->SetValue(LinValue);
}

void SAudioSliderBase::SetOrientation(EOrientation InOrientation)
{
	SetAttribute(Orientation, TAttribute<EOrientation>(InOrientation), EInvalidateWidgetReason::Layout);
	Slider->SetOrientation(InOrientation);
	LayoutWidgetSwitcher->SetActiveWidgetIndex(Orientation.Get());

	// Set widget component orientations
	const ISlateStyle* AudioWidgetsStyle = FSlateStyleRegistry::FindSlateStyle("AudioWidgetsStyle");
	if (AudioWidgetsStyle)
	{
		if (Orientation.Get() == Orient_Horizontal)
		{
			WidgetBackgroundImage->SetDesiredSizeOverride(AudioWidgetsStyle->GetVector("AudioSlider.DesiredWidgetSizeHorizontal"));
			SliderBackgroundRectangleImage->SetDesiredSizeOverride(FVector2D(AudioWidgetsStyle->GetBrush("AudioSlider.SliderBackgroundRectangle")->ImageSize.Y, AudioWidgetsStyle->GetBrush("AudioSlider.SliderBackgroundRectangle")->ImageSize.X));
			SliderBarTopCapImage->SetRenderTransform(FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(-90.0f)), FVector2D(AudioWidgetsStyle->GetBrush("AudioSlider.SliderBarCap")->ImageSize.Y / 2.0f, 0.0f)));
			SliderBarRectangleImage->SetDesiredSizeOverride(FVector2D(AudioWidgetsStyle->GetBrush("AudioSlider.SliderBarRectangle")->ImageSize.Y, AudioWidgetsStyle->GetBrush("AudioSlider.SliderBarRectangle")->ImageSize.X));
			SliderBarBottomCapImage->SetRenderTransform(FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(90.0f)), FVector2D(-AudioWidgetsStyle->GetBrush("AudioSlider.SliderBarCap")->ImageSize.Y / 2.0f, 0.0f)));

		}
		else if (Orientation.Get() == Orient_Vertical)
		{
			WidgetBackgroundImage->SetDesiredSizeOverride(TOptional<FVector2D>());
			SliderBackgroundRectangleImage->SetDesiredSizeOverride(TOptional<FVector2D>());
			SliderBarTopCapImage->SetRenderTransform(FSlateRenderTransform());
			SliderBarRectangleImage->SetDesiredSizeOverride(TOptional<FVector2D>());
			SliderBarBottomCapImage->SetRenderTransform(FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(180.0f))));
		}
	}
}

FVector2D SAudioSliderBase::ComputeDesiredSize(float) const
{
	if (DesiredSizeOverride.Get().IsSet())
	{
		return DesiredSizeOverride.Get().GetValue();
	}
	const ISlateStyle* AudioWidgetsStyle = FSlateStyleRegistry::FindSlateStyle("AudioWidgetsStyle");
	if (ensure(AudioWidgetsStyle))
	{
		const FName DesiredSizeName = Orientation.Get() == Orient_Vertical
			? "AudioSlider.DesiredWidgetSizeVertical"
			: "AudioSlider.DesiredWidgetSizeHorizontal";
		return AudioWidgetsStyle->GetVector(DesiredSizeName);
	}
	else
	{
		FVector2D DesiredWidgetSize = FVector2D(26.0f, 477.0f);
		return Orientation.Get() == Orient_Vertical ? DesiredWidgetSize : FVector2D(DesiredWidgetSize.Y, DesiredWidgetSize.X);
	}
}

void SAudioSliderBase::SetDesiredSizeOverride(const FVector2D Size)
{
	SetAttribute(DesiredSizeOverride, TAttribute<TOptional<FVector2D>>(Size), EInvalidateWidgetReason::Layout);
}

const float SAudioSliderBase::GetOutputValue(const float LinValue)
{
	return FMath::Clamp(LinValue, OutputRange.X, OutputRange.Y);
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
	SliderBackgroundLeftCapImage->SetColorAndOpacity(SliderBackgroundColor);
	SliderBackgroundBottomCapImage->SetColorAndOpacity(SliderBackgroundColor);
	SliderBackgroundRightCapImage->SetColorAndOpacity(SliderBackgroundColor);
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

void SAudioSliderBase::SetWidgetBackgroundColor(FSlateColor InWidgetBackgroundColor)
{
	SetAttribute(WidgetBackgroundColor, TAttribute<FSlateColor>(InWidgetBackgroundColor), EInvalidateWidgetReason::Paint);
	WidgetBackgroundImage->SetColorAndOpacity(WidgetBackgroundColor);
}

void SAudioSliderBase::SetOutputRange(const FVector2D Range)
{
	if (Range.Y > Range.X)
	{
		OutputRange = Range;
		SetValue(FMath::Clamp(ValueAttribute.Get(), Range.X, Range.Y));
		Label->UpdateValueTextWidth(Range);
	}
}

void SAudioSliderBase::SetLabelBackgroundColor(FSlateColor InColor)
{
	Label->SetLabelBackgroundColor(InColor);
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
	const ISlateStyle* AudioWidgetsStyle = FSlateStyleRegistry::FindSlateStyle("AudioWidgetsStyle");
	if (AudioWidgetsStyle)
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
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.HAlign(HAlign_Fill)
						.VAlign(VAlign_Center)
						.AutoWidth()
						.Padding(4.0f, 0.0f, 0.0f, 0.0f)
						[
							SliderBackgroundLeftCapImage.ToSharedRef()
						]
						+ SHorizontalBox::Slot()
						.HAlign(HAlign_Fill)
						.VAlign(VAlign_Center)
						.FillWidth(1.0f)
						[
							SliderBackgroundRectangleImage.ToSharedRef()
						]
						+ SHorizontalBox::Slot()
						.HAlign(HAlign_Fill)
						.VAlign(VAlign_Center)
						.AutoWidth()
						.Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SliderBackgroundRightCapImage.ToSharedRef()
						]
					]
					// Slider bar image
					+ SOverlay::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Center)
					.Padding(10.0f, 0.0f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.HAlign(HAlign_Fill)
						.VAlign(VAlign_Center)
						.AutoWidth()
						[
							SliderBarTopCapImage.ToSharedRef()
						]
						+ SHorizontalBox::Slot()
						.HAlign(HAlign_Fill)
						.VAlign(VAlign_Center)
						.FillWidth(1.0f)
						[
							SliderBarRectangleImage.ToSharedRef()
						]
						+ SHorizontalBox::Slot()
						.HAlign(HAlign_Fill)
						.VAlign(VAlign_Center)
						.AutoWidth()
						[
							SliderBarBottomCapImage.ToSharedRef()
						]
					]
					+ SOverlay::Slot()
					// Actual SSlider
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Center)
					[
						Slider.ToSharedRef()
					]
				]
				// Text Label
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
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
				.Padding(0.0f, AudioWidgetsStyle->GetFloat("AudioSlider.LabelVerticalPadding"))
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
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Fill)
						.AutoHeight()
						[
							SliderBackgroundTopCapImage.ToSharedRef()
						]
						+ SVerticalBox::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Fill)
						.FillHeight(1.0f)
						[
							SliderBackgroundRectangleImage.ToSharedRef()
						]
						+ SVerticalBox::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Fill)
						.AutoHeight()
						[
							SliderBackgroundBottomCapImage.ToSharedRef()
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
							SliderBarTopCapImage.ToSharedRef()
						]
						+ SVerticalBox::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Fill)
						.FillHeight(1.0f)
						[
							SliderBarRectangleImage.ToSharedRef()
						]
						+ SVerticalBox::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Fill)
						.AutoHeight()
						[
							SliderBarBottomCapImage.ToSharedRef()
						]
					]
					+ SOverlay::Slot()
					// Actual SSlider
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Fill)
					[
						Slider.ToSharedRef()
					]
				]
			]		
		];
		LayoutWidgetSwitcher->SetActiveWidgetIndex(Orientation.Get());
		SetOrientation(Orientation.Get());
	}
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
	Label->SetValueText(GetOutputValue(ValueAttribute.Get()));
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
		return LinToOutputCurve->GetFloatValue(LinValue);
	}
	return FMath::GetMappedRangeValueClamped(LinearRange, OutputRange, LinValue);
}

const float SAudioSlider::GetLinValue(const float OutputValue)
{
	if (OutputToLinCurve.IsValid())
	{
		return OutputToLinCurve->GetFloatValue(OutputValue);
	}
	return FMath::GetMappedRangeValueClamped(OutputRange, LinearRange, OutputValue);
}

// SAudioVolumeSlider
SAudioVolumeSlider::SAudioVolumeSlider()
{
}

void SAudioVolumeSlider::Construct(const SAudioSlider::FArguments& InArgs)
{
	SAudioSlider::Construct(InArgs);

	SetOutputRange(FVector2D(-100.0f, 12.0f));
	SetLinToOutputCurve(LoadObject<UCurveFloat>(NULL, TEXT("/AudioWidgets/AudioControlCurves/AudioControl_LinDbCurveDefault"), NULL, LOAD_None, NULL));
	SetOutputToLinCurve(LoadObject<UCurveFloat>(NULL, TEXT("/AudioWidgets/AudioControlCurves/AudioControl_DbLinCurveDefault"), NULL, LOAD_None, NULL));
	Label->SetUnitsText(FText::FromString("dB"));
}

// SAudioFrequencySlider
SAudioFrequencySlider::SAudioFrequencySlider()
{
}

void SAudioFrequencySlider::Construct(const SAudioSlider::FArguments& InArgs)
{
	SAudioSliderBase::Construct(InArgs);
	SetOutputRange(FVector2D(20.0f, 20000.0f));
	Label->SetUnitsText(FText::FromString("Hz"));
}

const float SAudioFrequencySlider::GetOutputValue(const float LinValue)
{
	return Audio::GetLogFrequencyClamped(LinValue, LinearRange, OutputRange);
}

const float SAudioFrequencySlider::GetLinValue(const float OutputValue)
{
	return Audio::GetLinearFrequencyClamped(OutputValue, LinearRange, OutputRange);
}
