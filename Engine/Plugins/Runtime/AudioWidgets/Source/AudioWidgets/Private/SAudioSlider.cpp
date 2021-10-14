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
const FVariablePrecisionNumericInterface SAudioSliderBase::NumericInterface = FVariablePrecisionNumericInterface();

SAudioSliderBase::SAudioSliderBase()
{
}

void SAudioSliderBase::Construct(const SAudioSliderBase::FArguments& InArgs)
{
	OnValueChanged = InArgs._OnValueChanged;
	ValueAttribute = InArgs._Value;
	AlwaysShowLabel = InArgs._AlwaysShowLabel;
	ShowUnitsText = InArgs._ShowUnitsText;
	LabelBackgroundColor = InArgs._LabelBackgroundColor;
	SliderBackgroundColor = InArgs._SliderBackgroundColor;
	SliderBarColor = InArgs._SliderBarColor;
	SliderThumbColor = InArgs._SliderThumbColor;
	WidgetBackgroundColor = InArgs._WidgetBackgroundColor;
	Orientation = InArgs._Orientation;
	DesiredSizeOverride = InArgs._DesiredSizeOverride;

	// Get style
	const ISlateStyle* AudioSliderStyle = FSlateStyleRegistry::FindSlateStyle("AudioSliderStyle");
	if (AudioSliderStyle)
	{
		// Create components
		// Images 
		SAssignNew(WidgetBackgroundImage, SImage)
			.Image(AudioSliderStyle->GetBrush("AudioSlider.WidgetBackground"))
			.ColorAndOpacity(WidgetBackgroundColor);

		SAssignNew(SliderBackgroundTopCapImage, SImage)
			.Image(AudioSliderStyle->GetBrush("AudioSlider.SliderBackgroundCap"))
			.ColorAndOpacity(SliderBackgroundColor)
			.RenderTransformPivot(FVector2D(0.5f, 0.5f));
		SAssignNew(SliderBackgroundLeftCapImage, SImage)
			.Image(AudioSliderStyle->GetBrush("AudioSlider.SliderBackgroundCapHorizontal"))
			.ColorAndOpacity(SliderBackgroundColor)
			.RenderTransformPivot(FVector2D(0.5f, 0.5f));
		SAssignNew(SliderBackgroundRectangleImage, SImage)
			.Image(AudioSliderStyle->GetBrush("AudioSlider.SliderBackgroundRectangle"))
			.ColorAndOpacity(SliderBackgroundColor);
		SAssignNew(SliderBackgroundBottomCapImage, SImage)
			.Image(AudioSliderStyle->GetBrush("AudioSlider.SliderBackgroundCap"))
			.ColorAndOpacity(SliderBackgroundColor)
			.RenderTransformPivot(FVector2D(0.5f, 0.5f))
			.RenderTransform(FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(180.0f))));
		SAssignNew(SliderBackgroundRightCapImage, SImage)
			.Image(AudioSliderStyle->GetBrush("AudioSlider.SliderBackgroundCapHorizontal"))
			.ColorAndOpacity(SliderBackgroundColor)
			.RenderTransformPivot(FVector2D(0.5f, 0.5f))
			.RenderTransform(FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(180.0f))));

		SAssignNew(SliderBarTopCapImage, SImage)
			.Image(AudioSliderStyle->GetBrush("AudioSlider.SliderBarCap"))
			.ColorAndOpacity(SliderBarColor)
			.RenderTransformPivot(FVector2D(0.5f, 0.5f));
		SAssignNew(SliderBarRectangleImage, SImage)
			.Image(AudioSliderStyle->GetBrush("AudioSlider.SliderBarRectangle"))
			.ColorAndOpacity(SliderBarColor);
		SAssignNew(SliderBarBottomCapImage, SImage)
			.Image(AudioSliderStyle->GetBrush("AudioSlider.SliderBarCap"))
			.ColorAndOpacity(SliderBarColor)
			.RenderTransformPivot(FVector2D(0.5f, 0.5f));

		// Underlying slider widget
		SAssignNew(Slider, SSlider)
			.Value(ValueAttribute.Get())
			.Style(&AudioSliderStyle->GetWidgetStyle<FSliderStyle>("AudioSlider.Slider"))
			.Orientation(Orientation.Get())
			.OnValueChanged_Lambda([this](float Value)
			{
				ValueAttribute.Set(Value);
				OnValueChanged.ExecuteIfBound(Value);
				const float OutputValue = GetOutputValue(Value);
				ValueText->SetText(FText::FromString(NumericInterface.ToString(OutputValue)));
			});

		// Text label
		SAssignNew(LabelBackgroundImage, SImage)
			.Image(AudioSliderStyle->GetBrush("AudioSlider.LabelBackground"))
			.ColorAndOpacity(LabelBackgroundColor);
		SAssignNew(ValueText, SEditableText)
			.Text(FText::AsNumber(GetOutputValue(ValueAttribute.Get())))
			.Justification(ETextJustify::Right)
			.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type CommitType)
				{
					const float OutputValue = FCString::Atof(*Text.ToString());
					const float LinValue = GetLinValue(OutputValue);
					ValueAttribute.Set(LinValue);
					Slider->SetValue(ValueAttribute);
					OnValueChanged.ExecuteIfBound(LinValue);
				})
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis);
		SAssignNew(UnitsText, SEditableText)
			.Text(FText::FromString("units"))
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis);

		TextWidgetSwitcher = CreateTextWidgetSwitcher();
		SAssignNew(TextLabel, SOverlay)
			.Visibility_Lambda([this]()
			{
				return (AlwaysShowLabel.Get() || this->IsHovered()) ? EVisibility::Visible : EVisibility::Hidden;
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
		CreateWidgetLayout()
	];
}

