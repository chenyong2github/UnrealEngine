// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/ViewModels/BaseTimingTrack.h"

struct FTimingEventsTrackDrawState;

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTS_API FTimingEventsTrack : public FBaseTimingTrack
{
public:
	explicit FTimingEventsTrack(const FName& InType, const FName& InSubType, const FString& InName);
	virtual ~FTimingEventsTrack();

	int32 GetNumLanes() const { return NumLanes; }
	void SetNumLanes(int32 InNumLanes) { NumLanes = InNumLanes; }

	//////////////////////////////////////////////////
	// FBaseTimingTrack

	virtual void Reset() override;
	virtual void PostUpdate(const ITimingTrackUpdateContext& Context) override;

	virtual void DrawEvent(const ITimingTrackDrawContext& Context, const ITimingEvent& InTimingEvent, EDrawEventMode InDrawMode) const override;

	virtual const TSharedPtr<const ITimingEvent> GetEvent(float InPosX, float InPosY, const FTimingTrackViewport& Viewport) const override;

	//////////////////////////////////////////////////

protected:
	void UpdateTrackHeight(const ITimingTrackUpdateContext& Context);

private:
	int32 NumLanes; // number of lanes (sub-tracks)
	TSharedPtr<FTimingEventsTrackDrawState> DrawState;

public:
	static bool bUseDownSampling;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
