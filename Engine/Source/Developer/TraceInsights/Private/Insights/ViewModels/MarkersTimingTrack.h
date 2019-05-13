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

static const float BookmarksTrackHeight = 14.0f;
static const float TimeMarkersTrackHeight = 28.0f;

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
	FMarkersTimingTrack(uint64 InTrackId);
	virtual ~FMarkersTimingTrack();

	virtual void Reset() override;

	bool IsCollapsed() const { return bIsCollapsed; }
	void ToggleCollapsed() { bIsCollapsed = !bIsCollapsed; }

	bool IsBookmarksTrack() const { return bUseOnlyBookmarks; }
	void SetIsBookmarksTrackFlag(bool bInUseOnlyBookmarks)
	{
		bUseOnlyBookmarks = bInUseOnlyBookmarks;
		UpdateHeight();
	}

	bool IsDirty() const { return bIsDirty; }
	void SetDirtyFlag() { bIsDirty = true; }
	void ClearDirtyFlag() { bIsDirty = false; }

	// Stats
	int GetNumLogMessages() const { return NumLogMessages; }
	int GetNumBoxes() const { return TimeMarkerBoxes.Num(); }
	int GetNumTexts() const { return TimeMarkerTexts.Num(); }

	virtual void UpdateHoveredState(float MX, float MY, const FTimingTrackViewport& Viewport) override;
	virtual void Tick(const double InCurrentTime, const float InDeltaTime) override;
	virtual void Update(const FTimingTrackViewport& Viewport) override;

	void Draw(FDrawContext& DC, const FTimingTrackViewport& Viewport) const;

protected:
	void ResetCache()
	{
		TimeMarkerBoxes.Reset();
		TimeMarkerTexts.Reset();
	}

	void UpdateHeight()
	{
		H = bUseOnlyBookmarks ? BookmarksTrackHeight : TimeMarkersTrackHeight;
	}

	void DrawHeader(FDrawContext& DC, bool bFirstDraw) const;

protected:
	TArray<FTimeMarkerBoxInfo> TimeMarkerBoxes;
	TArray<FTimeMarkerTextInfo> TimeMarkerTexts;

	bool bIsCollapsed; // If false, the vertical lines will extend to entire viewport height; otherwise will be limited to this track's height.
	bool bUseOnlyBookmarks; // If true, uses only bookmarks; otherwise it uses all log messages.
	bool bIsDirty; // Source list of bookmarks/markers has changed. Cached Boxes and Texts are dirty.

	float TargetHoveredAnimPercent; // [0.0 .. 1.0], 0.0 = hidden, 1.0 = visible
	float CurrentHoveredAnimPercent;

	// Stats
	int NumLogMessages;
	mutable int NumDrawBoxes;
	mutable int NumDrawTexts;

	// Slate resources
	const FSlateBrush* WhiteBrush;
	const FSlateFontInfo Font;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FTimeMarkerTrackBuilder : public FNoncopyable
{
public:
	FTimeMarkerTrackBuilder(FMarkersTimingTrack& InTrack, const FTimingTrackViewport& InViewport);
	//~FTimeMarkerTrackBuilder();

	const FTimingTrackViewport& GetViewport() { return Viewport; }

	void Begin();
	void BeginLog(const Trace::ILogProvider& LogProvider);
	void AddLogMessage(const Trace::FLogMessage& Message);
	void EndLog();
	void End();

	static FLinearColor GetColorByCategory(const TCHAR* const Category);
	static FLinearColor GetColorByVerbosity(const ELogVerbosity::Type Verbosity);

private:
	void Flush(float AvailableTextW);
	void AddTimeMarker(const float X, const TCHAR* const Category, const ELogVerbosity::Type Verbosity, const uint64 LogIndex);

public:
	FMarkersTimingTrack& Track;
	const FTimingTrackViewport& Viewport;

	const TSharedRef<FSlateFontMeasure> FontMeasureService;
	const FSlateFontInfo Font;

	const Trace::ILogProvider* LogProviderPtr; // valid only between BeginLog() and EndLog()

	float LastX1;
	float LastX2;
	const TCHAR* LastCategory;
	ELogVerbosity::Type LastVerbosity;
	uint64 LastLogIndex;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
