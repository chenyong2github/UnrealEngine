// Copyright Epic Games, Inc. All Rights Reserved.

#include "TaskTimingTrack.h"

#include "TraceServices/Model/TasksProfiler.h"

// Insights
#include "Insights/Common/TimeUtils.h"
#include "Insights/ITimingViewSession.h"
#include "Insights/TaskGraphProfiler/TaskGraphProfilerManager.h"
#include "Insights/TaskGraphProfiler/ViewModels/TaskTrackEvent.h"
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

	if (bResetOnNextTick)
	{
		bResetOnNextTick = false;
		if (!TimingView->GetSelectedEvent().IsValid() && 
			(!TimingView->GetSelectedTrack().IsValid() || TimingView->GetSelectedTrack().Get() != TaskTrack.Get()))
		{
			SetTaskId(FTaskTimingTrack::InvalidTaskId);
			FTaskGraphProfilerManager::Get()->ClearTaskRelations();
		}
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
	if (!FTaskGraphProfilerManager::Get()->GetShowRelations())
	{
		return;
	}

	if ((InSelectedEvent.IsValid() && InSelectedEvent->GetTrack()->Is<FTaskTimingTrack>())
		|| IsSelected())
	{
		// The user has selected a Task Event. Do nothing.
		return;
	}

	if (!InSelectedEvent.IsValid() || !InSelectedEvent->Is<FThreadTrackEvent>())
	{
		if (TaskId != InvalidTaskId)
		{
			TaskId = InvalidTaskId;
			FTaskGraphProfilerManager::Get()->ClearTaskRelations();
			SetDirtyFlag();
		}
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
	GetEventRelations(ThreadEvent);

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
	TSharedPtr<FTaskTrackEvent> TimingEvent;

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
			TimingEvent = MakeShared<FTaskTrackEvent>(SharedThis(this), Task->CreatedTimestamp, Task->LaunchedTimestamp, 0, ETaskTrackEventType::Launched);
		}
		else if (EventTime >= Task->LaunchedTimestamp && EventTime < Task->ScheduledTimestamp)
		{
			TimingEvent = MakeShared<FTaskTrackEvent>(SharedThis(this), Task->LaunchedTimestamp, Task->ScheduledTimestamp, 0, ETaskTrackEventType::Dispatched);
		}
		else if (EventTime >= Task->ScheduledTimestamp && EventTime < Task->StartedTimestamp)
		{
			TimingEvent = MakeShared<FTaskTrackEvent>(SharedThis(this), Task->ScheduledTimestamp, Task->StartedTimestamp, 0, ETaskTrackEventType::Scheduled);
		}
		else if (EventTime >= Task->StartedTimestamp && EventTime < Task->FinishedTimestamp)
		{
			TimingEvent = MakeShared<FTaskTrackEvent>(SharedThis(this), Task->StartedTimestamp, Task->FinishedTimestamp, 0, ETaskTrackEventType::Executed);
		}
		else if (EventTime >= Task->FinishedTimestamp && EventTime < Task->CompletedTimestamp)
		{
			TimingEvent = MakeShared<FTaskTrackEvent>(SharedThis(this), Task->FinishedTimestamp, Task->CompletedTimestamp, 0, ETaskTrackEventType::Completed);
		}

		if (TimingEvent.IsValid())
		{
			TimingEvent->SetTaskId(Task->Id);
		}
	}

	return TimingEvent;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskTimingTrack::InitTooltip(FTooltipDrawState& InOutTooltip, const ITimingEvent& InTooltipEvent) const
{
	InOutTooltip.ResetContent();

	if (!InTooltipEvent.CheckTrack(this) || !InTooltipEvent.Is<FTaskTrackEvent>())
	{
		return;
	}

	const FTaskTrackEvent& TaskTrackEvent = InTooltipEvent.As<FTaskTrackEvent>();
	InOutTooltip.AddTitle(TaskTrackEvent.GetEventName());

	InOutTooltip.AddNameValueTextLine(TaskTrackEvent.GetStartLabel(), TimeUtils::FormatTimeAuto(TaskTrackEvent.GetStartTime(), 6));
	InOutTooltip.AddNameValueTextLine(TaskTrackEvent.GetEndLabel(), TimeUtils::FormatTimeAuto(TaskTrackEvent.GetEndTime(), 6));
	InOutTooltip.AddNameValueTextLine(TEXT("Duration:"), TimeUtils::FormatTimeAuto(TaskTrackEvent.GetEndTime() - TaskTrackEvent.GetStartTime(), 6));

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

	const TraceServices::FTaskInfo* Task = TasksProvider->TryGetTask(TaskTrackEvent.GetTaskId());

	if (Task == nullptr)
	{
		return;
	}

	InOutTooltip.AddNameValueTextLine(TEXT("Task Id:"), FString::Printf(TEXT("%d"), Task->Id));

	switch (TaskTrackEvent.GetTaskEventType())
	{
	case ETaskTrackEventType::Launched:
		break;
	case ETaskTrackEventType::Dispatched:
	{
		InOutTooltip.AddNameValueTextLine(TEXT("Prerequisite tasks:"), FString::Printf(TEXT("%d"), Task->Prerequisites.Num()));
		break;
	}
	case ETaskTrackEventType::Scheduled:
		break;
	case ETaskTrackEventType::Executed:
		InOutTooltip.AddNameValueTextLine(TEXT("Nested tasks:"), FString::Printf(TEXT("%d"), Task->NestedTasks.Num()));
		break;
	case ETaskTrackEventType::Completed:
		InOutTooltip.AddNameValueTextLine(TEXT("Subsequent tasks:"), FString::Printf(TEXT("%d"), Task->Subsequents.Num()));
		break;
	default:
		checkf(false, TEXT("Unknown task event type"));
		break;
	}

	InOutTooltip.UpdateLayout();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTaskTimingTrack::GetEventRelations(const FThreadTrackEvent& InSelectedEvent)
{
	const int32 MaxTasksToShow = 30;
	double StartTime = InSelectedEvent.GetStartTime();

	TSharedPtr<const TraceServices::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	const TraceServices::ITasksProvider* TasksProvider = TraceServices::ReadTasksProvider(*Session.Get());
	if (TasksProvider)
	{
		STimingView* TimingView = SharedState.GetTimingView();
		TSharedRef<const FThreadTimingTrack> EventTrack = StaticCastSharedRef<const FThreadTimingTrack>(InSelectedEvent.GetTrack());
		uint32 ThreadId = EventTrack->GetThreadId();
		
		FTaskGraphProfilerManager::Get()->ShowTaskRelations(&InSelectedEvent, ThreadId);

		// if it's an event waiting for tasks completeness, add relations to these tasks
		const TraceServices::ITimingProfilerProvider& TimingProfilerProvider = *TraceServices::ReadTimingProfilerProvider(*Session.Get());
		const TraceServices::ITimingProfilerTimerReader* TimerReader;
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
		TimingProfilerProvider.ReadTimers([&TimerReader](const TraceServices::ITimingProfilerTimerReader& Out) { TimerReader = &Out; });

		const FThreadTrackEvent& ThreadTrackEvent = *static_cast<const FThreadTrackEvent*>(&InSelectedEvent);
		const TraceServices::FTimingProfilerTimer* Timer = TimerReader->GetTimer(ThreadTrackEvent.GetTimerIndex());
		check(Timer != nullptr);

		const TraceServices::FWaitingForTasks* Waiting = TasksProvider->TryGetWaiting(Timer->Name, ThreadId, StartTime);
		if (Waiting != nullptr)
		{
			int32 NumWaitedTasksToShow = FMath::Min(Waiting->Tasks.Num(), MaxTasksToShow);
			for (int32 TaskIndex = 0; TaskIndex != NumWaitedTasksToShow; ++TaskIndex)
			{
				const TraceServices::FTaskInfo* WaitedTask = TasksProvider->TryGetTask(Waiting->Tasks[TaskIndex]);
				if (WaitedTask != nullptr)
				{
					FTaskGraphProfilerManager::Get()->AddRelation(&InSelectedEvent, StartTime, ThreadId, WaitedTask->StartedTimestamp, WaitedTask->StartedThreadId, ETaskEventType::AddedNested);
					FTaskGraphProfilerManager::Get()->AddRelation(&InSelectedEvent, WaitedTask->CompletedTimestamp, WaitedTask->CompletedThreadId, WaitedTask->CompletedTimestamp, ThreadId, ETaskEventType::NestedCompleted);
				}
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply FTaskTimingTrack::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		MousePositionOnButtonDown = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	}

	return FReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply FTaskTimingTrack::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FVector2D MousePositionOnButtonUp = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && MousePositionOnButtonUp.Equals(MousePositionOnButtonDown, 2.0f))
	{
		SharedState.SetResetOnNextTick(true);
	}

	return FReply::Unhandled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
