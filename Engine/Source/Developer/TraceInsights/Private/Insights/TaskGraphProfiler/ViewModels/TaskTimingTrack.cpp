// Copyright Epic Games, Inc. All Rights Reserved.

#include "TaskTimingTrack.h"

#include "TraceServices/Model/TasksProfiler.h"

// Insights
#include "Insights/ITimingViewSession.h"
#include "Insights/TaskGraphProfiler/TaskGraphProfilerManager.h"
#include "Insights/ViewModels/TaskGraphRelation.h"
#include "Insights/ViewModels/ThreadTimingTrack.h"
#include "Insights/ViewModels/ThreadTrackEvent.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TimingViewDrawHelper.h"
#include "Insights/ViewModels/TooltipDrawState.h"
#include "Insights/Widgets/STimingView.h"

#define LOCTEXT_NAMESPACE "TaskTimingTrack"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTaskTimingSharedState
////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskTimingSharedState::OnBeginSession(Insights::ITimingViewSession& InSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	TaskTrack = nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskTimingSharedState::OnEndSession(Insights::ITimingViewSession& InSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	TaskTrack = nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskTimingSharedState::Tick(Insights::ITimingViewSession& InSession, const TraceServices::IAnalysisSession& InAnalysisSession)
{
	if (&InSession != TimingView)
	{
		return;
	}

	if (!TaskTrack.IsValid() && FTaskGraphProfilerManager::Get()->GetIsAvailable())
	{
		TaskTrack = MakeShared<FTaskTimingTrack>(*this, TEXT("Task Overview Track"), 0);
		TaskTrack->SetVisibilityFlag(true);
		TaskTrack->SetOrder(FTimingTrackOrder::Task);

		TimingView->OnSelectedEventChanged().AddSP(TaskTrack.Get(), &FTaskTimingTrack::OnTimingEventSelected);

		InSession.AddTopDockedTrack(TaskTrack);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskTimingSharedState::ExtendFilterMenu(Insights::ITimingViewSession& InSession, FMenuBuilder& InOutMenuBuilder)
{
	if (&InSession != TimingView)
	{
		return;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskTimingSharedState::SetTaskId(uint32 InTaskId)
{
	if (TaskTrack.IsValid())
	{
		TaskTrack->SetTaskId(InTaskId);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FTaskTimingTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

INSIGHTS_IMPLEMENT_RTTI(FTaskTimingTrack)

////////////////////////////////////////////////////////////////////////////////////////////////////

const uint32 FTaskTimingTrack::InvalidTaskId = ~0;


void FTaskTimingTrack::BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context)
{
	if (TaskId == InvalidTaskId)
	{
		return;
	}

	TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (!Session.IsValid())
	{
		return;
	}

	TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

	const TraceServices::ITasksProvider* TasksProvider = TraceServices::ReadTasksProvider(*Session.Get());

	if (TasksProvider == nullptr)
	{
		return;
	}

	const TraceServices::FTaskInfo* Task = TasksProvider->TryGetTask(TaskId);

	if (Task == nullptr)
	{
		return;
	}

	Builder.AddEvent(Task->CreatedTimestamp, Task->LaunchedTimestamp, 0, TEXT("Launched"), 0, FTaskGraphProfilerManager::Get()->GetColorForTaskEvent(ETaskEventType::Created).ToFColor(true).ToPackedARGB());
	Builder.AddEvent(Task->LaunchedTimestamp, Task->ScheduledTimestamp, 0, TEXT("Dispatched"), 0, FTaskGraphProfilerManager::Get()->GetColorForTaskEvent(ETaskEventType::Launched).ToFColor(true).ToPackedARGB());
	Builder.AddEvent(Task->ScheduledTimestamp, Task->StartedTimestamp, 0, TEXT("Scheduled"), 0,  FTaskGraphProfilerManager::Get()->GetColorForTaskEvent(ETaskEventType::Scheduled).ToFColor(true).ToPackedARGB());
	Builder.AddEvent(Task->StartedTimestamp, Task->FinishedTimestamp, 0, TEXT("Executed"), 0, FTaskGraphProfilerManager::Get()->GetColorForTaskEvent(ETaskEventType::NestedCompleted).ToFColor(true).ToPackedARGB());
	if (Task->CompletedTimestamp > Task->FinishedTimestamp)
	{
		Builder.AddEvent(Task->FinishedTimestamp, Task->CompletedTimestamp, 0, TEXT("Completed"), 0, FTaskGraphProfilerManager::Get()->GetColorForTaskEvent(ETaskEventType::Completed).ToFColor(true).ToPackedARGB());
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskTimingTrack::BuildFilteredDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context)
{
	BuildDrawState(Builder, Context);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskTimingTrack::PostDraw(const ITimingTrackDrawContext& Context) const
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskTimingTrack::OnTimingEventSelected(TSharedPtr<const ITimingEvent> InSelectedEvent)
{
	if (!InSelectedEvent.IsValid() || !InSelectedEvent->Is<FThreadTrackEvent>())
	{
		TaskId = InvalidTaskId;
		SetDirtyFlag();
		return;
	}

	TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (!Session.IsValid())
	{
		return;
	}

	TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

	const TraceServices::ITasksProvider* TasksProvider = TraceServices::ReadTasksProvider(*Session.Get());

	if (TasksProvider == nullptr)
	{
		return;
	}

	const FThreadTrackEvent &ThreadEvent = InSelectedEvent->As<FThreadTrackEvent>();
	uint32 ThreadId = StaticCastSharedRef<const FThreadTimingTrack>(ThreadEvent.GetTrack())->GetThreadId();
	const TraceServices::FTaskInfo* Task = TasksProvider->TryGetTask(ThreadId, ThreadEvent.GetStartTime());

	if (Task != nullptr)
	{
		TaskId = Task->Id;
	}
	else
	{
		TaskId = InvalidTaskId;
	}

	SetDirtyFlag();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TSharedPtr<const ITimingEvent> FTaskTimingTrack::GetEvent(float InPosX, float InPosY, const FTimingTrackViewport& Viewport) const
{
	TSharedPtr<FTimingEvent> TimingEvent;

	if (TaskId == InvalidTaskId)
	{
		return TimingEvent;
	}

	const FTimingViewLayout& Layout = Viewport.GetLayout();
	const float TopLaneY = GetPosY() + 1.0f + Layout.TimelineDY; // +1.0f is for horizontal line between timelines
	const float DY = InPosY - TopLaneY;

	// If mouse is not above first sub-track or below last sub-track...
	if (DY >= 0 && DY < GetHeight() - 1.0f - 2 * Layout.TimelineDY)
	{
		const double EventTime = Viewport.SlateUnitsToTime(InPosX);

		TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (!Session.IsValid())
		{
			return TimingEvent;
		}

		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

		const TraceServices::ITasksProvider* TasksProvider = TraceServices::ReadTasksProvider(*Session.Get());

		if (TasksProvider == nullptr)
		{
			return TimingEvent;
		}

		const TraceServices::FTaskInfo* Task = TasksProvider->TryGetTask(TaskId);

		if (Task == nullptr)
		{
			return TimingEvent;
		}

		// This logic will be replaced
		if (EventTime >= Task->CreatedTimestamp && EventTime < Task->LaunchedTimestamp)
		{
			TimingEvent = MakeShared<FTimingEvent>(SharedThis(this), Task->CreatedTimestamp, Task->LaunchedTimestamp, 0);
		}
		else if (EventTime >= Task->LaunchedTimestamp && EventTime < Task->ScheduledTimestamp)
		{
			TimingEvent = MakeShared<FTimingEvent>(SharedThis(this), Task->LaunchedTimestamp, Task->ScheduledTimestamp, 0);
		}
		else if (EventTime >= Task->ScheduledTimestamp && EventTime < Task->StartedTimestamp)
		{
			TimingEvent = MakeShared<FTimingEvent>(SharedThis(this), Task->ScheduledTimestamp, Task->StartedTimestamp, 0);
		}
		else if (EventTime >= Task->StartedTimestamp && EventTime < Task->FinishedTimestamp)
		{
			TimingEvent = MakeShared<FTimingEvent>(SharedThis(this), Task->StartedTimestamp, Task->FinishedTimestamp, 0);
		}
		else if (EventTime >= Task->FinishedTimestamp && EventTime < Task->CompletedTimestamp)
		{
			TimingEvent = MakeShared<FTimingEvent>(SharedThis(this), Task->FinishedTimestamp, Task->CompletedTimestamp, 0);
		}
	}

	return TimingEvent;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskTimingTrack::InitTooltip(FTooltipDrawState& InOutTooltip, const ITimingEvent& InTooltipEvent) const
{
	InOutTooltip.ResetContent();

	// The rest of the implemention will follow
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
