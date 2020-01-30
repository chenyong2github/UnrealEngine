// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPacketContentView.h"

#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SScrollBar.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/Common/TimeUtils.h"
#include "Insights/Common/Stopwatch.h"
#include "Insights/InsightsManager.h"
#include "Insights/InsightsStyle.h"
#include "Insights/NetworkingProfiler/Widgets/SNetworkingProfilerWindow.h"
#include "Insights/NetworkingProfiler/Widgets/SPacketView.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#define LOCTEXT_NAMESPACE "SPacketContentView"

////////////////////////////////////////////////////////////////////////////////////////////////////

SPacketContentView::SPacketContentView()
	: ProfilerWindow()
	, DrawState(MakeShared<FPacketContentViewDrawState>())
	, FilteredDrawState(MakeShared<FPacketContentViewDrawState>())
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

	bFilterByEventType = false;
	FilterEventTypeIndex = 0;
	FilterEventName = FText::GetEmpty();

	bFilterByNetId = false;
	FilterNetId = 0;

	bHighlightFilteredEvents = false;

	DrawState->Reset();
	FilteredDrawState->Reset();
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
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Top)
		.Padding(FMargin(0, 0, 0, 0))
		[
			SNew(SHorizontalBox)

			//////////////////////////////////////////////////
			// Find Packet

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("FindPacketText", "Find Packet:"))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ToolTipText(LOCTEXT("PreviousPacketToolTip", "Previous Packet"))
				.ContentPadding(0.0f)
				.OnClicked(this, &SPacketContentView::FindPreviousPacket_OnClicked)
				.Content()
				[
					SNew(SImage)
					.Image(FInsightsStyle::GetBrush("FindPrevious"))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SEditableTextBox)
				.RevertTextOnEscape(true)
				.SelectAllTextWhenFocused(true)
				.Text(this, &SPacketContentView::GetPacketText)
				.OnTextCommitted(this, &SPacketContentView::Packet_OnTextCommitted)
				.MinDesiredWidth(30.0f)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ToolTipText(LOCTEXT("NextPacketToolTip", "Next Packet"))
				.ContentPadding(0.0f)
				.OnClicked(this, &SPacketContentView::FindNextPacket_OnClicked)
				.Content()
				[
					SNew(SImage)
					.Image(FInsightsStyle::GetBrush("FindNext"))
				]
			]

			//////////////////////////////////////////////////
			// Find Event

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(12.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("FindEventText", "Find Event:"))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ToolTipText(LOCTEXT("FindFirstEventToolTip", "First Event"))
				.ContentPadding(0.0f)
				.OnClicked(this, &SPacketContentView::FindFirstEvent)
				.Content()
				[
					SNew(SImage)
					.Image(FInsightsStyle::GetBrush("FindFirst"))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ToolTipText(LOCTEXT("FindPreviousEventToolTip", "Previous Event"))
				.ContentPadding(0.0f)
				.OnClicked(this, &SPacketContentView::FindPreviousEvent)
				.Content()
				[
					SNew(SImage)
					.Image(FInsightsStyle::GetBrush("FindPrevious"))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ToolTipText(LOCTEXT("FindNextEventToolTip", "Next Event"))
				.ContentPadding(0.0f)
				.OnClicked(this, &SPacketContentView::FindNextEvent)
				.Content()
				[
					SNew(SImage)
					.Image(FInsightsStyle::GetBrush("FindNext"))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ToolTipText(LOCTEXT("FindLastEventToolTip", "Last Event"))
				.ContentPadding(0.0f)
				.OnClicked(this, &SPacketContentView::FindLastEvent)
				.Content()
				[
					SNew(SImage)
					.Image(FInsightsStyle::GetBrush("FindLast"))
				]
			]

			//////////////////////////////////////////////////
			// By NetId

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(12.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("FilterByNetId_Tooltip", "Filter events that have the specified NetId."))
				.IsChecked(this, &SPacketContentView::FilterByNetId_IsChecked)
				.OnCheckStateChanged(this, &SPacketContentView::FilterByNetId_OnCheckStateChanged)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("FilterByNetId_Text", "By NetId:"))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SEditableTextBox)
				.RevertTextOnEscape(true)
				.SelectAllTextWhenFocused(true)
				.Text(this, &SPacketContentView::GetFilterNetIdText)
				.OnTextCommitted(this, &SPacketContentView::FilterNetId_OnTextCommitted)
				.MinDesiredWidth(40.0f)
			]

			//////////////////////////////////////////////////
			// By Event Type

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(12.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("FilterByEventType_Tooltip",
					"Filter events that have the specified type.\n\n"
					"To set the event type:\n"
					"\tdouble click either an event in the Packet Content view\n"
					"\tor an event type in the NetStats tree view."))
				.IsChecked(this, &SPacketContentView::FilterByEventType_IsChecked)
				.OnCheckStateChanged(this, &SPacketContentView::FilterByEventType_OnCheckStateChanged)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("FilterByEventType_Text", "By Type:"))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SEditableTextBox)
				.Text(this, &SPacketContentView::GetFilterEventTypeText)
				.IsReadOnly(true)
				.MinDesiredWidth(120.0f)
			]

			//////////////////////////////////////////////////
			// Highlight Filtered Events

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(12.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("HighlightFilteredEvents_Tooltip", "Highlight filtered events."))
				.IsChecked(this, &SPacketContentView::HighlightFilteredEvents_IsChecked)
				.OnCheckStateChanged(this, &SPacketContentView::HighlightFilteredEvents_OnCheckStateChanged)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("HighlightFilteredEvents_Text", "Highlight"))
				]
			]

			//////////////////////////////////////////////////
		]

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

