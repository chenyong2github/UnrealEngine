// Copyright Epic Games, Inc. All Rights Reserved.

#include "Insights/TaskGraphProfiler/ViewModels/TaskTrackEvent.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

INSIGHTS_IMPLEMENT_RTTI(FTaskTrackEvent)

////////////////////////////////////////////////////////////////////////////////////////////////////

FTaskTrackEvent::FTaskTrackEvent(const TSharedRef<const FBaseTimingTrack> InTrack, double InStartTime, double InEndTime, uint32 InDepth, ETaskTrackEventType InType)
	: FTimingEvent(InTrack, InStartTime, InEndTime, InDepth)
	, TaskEventType(InType)
{}

////////////////////////////////////////////////////////////////////////////////////////////////////

FString FTaskTrackEvent::GetStartLabel() const
{
	switch (TaskEventType)
	{
	case ETaskTrackEventType::Launched:
		return TEXT("Created Time:");
	case ETaskTrackEventType::Dispatched:
		return TEXT("Launched Time");
	case ETaskTrackEventType::Scheduled:
		return TEXT("Scheduled Time:");
	case ETaskTrackEventType::Executed:
		return TEXT("Started Time:");
	case ETaskTrackEventType::Completed:
		return TEXT("Finished Time:");
	default:
		checkf(false, TEXT("Unknown task event type"));
		break;
	}

	return TEXT("");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FString FTaskTrackEvent::GetEndLabel() const
{
	switch (TaskEventType)
	{
	case ETaskTrackEventType::Launched:
		return TEXT("Launched Time:");
	case ETaskTrackEventType::Dispatched:
		return TEXT("Scheduled Time");
	case ETaskTrackEventType::Scheduled:
		return TEXT("Started Time:");
	case ETaskTrackEventType::Executed:
		return TEXT("Finished Time:");
	case ETaskTrackEventType::Completed:
		return TEXT("Completed Time:");
	default:
		checkf(false, TEXT("Unknown task event type"));
		break;
	}

	return TEXT("");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FString FTaskTrackEvent::GetEventName() const
{
	switch (TaskEventType)
	{
	case ETaskTrackEventType::Launched:
		return TEXT("Launched");
	case ETaskTrackEventType::Dispatched:
		return TEXT("Dispatched");
	case ETaskTrackEventType::Scheduled:
		return TEXT("Scheduled");
	case ETaskTrackEventType::Executed:
		return TEXT("Executed");
	case ETaskTrackEventType::Completed:
		return TEXT("Completed");
	default:
		checkf(false, TEXT("Unknown task event type"));
		break;
	}

	return TEXT("");
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
