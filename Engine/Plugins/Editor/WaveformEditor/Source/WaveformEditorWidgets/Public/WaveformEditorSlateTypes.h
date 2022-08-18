// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "Styling/SlateStyle.h"
#include "Styling/SlateWidgetStyle.h"

#include "WaveformEditorSlateTypes.generated.h"

/**
 * Represents the appearance of a Waveform Viewer
 */
USTRUCT(BlueprintType)
struct FWaveformViewerStyle : public FSlateWidgetStyle
{
	GENERATED_USTRUCT_BODY()

	FWaveformViewerStyle();

	static const FWaveformViewerStyle& GetDefault();
	virtual const FName GetTypeName() const override { return TypeName; };
	virtual void GetResources(TArray< const FSlateBrush* >& OutBrushes) const override;

	static const FName TypeName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor WaveformColor;
	FWaveformViewerStyle& SetWaveformColor(const FSlateColor InWaveformColor) { WaveformColor = InWaveformColor; return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor MajorGridLineColor;
	FWaveformViewerStyle& SetMajorGridLineColor(const FSlateColor InMajorGridLineColor) { MajorGridLineColor = InMajorGridLineColor; return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor MinorGridLineColor;
	FWaveformViewerStyle& SetMinorGridLineColor(const FSlateColor InMinorGridLineColor) { MinorGridLineColor = InMinorGridLineColor; return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor WaveformBackgroundColor;
	FWaveformViewerStyle& SetBackgroundColor(const FSlateColor InBackgroundColor) { WaveformBackgroundColor = InBackgroundColor; return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateBrush BackgroundBrush;
	FWaveformViewerStyle& SetBackgroundBrush(const FSlateBrush InBackgroundBrush) { BackgroundBrush = InBackgroundBrush; return *this; }
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	float DesiredWidth;
	FWaveformViewerStyle& SetDesiredWidth(const float InDesiredWidth) { DesiredWidth = InDesiredWidth; return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	float DesiredHeight;
	FWaveformViewerStyle& SetDesiredHeight(const float InDesiredHeight) { DesiredHeight = InDesiredHeight; return *this; }
};

/**
 * Represents the appearance of a Waveform Viewer Overlay style
 */
USTRUCT(BlueprintType)
struct FWaveformViewerOverlayStyle : public FSlateWidgetStyle
{
	GENERATED_USTRUCT_BODY()

	FWaveformViewerOverlayStyle();

	static const FWaveformViewerOverlayStyle& GetDefault();
	virtual const FName GetTypeName() const override { return TypeName; };

	static const FName TypeName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor PlayheadColor;
	FWaveformViewerOverlayStyle& SetPlayheadColor(const FSlateColor InPlayheadColor) { PlayheadColor = InPlayheadColor; return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	float PlayheadWidth;
	FWaveformViewerOverlayStyle& SetPlayheadWidth(const float InPlayheadWidth) { PlayheadWidth = InPlayheadWidth; return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	float DesiredWidth;
	FWaveformViewerOverlayStyle& SetDesiredWidth(const float InDesiredWidth) { DesiredWidth = InDesiredWidth; return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	float DesiredHeight;
	FWaveformViewerOverlayStyle& SetDesiredHeight(const float InDesiredHeight) { DesiredHeight = InDesiredHeight; return *this; }
};

/**
 * Represents the appearance of a Waveform Editor Time Ruler
 */
USTRUCT(BlueprintType)
struct FWaveformEditorTimeRulerStyle : public FSlateWidgetStyle
{
	GENERATED_USTRUCT_BODY()

	FWaveformEditorTimeRulerStyle();

	static const FWaveformEditorTimeRulerStyle& GetDefault();
	virtual const FName GetTypeName() const override { return TypeName; };
	virtual void GetResources(TArray< const FSlateBrush* >& OutBrushes) const override;

	static const FName TypeName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	float HandleWidth;
	FWaveformEditorTimeRulerStyle& SetHandleWidth(const float InHandleWidth) { HandleWidth = InHandleWidth; return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor HandleColor;
	FWaveformEditorTimeRulerStyle& SetHandleColor(const FSlateColor& InHandleColor) { HandleColor = InHandleColor; return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateBrush HandleBrush;
	FWaveformEditorTimeRulerStyle& SetHandleBrush(const FSlateBrush& InHandleBrush) { HandleBrush = InHandleBrush; return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor TicksColor;
	FWaveformEditorTimeRulerStyle& SetTicksColor(const FSlateColor& InTicksColor) { TicksColor = InTicksColor; return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor TicksTextColor;
	FWaveformEditorTimeRulerStyle& SetTicksTextColor(const FSlateColor& InTicksTextColor) { TicksTextColor = InTicksTextColor; return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateFontInfo TicksTextFont;
	FWaveformEditorTimeRulerStyle& SetTicksTextFont(const FSlateFontInfo& InTicksTextFont) { TicksTextFont = InTicksTextFont; return *this; }
	FWaveformEditorTimeRulerStyle& SetFontSize(const float InFontSize) {TicksTextFont.Size = InFontSize; return *this;	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	float TicksTextOffset;
	FWaveformEditorTimeRulerStyle& SetTicksTextOffset(const float InTicksTextOffset) { TicksTextOffset = InTicksTextOffset; return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateColor BackgroundColor;
	FWaveformEditorTimeRulerStyle& SetBackgroundColor(const FSlateColor& InBackgroundColor) { BackgroundColor = InBackgroundColor; return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	FSlateBrush BackgroundBrush;
	FWaveformEditorTimeRulerStyle& SetBackgroundBrush(const FSlateBrush& InBackgroundBrush) { BackgroundBrush = InBackgroundBrush; return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	float DesiredWidth;
	FWaveformEditorTimeRulerStyle& SetDesiredWidth(const float InDesiredWidth) { DesiredWidth = InDesiredWidth; return *this; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Appearance)
	float DesiredHeight;
	FWaveformEditorTimeRulerStyle& SetDesiredHeight(const float InDesiredHeight) { DesiredHeight = InDesiredHeight; return *this; }
};