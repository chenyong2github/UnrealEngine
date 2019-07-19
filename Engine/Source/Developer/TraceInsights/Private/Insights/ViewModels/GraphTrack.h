// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

// Insights
#include "Insights/ViewModels/BaseTimingTrack.h"

struct FDrawContext;
struct FSlateBrush;
class FMenuBuilder;
class FTimingTrackViewport;

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FGraphBox
{
	float X;
	float W;
	float Y;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FGraphSeries
{
	friend class FGraphTrack;
	friend class FGraphTrackBuilder;

public:
	FGraphSeries();
	~FGraphSeries();

	const FText& GetName() const { return Name; }
	void SetName(const TCHAR* InName) { Name = FText::FromString(InName); }
	void SetName(const FString& InName) { Name = FText::FromString(InName); }
	void SetName(const FText& InName) { Name = InName; }

	const FText& GetDescription() const { return Description; }
	void SetDescription(const TCHAR* InDescription) { Description = FText::FromString(InDescription); }
	void SetDescription(const FString& InDescription) { Description = FText::FromString(InDescription); }
	void SetDescription(const FText& InDescription) { Description = InDescription; }

	bool IsVisible() const { return bIsVisible; }
	void SetVisibility(bool bOnOff) { bIsVisible = bOnOff; }

	bool IsDirty() const { return bIsDirty; }
	void SetDirtyFlag() { bIsDirty = true; }
	void ClearDirtyFlag() { bIsDirty = false; }

	const FLinearColor& GetColor() const { return Color; }
	const FLinearColor& GetBorderColor() const { return BorderColor; }

	void SetColor(FLinearColor InColor, FLinearColor InBorderColor)
	{
		Color = InColor;
		BorderColor = InBorderColor;
	}

	/**
	 * @return Y position (in viewport local space) of the baseline (with Value == 0); in pixels (Slate units).
	 * Y == 0 at the top of the graph track, positive values are downward.
	 */
	double GetBaselineY() const { return BaselineY; }
	void SetBaselineY(const double InBaselineY) { BaselineY = InBaselineY; }

	/**
	 * @return The scale between Value units and viewport units; in pixels (Slate units) / Value unit.
	 */
	double GetScaleY() const { return ScaleY; }
	void SetScaleY(const double InScaleY) { ScaleY = InScaleY; }

	/**
	 * @param Value a value; in Value units
	 * @return Y position (in viewport local space) for a Value; in pixels (Slate units).
	 * Y == 0 at the top of the graph track, positive values are downward.
	 */
	float GetYForValue(double Value) const
	{
		return static_cast<float>(BaselineY - Value * ScaleY);
	}

	/**
	 * @param Y a Y position (in viewport local space); in pixels (Slate units).
	 * @return Value for specified Y position.
	 */
	double GetValueForY(float Y) const
	{
		return (BaselineY - static_cast<double>(Y)) / ScaleY;
	}

	bool IsAutoZoomEnabled() const { return bAutoZoom; }
	void EnableAutoZoom() { bAutoZoom = true; }

	/** target low value of auto zoom interval (corresponding to bottom of the track) */
	double GetTargetAutoZoomLowValue() const { return TargetAutoZoomLowValue; }
	/** target high value of auto zoom interval (corresponding to top of the track) */
	double GetTargetAutoZoomHighValue() const { return TargetAutoZoomHighValue; }
	void SetTargetAutoZoomRange(double LowValue, double HighValue) { TargetAutoZoomLowValue = LowValue; TargetAutoZoomHighValue = HighValue; }

	/** current auto zoom low value */
	double GetAutoZoomLowValue() const { return AutoZoomLowValue; }
	/** current auto zoom high value */
	double GetAutoZoomHighValue() const { return AutoZoomHighValue; }
	void SetAutoZoomRange(double LowValue, double HighValue) { AutoZoomLowValue = LowValue; AutoZoomHighValue = HighValue; }
	
	/**
	 * Compute BaselineY and ScaleY so the [Low, High] Value range will correspond to [Top, Bottom] Y position range.
	 * GetYForValue(InHighValue) == InTopY
	 * GetYForValue(InLowValue) == InBottomY
	 */
	void ComputeBaselineAndScale(const double InLowValue, const double InHighValue, const float InTopY, const float InBottomY, double& OutBaselineY, double& OutScaleY) const
	{
		ensure(InLowValue < InHighValue);
		ensure(InTopY < InBottomY);
		const double InvRange = 1.0 / (InHighValue - InLowValue);
		OutScaleY = static_cast<double>(InBottomY - InTopY) * InvRange;
		//OutBaselineY = (InHighValue * static_cast<double>(InBottomY) - InLowValue * static_cast<double>(InTopY)) * InvRange;
		OutBaselineY = static_cast<double>(InTopY) + InHighValue * OutScaleY;
	}

private:
	FText Name;
	FText Description;

	bool bIsVisible;
	bool bIsDirty;

	bool bAutoZoom;
	double TargetAutoZoomLowValue; // target low value of auto zoom interval (corresponding to bottom of the track)
	double TargetAutoZoomHighValue; // target high value of auto zoom interval (corresponding to top of the track)
	double AutoZoomLowValue; // current auto zoom low value
	double AutoZoomHighValue; // current auto zoom high value

	double BaselineY; // Y position (in viewport local space) of the baseline (with Value == 0); in pixels (Slate units)
	double ScaleY; // scale between Value units and viewport units; in pixels (Slate units) / Value unit

	FLinearColor Color;
	FLinearColor BorderColor;

protected:
	TArray<FVector2D> Points;
	TArray<FVector2D> LinePoints;
	TArray<FGraphBox> Boxes;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FGraphTrack : public FBaseTimingTrack
{
	friend class FGraphTrackBuilder;

private:
	// Visual size of points (in pixels).
	static constexpr float PointVisualSize = 5.0f;

	// Size of points (in pixels) used in reduction algorithm.
	static constexpr double PointSizeX = 3.0f;
	static constexpr float PointSizeY = 3.0f;

public:
	FGraphTrack(uint64 InTrackId);
	virtual ~FGraphTrack();

	virtual void Reset() override;

	virtual void UpdateHoveredState(float MouseX, float MouseY, const FTimingTrackViewport& Viewport) override;

	virtual void Update(const FTimingTrackViewport& Viewport) override = 0;

	void Draw(FDrawContext& DrawContext, const FTimingTrackViewport& Viewport) const;

	virtual void BuildContextMenu(FMenuBuilder& MenuBuilder) override;

	//virtual int GetDebugLineCount() override;
	//virtual void BuildDebugLines(FString& OutStr) override;

	int32 GetNumAddedEvents() const { return NumAddedEvents; }
	int32 GetNumDrawPoints() const { return NumDrawPoints; }
	int32 GetNumDrawLines() const { return NumDrawLines; }
	int32 GetNumDrawBoxes() const { return NumDrawBoxes; }

protected:
	void UpdateStats();

	void DrawBackground(FDrawContext& DrawContext, const FTimingTrackViewport& Viewport) const;
	void DrawSeries(const FGraphSeries& Series, FDrawContext& DrawContext, const FTimingTrackViewport& Viewport) const;

private:
	void ContextMenu_ShowPoints_Execute();
	bool ContextMenu_ShowPoints_CanExecute();
	bool ContextMenu_ShowPoints_IsChecked();

	void ContextMenu_ShowPointsWithBorder_Execute();
	bool ContextMenu_ShowPointsWithBorder_CanExecute();
	bool ContextMenu_ShowPointsWithBorder_IsChecked();

	void ContextMenu_ShowLines_Execute();
	bool ContextMenu_ShowLines_CanExecute();
	bool ContextMenu_ShowLines_IsChecked();

	void ContextMenu_ShowPolygon_Execute();
	bool ContextMenu_ShowPolygon_CanExecute();
	bool ContextMenu_ShowPolygon_IsChecked();

	void ContextMenu_UseEventDuration_Execute();
	bool ContextMenu_UseEventDuration_CanExecute();
	bool ContextMenu_UseEventDuration_IsChecked();

	void ContextMenu_ShowBars_Execute();
	bool ContextMenu_ShowBars_CanExecute();
	bool ContextMenu_ShowBars_IsChecked();

	void ContextMenu_ShowSeries_Execute(FGraphSeries* Series);
	bool ContextMenu_ShowSeries_CanExecute(FGraphSeries* Series);
	bool ContextMenu_ShowSeries_IsChecked(FGraphSeries* Series);

protected:
	TArray<TSharedPtr<FGraphSeries>> AllSeries;

	// Slate resources
	const FSlateBrush* WhiteBrush;
	const FSlateBrush* PointBrush;
	const FSlateBrush* BorderBrush;
	const FSlateFontInfo Font;

	bool bDrawPoints;
	bool bDrawPointsWithBorder;
	bool bDrawLines;
	bool bDrawPolygon;
	bool bUseEventDuration;
	bool bDrawBoxes;

	// Stats
	int32 NumAddedEvents;
	int32 NumDrawPoints;
	int32 NumDrawLines;
	int32 NumDrawBoxes;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FRandomGraphTrack : public FGraphTrack
{
public:
	FRandomGraphTrack(uint64 InTrackId);
	virtual ~FRandomGraphTrack();

	virtual void Update(const FTimingTrackViewport& Viewport) override;

	void AddDefaultSeries();

protected:
	void GenerateSeries(FGraphSeries& Series, const FTimingTrackViewport& Viewport, const int32 EventCount, int32 Seed);
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FGraphTrackBuilder
{
private:
	struct FPointInfo
	{
		bool bValid;
		float X;
		float Y;

		FPointInfo() : bValid(false) {}
	};

public:
	FGraphTrackBuilder(FGraphTrack& InTrack, FGraphSeries& InSeries, const FTimingTrackViewport& InViewport);
	~FGraphTrackBuilder();

	/**
	 * Non-copyable
	 */
	FGraphTrackBuilder(const FGraphTrackBuilder&) = delete;
	FGraphTrackBuilder& operator=(const FGraphTrackBuilder&) = delete;

	FGraphTrack& GetTrack() const { return Track; }
	FGraphSeries& GetSeries() const { return Series; }
	const FTimingTrackViewport& GetViewport() const { return Viewport; }

	void AddEvent(double Time, double Duration, double Value);

private:
	void BeginPoints();
	void AddPoint(double Time, double Value);
	void FlushPoints();
	void EndPoints();

	void BeginConnectedLines();
	void AddConnectedLine(double Time, double Value);
	void FlushConnectedLine();
	void EndConnectedLines();

	void BeginBoxes();
	void AddBox(double Time, double Duration, double Value);
	void FlushBox();
	void EndBoxes();

private:
	FGraphTrack& Track;
	FGraphSeries& Series;
	const FTimingTrackViewport& Viewport;

	// Used by the point reduction algorithm.
	double PointsCurrentX;
	TArray<FPointInfo> PointsAtCurrentX;

	// Used by the line reduction algorithm.
	float LinesCurrentX;
	float LinesMinY;
	float LinesMaxY;
	float LinesFirstY;
	float LinesLastY;
	bool bIsLastLineAdded;

	// Used by the box reduction algorithm.
	//...
};

////////////////////////////////////////////////////////////////////////////////////////////////////
