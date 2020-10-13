// Copyright Epic Games, Inc. All Rights Reserved.

#include "CommonMovieSceneTools.h"
#include "Framework/Application/SlateApplication.h"
#include "EditorStyleSet.h"
#include "Fonts/FontMeasure.h"
#include "SequencerSectionPainter.h"

void DrawFrameNumberHint(FSequencerSectionPainter& InPainter, FFrameTime CurrentTime, int32 FrameNumber)
{
	FString FrameString = FString::FromInt(FrameNumber);

	const FSlateFontInfo SmallLayoutFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);
	const TSharedRef< FSlateFontMeasure > FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	FVector2D TextSize = FontMeasureService->Measure(FrameString, SmallLayoutFont);

	const float PixelX = InPainter.GetTimeConverter().FrameToPixel(CurrentTime);

	// Flip the text position if getting near the end of the view range
	static const float TextOffsetPx = 10.f;
	bool  bDrawLeft = (InPainter.SectionGeometry.Size.X - PixelX) < (TextSize.X + 22.f) - TextOffsetPx;
	float TextPosition = bDrawLeft ? PixelX - TextSize.X - TextOffsetPx : PixelX + TextOffsetPx;
	//handle mirrored labels
	const float MajorTickHeight = 9.0f;
	FVector2D TextOffset(TextPosition, InPainter.SectionGeometry.Size.Y - (MajorTickHeight + TextSize.Y));

	const FLinearColor DrawColor = FEditorStyle::GetSlateColor("SelectionColor").GetColor(FWidgetStyle()).CopyWithNewOpacity(InPainter.GhostAlpha);
	const FVector2D BoxPadding = FVector2D(4.0f, 2.0f);
	// draw time string

	FSlateDrawElement::MakeBox(
		InPainter.DrawElements,
		InPainter.LayerId + 5,
		InPainter.SectionGeometry.ToPaintGeometry(TextOffset - BoxPadding, TextSize + 2.0f * BoxPadding),
		FEditorStyle::GetBrush("WhiteBrush"),
		ESlateDrawEffect::None,
		FLinearColor::Black.CopyWithNewOpacity(0.5f * InPainter.GhostAlpha)
	);

	ESlateDrawEffect DrawEffects = InPainter.bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

	FSlateDrawElement::MakeText(
		InPainter.DrawElements,
		InPainter.LayerId + 6,
		InPainter.SectionGeometry.ToPaintGeometry(TextOffset, TextSize),
		FrameString,
		SmallLayoutFont,
		DrawEffects,
		DrawColor
	);
}