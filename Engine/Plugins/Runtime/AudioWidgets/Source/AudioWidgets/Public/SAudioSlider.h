// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioSliderStyle.h"
#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Framework/SlateDelegates.h"
#include "Misc/Attribute.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Styling/SlateWidgetStyleAsset.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/NumericTypeInterface.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SWidgetSwitcher.h"

// FVariablePrecisionNumericInterface
// Taken from PropertyEditor/VariablePrecisionNumericInterface.h
// Todo: move to a shared location for use in other audio code
// 
// Allow more precision as the numbers get closer to zero
struct FVariablePrecisionNumericInterface : public TDefaultNumericTypeInterface<float>
{
	FVariablePrecisionNumericInterface() {}

	virtual FString ToString(const float& Value) const override
	{
		// examples: 1000, 100.1, 10.12, 1.123
		float AbsValue = FMath::Abs(Value);
		int32 FractionalDigits = 3;
		if ((AbsValue / 1000.f) >= 1.f)
			FractionalDigits = 0;
		else if ((AbsValue / 100.f) >= 1.f)
			FractionalDigits = 1;
		else if ((AbsValue / 10.f) >= 1.f)
			FractionalDigits = 2;

		const FNumberFormattingOptions NumberFormattingOptions = FNumberFormattingOptions()
			.SetUseGrouping(false)
			.SetMinimumFractionalDigits(FractionalDigits)
			.SetMaximumFractionalDigits(FractionalDigits);
		return FastDecimalFormat::NumberToString(Value, ExpressionParser::GetLocalizedNumberFormattingRules(), NumberFormattingOptions);
	}
};

/**
 * Slate audio sliders that wrap SSlider and provides additional audio specific functionality.
 * This is a nativized version of the previous Audio Fader widget. 
 */
class AUDIOWIDGETS_API SAudioSliderBase
	: public SCompoundWidget 
{
public:
	SLATE_BEGIN_ARGS(SAudioSliderBase)
	{
		_Value = 0.0f;
		_AlwaysShowLabel = false;
		_ShowUnitsText = true;
		_Orientation = Orient_Vertical;

		const ISlateStyle* AudioSliderStyle = FSlateStyleRegistry::FindSlateStyle("AudioSliderStyle");
		if (ensure(AudioSliderStyle))
		{
			_LabelBackgroundColor = AudioSliderStyle->GetColor("AudioSlider.DefaultBackgroundColor");
			_SliderBackgroundColor = AudioSliderStyle->GetColor("AudioSlider.DefaultBackgroundColor");
			_SliderBarColor = AudioSliderStyle->GetColor("AudioSlider.DefaultBarColor");
			_SliderThumbColor = AudioSliderStyle->GetColor("AudioSlider.DefaultThumbColor");
			_WidgetBackgroundColor = AudioSliderStyle->GetColor("AudioSlider.DefaultWidgetBackgroundColor");
		}
	}
		/** A value representing the audio slider value. */
		SLATE_ATTRIBUTE(float, Value)

		/** Whether the text label is always shown or only on hover. */
		SLATE_ATTRIBUTE(bool, AlwaysShowLabel)
			
		/** Whether to show the units part of the text label. */
		SLATE_ATTRIBUTE(bool, ShowUnitsText)

		/** The orientation of the slider. */
		SLATE_ARGUMENT(EOrientation, Orientation)

		/** The color to draw the label background in. */
		SLATE_ATTRIBUTE(FSlateColor, LabelBackgroundColor)

		/** The color to draw the slider background in. */
		SLATE_ATTRIBUTE(FSlateColor, SliderBackgroundColor)

		/** The color to draw the slider bar in. */
		SLATE_ATTRIBUTE(FSlateColor, SliderBarColor)

		/** The color to draw the slider thumb in. */
		SLATE_ATTRIBUTE(FSlateColor, SliderThumbColor)
		
		/** The color to draw the widget background in. */
		SLATE_ATTRIBUTE(FSlateColor, WidgetBackgroundColor)
		
		/** When specified, use this as the slider's desired size */
		SLATE_ATTRIBUTE(TOptional<FVector2D>, DesiredSizeOverride)

		/** Called when the value is changed by slider or typing */
		SLATE_EVENT(FOnFloatValueChanged, OnValueChanged)

	SLATE_END_ARGS()

	SAudioSliderBase();
	virtual ~SAudioSliderBase() {};

	// Holds a delegate that is executed when the slider's value changed.
	FOnFloatValueChanged OnValueChanged;

	/**
	 * Construct the widget.
	 *
	 * @param InDeclaration A declaration from which to construct the widget.
	 */
	virtual void Construct(const SAudioSliderBase::FArguments& InDeclaration);
	virtual const float GetOutputValue(const float LinValue);
	virtual const float GetLinValue(const float OutputValue);
	
	/**
	 * Set the slider's linear (0-1 normalized) value. 
	 */
	void SetValue(float LinValue);
	FVector2D ComputeDesiredSize(float) const;
	void SetDesiredSizeOverride(const FVector2D DesiredSize);
	void SetUnitsText(const FText Units);
	/**
	*  Set whether text label (both value and units) read only or editable.
	*/
	void SetAllTextReadOnly(const bool bIsReadOnly);
	void SetUnitsTextReadOnly(const bool bIsReadOnly);

	/**
	 * Set whether the text label is always shown or only on hover.
	 */
	void SetAlwaysShowLabel(const bool bAlwaysShowLabel);
	void SetShowUnitsText(const bool bShowUnitsText);
	void SetOrientation(EOrientation InOrientation);
	void SetSliderBackgroundColor(FSlateColor InSliderBackgroundColor);
	void SetSliderBarColor(FSlateColor InSliderBarColor);
	void SetSliderThumbColor(FSlateColor InSliderThumbColor);
	void SetLabelBackgroundColor(FSlateColor InLabelBackgroundColor);
	void SetWidgetBackgroundColor(FSlateColor InWidgetBackgroundColor);
	void SetOutputRange(const FVector2D Range);

protected:
	// Holds the slider's current linear value, from 0.0 - 1.0f
	TAttribute<float> ValueAttribute;
	// Whether the text label is always shown or only on hover
	TAttribute<bool> AlwaysShowLabel;
	// Whether to show the units part of the text label 
	TAttribute<bool> ShowUnitsText;
	// Holds the slider's orientation
	TAttribute<EOrientation> Orientation;
	// Optional override for desired size 
	TAttribute<TOptional<FVector2D>> DesiredSizeOverride;

	// Various colors 
	TAttribute<FSlateColor> LabelBackgroundColor;
	TAttribute<FSlateColor> SliderBackgroundColor;
	TAttribute<FSlateColor> SliderBarColor;
	TAttribute<FSlateColor> SliderThumbColor;
	TAttribute<FSlateColor> WidgetBackgroundColor;

	// Widget components
	TSharedPtr<SSlider> Slider;
	TSharedPtr<SEditableText> ValueText;
	TSharedPtr<SEditableText> UnitsText;
	TSharedPtr<SImage> LabelBackgroundImage;
	TSharedPtr<SImage> SliderBackgroundTopCapImage;
	TSharedPtr<SImage> SliderBackgroundLeftCapImage;
	TSharedPtr<SImage> SliderBackgroundBottomCapImage;
	TSharedPtr<SImage> SliderBackgroundRightCapImage;
	TSharedPtr<SImage> SliderBackgroundRectangleImage;
	TSharedPtr<SImage> SliderBarTopCapImage;
	TSharedPtr<SImage> SliderBarBottomCapImage;
	TSharedPtr<SImage> SliderBarRectangleImage;
	TSharedPtr<SImage> WidgetBackgroundImage;
	TSharedPtr<SOverlay> TextLabel;

	// Range for output, currently only used for frequency sliders and sliders without curves
	FVector2D OutputRange = FVector2D(0.0f, 1.0f);
	static const FVector2D LinearRange;
	/** Used to convert and format value text strings **/
	static const FVariablePrecisionNumericInterface NumericInterface;
private:
	/** Switches between the vertical and horizontal views */
	TSharedPtr<SWidgetSwitcher> LayoutWidgetSwitcher;
	TSharedPtr<SWidgetSwitcher> TextWidgetSwitcher;

	TSharedRef<SWidgetSwitcher> CreateWidgetLayout();
	TSharedRef<SWidgetSwitcher> CreateTextWidgetSwitcher();
	void UpdateValueTextWidth();
};

