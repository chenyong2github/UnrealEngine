// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioWidgetsStyle.h"
#include "Brushes/SlateImageBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/StyleColors.h"
#include "Styling/SlateTypes.h"

FAudioWidgetsStyle::FAudioWidgetsStyle()
	: FSlateStyleSet("AudioWidgetsStyle")
{
	SetParentStyleName("CoreStyle");
	SetContentRoot(FPaths::EnginePluginsDir() / TEXT("Runtime/AudioWidgets/Content"));

	/** 
	* AudioTextBox Style
	*/
	const float LabelWidth = 56.0f;
	const float LabelHeight = 28.0f;
	const FVector2D LabelBackgroundSize = FVector2D(LabelWidth, LabelHeight);
	const FLinearColor LabelBackgroundColor = FStyleColors::Recessed.GetSpecifiedColor();
	const float LabelCornerRadius = 4.0f;

	Set("AudioTextBox.LabelBackgroundSize", LabelBackgroundSize);
	Set("AudioTextBox.LabelBackgroundColor", LabelBackgroundColor);
	Set("AudioTextBox.LabelBackground", new FSlateRoundedBoxBrush(FStyleColors::White, LabelCornerRadius, LabelBackgroundSize));

	/**
	* AudioSlider Style
	*/
	// Default orientation is vertical, so width/height are relative to that
	const FVector2D ThumbImageSize = FVector2D(22.0f, 22.0f);

	const float SliderBarWidth = 10.0f;
	const float SliderBarHeight = 432.0f;
	const FVector2D SliderBarCapSize = FVector2D(SliderBarWidth, SliderBarWidth / 2.0f);
	const FVector2D SliderBarRectangleSize = FVector2D(SliderBarWidth, SliderBarHeight - SliderBarWidth);

	const float SliderBackgroundWidth = 28.0f;
	const float SliderBackgroundHeight = 450.0f;
	const FVector2D SliderBackgroundCapSize = FVector2D(SliderBackgroundWidth, SliderBackgroundWidth / 2.0f);
	const FVector2D SliderBackgroundRectangleSize = FVector2D(SliderBackgroundWidth, SliderBackgroundHeight - SliderBackgroundWidth);
	const FVector2D SliderBackgroundSize = FVector2D(SliderBackgroundWidth, SliderBackgroundHeight);
	
	const float LabelVerticalPadding = 3.0f;
	const FVector2D DesiredWidgetSizeVertical = FVector2D(LabelBackgroundSize.X, LabelBackgroundSize.Y + LabelVerticalPadding + SliderBackgroundHeight);
	const FVector2D DesiredWidgetSizeHorizontal = FVector2D(SliderBackgroundHeight + LabelBackgroundSize.X, SliderBackgroundWidth);
	
	Set("AudioSlider.LabelVerticalPadding", LabelVerticalPadding);
	Set("AudioSlider.DesiredWidgetSizeVertical", DesiredWidgetSizeVertical);
	Set("AudioSlider.DesiredWidgetSizeHorizontal", DesiredWidgetSizeHorizontal);

	Set("AudioSlider.SliderBarCap", new IMAGE_BRUSH_SVG(TEXT("AudioSlider/SliderBarCap"), SliderBarCapSize));
	Set("AudioSlider.SliderBarRectangle", new IMAGE_BRUSH_SVG(TEXT("AudioSlider/SliderBarRectangle"), SliderBarRectangleSize));
	Set("AudioSlider.SliderBackgroundCap", new IMAGE_BRUSH_SVG(TEXT("AudioSlider/SliderBackgroundCap"), SliderBackgroundCapSize));
	Set("AudioSlider.SliderBackgroundCapHorizontal", new IMAGE_BRUSH_SVG(TEXT("AudioSlider/SliderBackgroundCapHorizontal"), FVector2D(SliderBackgroundCapSize.Y, SliderBackgroundCapSize.X)));
	Set("AudioSlider.SliderBackgroundRectangle", new IMAGE_BRUSH_SVG(TEXT("AudioSlider/SliderBackgroundRectangle"), SliderBackgroundRectangleSize));
	Set("AudioSlider.WidgetBackground", new IMAGE_BRUSH_SVG(TEXT("AudioSlider/WidgetBackground"), DesiredWidgetSizeVertical));

	Set("AudioSlider.DefaultBackgroundColor", FStyleColors::Recessed.GetSpecifiedColor());
	Set("AudioSlider.DefaultBarColor", FStyleColors::Black.GetSpecifiedColor());
	Set("AudioSlider.DefaultThumbColor", FStyleColors::AccentGray.GetSpecifiedColor());
	Set("AudioSlider.DefaultWidgetBackgroundColor", FStyleColors::Transparent.GetSpecifiedColor());
	Set("AudioSlider.DefaultLabelBackgroundColor", FStyleColors::Recessed.GetSpecifiedColor());

	FSlateBrush NoImageThumb = FSlateBrush();
	NoImageThumb.SetImageSize(ThumbImageSize);
	NoImageThumb.DrawAs = ESlateBrushDrawType::NoDrawType;
	FSlateBrush NoImageSlider = FSlateBrush();
	NoImageSlider.SetImageSize(SliderBackgroundSize);
	NoImageSlider.DrawAs = ESlateBrushDrawType::NoDrawType;

	Set("AudioSlider.Slider", FSliderStyle()
		.SetNormalBarImage(NoImageSlider)
		.SetHoveredBarImage(NoImageSlider)
		.SetDisabledBarImage(NoImageSlider)
		.SetNormalThumbImage(IMAGE_BRUSH_SVG(TEXT("AudioSlider/SliderThumb"), ThumbImageSize))
		.SetHoveredThumbImage(IMAGE_BRUSH_SVG(TEXT("AudioSlider/SliderThumb"), ThumbImageSize))
		.SetDisabledThumbImage(NoImageThumb)
	);

	/**
	* AudioRadialSlider Style
	*/
	Set("AudioRadialSlider.CenterBackgroundColor", FStyleColors::Recessed.GetSpecifiedColor());
	Set("AudioRadialSlider.LabelBackgroundColor", FStyleColors::Recessed.GetSpecifiedColor());
	Set("AudioRadialSlider.SliderProgressColor", FStyleColors::White.GetSpecifiedColor());
	Set("AudioRadialSlider.SliderBarColor", FStyleColors::AccentGray.GetSpecifiedColor());

	FSlateStyleRegistry::RegisterSlateStyle(*this);
}
