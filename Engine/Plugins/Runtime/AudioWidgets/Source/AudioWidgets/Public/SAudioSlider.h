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
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SConstraintCanvas.h"

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

		/** Called when the value is changed by slider or typing */
		SLATE_EVENT(FOnFloatValueChanged, OnValueChanged)

	SLATE_END_ARGS()

	SAudioSliderBase();
	virtual ~SAudioSliderBase() {};

	/**
	 * Construct the widget.
	 *
	 * @param InDeclaration A declaration from which to construct the widget.
	 */
	virtual void Construct(const SAudioSliderBase::FArguments& InDeclaration);
	virtual const float GetOutputValue(const float LinValue);
	virtual const float GetLinValue(const float OutputValue);
	
	FVector2D ComputeDesiredSize(float) const;
	void SetUnitsText(const FText Units);
	/**
	*  Set whether text label read only or editable.
	*/
	void SetTextReadOnly(const bool bIsReadOnly);
	/**
	 * Set whether the text label is always shown or only on hover.
	 */
	void SetAlwaysShowLabel(const bool bAlwaysShowLabel);
	void SetSliderBackgroundColor(FSlateColor InSliderBackgroundColor);
	void SetSliderBarColor(FSlateColor InSliderBarColor);
	void SetSliderThumbColor(FSlateColor InSliderThumbColor);
	void SetLabelBackgroundColor(FSlateColor InLabelBackgroundColor);
	void SetWidgetBackgroundColor(FSlateColor InWidgetBackgroundColor);

protected:
	// Holds the slider's current linear value, from 0.0 - 1.0f
	TAttribute<float> ValueAttribute;
	// Whether the text label is always shown or only on hover
	TAttribute<bool> AlwaysShowLabel;

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
	TSharedPtr<SImage> SliderBackgroundBottomCapImage;
	TSharedPtr<SImage> SliderBackgroundRectangleImage;
	TSharedPtr<SImage> SliderBarTopCapImage;
	TSharedPtr<SImage> SliderBarBottomCapImage;
	TSharedPtr<SImage> SliderBarRectangleImage;
	TSharedPtr<SImage> WidgetBackgroundImage;

	// Holds a delegate that is executed when the slider's value changed.
	FOnFloatValueChanged OnValueChanged;
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
	void SetOutputRange(const FVector2D Range);
	const float GetOutputValue(const float LinValue);
	const float GetLinValue(const float OutputValue);
protected:
	FVector2D OutputRange;
	static const FVector2D LinearRange;
};
