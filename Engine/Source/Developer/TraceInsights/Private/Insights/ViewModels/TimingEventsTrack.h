// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/ViewModels/BaseTimingTrack.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FTimingEventsTrackLayout
{
	static constexpr float RealMinTimelineH = 13.0f;

	static constexpr float NormalLayoutEventH = 14.0f;
	static constexpr float NormalLayoutEventDY = 2.0f;
	static constexpr float NormalLayoutTimelineDY = 14.0f;
	static constexpr float NormalLayoutMinTimelineH = 0.0f;

	static constexpr float CompactLayoutEventH = 2.0f;
	static constexpr float CompactLayoutEventDY = 1.0f;
	static constexpr float CompactLayoutTimelineDY = 3.0f;
	static constexpr float CompactLayoutMinTimelineH = 0.0f;

	//////////////////////////////////////////////////

	bool bIsCompactMode;

	float EventH; // height of a timing event, in Slate units
	float EventDY; // vertical distance between two timing event sub-tracks, in Slate units
	float TimelineDY; // space at top and bottom of each timeline, in Slate units
	float MinTimelineH;
	float TargetMinTimelineH;

	//////////////////////////////////////////////////

	float GetLaneY(uint32 Depth) const { return 1.0f + TimelineDY + Depth * (EventDY + EventH); }

	void ForceNormalMode();
	void ForceCompactMode();
	void Update();
};

////////////////////////////////////////////////////////////////////////////////////////////////////

enum class ETimingEventsTrackType
{
	Cpu,
	Gpu,
	Loading,
	Io,
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimingEventsTrack : public FBaseTimingTrack
{
public:
	FTimingEventsTrack(uint64 InTrackId, ETimingEventsTrackType InType, const FString& InName, const TCHAR* InGroupName);
	virtual ~FTimingEventsTrack();

	ETimingEventsTrackType GetType() const { return Type; }
	const FString& GetName() const { return Name; }

	const TCHAR* GetGroupName() const { return GroupName; };

	void SetThreadId(uint32 InThreadId) { ThreadId = InThreadId; }
	uint32 GetThreadId() const { return ThreadId; }

	void SetOrder(int32 InOrder) { Order = InOrder; }
	int32 GetOrder() const { return Order; }

	int32 GetDepth() const { return Depth; }
	int32 GetNumLanes() const { return Depth + 1; }

	virtual void Reset() override;
	virtual void UpdateHoveredState(float MX, float MY, const FTimingTrackViewport& Viewport);

public:
	ETimingEventsTrackType Type;
	FString Name;
	const TCHAR* GroupName;
	uint32 ThreadId;
	int32 Order;
	int32 Depth; // number of lanes == Depth + 1
	bool bIsCollapsed;

	// TODO: Cached OnPaint state.
	//TArray<FEventBoxInfo> Boxes;
	//TArray<FEventBoxInfo> MergedBorderBoxes;
	//TArray<FEventBoxInfo> Borders;
	//TArray<FTextInfo> Texts;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
