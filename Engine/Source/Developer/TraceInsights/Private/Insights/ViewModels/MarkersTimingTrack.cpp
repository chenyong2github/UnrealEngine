// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MarkersTimingTrack.h"

#include "Brushes/SlateColorBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "TraceServices/AnalysisService.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/InsightsManager.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/ViewModels/TimingTrackViewport.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "MarkersTimingTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FMarkersTimingTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

FMarkersTimingTrack::FMarkersTimingTrack(uint64 InTrackId)
	: FBaseTimingTrack(InTrackId)
	//, TimeMarkerBoxes()
	//, TimeMarkerTexts()
	, bIsCollapsed(false)
	, bUseOnlyBookmarks(true)
	, bIsDirty(true)
	, TargetHoveredAnimPercent(0.0f)
	, CurrentHoveredAnimPercent(0.0f)
	, NumLogMessages(0)
	, NumDrawBoxes(0)
	, NumDrawTexts(0)
	, WhiteBrush(FCoreStyle::Get().GetBrush("WhiteBrush"))
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
	bIsDirty = true;

	TargetHoveredAnimPercent = 0.0f;
	CurrentHoveredAnimPercent = 0.0f;

	NumLogMessages = 0;
	NumDrawBoxes = 0;
	NumDrawTexts = 0;

	UpdateHeight();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMarkersTimingTrack::UpdateHoveredState(float MX, float MY, const FTimingTrackViewport& Viewport)
{
	if (MY >= Y && MY < Y + H)
	{
		bIsHovered = true;
		TargetHoveredAnimPercent = 1.0f;

		if (MX < 80.0f && MY < Y + 14.0f)
		{
			bIsHeaderHovered = true;
		}
		else
		{
			bIsHeaderHovered = false;
		}
	}
	else
	{
		bIsHovered = false;
		TargetHoveredAnimPercent = 0.0f;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMarkersTimingTrack::Tick(const double InCurrentTime, const float InDeltaTime)
{
	if (CurrentHoveredAnimPercent != TargetHoveredAnimPercent)
	{
		if (CurrentHoveredAnimPercent < TargetHoveredAnimPercent)
		{
			const float ShowAnimSpeed = 2 * InDeltaTime;
			CurrentHoveredAnimPercent += ShowAnimSpeed;
			if (CurrentHoveredAnimPercent > TargetHoveredAnimPercent)
			{
				CurrentHoveredAnimPercent = TargetHoveredAnimPercent;
			}
		}
		else
		{
			const float HideAnimSpeed = 3 * InDeltaTime;
			CurrentHoveredAnimPercent -= HideAnimSpeed;
			if (CurrentHoveredAnimPercent < TargetHoveredAnimPercent)
			{
				CurrentHoveredAnimPercent = TargetHoveredAnimPercent;
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMarkersTimingTrack::Update(const FTimingTrackViewport& InViewport)
{
	FTimeMarkerTrackBuilder CB(*this, InViewport);
	CB.Begin();

	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		Trace::FAnalysisSessionReadScope _(*Session.Get());

		Session->ReadLogProvider([&CB](const Trace::ILogProvider& LogProvider)
		{
			CB.BeginLog(LogProvider);

			LogProvider.EnumerateMessages(
				CB.GetViewport().StartTime,
				CB.GetViewport().EndTime,
				[&CB](const Trace::FLogMessage& Message) { CB.AddLogMessage(Message); },
				false);

			CB.EndLog();
		});
	}

	CB.End();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMarkersTimingTrack::Draw(FDrawContext& DC, const FTimingTrackViewport& Viewport) const
{
	// Draw background.
	DC.DrawBox(0.0f, Y, Viewport.Width, H, WhiteBrush, FLinearColor(0.04f, 0.04f, 0.04f, 1.0f));
	DC.LayerId++;

	// Draw the track's header, in background.
	if (!IsHovered() || CurrentHoveredAnimPercent < 1.0)
	{
		DrawHeader(DC, true);
	}

	//////////////////////////////////////////////////
	// Draw vertical lines.
	// Multiple adjacent vertical lines with same color are merged into a single box.

	float BoxY, BoxH;
	if (IsCollapsed())
	{
		BoxY = Y;
		BoxH = H;
	}
	else
	{
		BoxY = 0.0f;
		BoxH = Viewport.Height;
	}

	int32 NumBoxes = TimeMarkerBoxes.Num();
	for (int32 BoxIndex = 0; BoxIndex < NumBoxes; BoxIndex++)
	{
		const FTimeMarkerBoxInfo& Box = TimeMarkerBoxes[BoxIndex];
		DC.DrawBox(Box.X, BoxY, Box.W, BoxH, WhiteBrush, Box.Color);
		//NumDrawBoxes++;
	}
	DC.LayerId++;
	NumDrawBoxes = NumBoxes;

	//////////////////////////////////////////////////
	// Draw texts (strings are already truncated).

	const float CategoryY = Y + 2.0f;
	const float MessageY = Y + (IsBookmarksTrack() ? 1.0f : 14.0f);

	int32 NumTexts = TimeMarkerTexts.Num();
	for (int32 TextIndex = 0; TextIndex < NumTexts; TextIndex++)
	{
		const FTimeMarkerTextInfo& TextInfo = TimeMarkerTexts[TextIndex];

		if (!IsBookmarksTrack() && TextInfo.Category.Len() > 0)
		{
			DC.DrawText(TextInfo.X, CategoryY, TextInfo.Category, Font, TextInfo.Color);
			NumDrawTexts++;
		}

		if (TextInfo.Message.Len() > 0)
		{
			DC.DrawText(TextInfo.X, MessageY, TextInfo.Message, Font, TextInfo.Color);
			NumDrawTexts++;
		}
	}
	DC.LayerId++;
	//NumDrawTexts = NumTexts * 2;

	//////////////////////////////////////////////////

	// When hovered, the track's header is draw on top.
	if (IsHovered() || CurrentHoveredAnimPercent > 0.0)
	{
		DrawHeader(DC, false);
	}
}


////////////////////////////////////////////////////////////////////////////////////////////////////

void FMarkersTimingTrack::DrawHeader(FDrawContext& DC, bool bFirstDraw) const
{
	const FString Name = IsBookmarksTrack() ? TEXT("Bookmarks") : TEXT("Logs");
	//const float ArrowSize = 6.0f;
	const float ArrowSizeX = 4.0f;
	const float ArrowSizeY = 8.0f;
	const float ArrowX = IsBookmarksTrack() ? 64.0f : 31.0f;
	const float ArrowY = Y + 3.0f;
	const float HeaderW = ArrowX + ArrowSizeX + 4.0f;
	const float HeaderH = 14.0f;

	if (!bFirstDraw)
	{
		DC.DrawBox(0.0f, Y, HeaderW, HeaderH, WhiteBrush, FLinearColor(0.04f, 0.04f, 0.04f, CurrentHoveredAnimPercent));
		DC.LayerId++;
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
	DC.DrawText(2.0f, Y + 1.0f, Name, Font, Color);

	if (IsCollapsed())
	{
		// Draw "down arrow".
		//for (float A = 0.0f; A < ArrowSize; A += 1.0f)
		//{
		//	DC.DrawBox(ArrowX - A, ArrowY + ArrowSize - A , 2.0f * A + 1.0f, 1.0f, WhiteBrush, Color);
		//}

		// Draw "right empty arrow".
		TArray<FVector2D> Points;
		Points.SetNum(4);
		Points[0] = FVector2D(ArrowX, ArrowY);
		Points[1] = FVector2D(ArrowX, ArrowY + ArrowSizeY);
		Points[2] = FVector2D(ArrowX + ArrowSizeX, ArrowY + ArrowSizeY / 2.0f);
		Points[3] = FVector2D(ArrowX, ArrowY);
		FSlateDrawElement::MakeLines(DC.ElementList, DC.LayerId, DC.Geometry.ToPaintGeometry(), Points, DC.DrawEffects, Color, false, 1.0f);
	}
	else
	{
		// Draw "up arrow".
		//for (float A = 0.0f; A < ArrowSize; A += 1.0f)
		//{
		//	DC.DrawBox(ArrowX - A, ArrowY + A, 2.0f * A + 1.0f, 1.0f, WhiteBrush, Color);
		//}

		// Draw "down-right filled arrow".
		for (float A = 1.0f; A < ArrowSizeY; A += 1.0f)
		{
			DC.DrawBox(ArrowX - 3.0 + ArrowSizeY - A, ArrowY + A - 1.0f, A, 1.0f, WhiteBrush, Color);
		}
	}

	DC.LayerId++;
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
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimeMarkerTrackBuilder::Begin()
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
	LastCategory = nullptr;
	LastVerbosity = (ELogVerbosity::Type)0;
	LastLogIndex = 0;
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
			[this](const Trace::FLogMessage& Message) { AddLogMessage(Message); },
			false); // do not resolve format string
	}

	if (!Track.bUseOnlyBookmarks || FCString::Strcmp(*Message.Category->Name, TEXT("LogBookmark")) == 0)
	{
		float X = Viewport.TimeToSlateUnitsRounded(Message.Time);
		if (X < 0)
		{
			X = -1.0f;
		}
		AddTimeMarker(X, *Message.Category->Name, Message.Verbosity, Message.Index);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FLinearColor FTimeMarkerTrackBuilder::GetColorByCategory(const TCHAR* const Category)
{
	// Strip the "Log" prefix.
	FString CategoryStr(Category);
	if (CategoryStr.StartsWith(TEXT("Log")))
		CategoryStr = CategoryStr.RightChop(3);

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
			Box.Color.A = 0.5f;
		}

		if (AvailableTextW > 6.0f)
		{
			LogProviderPtr->ReadMessage(
				LastLogIndex,
				[this, AvailableTextW, &Color](const Trace::FLogMessage& Message)
			{
				// Strip the "Log" prefix.
				FString CategoryStr(Message.Category->Name);
				if (CategoryStr.StartsWith(TEXT("Log")))
					CategoryStr = CategoryStr.RightChop(3);

				const int32 LastWholeCharacterIndexCategory = FontMeasureService->FindLastWholeCharacterIndexBeforeOffset(CategoryStr, Font, FMath::RoundToInt(AvailableTextW - 2.0f));
				const int32 LastWholeCharacterIndexMessage = FontMeasureService->FindLastWholeCharacterIndexBeforeOffset(Message.Message, Font, FMath::RoundToInt(AvailableTextW - 2.0f));

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
						TextInfo.Message.AppendChars(Message.Message, LastWholeCharacterIndexMessage + 1);
					}
				}
			},
				true); // resolve format string
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimeMarkerTrackBuilder::AddTimeMarker(const float X, const TCHAR* const Category, const ELogVerbosity::Type Verbosity, const uint64 LogIndex)
{
	float W = X - LastX2;

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
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimeMarkerTrackBuilder::EndLog()
{
	Flush(Viewport.Width - LastX2);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTimeMarkerTrackBuilder::End()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
