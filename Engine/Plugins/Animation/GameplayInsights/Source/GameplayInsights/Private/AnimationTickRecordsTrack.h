// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTrack.h"
#include "Insights/ViewModels/TimingEventsTrack.h"

class FAnimationSharedData;
struct FTickRecordMessage;
class FTimingEventSearchParameters;

class FAnimationTickRecordsTrack : public TGameplayTrackMixin<FTimingEventsTrack>
{
public:
	static const FName TypeName;
	static const FName SubTypeName;

	FAnimationTickRecordsTrack(const FAnimationSharedData& InSharedData, uint64 InObjectId, uint64 InAssetId, const TCHAR* InName);

	virtual void Draw(ITimingViewDrawHelper& Helper) const override;
	virtual void InitTooltip(FTooltipDrawState& Tooltip, const FTimingEvent& HoveredTimingEvent) const override;
	virtual bool SearchTimingEvent(const FTimingEventSearchParameters& InSearchParameters, FTimingEvent& InOutTimingEvent) const override;

	/** Get the asset ID that this track uses */
	uint64 GetAssetId() const { return AssetId; }

private:
	// Helper function used to find a tick record
	bool FindTickRecordMessage(const FTimingEventSearchParameters& InParameters, TFunctionRef<void(double, double, uint32, const FTickRecordMessage&)> InFoundPredicate) const;

private:
	/** The shared data */
	const FAnimationSharedData& SharedData;

	/** The asset ID that this track uses */
	uint64 AssetId;
};