// Copyright Epic Games, Inc. All Rights Reserved.

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
// FMarkersTimingTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

FMarkersTimingTrack::FMarkersTimingTrack()
	: FBaseTimingTrack(FName("Markers"))
	//, TimeMarkerBoxes()
	//, TimeMarkerTexts()
	, bIsCollapsed(false)
	, bUseOnlyBookmarks(true)
	, TargetHoveredAnimPercent(0.0f)
	, CurrentHoveredAnimPercent(0.0f)
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

	bIsCollapsed = false;
	bUseOnlyBookmarks = true;

	TargetHoveredAnimPercent = 0.0f;
	CurrentHoveredAnimPercent = 0.0f;

	NumLogMessages = 0;
	NumDrawBoxes = 0;
	NumDrawTexts = 0;

	UpdateHeight();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMarkersTimingTrack::UpdateHeight()
{
	constexpr float BookmarksTrackHeight = 14.0f;
	constexpr float TimeMarkersTrackHeight = 28.0f;

	if (bUseOnlyBookmarks)
	{
		SetHeight(BookmarksTrackHeight);
	}
	else
	{
		SetHeight(TimeMarkersTrackHeight);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMarkersTimingTrack::PostUpdate(const ITimingTrackUpdateContext& Context)
{
	constexpr float HeaderWidth = 80.0f;
	constexpr float HeaderHeight = 14.0f;

	const float MouseY = Context.GetMousePosition().Y;
	if (MouseY >= GetPosY() && MouseY < GetPosY() + GetHeight())
	{
		SetHoveredState(true);
		const float MouseX = Context.GetMousePosition().X;
		SetHeaderHoveredState(MouseX < HeaderWidth && MouseY < GetPosY() + HeaderHeight);
		TargetHoveredAnimPercent = 1.0f;
	}
	else
	{
		SetHoveredState(false);
		TargetHoveredAnimPercent = 0.0f;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMarkersTimingTrack::Update(const ITimingTrackUpdateContext& InContext)
{
	if (CurrentHoveredAnimPercent != TargetHoveredAnimPercent)
	{
		if (CurrentHoveredAnimPercent < TargetHoveredAnimPercent)
		{
			const float ShowAnimSpeed = 2 * InContext.GetDeltaTime();
			CurrentHoveredAnimPercent += ShowAnimSpeed;
			if (CurrentHoveredAnimPercent > TargetHoveredAnimPercent)
			{
				CurrentHoveredAnimPercent = TargetHoveredAnimPercent;
			}
		}
		else
		{
			const float HideAnimSpeed = 3 * InContext.GetDeltaTime();
			CurrentHoveredAnimPercent -= HideAnimSpeed;
			if (CurrentHoveredAnimPercent < TargetHoveredAnimPercent)
			{
				CurrentHoveredAnimPercent = TargetHoveredAnimPercent;
			}
		}
	}

	const FTimingTrackViewport& Viewport = InContext.GetViewport();
	if (IsDirty() || Viewport.IsHorizontalViewportDirty())
	{
		ClearDirtyFlag();

		UpdateDrawState(Viewport);
	}
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

	// Draw the track's header, in background.
	if (!IsHovered() || CurrentHoveredAnimPercent < 1.0f)
	{
		DrawHeader(DrawContext, true);
	}
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

	// When hovered, the track's header is draw on top.
	if (IsHovered() || CurrentHoveredAnimPercent > 0.0f)
	{
		DrawHeader(DrawContext, false);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMarkersTimingTrack::DrawHeader(FDrawContext& DrawContext, bool bFirstDraw) const
{
	const FString NameString = IsBookmarksTrack() ? TEXT("Bookmarks") : TEXT("Logs");
	//const float ArrowSize = 6.0f;
	const float ArrowSizeX = 4.0f;
	const float ArrowSizeY = 8.0f;
	const float ArrowX = IsBookmarksTrack() ? 64.0f : 31.0f;
	const float ArrowY = GetPosY() + 3.0f;
	const float HeaderW = ArrowX + ArrowSizeX + 4.0f;
	const float HeaderH = 14.0f;

	if (!bFirstDraw)
	{
		DrawContext.DrawBox(0.0f, GetPosY(), HeaderW, HeaderH, WhiteBrush, FLinearColor(0.04f, 0.04f, 0.04f, CurrentHoveredAnimPercent));
		DrawContext.LayerId++;
	}

	FLinearColor Color;
	if (bFirstDraw || CurrentHoveredAnimPercent == 0.0)
	{
		Color = FLinearColor(0.07f, 0.07f, 0.07f, 1.0f);
	}
	else if (IsHeaderHovered())
	{
		Color = FLinearColor(1.0f, 1.0f, 0.0f, CurrentHoveredAnimPercent);
	}
	else
	{
		Color = FLinearColor(1.0f, 1.0f, 1.0f, CurrentHoveredAnimPercent);
	}

	// Draw "Bookmarks" or "Logs" text.
	DrawContext.DrawText(2.0f, GetPosY() + 1.0f, NameString, Font, Color);

	if (IsCollapsed())
	{
		// Draw "right empty arrow".
		TArray<FVector2D> Points =
		{
			FVector2D(ArrowX, ArrowY),
			FVector2D(ArrowX, ArrowY + ArrowSizeY),
			FVector2D(ArrowX + ArrowSizeX, ArrowY + ArrowSizeY / 2.0f),
			FVector2D(ArrowX, ArrowY)
		};
		FSlateDrawElement::MakeLines(DrawContext.ElementList, DrawContext.LayerId, DrawContext.Geometry.ToPaintGeometry(), Points, DrawContext.DrawEffects, Color, false, 1.0f);
	}
	else
	{
		// Draw "down-right filled arrow".
		for (float A = 1.0f; A < ArrowSizeY; A += 1.0f)
		{
			DrawContext.DrawBox(ArrowX - 3.0 + ArrowSizeY - A, ArrowY + A - 1.0f, A, 1.0f, WhiteBrush, Color);
		}
	}

	DrawContext.LayerId++;
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
