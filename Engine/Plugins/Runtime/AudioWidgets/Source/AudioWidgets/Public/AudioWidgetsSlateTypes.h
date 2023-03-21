// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"
#include "Styling/SlateTypes.h"

#include "AudioWidgetsSlateTypes.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnAudioWidgetStyleUpdated, const FNotifyingAudioWidgetStyle& /*Updated Widget Style*/);

USTRUCT()
struct FNotifyingAudioWidgetStyle : public FSlateWidgetStyle
{
	GENERATED_USTRUCT_BODY()

	FNotifyingAudioWidgetStyle() = default;
	virtual ~FNotifyingAudioWidgetStyle() = default;

	virtual void BroadcastStyleUpdate() const { OnStyleUpdated.Broadcast(*this); }

	FOnAudioWidgetStyleUpdated OnStyleUpdated;
};

/**
 * Represents the appearance of an Audio Text Box 
 */
USTRUCT(BlueprintType)
struct AUDIOWIDGETS_API FAudioTextBoxStyle : public FSlateWidgetStyle
{
	GENERATED_USTRUCT_BODY()

	FAudioTextBoxStyle();

	virtual ~FAudioTextBoxStyle() {}

	virtual void GetResources(TArray< const FSlateBrush* >& OutBrushes) const override;

	static const FName TypeName;
	virtual const FName GetTypeName() const override { return TypeName; };

	static const FAudioTextBoxStyle& GetDefault();

	/** Image for the label border */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateBrush BackgroundImage;
	FAudioTextBoxStyle& SetBackgroundImage(const FSlateBrush& InBackgroundImage) { BackgroundImage = InBackgroundImage; return *this; }

	/** Color used to draw the label background */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor BackgroundColor;
	FAudioTextBoxStyle& SetBackgroundColor(const FSlateColor& InBackgroundColor) { BackgroundColor = InBackgroundColor; return *this; }

	/**
	* Unlinks all colors in this style.
	* @see FSlateColor::Unlink
	 */
	void UnlinkColors()
	{
		BackgroundImage.UnlinkColors();
	}
};

/**
 * Represents the appearance of an Audio Slider
 */
USTRUCT(BlueprintType)
struct AUDIOWIDGETS_API FAudioSliderStyle : public FSlateWidgetStyle
{
	GENERATED_USTRUCT_BODY()

	FAudioSliderStyle();

	virtual ~FAudioSliderStyle() {}

	virtual void GetResources(TArray< const FSlateBrush* >& OutBrushes) const override;

	static const FName TypeName;
	virtual const FName GetTypeName() const override { return TypeName; };

	static const FAudioSliderStyle& GetDefault();

	/** The style to use for the underlying SSlider. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSliderStyle SliderStyle;
	FAudioSliderStyle& SetSliderStyle(const FSliderStyle& InSliderStyle) { SliderStyle = InSliderStyle; return *this; }

	/** The style to use for the audio text box widget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FAudioTextBoxStyle TextBoxStyle;
	FAudioSliderStyle& SetTextBoxStyle(const FAudioTextBoxStyle& InTextBoxStyle) { TextBoxStyle = InTextBoxStyle; return *this; }

	/** Image for the widget background */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateBrush WidgetBackgroundImage;
	FAudioSliderStyle& SetWidgetBackgroundImage(const FSlateBrush& InWidgetBackgroundImage) { WidgetBackgroundImage = InWidgetBackgroundImage; return *this; }

	/** Color used to draw the slider background */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor SliderBackgroundColor;
	FAudioSliderStyle& SetSliderBackgroundColor(const FSlateColor& InSliderBackgroundColor) { SliderBackgroundColor = InSliderBackgroundColor; return *this; }

	/** Size of the slider background (slider default is vertical)*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FVector2D SliderBackgroundSize;
	FAudioSliderStyle& SetSliderBackgroundSize(const FVector2D& InSliderBackgroundSize) { SliderBackgroundSize = InSliderBackgroundSize; return *this; }

	/** Size of the padding between the label and the slider */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	float LabelPadding;
	FAudioSliderStyle& SetLabelPadding(const float& InLabelPadding) { LabelPadding = InLabelPadding; return *this; }

	/** Color used to draw the slider bar */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor SliderBarColor;
	FAudioSliderStyle& SetSliderBarColor(const FSlateColor& InSliderBarColor) { SliderBarColor = InSliderBarColor; return *this; }

	/** Color used to draw the slider thumb (handle) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor SliderThumbColor;
	FAudioSliderStyle& SetSliderThumbColor(const FSlateColor& InSliderThumbColor) { SliderThumbColor = InSliderThumbColor; return *this; }

	/** Color used to draw the widget background */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor WidgetBackgroundColor;
	FAudioSliderStyle& SetWidgetBackgroundColor(const FSlateColor& InWidgetBackgroundColor) { WidgetBackgroundColor = InWidgetBackgroundColor; return *this; }

