// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"
#include "HAL/Platform.h"
#include "ISampledSequenceGridService.h"
#include "Math/Range.h"
#include "Misc/FrameRate.h"

struct FSlateFontInfo;

class WAVEFORMEDITORWIDGETS_API FWaveformEditorGridData : public ISampledSequenceGridService
{
public: 
	explicit FWaveformEditorGridData(const uint32 InTotalFrames, const uint32 InSampleRateHz, const FSlateFontInfo* InTicksTimeFont = nullptr);

	void UpdateDisplayRange(const TRange<uint32> InDisplayRange);
	bool UpdateGridMetrics(const float InGridPixelWidth);
	virtual const FSampledSequenceGridMetrics GetGridMetrics() const override;
	void SetTicksTimeFont(const FSlateFontInfo* InNewFont);
	const float SnapPositionToClosestFrame(const float InPixelPosition) const;

private:
	FSampledSequenceGridMetrics GridMetrics;
	uint32 TotalFrames = 0;
	TRange<uint32> DisplayRange;

	float GridPixelWidth = 0.f;
	const FSlateFontInfo* TicksTimeFont = nullptr;
	FFrameRate GridFrameRate;
};