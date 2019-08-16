// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

// Insights
#include "Insights/ViewModels/BaseTimingTrack.h"

struct FDrawContext;
struct FSlateBrush;
class FTimingTrackViewport;

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FGraphBox
{
	float X;
	float W;
	float Y;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FGraphTrackSeries
{
	friend class FGraphTrack;
	friend class FGraphTrackBuilder;

public:
	FGraphTrackSeries();
	~FGraphTrackSeries();

	void SetColor(FLinearColor InColor, FLinearColor InBorderColor)
	{
		Color = InColor;
		BorderColor = InBorderColor;
	}

protected:
	//FText Name;
	//FText Description;

	FLinearColor Color;
	FLinearColor BorderColor;

	TArray<FVector2D> Points;
	TArray<FVector2D> LinePoints;
	TArray<FGraphBox> Boxes;

	//bool bIsVisible;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FGraphTrack : public FBaseTimingTrack
{
	friend class FGraphTrackBuilder;

private:
	// Visual size of points (in pixels).
	static constexpr float PointVisualSize = 7.0f;

	// Size of points (in pixels) used in reduction algorithm.
	static constexpr double PointSizeX = 3.0f;
	static constexpr float PointSizeY = 3.0f;

public:
	FGraphTrack(uint64 InTrackId);
	virtual ~FGraphTrack();

	virtual void UpdateHoveredState(float MouseX, float MouseY, const FTimingTrackViewport& Viewport);

	virtual void Update(const FTimingTrackViewport& Viewport) override = 0;

	void Draw(FDrawContext& DrawContext, const FTimingTrackViewport& Viewport) const;

	int32 GetNumAddedEvents() const { return NumAddedEvents; }
	int32 GetNumDrawPoints() const { return NumDrawPoints; }
	int32 GetNumDrawLines() const { return NumDrawLines; }
	int32 GetNumDrawBoxes() const { return NumDrawBoxes; }

protected:
	void UpdateStats();

	void DrawSeries(const FGraphTrackSeries& Series, FDrawContext& DrawContext, const FTimingTrackViewport& Viewport) const;

	float GetYForValue(double Value) const
	{
		return BaselineY - static_cast<float>(Value * ScaleY); // TODO: vertical zooming and panning
	}

protected:
	TArray<FGraphTrackSeries> AllSeries;

	// Slate resources
	const FSlateBrush* WhiteBrush;
	const FSlateBrush* PointBrush;
	const FSlateBrush* BorderBrush;
	const FSlateFontInfo Font;

	bool bDrawPoints;
	bool bDrawPointsWithBorder;
	bool bDrawLines;
	bool bDrawLinesWithDuration;
	bool bDrawBoxes;

	float BaselineY; // Y position (in viewport local space) of the baseline (with Value == 0); in pixels (Slate units)
	double ScaleY; // scale between Value units and viewport units; in pixels (Slate units) / Value unit

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

protected:
	void GenerateSeries(FGraphTrackSeries& Series, const FTimingTrackViewport& Viewport, const int32 EventCount);
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FFramesGraphTrack : public FGraphTrack
{
public:
	FFramesGraphTrack(uint64 InTrackId);
	virtual ~FFramesGraphTrack();

	virtual void Update(const FTimingTrackViewport& Viewport) override;

protected:
	void UpdateSeries(FGraphTrackSeries& Series, const FTimingTrackViewport& Viewport, ETraceFrameType FrameType);
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
	FGraphTrackBuilder(FGraphTrack& InTrack, FGraphTrackSeries& InSeries, const FTimingTrackViewport& InViewport);
	~FGraphTrackBuilder();

	/**
	 * Non-copyable
	 */
	FGraphTrackBuilder(const FGraphTrackBuilder&) = delete;
	FGraphTrackBuilder& operator=(const FGraphTrackBuilder&) = delete;

	FGraphTrack& GetTrack() const { return Track; }
	FGraphTrackSeries& GetSeries() const { return Series; }
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
	FGraphTrackSeries& Series;
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
