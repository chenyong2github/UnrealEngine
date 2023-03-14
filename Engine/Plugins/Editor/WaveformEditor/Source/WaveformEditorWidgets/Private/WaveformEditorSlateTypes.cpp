// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaveformEditorSlateTypes.h"

#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"

namespace WaveformEditorStylesSharedParams
{
	const FLazyName BackgroundBrushName = TEXT("WhiteBrush");
	const FLazyName HandleBrushName = TEXT("Sequencer.Timeline.VanillaScrubHandleDown");
	const FLinearColor PlayheadColor = FLinearColor(255.f, 0.1f, 0.2f, 1.f);
	const FLinearColor RulerTicksColor = FLinearColor(1.f, 1.f, 1.f, 0.9f);
	const float ViewerHeight = 720.f;
	const float ViewerWidth = 1280.f;
}

const FName FWaveformEditorTimeRulerStyle::TypeName("FWaveformEditorTimeRulerStyle");

FWaveformEditorTimeRulerStyle::FWaveformEditorTimeRulerStyle()
	: HandleWidth(15.f)
	, HandleColor(WaveformEditorStylesSharedParams::PlayheadColor)
	, HandleBrush(*FAppStyle::GetBrush(WaveformEditorStylesSharedParams::HandleBrushName))
	, TicksColor(WaveformEditorStylesSharedParams::RulerTicksColor)
	, TicksTextColor(WaveformEditorStylesSharedParams::RulerTicksColor)
	, TicksTextFont(FAppStyle::GetFontStyle("Regular"))
	, TicksTextOffset(5.f)
	, BackgroundColor(FLinearColor::Black)
	, BackgroundBrush(*FAppStyle::GetBrush(WaveformEditorStylesSharedParams::BackgroundBrushName))
	, DesiredWidth(WaveformEditorStylesSharedParams::ViewerWidth)
	, DesiredHeight(30.f)
{
}

const FWaveformEditorTimeRulerStyle& FWaveformEditorTimeRulerStyle::GetDefault()
{
	static FWaveformEditorTimeRulerStyle Default;
	return Default;
}

void FWaveformEditorTimeRulerStyle::GetResources(TArray< const FSlateBrush* >& OutBrushes) const
{
	OutBrushes.Add(&HandleBrush);
	OutBrushes.Add(&BackgroundBrush);
}

