// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

// Insights
#include "Insights/ViewModels/BaseTimingTrack.h"
#include "Insights/ViewModels/TrackHeader.h"

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

	INSIGHTS_DECLARE_RTTI(FMarkersTimingTrack, FBaseTimingTrack)

public:
	FMarkersTimingTrack();
	virtual ~FMarkersTimingTrack();

	virtual void Reset() override;

	bool IsCollapsed() const { return Header.IsCollapsed(); }
	void ToggleCollapsed() { Header.ToggleCollapsed(); }

	bool IsBookmarksTrack() const { return bUseOnlyBookmarks; }
	void SetBookmarksTrackFlag(bool bInUseOnlyBookmarks)
	{
		bUseOnlyBookmarks = bInUseOnlyBookmarks;
		UpdateTrackNameAndHeight();
	}

	// Stats
	int32 GetNumLogMessages() const { return NumLogMessages; }
	int32 GetNumBoxes() const { return TimeMarkerBoxes.Num(); }
	int32 GetNumTexts() const { return TimeMarkerTexts.Num(); }

	// FBaseTimingTrack
	virtual void Update(const ITimingTrackUpdateContext& Context) override;
	virtual void PostUpdate(const ITimingTrackUpdateContext& Context) override;
	virtual void Draw(const ITimingTrackDrawContext& Context) const override;
	virtual void PostDraw(const ITimingTrackDrawContext& Context) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	void ResetCache()
	{
		TimeMarkerBoxes.Reset();
		TimeMarkerTexts.Reset();
	}

	void UpdateTrackNameAndHeight();
	void UpdateDrawState(const FTimingTrackViewport& Viewport);

private:
	TArray<FTimeMarkerBoxInfo> TimeMarkerBoxes;
	TArray<FTimeMarkerTextInfo> TimeMarkerTexts;

	bool bUseOnlyBookmarks; // If true, uses only bookmarks; otherwise it uses all log messages.

	FTrackHeader Header;

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
