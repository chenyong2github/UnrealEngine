// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

// Insights
#include "Insights/ViewModels/BaseTimingTrack.h"

class FMenuBuilder;
struct FSlateBrush;

class FGraphSeries;
class FTimingTrackViewport;

////////////////////////////////////////////////////////////////////////////////////////////////////

// Various available options for display
enum class EGraphOptions
{
	None					= 0,

	ShowDebugInfo			= (1 << 0),
	ShowPoints				= (1 << 1),
	ShowPointsWithBorder	= (1 << 2),
	ShowLines				= (1 << 3),
	ShowPolygon				= (1 << 4),
	UseEventDuration		= (1 << 5),
	ShowBars				= (1 << 6),

	All = ShowPoints | ShowPointsWithBorder | ShowLines | ShowPolygon | UseEventDuration | ShowBars,
};

ENUM_CLASS_FLAGS(EGraphOptions);

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTS_API FGraphTrack : public FBaseTimingTrack
{
	friend class FGraphTrackBuilder;

	INSIGHTS_DECLARE_RTTI(FGraphTrack, FBaseTimingTrack)

private:
	// Visual size of points (in pixels).
	static constexpr float PointVisualSize = 5.0f;

	// Size of points (in pixels) used in reduction algorithm.
	static constexpr double PointSizeX = 3.0f;
	static constexpr float PointSizeY = 3.0f;

public:
	explicit FGraphTrack();
	explicit FGraphTrack(const FString& InName);
	virtual ~FGraphTrack();

	//////////////////////////////////////////////////
	// FBaseTimingTrack

	virtual void Reset() override;

	virtual void PostUpdate(const ITimingTrackUpdateContext& Context) override;

	virtual void PreDraw(const ITimingTrackDrawContext& Context) const override;
	virtual void Draw(const ITimingTrackDrawContext& Context) const override;
	virtual void DrawEvent(const ITimingTrackDrawContext& Context, const ITimingEvent& InTimingEvent, EDrawEventMode InDrawMode) const override;

	virtual void InitTooltip(FTooltipDrawState& InOutTooltip, const ITimingEvent& InTooltipEvent) const override;

	virtual const TSharedPtr<const ITimingEvent> GetEvent(float InPosX, float InPosY, const FTimingTrackViewport& Viewport) const override;

	virtual void BuildContextMenu(FMenuBuilder& MenuBuilder) override;

	//////////////////////////////////////////////////

	TArray<TSharedPtr<FGraphSeries>>& GetSeries() { return AllSeries; }

	//TODO: virtual int GetDebugStringLineCount() const override;
	//TODO: virtual void BuildDebugString(FString& OutStr) const override;

	int32 GetNumAddedEvents() const { return NumAddedEvents; }
	int32 GetNumDrawPoints() const { return NumDrawPoints; }
	int32 GetNumDrawLines() const { return NumDrawLines; }
	int32 GetNumDrawBoxes() const { return NumDrawBoxes; }

protected:
	void UpdateStats();

	void DrawSeries(const FGraphSeries& Series, FDrawContext& DrawContext, const FTimingTrackViewport& Viewport) const;

	// Get the Y value that is used to provide a clipping border between adjacent graph tracks.
	virtual float GetBorderY() const { return 0.0f; }

private:
	bool ContextMenu_ShowDebugInfo_CanExecute();
	void ContextMenu_ShowDebugInfo_Execute();
	bool ContextMenu_ShowDebugInfo_IsChecked();

	bool ContextMenu_ShowPoints_CanExecute();
	void ContextMenu_ShowPoints_Execute();
	bool ContextMenu_ShowPoints_IsChecked();

	bool ContextMenu_ShowPointsWithBorder_CanExecute();
	void ContextMenu_ShowPointsWithBorder_Execute();
	bool ContextMenu_ShowPointsWithBorder_IsChecked();

	bool ContextMenu_ShowLines_CanExecute();
	void ContextMenu_ShowLines_Execute();
	bool ContextMenu_ShowLines_IsChecked();

	bool ContextMenu_ShowPolygon_CanExecute();
	void ContextMenu_ShowPolygon_Execute();
	bool ContextMenu_ShowPolygon_IsChecked();

	bool ContextMenu_UseEventDuration_CanExecute();
	void ContextMenu_UseEventDuration_Execute();
	bool ContextMenu_UseEventDuration_IsChecked();

	bool ContextMenu_ShowBars_CanExecute();
	void ContextMenu_ShowBars_Execute();
	bool ContextMenu_ShowBars_IsChecked();

protected:
	TArray<TSharedPtr<FGraphSeries>> AllSeries;

	// Slate resources
	const FSlateBrush* WhiteBrush;
	const FSlateBrush* PointBrush;
	const FSlateBrush* BorderBrush;
	const FSlateFontInfo Font;

	bool bDrawDebugInfo;
	bool bDrawPoints;
	bool bDrawPointsWithBorder;
	bool bDrawLines;
	bool bDrawPolygon;
	bool bUseEventDuration;
	bool bDrawBoxes;
	bool bDrawBaseline;

	// Flags controlling whether menu items are available
	EGraphOptions VisibleOptions;
	EGraphOptions EditableOptions;

	// Stats
	int32 NumAddedEvents; // total event count
	int32 NumDrawPoints;
	int32 NumDrawLines;
	int32 NumDrawBoxes;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTS_API FRandomGraphTrack : public FGraphTrack
{
	INSIGHTS_DECLARE_RTTI(FRandomGraphTrack, FGraphTrack)

public:
	FRandomGraphTrack();
	virtual ~FRandomGraphTrack();

	virtual void Update(const ITimingTrackUpdateContext& Context) override;

	void AddDefaultSeries();

protected:
	void GenerateSeries(FGraphSeries& Series, const FTimingTrackViewport& Viewport, const int32 EventCount, int32 Seed);
};

////////////////////////////////////////////////////////////////////////////////////////////////////
