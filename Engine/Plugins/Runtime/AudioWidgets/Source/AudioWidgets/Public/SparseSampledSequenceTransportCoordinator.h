// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"
#include "ISparseSampledSequenceTransportCoordinator.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnDisplayRangeUpdated, const TRange<float> /* New Display Range */);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnFocusPointScrubUpdate, const float /* Focused Playback Ratio */, const bool /*Playhead is Moving*/);


class AUDIOWIDGETS_API FSparseSampledSequenceTransportCoordinator : public ISparseSampledSequenceTransportCoordinator
{
public:
	FSparseSampledSequenceTransportCoordinator() = default;
	virtual ~FSparseSampledSequenceTransportCoordinator() = default;

	/** Called when the playhead is scrubbed */
	FOnFocusPointScrubUpdate OnFocusPointScrubUpdate;

	/** Called when the display range is updated */
	FOnDisplayRangeUpdated OnDisplayRangeUpdated;

	/** ISparseSampledSequenceTransportCoordinator interface */
	const TRange<float> GetDisplayRange() const;
	const float GetFocusPoint() const override;
	void ScrubFocusPoint(const float InTargetFocusPoint, const bool bIsMoving) override;
	const bool IsScrubbing() const;
	void SetProgressRatio(const float NewRatio) override;
	void SetZoomRatio(const float NewRatio) override;
	float ConvertAbsoluteRatioToZoomed(const float InAbsoluteRatio) const override;
	float ConvertZoomedRatioToAbsolute(const float InZoomedRatio) const override;

	void UpdatePlaybackRange(const TRange<float>& NewRange);
	void Stop();

private:
	FORCEINLINE void MoveFocusPoint(const float InFocusPoint);
	void UpdateZoomRatioAndDisplayRange(const float NewZoomRatio);

	void UpdateDisplayRange(const float MinValue, const float MaxValue);
	bool IsRatioWithinDisplayRange(const float Ratio) const;

	float GetPlayBackRatioFromFocusPoint(const float InFocusPoint) const;
	
	float CurrentPlaybackRatio = 0.f;
	float FocusPointLockPosition = 0.95f;
	float FocusPoint = 0.f;
	float ZoomRatio = 1.f;
	
	/* The currently displayed render data range */
	TRange<float> DisplayRange = TRange<float>::Inclusive(0.f, 1.f);

	/* Progress range to scale the incoming progress ratio with*/
	TRange<float> ProgressRange = TRange<float>::Inclusive(0.f, 1.f);
	
	bool bIsScrubbing = false;
};