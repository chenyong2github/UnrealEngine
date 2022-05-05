// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContextualAnimMovieSceneNotifySection.h"
#include "ContextualAnimMovieSceneNotifyTrack.h"
#include "Animation/AnimSequenceBase.h"
#include "MovieScene.h"

UContextualAnimMovieSceneNotifyTrack* UContextualAnimMovieSceneNotifySection::GetOwnerTrack() const
{
	return GetTypedOuter<UContextualAnimMovieSceneNotifyTrack>();
}

void UContextualAnimMovieSceneNotifySection::Initialize(const FAnimNotifyEvent& NotifyEvent)
{
	FFrameRate TickResolution = GetOwnerTrack()->GetTypedOuter<UMovieScene>()->GetTickResolution();
	FFrameNumber StartFrame = (NotifyEvent.GetTriggerTime() * TickResolution).RoundToFrame();
	FFrameNumber EndFrame = (NotifyEvent.GetEndTriggerTime() * TickResolution).RoundToFrame();
	SetRange(TRange<FFrameNumber>::Exclusive(StartFrame, EndFrame));

	AnimNotifyEventGuid = NotifyEvent.Guid;
}

FAnimNotifyEvent* UContextualAnimMovieSceneNotifySection::GetAnimNotifyEvent() const
{
	UAnimSequenceBase& Animation = GetOwnerTrack()->GetAnimation();
	for (FAnimNotifyEvent& NotifyEvent : Animation.Notifies)
	{
		if (NotifyEvent.Guid == AnimNotifyEventGuid)
		{
			return &NotifyEvent;
		}
	}

	return nullptr;
}

UAnimNotifyState* UContextualAnimMovieSceneNotifySection::GetAnimNotifyState() const
{
	UAnimSequenceBase& Animation = GetOwnerTrack()->GetAnimation();
	if(const FAnimNotifyEvent* NotifyEventPtr = GetAnimNotifyEvent())
	{
		return NotifyEventPtr->NotifyStateClass;
	}

	return nullptr;
}