void SAudioSliderBase::SetValue(float LinValue)
{
	ValueAttribute.Set(LinValue);
	const float OutputValue = GetOutputValue(LinValue);
	ValueText->SetText(FText::FromString(NumericInterface.ToString(OutputValue)));
	Slider->SetValue(LinValue);
}

void SAudioSliderBase::SetOrientation(EOrientation InOrientation)
{
	SetAttribute(Orientation, TAttribute<EOrientation>(InOrientation), EInvalidateWidgetReason::Layout);
	Slider->SetOrientation(InOrientation);
	LayoutWidgetSwitcher->SetActiveWidgetIndex(Orientation.Get());

	// Set widget component orientations
	const ISlateStyle* AudioSliderStyle = FSlateStyleRegistry::FindSlateStyle("AudioSliderStyle");
	if (AudioSliderStyle)
	{
		if (Orientation.Get() == Orient_Horizontal)
		{
			WidgetBackgroundImage->SetDesiredSizeOverride(AudioSliderStyle->GetVector("AudioSlider.DesiredWidgetSizeHorizontal"));
			SliderBackgroundRectangleImage->SetDesiredSizeOverride(FVector2D(AudioSliderStyle->GetBrush("AudioSlider.SliderBackgroundRectangle")->ImageSize.Y, AudioSliderStyle->GetBrush("AudioSlider.SliderBackgroundRectangle")->ImageSize.X));
			SliderBarTopCapImage->SetRenderTransform(FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(-90.0f)), FVector2D(AudioSliderStyle->GetBrush("AudioSlider.SliderBarCap")->ImageSize.Y / 2.0f, 0.0f)));
			SliderBarRectangleImage->SetDesiredSizeOverride(FVector2D(AudioSliderStyle->GetBrush("AudioSlider.SliderBarRectangle")->ImageSize.Y, AudioSliderStyle->GetBrush("AudioSlider.SliderBarRectangle")->ImageSize.X));
			SliderBarBottomCapImage->SetRenderTransform(FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(90.0f)), FVector2D(-AudioSliderStyle->GetBrush("AudioSlider.SliderBarCap")->ImageSize.Y / 2.0f, 0.0f)));

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
	const ISlateStyle* AudioSliderStyle = FSlateStyleRegistry::FindSlateStyle("AudioSliderStyle");
	if (ensure(AudioSliderStyle))
	{
		const FName DesiredSizeName = Orientation.Get() == Orient_Vertical
			? "AudioSlider.DesiredWidgetSizeVertical"
			: "AudioSlider.DesiredWidgetSizeHorizontal";
		return AudioSliderStyle->GetVector(DesiredSizeName);
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

void SAudioSliderBase::SetUnitsText(const FText Units)
{
	UnitsText->SetText(Units);
}

void SAudioSliderBase::SetAllTextReadOnly(const bool bIsReadOnly)
{
	UnitsText->SetIsReadOnly(bIsReadOnly);
	ValueText->SetIsReadOnly(bIsReadOnly);
}

void SAudioSliderBase::SetUnitsTextReadOnly(const bool bIsReadOnly)
{
	UnitsText->SetIsReadOnly(bIsReadOnly);
}

void SAudioSliderBase::SetAlwaysShowLabel(const bool bAlwaysShowLabel)
{
	AlwaysShowLabel = bAlwaysShowLabel;
}

void SAudioSliderBase::SetShowUnitsText(const bool bShowUnitsText)
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
	UpdateValueTextWidth();
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

// Update value text size to accommodate the largest numbers possible within the output range
void SAudioSliderBase::UpdateValueTextWidth()
{
	const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FText OutputRangeXText = FText::FromString(SAudioSliderBase::NumericInterface.ToString(OutputRange.X));
	const FText OutputRangeYText = FText::FromString(SAudioSliderBase::NumericInterface.ToString(OutputRange.Y));
	const FSlateFontInfo Font = FTextBlockStyle::GetDefault().Font;
	const float OutputRangeXTextWidth = FontMeasureService->Measure(OutputRangeXText, Font).X;
	const float OutputRangeYTextWidth = FontMeasureService->Measure(OutputRangeYText, Font).X;
	// add 1 digit of padding
	const float Padding = FontMeasureService->Measure(FText::FromString("-"), Font).X;
	float MaxValueLabelWidth = FMath::Max(OutputRangeXTextWidth, OutputRangeYTextWidth) + Padding;

	if (!ShowUnitsText.Get())
	{
		// set to max of label background width and calculated max width
		const ISlateStyle* AudioSliderStyle = FSlateStyleRegistry::FindSlateStyle("AudioSliderStyle");
		if (AudioSliderStyle)
		{
			MaxValueLabelWidth = FMath::Max(MaxValueLabelWidth, AudioSliderStyle->GetVector("AudioSlider.LabelBackgroundSize").X);
		}
	}
	ValueText->SetMinDesiredWidth(MaxValueLabelWidth);
	// slot index 0 is value text
	TSharedPtr<SHorizontalBox> HorizontalTextBox = StaticCastSharedPtr<SHorizontalBox>(TextWidgetSwitcher->GetActiveWidget());
	HorizontalTextBox->GetSlot(0).SetMaxWidth(MaxValueLabelWidth);
}

void SAudioSliderBase::SetOutputRange(const FVector2D Range)
{
	if (Range.Y > Range.X)
	{
		OutputRange = Range;
		SetValue(FMath::Clamp(ValueAttribute.Get(), Range.X, Range.Y));
		UpdateValueTextWidth();
	}
}

TSharedRef<SWidgetSwitcher> SAudioSliderBase::CreateTextWidgetSwitcher()
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
	SetShowUnitsText(ShowUnitsText.Get());
	return TextWidgetSwitcher.ToSharedRef();
}

