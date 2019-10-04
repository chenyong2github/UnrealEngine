// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

// Insights
#include "Insights/ViewModels/BaseTimingTrack.h"

namespace Trace
{
	struct FLogMessage;
	class ILogProvider;
}

struct FDrawContext;
struct FSlateBrush;
class FTimingTrackViewport;

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FTimeMarkerBoxInfo
{
	float X;
	float W;
	FLinearColor Color;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FTimeMarkerTextInfo
{
	float X;
	FLinearColor Color;
	FString Category; // truncated Category string
	FString Message; // truncated Message string
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FMarkersTimingTrack : public FBaseTimingTrack
{
	friend class FTimeMarkerTrackBuilder;

public:
	explicit FMarkersTimingTrack(uint64 InTrackId);
	virtual ~FMarkersTimingTrack();

	virtual void Reset() override;

	bool IsCollapsed() const { return bIsCollapsed; }
	void ToggleCollapsed() { bIsCollapsed = !bIsCollapsed; }

	bool IsBookmarksTrack() const { return bUseOnlyBookmarks; }
	void SetBookmarksTrackFlag(bool bInUseOnlyBookmarks)
	{
		bUseOnlyBookmarks = bInUseOnlyBookmarks;
		UpdateHeight();
	}

	// Stats
	int32 GetNumLogMessages() const { return NumLogMessages; }
	int32 GetNumBoxes() const { return TimeMarkerBoxes.Num(); }
	int32 GetNumTexts() const { return TimeMarkerTexts.Num(); }

	virtual void UpdateHoveredState(float MouseX, float MouseY, const FTimingTrackViewport& Viewport) override;
	virtual void Tick(const double CurrentTime, const float DeltaTime) override;
	virtual void Update(const FTimingTrackViewport& Viewport) override;

	void Draw(FDrawContext& DrawContext, const FTimingTrackViewport& Viewport) const;

private:
	void ResetCache()
	{
		TimeMarkerBoxes.Reset();
		TimeMarkerTexts.Reset();
	}

	void UpdateHeight();

	void DrawHeader(FDrawContext& DrawContext, bool bFirstDraw) const;

private:
	TArray<FTimeMarkerBoxInfo> TimeMarkerBoxes;
	TArray<FTimeMarkerTextInfo> TimeMarkerTexts;

	bool bIsCollapsed; // If false, the vertical lines will extend to entire viewport height; otherwise will be limited to this track's height.
	bool bUseOnlyBookmarks; // If true, uses only bookmarks; otherwise it uses all log messages.

	float TargetHoveredAnimPercent; // [0.0 .. 1.0], 0.0 = hidden, 1.0 = visible
	float CurrentHoveredAnimPercent;

	// Stats
	int32 NumLogMessages;
	mutable int32 NumDrawBoxes;
	mutable int32 NumDrawTexts;

	// Slate resources
	const FSlateBrush* WhiteBrush;
	const FSlateFontInfo Font;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimeMarkerTrackBuilder
{
public:
	explicit FTimeMarkerTrackBuilder(FMarkersTimingTrack& InTrack, const FTimingTrackViewport& InViewport);

	/**
	 * Non-copyable
	 */
	FTimeMarkerTrackBuilder(const FTimeMarkerTrackBuilder&) = delete;
	FTimeMarkerTrackBuilder& operator=(const FTimeMarkerTrackBuilder&) = delete;

	const FTimingTrackViewport& GetViewport() { return Viewport; }

	void BeginLog(const Trace::ILogProvider& LogProvider);
	void AddLogMessage(const Trace::FLogMessage& Message);
	void EndLog();

	static FLinearColor GetColorByCategory(const TCHAR* const Category);
	static FLinearColor GetColorByVerbosity(const ELogVerbosity::Type Verbosity);

private:
	void Flush(float AvailableTextW);
	void AddTimeMarker(const float X, const uint64 LogIndex, const ELogVerbosity::Type Verbosity, const TCHAR* const Category, const TCHAR* Message);

private:
	FMarkersTimingTrack& Track;
	const FTimingTrackViewport& Viewport;

	const TSharedRef<class FSlateFontMeasure> FontMeasureService;
	const FSlateFontInfo Font;

	const Trace::ILogProvider* LogProviderPtr; // valid only between BeginLog() and EndLog()

	float LastX1;
	float LastX2;
	uint64 LastLogIndex;
	ELogVerbosity::Type LastVerbosity;
	const TCHAR* LastCategory;
	const TCHAR* LastMessage;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
