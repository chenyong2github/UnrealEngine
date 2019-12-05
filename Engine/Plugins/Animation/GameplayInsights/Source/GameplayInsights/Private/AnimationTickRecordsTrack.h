// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameplayTrack.h"
#include "Insights/ViewModels/GraphTrack.h"
#include "Insights/ViewModels/GraphSeries.h"

class FAnimationSharedData;
struct FTickRecordMessage;
class FTimingEventSearchParameters;
class FGameplaySharedData;
class UAnimBlueprintGeneratedClass;
class FAnimationTickRecordsTrack;
class FTimingTrackViewport;

class FTickRecordSeries : public FGraphSeries
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

	// Custom overload for auto zoom
	void UpdateAutoZoom(const FTimingTrackViewport& InViewport, const FAnimationTickRecordsTrack& InTrack);

public:
	ESeriesType Type;
	double CurrentMin;
	double CurrentMax;
};

class FAnimationTickRecordsTrack : public TGameplayTrackMixin<FGraphTrack>
{
public:
	static const FName TypeName;
	static const FName SubTypeName;

	FAnimationTickRecordsTrack(const FAnimationSharedData& InSharedData, uint64 InObjectId, uint64 InAssetId, int32 InNodeId, const TCHAR* InName);

	virtual void PreUpdate(const ITimingTrackUpdateContext& Context) override;
	virtual void Draw(const ITimingTrackDrawContext& Context) const override;
	virtual void InitTooltip(FTooltipDrawState& Tooltip, const ITimingEvent& HoveredTimingEvent) const override;
	virtual const TSharedPtr<const ITimingEvent> SearchEvent(const FTimingEventSearchParameters& InSearchParameters) const override;
	virtual void BuildContextMenu(FMenuBuilder& MenuBuilder) override;

	virtual float GetBorderY() const { return BorderY; }

	/** Get the asset ID that this track uses */
	uint64 GetAssetId() const { return AssetId; }

	/** Get the node ID that this track uses */
	int32 GetNodeId() const { return NodeId; }

	/** The current height in lanes */
	int32 HeightInLanes;

private:
	// Add all the series that this track can represent
	void AddAllSeries();

	// Helper for PreUpdate to update the track height
	void UpdateTrackHeight(const ITimingTrackUpdateContext& Context);

	// Helper function used to find a tick record
	void FindTickRecordMessage(const FTimingEventSearchParameters& InParameters, TFunctionRef<void(double, double, uint32, const FTickRecordMessage&)> InFoundPredicate) const;

	// Override certain UI items to be disabled
	virtual bool ContextMenu_UseEventDuration_CanExecute() { return false; }
	virtual bool ContextMenu_ShowBars_CanExecute() { return false; }

	// Helper used to build track name
	FText MakeTrackName(const FGameplaySharedData& InSharedData, uint64 InAssetID, const TCHAR* InName) const;

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

	/** The track size we want to be displayed at */
	float RequestedTrackSizeScale;

	/** The size of the border between tracks */
	float BorderY;
};