	/**
	* Unlinks all colors in this style.
	* @see FSlateColor::Unlink
	 */
	void UnlinkColors()
	{
		WidgetBackgroundImage.UnlinkColors();
	}
};


/**
 * Represents the appearance of an Audio Radial Slider
 */
USTRUCT(BlueprintType)
struct AUDIOWIDGETS_API FAudioRadialSliderStyle : public FSlateWidgetStyle
{
	GENERATED_USTRUCT_BODY()

	FAudioRadialSliderStyle();

	virtual ~FAudioRadialSliderStyle() {}

	virtual void GetResources(TArray< const FSlateBrush* >& OutBrushes) const override {}

	static const FName TypeName;
	virtual const FName GetTypeName() const override { return TypeName; };

	static const FAudioRadialSliderStyle& GetDefault();
	
	/** The style to use for the audio text box widget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FAudioTextBoxStyle TextBoxStyle;
	FAudioRadialSliderStyle& SetTextBoxStyle(const FAudioTextBoxStyle& InTextBoxStyle) { TextBoxStyle = InTextBoxStyle; return *this; }

	/** Color used to draw the slider center background */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor CenterBackgroundColor;
	FAudioRadialSliderStyle& SetCenterBackgroundColor(const FSlateColor& InCenterBackgroundColor) { CenterBackgroundColor = InCenterBackgroundColor; return *this; }

	/** Color used to draw the unprogressed slider bar */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor SliderBarColor;
	FAudioRadialSliderStyle& SetSliderBarColor(const FSlateColor& InSliderBarColor) { SliderBarColor = InSliderBarColor; return *this; }

	/** Color used to draw the progress bar */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor SliderProgressColor;
	FAudioRadialSliderStyle& SetSliderProgressColor(const FSlateColor& InSliderProgressColor) { SliderProgressColor = InSliderProgressColor; return *this; }
	
	/** Size of the padding between the label and the slider */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	float LabelPadding;
	FAudioRadialSliderStyle& SetLabelPadding(const float& InLabelPadding) { LabelPadding = InLabelPadding; return *this; }

	/** Default size of the slider itself (not including label) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	float DefaultSliderRadius;
	FAudioRadialSliderStyle& SetDefaultSliderRadius(const float& InDefaultSliderRadius) { DefaultSliderRadius = InDefaultSliderRadius; return *this; }
};

/**
 * Represents the appearance of a Sampled Sequence Viewer
 */
USTRUCT(BlueprintType)
struct AUDIOWIDGETS_API FSampledSequenceViewerStyle : public FNotifyingAudioWidgetStyle
{
	GENERATED_USTRUCT_BODY()

	FSampledSequenceViewerStyle();

	static const FSampledSequenceViewerStyle& GetDefault();
	virtual const FName GetTypeName() const override { return TypeName; };
	virtual void GetResources(TArray< const FSlateBrush* >& OutBrushes) const override;

