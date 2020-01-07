// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayGraphTrack.h"

class FAnimationSharedData;
struct FTickRecordMessage;
class FTimingEventSearchParameters;
class FGameplaySharedData;
class UAnimBlueprintGeneratedClass;
class FAnimationTickRecordsTrack;
class FTimingTrackViewport;

class FTickRecordSeries : public FGameplayGraphSeries
{
public:
	enum class ESeriesType : uint32
	{
		BlendWeight,
		PlaybackTime,
		RootMotionWeight,
		PlayRate,
		BlendSpacePositionX,
		BlendSpacePositionY,

		Count,
	};

	virtual FString FormatValue(double Value) const override;

public:
	ESeriesType Type;
};

class FAnimationTickRecordsTrack : public FGameplayGraphTrack
{
	INSIGHTS_DECLARE_RTTI(FAnimationTickRecordsTrack, FGameplayGraphTrack)

public:
	FAnimationTickRecordsTrack(const FAnimationSharedData& InSharedData, uint64 InObjectId, uint64 InAssetId, int32 InNodeId, const TCHAR* InName);

	virtual void InitTooltip(FTooltipDrawState& Tooltip, const ITimingEvent& HoveredTimingEvent) const override;
	virtual const TSharedPtr<const ITimingEvent> SearchEvent(const FTimingEventSearchParameters& InSearchParameters) const override;
	virtual void BuildContextMenu(FMenuBuilder& MenuBuilder) override;
	virtual bool UpdateSeriesBounds(FGameplayGraphSeries& InSeries, const FTimingTrackViewport& InViewport) override;
	virtual void UpdateSeries(FGameplayGraphSeries& InSeries, const FTimingTrackViewport& InViewport) override;
	virtual void AddAllSeries() override;

	/** Get the asset ID that this track uses */
	uint64 GetAssetId() const { return AssetId; }

	/** Get the node ID that this track uses */
	int32 GetNodeId() const { return NodeId; }

private:
	// Helper function used to find a tick record
	void FindTickRecordMessage(const FTimingEventSearchParameters& InParameters, TFunctionRef<void(double, double, uint32, const FTickRecordMessage&)> InFoundPredicate) const;

	// Helper used to build track name
	FText MakeTrackName(const FGameplaySharedData& InSharedData, uint64 InAssetID, const TCHAR* InName) const;

	// Helper function for series bounds update
	template<typename ProjectionType>
	bool UpdateSeriesBoundsHelper(FTickRecordSeries& InSeries, const FTimingTrackViewport& InViewport, ProjectionType Projection);

	// Helper function for series update
	template<typename ProjectionType>
	void UpdateSeriesHelper(FTickRecordSeries& InSeries, const FTimingTrackViewport& InViewport, ProjectionType Projection);

private:
	/** The shared data */
	const FAnimationSharedData& SharedData;

#if WITH_EDITOR
	/** The class that output this tick record */
	UAnimBlueprintGeneratedClass* InstanceClass;
#endif

	/** Colors for line drawing */
	FLinearColor MainSeriesLineColor;
	FLinearColor MainSeriesFillColor;

	/** The asset ID that this track uses */
	uint64 AssetId;

	/** The node ID that this track's data comes from */
	int32 NodeId;
};