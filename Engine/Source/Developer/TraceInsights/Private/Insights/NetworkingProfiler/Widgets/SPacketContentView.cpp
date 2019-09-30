// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SPacketContentView.h"

#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SScrollBar.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/Common/TimeUtils.h"
#include "Insights/Common/Stopwatch.h"
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/NetworkingProfiler/Widgets/SNetworkingProfilerWindow.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "SPacketContentView"

////////////////////////////////////////////////////////////////////////////////////////////////////

SPacketContentView::SPacketContentView()
	: ProfilerWindow()
	, DrawState(MakeShareable(new FPacketContentViewDrawState()))
{
	Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SPacketContentView::~SPacketContentView()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::Reset()
{
	//ProfilerWindow

	Viewport.Reset();
	//FAxisViewportDouble& ViewportX = Viewport.GetHorizontalAxisViewport();
	//ViewportX.SetScaleLimits(0.000001, 100000.0);
	//ViewportX.SetScale(1.0);
	bIsViewportDirty = true;

	GameInstanceIndex = 0;
	ConnectionIndex = 0;
	ConnectionMode = Trace::ENetProfilerConnectionMode::Outgoing;
	PacketIndex = 0;
	PacketBitSize = 0;

	DrawState->Reset();
	bIsStateDirty = true;

	MousePosition = FVector2D::ZeroVector;

	MousePositionOnButtonDown = FVector2D::ZeroVector;
	ViewportPosXOnButtonDown = 0.0f;

	MousePositionOnButtonUp = FVector2D::ZeroVector;

	bIsLMB_Pressed = false;
	bIsRMB_Pressed = false;

	bIsScrolling = false;

	HoveredEvent.Reset();
	SelectedEvent.Reset();
	Tooltip.Reset();

	//ThisGeometry

	CursorType = ECursorType::Default;

	UpdateDurationHistory.Reset();
	DrawDurationHistory.Reset();
	OnPaintDurationHistory.Reset();
	LastOnPaintTime = FPlatformTime::Cycles64();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::Construct(const FArguments& InArgs, TSharedPtr<SNetworkingProfilerWindow> InProfilerWindow)
{
	ProfilerWindow = InProfilerWindow;

	ChildSlot
	[
		SNew(SOverlay)
		.Visibility(EVisibility::SelfHitTestInvisible)

		+ SOverlay::Slot()
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(0, 0, 0, 0))
		[
			SAssignNew(HorizontalScrollBar, SScrollBar)
			.Orientation(Orient_Horizontal)
			.AlwaysShowScrollbar(false)
			.Visibility(EVisibility::Visible)
			.Thickness(FVector2D(5.0f, 5.0f))
			.RenderOpacity(0.75)
			.OnUserScrolled(this, &SPacketContentView::HorizontalScrollBar_OnUserScrolled)
		]
	];

	UpdateHorizontalScrollBar();

	BindCommands();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (ThisGeometry != AllottedGeometry || bIsViewportDirty)
	{
		bIsViewportDirty = false;
		const float ViewWidth = AllottedGeometry.GetLocalSize().X;
		const float ViewHeight = AllottedGeometry.GetLocalSize().Y;
		Viewport.SetSize(ViewWidth, ViewHeight);
		bIsStateDirty = true;
	}

	ThisGeometry = AllottedGeometry;

	FAxisViewportDouble& ViewportX = Viewport.GetHorizontalAxisViewport();

	if (!bIsScrolling)
	{
		// Elastic snap to horizontal limits.
		if (ViewportX.UpdatePosWithinLimits())
		{
			bIsStateDirty = true;
		}
	}

	if (bIsStateDirty)
	{
		bIsStateDirty = false;
		UpdateState();
	}

	Tooltip.Update();
	if (!MousePosition.IsZero())
	{
		Tooltip.SetPosition(MousePosition, 0.0f, Viewport.GetWidth(), 0.0f, Viewport.GetHeight() - 12.0f);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::ResetPacket()
{
	GameInstanceIndex = 0;
	ConnectionIndex = 0;
	ConnectionMode = Trace::ENetProfilerConnectionMode::Outgoing;
	PacketIndex = 0;
	PacketBitSize = 0;

	FAxisViewportDouble& ViewportX = Viewport.GetHorizontalAxisViewport();
	ViewportX.SetMinMaxValueInterval(0.0, 0.0);
	ViewportX.CenterOnValue(0.0);
	UpdateHorizontalScrollBar();

	DrawState->Reset();
	bIsStateDirty = true;

	HoveredEvent.Reset();
	SelectedEvent.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::SetPacket(uint32 InGameInstanceIndex, uint32 InConnectionIndex, Trace::ENetProfilerConnectionMode InConnectionMode, uint32 InPacketIndex, int64 InPacketBitSize)
{
	GameInstanceIndex = InGameInstanceIndex;
	ConnectionIndex = InConnectionIndex;
	ConnectionMode = InConnectionMode;
	PacketIndex = InPacketIndex;
	PacketBitSize = InPacketBitSize;

	FAxisViewportDouble& ViewportX = Viewport.GetHorizontalAxisViewport();
	ViewportX.SetMinMaxValueInterval(0.0, static_cast<double>(InPacketBitSize));
	ViewportX.CenterOnValueInterval(0.0, static_cast<double>(InPacketBitSize));
	UpdateHorizontalScrollBar();

	DrawState->Reset();
	bIsStateDirty = true;

	HoveredEvent.Reset();
	SelectedEvent.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::UpdateState()
{
	FStopwatch Stopwatch;
	Stopwatch.Start();

	if (PacketBitSize > 0)
	{
		FPacketContentViewDrawStateBuilder Builder(*DrawState, Viewport);

		TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid())
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
			const Trace::INetProfilerProvider& NetProfilerProvider = Trace::ReadNetProfilerProvider(*Session.Get());

			//uint32 StartEventIndex;
			//uint32 EndEventIndex;
			//NetProfilerProvider.ReadConnection(ConnectionIndex, [&StartEventIndex, &EndEventIndex](const Trace::FNetProfilerConnection& Connection)
			//{
			//	StartEventIndex = Connection.StartEventIndex;
			//	EndEventIndex = Connection.EndEventIndex;
			//});
			//NetProfilerProvider.EnumeratePacketContentEventsByIndex(ConnectionIndex, ConnectionMode, StartEventIndex, uint32 EndEventIndex, [](const Trace::FNetProfilerContentEvent& Event)
			//{
			//});

			const FAxisViewportDouble& ViewportX = Viewport.GetHorizontalAxisViewport();

			const int64 MinPos = static_cast<int64>(FMath::FloorToDouble(ViewportX.GetValueAtOffset(0.0f)));
			const int64 MaxPos = static_cast<int64>(FMath::CeilToDouble(ViewportX.GetValueAtOffset(ViewportX.GetSize())));

			const uint32 StartPos = 0;
			const uint32 EndPos = PacketBitSize;
			NetProfilerProvider.EnumeratePacketContentEventsByPosition(ConnectionIndex, ConnectionMode, PacketIndex, StartPos, EndPos, [this, &Builder, &NetProfilerProvider](const Trace::FNetProfilerContentEvent& Event)
			{
				const TCHAR* Name = nullptr;
				NetProfilerProvider.ReadName(Event.NameIndex, [&Name](const Trace::FNetProfilerName& NetProfilerName)
				{
					Name = NetProfilerName.Name;
				});

				uint32 NetId = 0;
				if (Event.ObjectInstanceIndex != 0)
				{
					NetProfilerProvider.ReadObject(GameInstanceIndex, Event.ObjectInstanceIndex, [&NetId](const Trace::FNetProfilerObjectInstance& ObjectInstance)
					{
						NetId = ObjectInstance.NetId;
					});
				}

				Builder.AddEvent(Event, Name, NetId);
			});
		}

		Builder.Flush();
	}

	Stopwatch.Stop();
	UpdateDurationHistory.AddValue(Stopwatch.AccumulatedTime);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::UpdateHoveredEvent()
{
	HoveredEvent = GetEventAtMousePosition(MousePosition.X, MousePosition.Y);
	//if (!HoveredEvent.IsValid())
	//{
	//	HoveredEvent = GetEventAtMousePosition(MousePosition.X - 1.0f, MousePosition.Y);
	//}
	//if (!HoveredEvent.IsValid())
	//{
	//	HoveredEvent = GetEventAtMousePosition(MousePosition.X + 1.0f, MousePosition.Y);
	//}

	if (HoveredEvent.IsValid())
	{
		// Init the tooltip's content.
		Tooltip.ResetContent();

		const FNetworkPacketEvent& Event = HoveredEvent.Event;
		FString Name(TEXT("?"));
		Trace::FNetProfilerEventType EventType;
		Trace::FNetProfilerObjectInstance ObjectInstance;

		TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid())
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
			const Trace::INetProfilerProvider& NetProfilerProvider = Trace::ReadNetProfilerProvider(*Session.Get());

			NetProfilerProvider.ReadEventType(Event.EventTypeIndex, [&EventType](const Trace::FNetProfilerEventType& InEventType)
			{
				EventType = InEventType;
			});

			NetProfilerProvider.ReadName(EventType.NameIndex, [&Name](const Trace::FNetProfilerName& NetProfilerName)
			{
				Name = NetProfilerName.Name;
			});

			if (Event.ObjectInstanceIndex != 0)
			{
				NetProfilerProvider.ReadObject(GameInstanceIndex, Event.ObjectInstanceIndex, [&ObjectInstance](const Trace::FNetProfilerObjectInstance& InObjectInstance)
				{
					ObjectInstance = InObjectInstance;
				});
			}
		}

		Tooltip.AddTitle(Name);

		if (Event.ObjectInstanceIndex != 0)
		{
			Tooltip.AddNameValueTextLine(TEXT("Net Id:"), FText::AsNumber(ObjectInstance.NetId).ToString());
			Tooltip.AddNameValueTextLine(TEXT("Type Id:"), FString::Printf(TEXT("0x%016X"), ObjectInstance.TypeId));
			Tooltip.AddNameValueTextLine(TEXT("Obj. LifeTime:"), FString::Format(TEXT("from {0} to {1}"),
				{ TimeUtils::FormatTimeAuto(ObjectInstance.LifeTime.Begin), TimeUtils::FormatTimeAuto(ObjectInstance.LifeTime.End) }));
		}

		Tooltip.AddNameValueTextLine(TEXT("Offset:"), FString::Format(TEXT("bit {0}"), { FText::AsNumber(Event.BitOffset).ToString() }));
		if (Event.BitSize == 1)
		{
			Tooltip.AddNameValueTextLine(TEXT("Size:"), TEXT("1 bit"));
		}
		else
		{
			Tooltip.AddNameValueTextLine(TEXT("Size:"), FString::Format(TEXT("{0} bits"), { FText::AsNumber(Event.BitSize).ToString() }));
		}

		Tooltip.AddNameValueTextLine(TEXT("Level:"), FText::AsNumber(Event.Level).ToString());

		Tooltip.UpdateLayout();

		Tooltip.SetDesiredOpacity(1.0f);
	}
	else
	{
		Tooltip.SetDesiredOpacity(0.0f);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::OnSelectedEventChanged()
{
	if (SelectedEvent.IsValid() && ProfilerWindow.IsValid())
	{
		// Select the node coresponding to net event type of selected net event instance.
		const uint64 EventTypeId = static_cast<uint64>(SelectedEvent.Event.EventTypeIndex);
		ProfilerWindow->SetSelectedEventType(EventTypeId);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::SelectHoveredEvent()
{
	SelectedEvent = HoveredEvent;
	//if (SelectedEvent.IsValid())
	//{
	//	LastSelectionType = ESelectionType::TimingEvent;
	//	BringIntoView(SelectedEvent.StartPos, SelectedEvent.EndPos);
	//}
	OnSelectedEventChanged();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FNetworkPacketEventRef SPacketContentView::GetEventAtMousePosition(float X, float Y)
{
	if (!bIsStateDirty)
	{
		for (const FNetworkPacketEvent& Event : DrawState->Events)
		{
			const FAxisViewportDouble& ViewportX = Viewport.GetHorizontalAxisViewport();

			const float EventX1 = ViewportX.GetRoundedOffsetForValue(static_cast<double>(Event.BitOffset));
			const float EventX2 = ViewportX.GetRoundedOffsetForValue(static_cast<double>(Event.BitOffset + Event.BitSize));

			constexpr float Y0 = 0.0f;
			constexpr float EventH = 14.0f;
			constexpr float EventDY = 2.0f;
			const float EventY = Y0 + (EventH + EventDY) * Event.Level;

			constexpr float ToleranceX = 1.0f;

			if (X >= EventX1 - ToleranceX && X <= EventX2 &&
				Y >= EventY - EventDY / 2 && Y < EventY + EventH + EventDY / 2)
			{
				return FNetworkPacketEventRef(Event);
			}
		}
	}
	return FNetworkPacketEventRef();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 SPacketContentView::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const ESlateDrawEffect DrawEffects = bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	FDrawContext DrawContext(AllottedGeometry, MyCullingRect, InWidgetStyle, DrawEffects, OutDrawElements, LayerId);

	const float ViewWidth = AllottedGeometry.Size.X;
	const float ViewHeight = AllottedGeometry.Size.Y;

	//////////////////////////////////////////////////
	{
		FStopwatch Stopwatch;
		Stopwatch.Start();

		FPacketContentViewDrawHelper Helper(DrawContext, Viewport);

		Helper.DrawBackground();

		// Draw the cached state (the network events of the package breakdown).
		Helper.Draw(*DrawState);

		if (!FNetworkPacketEventRef::AreEquals(SelectedEvent, HoveredEvent))
		{
			// Highlight the selected event (if any).
			if (SelectedEvent.IsValid())
			{
				Helper.DrawEventHighlight(SelectedEvent.Event, FPacketContentViewDrawHelper::EHighlightMode::Selected);
			}

			// Highlight the hovered event (if any).
			if (HoveredEvent.IsValid())
			{
				Helper.DrawEventHighlight(HoveredEvent.Event, FPacketContentViewDrawHelper::EHighlightMode::Hovered);
			}
		}
		else
		{
			// Highlight the selected and hovered event (if any).
			if (SelectedEvent.IsValid())
			{
				Helper.DrawEventHighlight(SelectedEvent.Event, FPacketContentViewDrawHelper::EHighlightMode::SelectedAndHovered);
			}
		}

		// Draw tooltip for hovered Event.
		Tooltip.Draw(DrawContext);

		Stopwatch.Stop();
		DrawDurationHistory.AddValue(Stopwatch.AccumulatedTime);
	}
	//////////////////////////////////////////////////

	const bool bShouldDisplayDebugInfo = FInsightsManager::Get()->IsDebugInfoEnabled();
	if (bShouldDisplayDebugInfo)
	{
		const FSlateBrush* WhiteBrush = FInsightsStyle::Get().GetBrush("WhiteBrush");
		FSlateFontInfo SummaryFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);

		const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const float MaxFontCharHeight = FontMeasureService->Measure(TEXT("!"), SummaryFont).Y;
		const float DbgDY = MaxFontCharHeight;

		const float DbgW = 280.0f;
		const float DbgH = DbgDY * 5 + 3.0f;
		const float DbgX = ViewWidth - DbgW - 20.0f;
		float DbgY = 7.0f;

		DrawContext.LayerId++;

		DrawContext.DrawBox(DbgX - 2.0f, DbgY - 2.0f, DbgW, DbgH, WhiteBrush, FLinearColor(1.0, 1.0, 1.0, 0.9));
		DrawContext.LayerId++;

		FLinearColor DbgTextColor(0.0, 0.0, 0.0, 0.9);

		// Time interval since last OnPaint call.
		const uint64 CurrentTime = FPlatformTime::Cycles64();
		const uint64 OnPaintDuration = CurrentTime - LastOnPaintTime;
		LastOnPaintTime = CurrentTime;
		OnPaintDurationHistory.AddValue(OnPaintDuration);
		const uint64 AvgOnPaintDuration = OnPaintDurationHistory.ComputeAverage();
		const uint64 AvgOnPaintDurationMs = FStopwatch::Cycles64ToMilliseconds(AvgOnPaintDuration);
		const double AvgOnPaintFps = AvgOnPaintDurationMs != 0 ? 1.0 / FStopwatch::Cycles64ToSeconds(AvgOnPaintDuration) : 0.0;

		const uint64 AvgUpdateDurationMs = FStopwatch::Cycles64ToMilliseconds(UpdateDurationHistory.ComputeAverage());
		const uint64 AvgDrawDurationMs = FStopwatch::Cycles64ToMilliseconds(DrawDurationHistory.ComputeAverage());

		// Draw performance info.
		DrawContext.DrawText
		(
			DbgX, DbgY,
			FString::Printf(TEXT("U: %llu ms    D: %llu ms + %llu ms = %llu ms (%d fps)"),
				AvgUpdateDurationMs, // average duration of UpdateState calls
				AvgDrawDurationMs, // drawing time
				AvgOnPaintDurationMs - AvgDrawDurationMs, // other overhead to OnPaint calls
				AvgOnPaintDurationMs, // average time between two OnPaint calls
				FMath::RoundToInt(AvgOnPaintFps)), // framerate of OnPaint calls
			SummaryFont, DbgTextColor
		);
		DbgY += DbgDY;

		// Draw "the update stats".
		DrawContext.DrawText
		(
			DbgX, DbgY,
			FString::Printf(TEXT("U: %s events"),
				*FText::AsNumber(DrawState->Events.Num()).ToString()),
			SummaryFont, DbgTextColor
		);
		DbgY += DbgDY;

		// Draw "the draw stats".
		DrawContext.DrawText
		(
			DbgX, DbgY,
			FString::Printf(TEXT("D: %s boxes, %s borders (%s merged), %s texts"),
				*FText::AsNumber(DrawState->Boxes.Num()).ToString(),
				*FText::AsNumber(DrawState->Borders.Num()).ToString(),
				*FText::AsNumber(DrawState->GetNumMergedBoxes()).ToString(),
				*FText::AsNumber(DrawState->Texts.Num()).ToString()),
			SummaryFont, DbgTextColor
		);
		DbgY += DbgDY;

		// Draw viewport's horizontal info.
		DrawContext.DrawText
		(
			DbgX, DbgY,
			Viewport.GetHorizontalAxisViewport().ToDebugString(TEXT("X")),
			SummaryFont, DbgTextColor
		);
		DbgY += DbgDY;

		// Draw packet info.
		DrawContext.DrawText
		(
			DbgX, DbgY,
			FString::Printf(TEXT("Game Instance %d, Connection %d (%s), Packet %d"),
				GameInstanceIndex,
				ConnectionIndex,
				(ConnectionMode == Trace::ENetProfilerConnectionMode::Outgoing) ? TEXT("Outgoing") : TEXT("Incoming"),
				PacketIndex),
			SummaryFont, DbgTextColor
		);
		DbgY += DbgDY;
	}

	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled && IsEnabled());
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SPacketContentView::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	MousePosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	MousePositionOnButtonDown = MousePosition;

	ViewportPosXOnButtonDown = Viewport.GetHorizontalAxisViewport().GetPos();

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bIsLMB_Pressed = true;

		// Capture mouse.
		Reply = FReply::Handled().CaptureMouse(SharedThis(this));
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		bIsRMB_Pressed = true;

		// Capture mouse, so we can drag outside this widget.
		Reply = FReply::Handled().CaptureMouse(SharedThis(this));
	}

	return Reply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SPacketContentView::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	MousePosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	MousePositionOnButtonUp = MousePosition;

	const bool bIsValidForMouseClick = MousePositionOnButtonUp.Equals(MousePositionOnButtonDown, MOUSE_SNAP_DISTANCE);

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (bIsLMB_Pressed)
		{
			if (bIsScrolling)
			{
				bIsScrolling = false;
				CursorType = ECursorType::Default;
			}
			else if (bIsValidForMouseClick)
			{
				// Select the hovered timing event (if any).
				UpdateHoveredEvent();
				SelectHoveredEvent();
			}

			// Release mouse as we no longer drag.
			Reply = FReply::Handled().ReleaseMouseCapture();

			bIsLMB_Pressed = false;
		}
	}
	else if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		if (bIsRMB_Pressed)
		{
			if (bIsScrolling)
			{
				bIsScrolling = false;
				CursorType = ECursorType::Default;
			}
			else if (bIsValidForMouseClick)
			{
				//ShowContextMenu(MouseEvent);
			}

			// Release mouse as we no longer drag.
			Reply = FReply::Handled().ReleaseMouseCapture();

			bIsRMB_Pressed = false;
		}
	}

	return Reply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SPacketContentView::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = FReply::Unhandled();

	MousePosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	if (!MouseEvent.GetCursorDelta().IsZero())
	{
		if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) ||
			MouseEvent.IsMouseButtonDown(EKeys::RightMouseButton))
		{
			if (HasMouseCapture())
			{
				if (!bIsScrolling)
				{
					bIsScrolling = true;
					CursorType = ECursorType::Hand;

					HoveredEvent.Reset();
					Tooltip.SetDesiredOpacity(0.0f);
				}

				FAxisViewportDouble& ViewportX = Viewport.GetHorizontalAxisViewport();
				const float PosX = ViewportPosXOnButtonDown + (MousePositionOnButtonDown.X - MousePosition.X);
				ViewportX.ScrollAtPos(PosX);
				UpdateHorizontalScrollBar();
				bIsStateDirty = true;
			}
		}
		else
		{
			UpdateHoveredEvent();
		}

		Reply = FReply::Handled();
	}

	return Reply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	if (!HasMouseCapture())
	{
		// No longer dragging (unless we have mouse capture).
		bIsScrolling = false;

		bIsLMB_Pressed = false;
		bIsRMB_Pressed = false;

		MousePosition = FVector2D::ZeroVector;

		HoveredEvent.Reset();
		Tooltip.SetDesiredOpacity(0.0f);

		CursorType = ECursorType::Default;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SPacketContentView::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	MousePosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	//if (MouseEvent.GetModifierKeys().IsShiftDown())
	//{
	//}
	//else //if (MouseEvent.GetModifierKeys().IsControlDown())
	{
		// Zoom in/out horizontally.
		const float Delta = MouseEvent.GetWheelDelta();
		ZoomHorizontally(Delta, MousePosition.X);
	}

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SPacketContentView::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return FReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FCursorReply SPacketContentView::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	FCursorReply CursorReply = FCursorReply::Unhandled();

	if (CursorType == ECursorType::Arrow)
	{
		CursorReply = FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
	}
	else if (CursorType == ECursorType::Hand)
	{
		CursorReply = FCursorReply::Cursor(EMouseCursor::GrabHand);
	}

	return CursorReply;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::BindCommands()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::HorizontalScrollBar_OnUserScrolled(float ScrollOffset)
{
	FAxisViewportDouble& ViewportX = Viewport.GetHorizontalAxisViewport();
	ViewportX.OnUserScrolled(HorizontalScrollBar, ScrollOffset);
	bIsStateDirty = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::UpdateHorizontalScrollBar()
{
	FAxisViewportDouble& ViewportX = Viewport.GetHorizontalAxisViewport();
	ViewportX.UpdateScrollBar(HorizontalScrollBar);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::ZoomHorizontally(const float Delta, const float X)
{
	FAxisViewportDouble& ViewportX = Viewport.GetHorizontalAxisViewport();
	ViewportX.RelativeZoomWithFixedOffset(Delta, X);
	UpdateHorizontalScrollBar();
	bIsStateDirty = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
