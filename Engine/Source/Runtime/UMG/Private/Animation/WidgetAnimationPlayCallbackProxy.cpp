// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Animation/WidgetAnimationPlayCallbackProxy.h"
#include "Animation/UMGSequencePlayer.h"
#include "Engine/World.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "UMG"

UWidgetAnimationPlayCallbackProxy* UWidgetAnimationPlayCallbackProxy::CreatePlayAnimationProxyObject(class UUMGSequencePlayer*& Result, class UUserWidget* Widget, UWidgetAnimation* InAnimation, float StartAtTime, int32 NumLoopsToPlay, EUMGSequencePlayMode::Type PlayMode, float PlaybackSpeed)
{
	UWidgetAnimationPlayCallbackProxy* Proxy = NewObject<UWidgetAnimationPlayCallbackProxy>();
	Proxy->SetFlags(RF_StrongRefOnFrame);
	Result = Proxy->ExecutePlayAnimation(Widget, InAnimation, StartAtTime, NumLoopsToPlay, PlayMode, PlaybackSpeed);
	return Proxy;
}

UWidgetAnimationPlayCallbackProxy* UWidgetAnimationPlayCallbackProxy::CreatePlayAnimationTimeRangeProxyObject(class UUMGSequencePlayer*& Result, class UUserWidget* Widget, UWidgetAnimation* InAnimation, float StartAtTime, float EndAtTime, int32 NumLoopsToPlay, EUMGSequencePlayMode::Type PlayMode, float PlaybackSpeed)
{
	UWidgetAnimationPlayCallbackProxy* Proxy = NewObject<UWidgetAnimationPlayCallbackProxy>();
	Proxy->SetFlags(RF_StrongRefOnFrame);
	Result = Proxy->ExecutePlayAnimationTimeRange(Widget, InAnimation, StartAtTime, EndAtTime, NumLoopsToPlay, PlayMode, PlaybackSpeed);
	return Proxy;
}

UWidgetAnimationPlayCallbackProxy::UWidgetAnimationPlayCallbackProxy(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

class UUMGSequencePlayer* UWidgetAnimationPlayCallbackProxy::ExecutePlayAnimation(class UUserWidget* Widget, UWidgetAnimation* InAnimation, float StartAtTime, int32 NumLoopsToPlay, EUMGSequencePlayMode::Type PlayMode, float PlaybackSpeed)
{
	if (!Widget)
	{
		return nullptr;
	}

	WorldPtr = Widget->GetWorld();

	UUMGSequencePlayer* Player = Widget->PlayAnimation(InAnimation, StartAtTime, NumLoopsToPlay, PlayMode, PlaybackSpeed);
	if (Player)
	{
		Player->OnSequenceFinishedPlaying().AddUObject(this, &UWidgetAnimationPlayCallbackProxy::OnFinished);
	}

	return Player;
}

class UUMGSequencePlayer* UWidgetAnimationPlayCallbackProxy::ExecutePlayAnimationTimeRange(class UUserWidget* Widget, UWidgetAnimation* InAnimation, float StartAtTime, float EndAtTime, int32 NumLoopsToPlay, EUMGSequencePlayMode::Type PlayMode, float PlaybackSpeed)
{
	if (!Widget)
	{
		return nullptr;
	}

	WorldPtr = Widget->GetWorld();

	UUMGSequencePlayer* Player = Widget->PlayAnimationTimeRange(InAnimation, StartAtTime, EndAtTime, NumLoopsToPlay, PlayMode, PlaybackSpeed);
	if (Player)
	{
		OnFinishedHandle = Player->OnSequenceFinishedPlaying().AddUObject(this, &UWidgetAnimationPlayCallbackProxy::OnFinished);
	}

	return Player;
}

void UWidgetAnimationPlayCallbackProxy::OnFinished(class UUMGSequencePlayer& Player)
{
	Player.OnSequenceFinishedPlaying().Remove(OnFinishedHandle);

	// We delay the Finish trigger to next frame.
	if (UWorld* World = WorldPtr.Get())
	{
		// Use a dummy timer handle as we don't need to store it for later but we don't need to look for something to clear
		FTimerHandle TimerHandle;
		World->GetTimerManager().SetTimer(TimerHandle, this, &UWidgetAnimationPlayCallbackProxy::OnFinished_Delayed, 0.001f, false);
	}
}

void UWidgetAnimationPlayCallbackProxy::OnFinished_Delayed()
{
	Finished.Broadcast();
}

#undef LOCTEXT_NAMESPACE
