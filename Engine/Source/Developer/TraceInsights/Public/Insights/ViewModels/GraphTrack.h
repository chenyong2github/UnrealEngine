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

class TRACEINSIGHTS_API FGraphTrack : public FBaseTimingTrack
{
	friend class FGraphTrackBuilder;

private:
	// Visual size of points (in pixels).
	static constexpr float PointVisualSize = 5.0f;

	// Size of points (in pixels) used in reduction algorithm.
	static constexpr double PointSizeX = 3.0f;
	static constexpr float PointSizeY = 3.0f;

public:
	explicit FGraphTrack(const FName& InSubType = NAME_None);
	explicit FGraphTrack(const FName& InSubType, const FString& InName);
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

	virtual bool ContextMenu_ShowPoints_CanExecute();
	virtual bool ContextMenu_ShowPointsWithBorder_CanExecute();
	virtual bool ContextMenu_ShowLines_CanExecute();
	virtual bool ContextMenu_ShowPolygon_CanExecute();
	virtual bool ContextMenu_UseEventDuration_CanExecute();
	virtual bool ContextMenu_ShowBars_CanExecute();
	virtual bool ContextMenu_ShowSeries_CanExecute(FGraphSeries* Series);

	// Get the Y value that is used to provide a clipping border between adjacent graph tracks.
	virtual float GetBorderY() const { return 0.0f; }

private:
	bool ContextMenu_ShowDebugInfo_CanExecute();
	void ContextMenu_ShowDebugInfo_Execute();
	bool ContextMenu_ShowDebugInfo_IsChecked();

	void ContextMenu_ShowPoints_Execute();
	bool ContextMenu_ShowPoints_IsChecked();

	void ContextMenu_ShowPointsWithBorder_Execute();
	bool ContextMenu_ShowPointsWithBorder_IsChecked();

	void ContextMenu_ShowLines_Execute();
	bool ContextMenu_ShowLines_IsChecked();

	void ContextMenu_ShowPolygon_Execute();
	bool ContextMenu_ShowPolygon_IsChecked();

	void ContextMenu_UseEventDuration_Execute();
	bool ContextMenu_UseEventDuration_IsChecked();

	void ContextMenu_ShowBars_Execute();
	bool ContextMenu_ShowBars_IsChecked();

	void ContextMenu_ShowSeries_Execute(FGraphSeries* Series);
	bool ContextMenu_ShowSeries_IsChecked(FGraphSeries* Series);

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

	// Stats
	int32 NumAddedEvents; // total event count
	int32 NumDrawPoints;
	int32 NumDrawLines;
	int32 NumDrawBoxes;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class TRACEINSIGHTS_API FRandomGraphTrack : public FGraphTrack
{
public:
	FRandomGraphTrack();
	virtual ~FRandomGraphTrack();

	virtual void Update(const ITimingTrackUpdateContext& Context) override;

	void AddDefaultSeries();

protected:
	void GenerateSeries(FGraphSeries& Series, const FTimingTrackViewport& Viewport, const int32 EventCount, int32 Seed);
};

////////////////////////////////////////////////////////////////////////////////////////////////////
