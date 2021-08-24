// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioSliderStyle.h"

FAudioSliderStyle::FAudioSliderStyle()
	: FSlateStyleSet("AudioSliderStyle")
{
	SetParentStyleName("CoreStyle");
	SetContentRoot(FPaths::EnginePluginsDir() / TEXT("Runtime/AudioWidgets/Content/AudioSlider"));

	const float LabelWidth = 56.0f;
	const float LabelHeight = 22.0f;
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
	
	const float LabelVerticalPadding = 5.0f;
	const FVector2D DesiredWidgetSize = FVector2D(LabelWidth, LabelHeight + LabelVerticalPadding + SliderBackgroundHeight);
	
	Set("AudioSlider.LabelVerticalPadding", LabelVerticalPadding);
	Set("AudioSlider.DesiredWidgetSize", DesiredWidgetSize);

	Set("AudioSlider.LabelBackground", new IMAGE_BRUSH_SVG(TEXT("LabelBackground"), LabelBackgroundSize));
	Set("AudioSlider.SliderBarCap", new IMAGE_BRUSH_SVG(TEXT("SliderBarCap"), SliderBarCapSize));
	Set("AudioSlider.SliderBarRectangle", new IMAGE_BRUSH_SVG(TEXT("SliderBarRectangle"), SliderBarRectangleSize));
	Set("AudioSlider.SliderBackgroundCap", new IMAGE_BRUSH_SVG(TEXT("SliderBackgroundCap"), SliderBackgroundCapSize));
	Set("AudioSlider.SliderBackgroundRectangle", new IMAGE_BRUSH_SVG(TEXT("SliderBackgroundRectangle"), SliderBackgroundRectangleSize));
	Set("AudioSlider.WidgetBackground", new IMAGE_BRUSH_SVG(TEXT("WidgetBackground"), DesiredWidgetSize));

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
