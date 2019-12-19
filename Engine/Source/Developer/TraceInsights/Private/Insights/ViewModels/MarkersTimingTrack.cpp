// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MarkersTimingTrack.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "TraceServices/AnalysisService.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/ViewModels/TimingTrackViewport.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "MarkersTimingTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////

INSIGHTS_IMPLEMENT_RTTI(FMarkersTimingTrack)

////////////////////////////////////////////////////////////////////////////////////////////////////

FMarkersTimingTrack::FMarkersTimingTrack()
	: FBaseTimingTrack()
	//, TimeMarkerBoxes()
	//, TimeMarkerTexts()
	, bUseOnlyBookmarks(true)
	, Header(*this)
	, NumLogMessages(0)
	, NumDrawBoxes(0)
	, NumDrawTexts(0)
	, WhiteBrush(FInsightsStyle::Get().GetBrush("WhiteBrush"))
	, Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FMarkersTimingTrack::~FMarkersTimingTrack()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMarkersTimingTrack::Reset()
{
	FBaseTimingTrack::Reset();

	TimeMarkerBoxes.Reset();
	TimeMarkerTexts.Reset();

	bUseOnlyBookmarks = true;

	Header.Reset();
	Header.SetIsInBackground(true);
	Header.SetCanBeCollapsed(true);

	NumLogMessages = 0;
	NumDrawBoxes = 0;
	NumDrawTexts = 0;

	UpdateTrackNameAndHeight();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMarkersTimingTrack::UpdateTrackNameAndHeight()
{
	if (bUseOnlyBookmarks)
	{
		const FString NameString = TEXT("Bookmarks");
		SetName(NameString);
		SetHeight(14.0f);
	}
	else
	{
		const FString NameString = TEXT("Logs");
		SetName(NameString);
		SetHeight(28.0f);
	}

	Header.UpdateSize();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMarkersTimingTrack::Update(const ITimingTrackUpdateContext& Context)
{
	Header.Update(Context);

	const FTimingTrackViewport& Viewport = Context.GetViewport();
	if (IsDirty() || Viewport.IsHorizontalViewportDirty())
	{
		ClearDirtyFlag();

		UpdateDrawState(Viewport);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMarkersTimingTrack::PostUpdate(const ITimingTrackUpdateContext& Context)
{
	const float MouseY = Context.GetMousePosition().Y;
	SetHoveredState(MouseY >= GetPosY() && MouseY < GetPosY() + GetHeight());

	Header.PostUpdate(Context);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMarkersTimingTrack::UpdateDrawState(const FTimingTrackViewport& InViewport)
{
	FTimeMarkerTrackBuilder Builder(*this, InViewport);

	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

		const Trace::ILogProvider& LogProvider = Trace::ReadLogProvider(*Session.Get());
		Builder.BeginLog(LogProvider);

		LogProvider.EnumerateMessages(
			Builder.GetViewport().GetStartTime(),
			Builder.GetViewport().GetEndTime(),
			[&Builder](const Trace::FLogMessage& Message) { Builder.AddLogMessage(Message); });

		Builder.EndLog();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMarkersTimingTrack::Draw(const ITimingTrackDrawContext& Context) const
{
	FDrawContext& DrawContext = Context.GetDrawContext();
	const FTimingTrackViewport& Viewport = Context.GetViewport();

	// Draw background.
	const FLinearColor BackgroundColor(0.04f, 0.04f, 0.04f, 1.0f);
	DrawContext.DrawBox(0.0f, GetPosY(), Viewport.GetWidth(), GetHeight(), WhiteBrush, BackgroundColor);
	DrawContext.LayerId++;

	Header.Draw(Context);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMarkersTimingTrack::PostDraw(const ITimingTrackDrawContext& Context) const
{
	FDrawContext& DrawContext = Context.GetDrawContext();
	const FTimingTrackViewport& Viewport = Context.GetViewport();

	//////////////////////////////////////////////////
	// Draw vertical lines.
	// Multiple adjacent vertical lines with same color are merged into a single box.

	float BoxY, BoxH;
	if (IsCollapsed())
	{
		BoxY = GetPosY();
		BoxH = GetHeight();
	}
	else
	{
		BoxY = 0.0f;
		BoxH = Viewport.GetHeight();
	}

	const int32 NumBoxes = TimeMarkerBoxes.Num();
	for (int32 BoxIndex = 0; BoxIndex < NumBoxes; BoxIndex++)
	{
		const FTimeMarkerBoxInfo& Box = TimeMarkerBoxes[BoxIndex];
		DrawContext.DrawBox(Box.X, BoxY, Box.W, BoxH, WhiteBrush, Box.Color);
	}
	DrawContext.LayerId++;
	NumDrawBoxes = NumBoxes;

	//////////////////////////////////////////////////
	// Draw texts (strings are already truncated).

	const float CategoryY = GetPosY() + 2.0f;
	const float MessageY = GetPosY() + (IsBookmarksTrack() ? 1.0f : 14.0f);

	const int32 NumTexts = TimeMarkerTexts.Num();
	for (int32 TextIndex = 0; TextIndex < NumTexts; TextIndex++)
	{
		const FTimeMarkerTextInfo& TextInfo = TimeMarkerTexts[TextIndex];

		if (!IsBookmarksTrack() && TextInfo.Category.Len() > 0)
		{
			DrawContext.DrawText(TextInfo.X, CategoryY, TextInfo.Category, Font, TextInfo.Color);
			NumDrawTexts++;
		}

		if (TextInfo.Message.Len() > 0)
		{
			DrawContext.DrawText(TextInfo.X, MessageY, TextInfo.Message, Font, TextInfo.Color);
			NumDrawTexts++;
		}
	}
	DrawContext.LayerId++;

	//////////////////////////////////////////////////

	Header.PostDraw(Context);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply FMarkersTimingTrack::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (IsVisible() && IsHeaderHovered())
		{
			ToggleCollapsed();
			Reply = FReply::Handled();
		}
	}

	return Reply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply FMarkersTimingTrack::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return OnMouseButtonDown(MyGeometry, MouseEvent);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTimeMarkerTrackBuilder
////////////////////////////////////////////////////////////////////////////////////////////////////

FTimeMarkerTrackBuilder::FTimeMarkerTrackBuilder(FMarkersTimingTrack& InTrack, const FTimingTrackViewport& InViewport)
	: Track(InTrack)
	, Viewport(InViewport)
	, FontMeasureService(FSlateApplication::Get().GetRenderer()->GetFontMeasureService())
	, Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
{
	Track.ResetCache();
	Track.NumLogMessages = 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimeMarkerTrackBuilder::BeginLog(const Trace::ILogProvider& LogProvider)
{
	LogProviderPtr = &LogProvider;

	LastX1 = -1000.0f;
	LastX2 = -1000.0f;
	LastLogIndex = 0;
	LastVerbosity = ELogVerbosity::NoLogging;
	LastCategory = nullptr;
	LastMessage = nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimeMarkerTrackBuilder::AddLogMessage(const Trace::FLogMessage& Message)
{
	Track.NumLogMessages++;

	// Add also the log message imediately on the left of the screen (if any).
	if (Track.NumLogMessages == 1 && Message.Index > 0)
	{
		// Note: Reading message at Index-1 will not work as expected when using filter!
		//TODO: Search API like: LogProviderPtr->SearchMessage(StartIndex, ESearchDirection::Backward, LambdaPredicate, bResolveFormatString);
		LogProviderPtr->ReadMessage(
			Message.Index - 1,
			[this](const Trace::FLogMessage& Message) { AddLogMessage(Message); });
	}

	if (!Track.bUseOnlyBookmarks || FCString::Strcmp(Message.Category->Name, TEXT("LogBookmark")) == 0)
	{
		float X = Viewport.TimeToSlateUnitsRounded(Message.Time);
		if (X < 0)
		{
			X = -1.0f;
		}
		AddTimeMarker(X, Message.Index, Message.Verbosity, Message.Category->Name, Message.Message);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLinearColor FTimeMarkerTrackBuilder::GetColorByCategory(const TCHAR* const Category)
{
	// Strip the "Log" prefix.
	FString CategoryStr(Category);
	if (CategoryStr.StartsWith(TEXT("Log")))
	{
		CategoryStr.RightChopInline(3, false);
	}

	uint32 Hash = 0;
	for (const TCHAR* c = *CategoryStr; *c; ++c)
	{
		Hash = (Hash + *c) * 0x2c2c57ed;
	}

	// Divided by 128.0 in order to force bright colors.
	return FLinearColor(((Hash >> 16) & 0xFF) / 128.0f, ((Hash >> 8) & 0xFF) / 128.0f, (Hash & 0xFF) / 128.0f, 1.0f);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLinearColor FTimeMarkerTrackBuilder::GetColorByVerbosity(const ELogVerbosity::Type Verbosity)
{
	static FLinearColor Colors[] =
	{
		FLinearColor(0.0, 0.0, 0.0, 1.0), // NoLogging
		FLinearColor(1.0, 0.0, 0.0, 1.0), // Fatal
		FLinearColor(1.0, 0.3, 0.0, 1.0), // Error
		FLinearColor(0.7, 0.5, 0.0, 1.0), // Warning
		FLinearColor(0.0, 0.7, 0.0, 1.0), // Display
		FLinearColor(0.0, 0.7, 1.0, 1.0), // Log
		FLinearColor(0.7, 0.7, 0.7, 1.0), // Verbose
		FLinearColor(1.0, 1.0, 1.0, 1.0), // VeryVerbose
	};
	static_assert(sizeof(Colors) / sizeof(FLinearColor) == (int)ELogVerbosity::Type::All + 1, "ELogVerbosity::Type has changed!?");
	//return Colors[Verbosity & ELogVerbosity::VerbosityMask];
	return Colors[Verbosity & 7]; // using 7 instead of ELogVerbosity::VerbosityMask (15) to make static analyzer happy
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimeMarkerTrackBuilder::Flush(float AvailableTextW)
{
	// Is last marker valid?
	if (LastCategory != nullptr)
	{
		const FLinearColor Color = GetColorByCategory(LastCategory);

		bool bAddNewBox = true;
		if (Track.TimeMarkerBoxes.Num() > 0)
		{
			FTimeMarkerBoxInfo& PrevBox = Track.TimeMarkerBoxes[Track.TimeMarkerBoxes.Num() - 1];
			if (PrevBox.X + PrevBox.W == LastX1 &&
				PrevBox.Color.R == Color.R &&
				PrevBox.Color.G == Color.G &&
				PrevBox.Color.B == Color.B)
			{
				// Extend previous box instead.
				PrevBox.W += LastX2 - LastX1;
				bAddNewBox = false;
			}
		}

		if (bAddNewBox)
		{
			// Add new Box info to cache.
			FTimeMarkerBoxInfo& Box = Track.TimeMarkerBoxes[Track.TimeMarkerBoxes.AddDefaulted()];
			Box.X = LastX1;
			Box.W = LastX2 - LastX1;
			Box.Color = Color;
			Box.Color.A = 0.25f;
		}

		if (AvailableTextW > 6.0f)
		{
			// Strip the "Log" prefix.
			FString CategoryStr(LastCategory);
			if (CategoryStr.StartsWith(TEXT("Log")))
			{
				CategoryStr.RightChopInline(3, false);
			}

			const int32 LastWholeCharacterIndexCategory = FontMeasureService->FindLastWholeCharacterIndexBeforeOffset(CategoryStr, Font, FMath::RoundToInt(AvailableTextW - 2.0f));
			const int32 LastWholeCharacterIndexMessage = FontMeasureService->FindLastWholeCharacterIndexBeforeOffset(LastMessage, Font, FMath::RoundToInt(AvailableTextW - 2.0f));

			if (LastWholeCharacterIndexCategory >= 0 ||
				LastWholeCharacterIndexMessage >= 0)
			{
				// Add new Text info to cache.
				FTimeMarkerTextInfo& TextInfo = Track.TimeMarkerTexts[Track.TimeMarkerTexts.AddDefaulted()];
				TextInfo.X = LastX2 + 2.0f;
				TextInfo.Color = Color;
				if (LastWholeCharacterIndexCategory >= 0)
				{
					TextInfo.Category = CategoryStr.Left(LastWholeCharacterIndexCategory + 1);
				}
				if (LastWholeCharacterIndexMessage >= 0)
				{
					TextInfo.Message.AppendChars(LastMessage, LastWholeCharacterIndexMessage + 1);
				}
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimeMarkerTrackBuilder::AddTimeMarker(const float X, const uint64 LogIndex, const ELogVerbosity::Type Verbosity, const TCHAR* const Category, const TCHAR* Message)
{
	const float W = X - LastX2;

	if (W > 0.0f) // There is at least 1px from previous box?
	{
		// Flush previous marker (if any).
		Flush(W);

		// Begin new marker info.
		LastX1 = X;
		LastX2 = X + 1.0f;
	}
	else if (W == 0.0f) // Adjacent to previous box?
	{
		// Same color as previous marker?
		if (Category == LastCategory)
		{
			// Extend previous box.
			LastX2++;
		}
		else
		{
			// Flush previous marker (if any).
			Flush(0.0f);

			// Begin new box.
			LastX1 = X;
			LastX2 = X + 1.0f;
		}
	}
	else // Overlaps previous box?
	{
		// Same color as previous marker?
		if (Category == LastCategory)
		{
			// Keep previous box.
		}
		else
		{
			// Shrink previous box.
			LastX2--;

			if (LastX2 > LastX1)
			{
				// Flush previous marker (if any).
				Flush(0.0f);
			}

			// Begin new box.
			LastX1 = X;
			LastX2 = X + 1.0f;
		}
	}

	// Save marker.
	LastCategory = Category;
	LastVerbosity = Verbosity;
	LastLogIndex = LogIndex;
	LastMessage = Message;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimeMarkerTrackBuilder::EndLog()
{
	Flush(Viewport.GetWidth() - LastX2);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