FReply SPacketContentView::FindPreviousPacket_OnClicked()
{
	TSharedPtr<SPacketView> PacketView = ProfilerWindow->GetPacketView();
	if (PacketView.IsValid())
	{
		PacketView->SelectPreviousPacket();
	}

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SPacketContentView::FindNextPacket_OnClicked()
{
	TSharedPtr<SPacketView> PacketView = ProfilerWindow->GetPacketView();
	if (PacketView.IsValid())
	{
		PacketView->SelectNextPacket();
	}

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SPacketContentView::GetPacketText() const
{
	return FText::AsNumber(PacketIndex);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::Packet_OnTextCommitted(const FText& InNewText, ETextCommit::Type InTextCommit)
{
	if (InNewText.IsNumeric())
	{
		int32 NewPacketIndex = 0;
		TTypeFromString<int32>::FromString(NewPacketIndex, *InNewText.ToString());

		TSharedPtr<SPacketView> PacketView = ProfilerWindow->GetPacketView();
		if (PacketView.IsValid())
		{
			PacketView->SetSelectedPacket(NewPacketIndex);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SPacketContentView::FindFirstEvent()
{
	if (FilteredDrawState->Events.Num() > 0)
	{
		SelectedEvent.Set(FilteredDrawState->Events[0]);
		OnSelectedEventChanged();
		BringEventIntoView(SelectedEvent);
	}

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SPacketContentView::FindPreviousEvent()
{
	if (!SelectedEvent.IsValid())
	{
		return FindFirstEvent();
	}

	const int32 EventCount = FilteredDrawState->Events.Num();
	for (int32 EventIndex = EventCount - 1; EventIndex >= 0; --EventIndex)
	{
		const FNetworkPacketEvent& Event = FilteredDrawState->Events[EventIndex];
		if (Event.Equals(SelectedEvent.Event))
		{
			if (EventIndex > 0)
			{
				SelectedEvent.Set(FilteredDrawState->Events[EventIndex - 1]);
				OnSelectedEventChanged();
				break;
			}
		}
		else if (Event.BitOffset <= SelectedEvent.Event.BitOffset)
		{
			if (Event.BitOffset < SelectedEvent.Event.BitOffset || Event.Level < SelectedEvent.Event.Level)
			{
				SelectedEvent.Set(Event);
				OnSelectedEventChanged();
				break;
			}
		}
	}

	BringEventIntoView(SelectedEvent);
	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SPacketContentView::FindNextEvent()
{
	if (!SelectedEvent.IsValid())
	{
		return FindLastEvent();
	}

	const int32 EventCount = FilteredDrawState->Events.Num();
	for (int32 EventIndex = 0; EventIndex < EventCount; ++EventIndex)
	{
		const FNetworkPacketEvent& Event = FilteredDrawState->Events[EventIndex];
		if (Event.Equals(SelectedEvent.Event))
		{
			if (EventIndex < EventCount - 1)
			{
				SelectedEvent.Set(FilteredDrawState->Events[EventIndex + 1]);
				OnSelectedEventChanged();
				break;
			}
		}
		else if (Event.BitOffset >= SelectedEvent.Event.BitOffset)
		{
			if (Event.BitOffset > SelectedEvent.Event.BitOffset || Event.Level > SelectedEvent.Event.Level)
			{
				SelectedEvent.Set(Event);
				OnSelectedEventChanged();
				break;
			}
		}
	}

	BringEventIntoView(SelectedEvent);
	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SPacketContentView::FindLastEvent()
{
	if (FilteredDrawState->Events.Num() > 0)
	{
		SelectedEvent.Set(FilteredDrawState->Events.Last());
		OnSelectedEventChanged();
		BringEventIntoView(SelectedEvent);
	}

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ECheckBoxState SPacketContentView::FilterByNetId_IsChecked() const
{
	return bFilterByNetId ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::FilterByNetId_OnCheckStateChanged(ECheckBoxState NewState)
{
	bFilterByNetId = (NewState == ECheckBoxState::Checked);
	bIsStateDirty = true;

	TSharedPtr<SPacketView> PacketView = ProfilerWindow ? ProfilerWindow->GetPacketView() : nullptr;
	if (PacketView.IsValid())
	{
		PacketView->InvalidateState();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SPacketContentView::GetFilterNetIdText() const
{
	return FText::AsNumber(FilterNetId);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::FilterNetId_OnTextCommitted(const FText& InNewText, ETextCommit::Type InTextCommit)
{
	if (InNewText.IsNumeric())
	{
		int32 NewNetId = 0;
		TTypeFromString<int32>::FromString(NewNetId, *InNewText.ToString());
		SetFilterNetId(NewNetId);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ECheckBoxState SPacketContentView::FilterByEventType_IsChecked() const
{
	return bFilterByEventType ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::FilterByEventType_OnCheckStateChanged(ECheckBoxState NewState)
{
	bFilterByEventType = (NewState == ECheckBoxState::Checked);
	bIsStateDirty = true;

	TSharedPtr<SPacketView> PacketView = ProfilerWindow ? ProfilerWindow->GetPacketView() : nullptr;
	if (PacketView.IsValid())
	{
		PacketView->InvalidateState();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

ECheckBoxState SPacketContentView::HighlightFilteredEvents_IsChecked() const
{
	return bHighlightFilteredEvents ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::HighlightFilteredEvents_OnCheckStateChanged(ECheckBoxState NewState)
{
	bHighlightFilteredEvents = (NewState == ECheckBoxState::Checked);
	bIsStateDirty = true;
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
	FilteredDrawState->Reset();
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
	FilteredDrawState->Reset();
	bIsStateDirty = true;

	HoveredEvent.Reset();
	SelectedEvent.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::SetFilterNetId(const uint32 InNetId)
{
	FilterNetId = InNetId;

	if (bFilterByNetId)
	{
		bIsStateDirty = true;

		TSharedPtr<SPacketView> PacketView = ProfilerWindow ? ProfilerWindow->GetPacketView() : nullptr;
		if (PacketView.IsValid())
		{
			PacketView->InvalidateState();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::SetFilterEventType(const uint32 InEventTypeIndex, const FText& InEventName)
{
	FilterEventTypeIndex = InEventTypeIndex;
	FilterEventName = InEventName;

	if (bFilterByEventType)
	{
		bIsStateDirty = true;

		TSharedPtr<SPacketView> PacketView = ProfilerWindow ? ProfilerWindow->GetPacketView() : nullptr;
		if (PacketView.IsValid())
		{
			PacketView->InvalidateState();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::EnableFilterEventType(const uint32 InEventTypeIndex)
{
	FText EventName;

	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
		const Trace::INetProfilerProvider& NetProfilerProvider = Trace::ReadNetProfilerProvider(*Session.Get());

		NetProfilerProvider.ReadEventType(InEventTypeIndex, [&EventName](const Trace::FNetProfilerEventType& EventType)
		{
			EventName = FText::FromString(EventType.Name);
		});
	}

	bFilterByEventType = true;
	SetFilterEventType(InEventTypeIndex, EventName);
	bHighlightFilteredEvents = true;
	bIsStateDirty = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::DisableFilterEventType()
{
	bFilterByEventType = false;
	bHighlightFilteredEvents = false;
	bIsStateDirty = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::UpdateState()
{
	FStopwatch Stopwatch;
	Stopwatch.Start();

	if (PacketBitSize > 0)
	{
		FPacketContentViewDrawStateBuilder Builder(*DrawState, Viewport);
		FPacketContentViewDrawStateBuilder FilteredDrawStateBuilder(*FilteredDrawState, Viewport);

		TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid())
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
			const Trace::INetProfilerProvider& NetProfilerProvider = Trace::ReadNetProfilerProvider(*Session.Get());

			const FAxisViewportDouble& ViewportX = Viewport.GetHorizontalAxisViewport();

			//const int64 StartPos = static_cast<int64>(FMath::FloorToDouble(ViewportX.GetValueAtOffset(0.0f)));
			//const int64 EndPos = static_cast<int64>(FMath::CeilToDouble(ViewportX.GetValueAtOffset(ViewportX.GetSize())));
			const uint32 StartPos = 0;
			const uint32 EndPos = PacketBitSize;
			NetProfilerProvider.EnumeratePacketContentEventsByPosition(ConnectionIndex, ConnectionMode, PacketIndex, StartPos, EndPos, [this, &Builder, &FilteredDrawStateBuilder, &NetProfilerProvider](const Trace::FNetProfilerContentEvent& Event)
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

				if ((!bFilterByEventType || FilterEventTypeIndex == Event.EventTypeIndex) &&
					(!bFilterByNetId || (Event.ObjectInstanceIndex != 0 && FilterNetId == NetId)))
				{
					FilteredDrawStateBuilder.AddEvent(Event, Name, NetId);
				}
			});
		}

		Builder.Flush();
		FilteredDrawStateBuilder.Flush();
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
		ProfilerWindow->SetSelectedEventTypeIndex(SelectedEvent.Event.EventTypeIndex);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::SelectHoveredEvent()
{
	SelectedEvent = HoveredEvent;
	OnSelectedEventChanged();
	BringEventIntoView(SelectedEvent);
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

			constexpr float EventsPosY = 32.0f;
			constexpr float EventH = 14.0f;
			constexpr float EventDY = 2.0f;
			const float EventY = EventsPosY + (EventH + EventDY) * Event.Level;

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

		Helper.SetLayoutPosY(32.0f);
		//Helper.SetLayoutEventH(14.0f);
		//Helper.SetLayoutEventDY(2.0f);

		Helper.DrawBackground();

		if (bHighlightFilteredEvents && (bFilterByNetId || bFilterByEventType))
		{
			Helper.Draw(*DrawState, 0.1f);
			Helper.Draw(*FilteredDrawState);
		}
		else
		{
			// Draw the events contained by the network packet using the cached draw state.
			Helper.Draw(*DrawState);
		}

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
	// Select the hovered timing event (if any).
	UpdateHoveredEvent();
	SelectHoveredEvent();

	if (SelectedEvent.IsValid())
	{
		EnableFilterEventType(SelectedEvent.Event.EventTypeIndex);
	}
	else
	{
		DisableFilterEventType();
	}

	return FReply::Handled();
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

FReply SPacketContentView::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Left)
	{
		if (InKeyEvent.GetModifierKeys().IsShiftDown())
		{
			FindFirstEvent();
		}
		else
		{
			FindPreviousEvent();
		}
		return FReply::Handled();
	}
	else if (InKeyEvent.GetKey() == EKeys::Right)
	{
		if (InKeyEvent.GetModifierKeys().IsShiftDown())
		{
			FindLastEvent();
		}
		else
		{
			FindNextEvent();
		}
		return FReply::Handled();
	}

	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
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

void SPacketContentView::BringIntoView(const float X1, const float X2)
{
	FAxisViewportDouble& ViewportX = Viewport.GetHorizontalAxisViewport();

	// Increase interval with 8% (of view size) on each side.
	const float DX = ViewportX.GetSize() * 0.08f;

	float NewPos = ViewportX.GetPos();

	const float MinPos = X2 + DX - ViewportX.GetSize();
	if (NewPos < MinPos)
	{
		NewPos = MinPos;
	}

	const float MaxPos = X1 - DX;
	if (NewPos > MaxPos)
	{
		NewPos = MaxPos;
	}

	if (NewPos != ViewportX.GetPos())
	{
		ViewportX.ScrollAtPos(NewPos);
		UpdateHorizontalScrollBar();
		bIsStateDirty = true;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SPacketContentView::BringEventIntoView(const FNetworkPacketEventRef& EventRef)
{
	if (EventRef.IsValid())
	{
		const FAxisViewportDouble& ViewportX = Viewport.GetHorizontalAxisViewport();
		const float X1 = ViewportX.GetPosForValue(static_cast<double>(EventRef.Event.BitOffset));
		const float X2 = ViewportX.GetPosForValue(static_cast<double>(EventRef.Event.BitOffset + SelectedEvent.Event.BitSize));
		BringIntoView(X1, X2);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