TSharedRef<SWidgetSwitcher> SAudioSliderBase::CreateWidgetLayout()
{
	const ISlateStyle* AudioSliderStyle = FSlateStyleRegistry::FindSlateStyle("AudioSliderStyle");
	if (AudioSliderStyle)
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
					TextLabel.ToSharedRef()
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
				.Padding(0.0f, AudioSliderStyle->GetFloat("AudioSlider.LabelVerticalPadding"))
				.AutoHeight()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					TextLabel.ToSharedRef()
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
	ValueText->SetText(FText::FromString(SAudioSliderBase::NumericInterface.ToString(GetOutputValue(ValueAttribute.Get()))));
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
	SetUnitsText(FText::FromString("dB"));
}

// SAudioFrequencySlider
SAudioFrequencySlider::SAudioFrequencySlider()
{
}

void SAudioFrequencySlider::Construct(const SAudioSlider::FArguments& InArgs)
{
	SAudioSliderBase::Construct(InArgs);
	SetOutputRange(FVector2D(20.0f, 20000.0f));
	SetUnitsText(FText::FromString("Hz"));
}

const float SAudioFrequencySlider::GetOutputValue(const float LinValue)
{
	return Audio::GetLogFrequencyClamped(LinValue, LinearRange, OutputRange);
}

const float SAudioFrequencySlider::GetLinValue(const float OutputValue)
{
	return Audio::GetLinearFrequencyClamped(OutputValue, LinearRange, OutputRange);
}