/* 
*	An Audio Slider widget with customizable curves. 
*/
class AUDIOWIDGETS_API SAudioSlider
	: public SAudioSliderBase
{
public:
	SAudioSlider();
	virtual ~SAudioSlider() {};
	virtual void Construct(const SAudioSliderBase::FArguments& InDeclaration);
	void SetLinToOutputCurve(const TWeakObjectPtr<const UCurveFloat> LinToOutputCurve);
	void SetOutputToLinCurve(const TWeakObjectPtr<const UCurveFloat> OutputToLinCurve);
	const TWeakObjectPtr<const UCurveFloat> GetOutputToLinCurve();
	const TWeakObjectPtr<const UCurveFloat> GetLinToOutputCurve();
	const float GetOutputValue(const float LinValue);
	const float GetLinValue(const float OutputValue);

protected:
	// Curves for mapping linear (0.0 - 1.0) to output (ex. dB for volume)  
	TWeakObjectPtr<const UCurveFloat> LinToOutputCurve = nullptr;
	TWeakObjectPtr<const UCurveFloat> OutputToLinCurve = nullptr;
};

/*
* An Audio Curve Slider widget with default customizable curves for volume (dB). 
*/
class AUDIOWIDGETS_API SAudioVolumeSlider
	: public SAudioSlider
{
public:
	SAudioVolumeSlider();
	void Construct(const SAudioSliderBase::FArguments& InDeclaration);
};

/*
* An Audio Slider widget intended to be used for frequency output, with output frequency range but no customizable curves. 
*/
class AUDIOWIDGETS_API SAudioFrequencySlider
	: public SAudioSliderBase
{
public:
	SAudioFrequencySlider();
	void Construct(const SAudioSlider::FArguments& InDeclaration);
	const float GetOutputValue(const float LinValue);
	const float GetLinValue(const float OutputValue);
};