	static const FName TypeName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor SequenceColor;
	FSampledSequenceViewerStyle& SetSequenceColor(const FSlateColor InSequenceColor) { SequenceColor = InSequenceColor; BroadcastStyleUpdate(); return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	float SequenceLineThickness;
	FSampledSequenceViewerStyle& SetSequenceLineThickness(const float InSequenceLineThickness) { SequenceLineThickness = InSequenceLineThickness; BroadcastStyleUpdate(); return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor MajorGridLineColor;
	FSampledSequenceViewerStyle& SetMajorGridLineColor(const FSlateColor InMajorGridLineColor) { MajorGridLineColor = InMajorGridLineColor; BroadcastStyleUpdate(); return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor MinorGridLineColor;
	FSampledSequenceViewerStyle& SetMinorGridLineColor(const FSlateColor InMinorGridLineColor) { MinorGridLineColor = InMinorGridLineColor; BroadcastStyleUpdate(); return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor ZeroCrossingLineColor;
	FSampledSequenceViewerStyle& SetZeroCrossingLineColor(const FSlateColor InZeroCrossingLineColor) { ZeroCrossingLineColor = InZeroCrossingLineColor; BroadcastStyleUpdate(); return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	float ZeroCrossingLineThickness;
	FSampledSequenceViewerStyle& SetZeroCrossingLineThickness(const float InZeroCrossingLineThickness) { ZeroCrossingLineThickness = InZeroCrossingLineThickness; BroadcastStyleUpdate(); return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	float SampleMarkersSize;
	FSampledSequenceViewerStyle& SetSampleMarkersSize(const float InSampleMarkersSize) { SampleMarkersSize = InSampleMarkersSize; BroadcastStyleUpdate(); return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor SequenceBackgroundColor;
	FSampledSequenceViewerStyle& SetBackgroundColor(const FSlateColor InBackgroundColor) { SequenceBackgroundColor = InBackgroundColor; BroadcastStyleUpdate(); return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateBrush BackgroundBrush;
	FSampledSequenceViewerStyle& SetBackgroundBrush(const FSlateBrush InBackgroundBrush) { BackgroundBrush = InBackgroundBrush; BroadcastStyleUpdate(); return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	float DesiredWidth;
	FSampledSequenceViewerStyle& SetDesiredWidth(const float InDesiredWidth) { DesiredWidth = InDesiredWidth; BroadcastStyleUpdate(); return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	float DesiredHeight;
	FSampledSequenceViewerStyle& SetDesiredHeight(const float InDesiredHeight) { DesiredHeight = InDesiredHeight; BroadcastStyleUpdate(); return *this; }
};

/**
 * Represents the appearance of a Waveform Viewer Overlay style
 */
USTRUCT(BlueprintType)
struct AUDIOWIDGETS_API FPlayheadOverlayStyle : public FNotifyingAudioWidgetStyle
{
	GENERATED_USTRUCT_BODY()

	FPlayheadOverlayStyle();

	static const FPlayheadOverlayStyle& GetDefault();
	virtual const FName GetTypeName() const override { return TypeName; };

	static const FName TypeName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor PlayheadColor;
	FPlayheadOverlayStyle& SetPlayheadColor(const FSlateColor InPlayheadColor) { PlayheadColor = InPlayheadColor; BroadcastStyleUpdate(); return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	float PlayheadWidth;
	FPlayheadOverlayStyle& SetPlayheadWidth(const float InPlayheadWidth) { PlayheadWidth = InPlayheadWidth; BroadcastStyleUpdate(); return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	float DesiredWidth;
	FPlayheadOverlayStyle& SetDesiredWidth(const float InDesiredWidth) { DesiredWidth = InDesiredWidth; BroadcastStyleUpdate(); return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	float DesiredHeight;
	FPlayheadOverlayStyle& SetDesiredHeight(const float InDesiredHeight) { DesiredHeight = InDesiredHeight; BroadcastStyleUpdate(); return *this; }
};

/**
 * Represents the appearance of a Sampled Sequence Time Ruler
 */
USTRUCT(BlueprintType)
struct AUDIOWIDGETS_API FFixedSampleSequenceRulerStyle : public FNotifyingAudioWidgetStyle
{
	GENERATED_USTRUCT_BODY()

	FFixedSampleSequenceRulerStyle();

	static const FFixedSampleSequenceRulerStyle& GetDefault();
	virtual const FName GetTypeName() const override { return TypeName; };
	virtual void GetResources(TArray< const FSlateBrush* >& OutBrushes) const override;

	static const FName TypeName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	float HandleWidth;
	FFixedSampleSequenceRulerStyle& SetHandleWidth(const float InHandleWidth) { HandleWidth = InHandleWidth; BroadcastStyleUpdate(); return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor HandleColor;
	FFixedSampleSequenceRulerStyle& SetHandleColor(const FSlateColor& InHandleColor) { HandleColor = InHandleColor; BroadcastStyleUpdate(); return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateBrush HandleBrush;
	FFixedSampleSequenceRulerStyle& SetHandleBrush(const FSlateBrush& InHandleBrush) { HandleBrush = InHandleBrush; BroadcastStyleUpdate(); return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor TicksColor;
	FFixedSampleSequenceRulerStyle& SetTicksColor(const FSlateColor& InTicksColor) { TicksColor = InTicksColor; BroadcastStyleUpdate(); return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor TicksTextColor;
	FFixedSampleSequenceRulerStyle& SetTicksTextColor(const FSlateColor& InTicksTextColor) { TicksTextColor = InTicksTextColor; BroadcastStyleUpdate(); return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateFontInfo TicksTextFont;
	FFixedSampleSequenceRulerStyle& SetTicksTextFont(const FSlateFontInfo& InTicksTextFont) { TicksTextFont = InTicksTextFont; BroadcastStyleUpdate(); return *this; }
	FFixedSampleSequenceRulerStyle& SetFontSize(const float InFontSize) { TicksTextFont.Size = InFontSize; BroadcastStyleUpdate(); return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	float TicksTextOffset;
	FFixedSampleSequenceRulerStyle& SetTicksTextOffset(const float InTicksTextOffset) { TicksTextOffset = InTicksTextOffset; BroadcastStyleUpdate(); return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor BackgroundColor;
	FFixedSampleSequenceRulerStyle& SetBackgroundColor(const FSlateColor& InBackgroundColor) { BackgroundColor = InBackgroundColor; BroadcastStyleUpdate(); return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateBrush BackgroundBrush;
	FFixedSampleSequenceRulerStyle& SetBackgroundBrush(const FSlateBrush& InBackgroundBrush) { BackgroundBrush = InBackgroundBrush; BroadcastStyleUpdate(); return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	float DesiredWidth;
	FFixedSampleSequenceRulerStyle& SetDesiredWidth(const float InDesiredWidth) { DesiredWidth = InDesiredWidth; BroadcastStyleUpdate(); return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	float DesiredHeight;
	FFixedSampleSequenceRulerStyle& SetDesiredHeight(const float InDesiredHeight) { DesiredHeight = InDesiredHeight; BroadcastStyleUpdate(); return *this; }
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/StyleDefaults.h"
#endif
