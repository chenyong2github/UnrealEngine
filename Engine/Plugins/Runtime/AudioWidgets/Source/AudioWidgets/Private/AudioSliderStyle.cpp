// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioSliderStyle.h"
#include "Styling/SlateTypes.h"

FAudioSliderStyle::FAudioSliderStyle()
	: FSlateStyleSet("AudioSliderStyle")
{
	SetParentStyleName("CoreStyle");
	SetContentRoot(FPaths::EnginePluginsDir() / TEXT("Runtime/AudioWidgets/Content/AudioSlider"));

	// Default orientation is vertical, so width/height are relative to that
	const float LabelWidth = 56.0f;
	const float LabelHeight = 28.0f;
	const FVector2D LabelBackgroundSize = FVector2D(LabelWidth, LabelHeight);
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
	const FVector2D DesiredWidgetSizeVertical = FVector2D(LabelWidth, LabelHeight + LabelVerticalPadding + SliderBackgroundHeight);
	const FVector2D DesiredWidgetSizeHorizontal = FVector2D(SliderBackgroundHeight + LabelWidth, SliderBackgroundWidth);
	
	Set("AudioSlider.LabelVerticalPadding", LabelVerticalPadding);
	Set("AudioSlider.LabelBackgroundSize", LabelBackgroundSize);
	Set("AudioSlider.DesiredWidgetSizeVertical", DesiredWidgetSizeVertical);
	Set("AudioSlider.DesiredWidgetSizeHorizontal", DesiredWidgetSizeHorizontal);

	Set("AudioSlider.LabelBackground", new IMAGE_BRUSH_SVG(TEXT("LabelBackground"), LabelBackgroundSize));
	Set("AudioSlider.SliderBarCap", new IMAGE_BRUSH_SVG(TEXT("SliderBarCap"), SliderBarCapSize));
	Set("AudioSlider.SliderBarRectangle", new IMAGE_BRUSH_SVG(TEXT("SliderBarRectangle"), SliderBarRectangleSize));
	Set("AudioSlider.SliderBackgroundCap", new IMAGE_BRUSH_SVG(TEXT("SliderBackgroundCap"), SliderBackgroundCapSize));
	Set("AudioSlider.SliderBackgroundCapHorizontal", new IMAGE_BRUSH_SVG(TEXT("SliderBackgroundCapHorizontal"), FVector2D(SliderBackgroundCapSize.Y, SliderBackgroundCapSize.X)));
	Set("AudioSlider.SliderBackgroundRectangle", new IMAGE_BRUSH_SVG(TEXT("SliderBackgroundRectangle"), SliderBackgroundRectangleSize));
	Set("AudioSlider.WidgetBackground", new IMAGE_BRUSH_SVG(TEXT("WidgetBackground"), DesiredWidgetSizeVertical));

	Set("AudioSlider.DefaultBackgroundColor", FLinearColor(0.01033f, 0.01033f, 0.01033f));
	Set("AudioSlider.DefaultBarColor", FLinearColor::Black);
	Set("AudioSlider.DefaultThumbColor", FLinearColor::Gray);
	Set("AudioSlider.DefaultWidgetBackgroundColor", FLinearColor::Transparent);

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
		.SetNormalThumbImage(IMAGE_BRUSH_SVG(TEXT("SliderThumb"), ThumbImageSize))
		.SetHoveredThumbImage(IMAGE_BRUSH_SVG(TEXT("SliderThumb"), ThumbImageSize))
		.SetDisabledThumbImage(NoImageThumb)
	);
	FSlateStyleRegistry::RegisterSlateStyle(*this);
}
