// Copyright Epic Games, Inc. All Rights Reserved.

#include "TouchpadGesturesComponent.h"
#include "IMagicLeapControllerPlugin.h"
#include "MagicLeapController.h"

UMagicLeapTouchpadGesturesComponent::UMagicLeapTouchpadGesturesComponent()
{
	// Make sure this component ticks
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	bAutoActivate = true;
}

void UMagicLeapTouchpadGesturesComponent::OnTouchpadGestureStartCallback(const FMagicLeapTouchpadGesture& GestureData)
{
	FScopeLock Lock(&CriticalSection);
	PendingTouchpadGestureStart.Add(GestureData);
}

void UMagicLeapTouchpadGesturesComponent::OnTouchpadGestureContinueCallback(const FMagicLeapTouchpadGesture& GestureData)
{
	FScopeLock Lock(&CriticalSection);
	PendingTouchpadGestureContinue.Add(GestureData);
}

void UMagicLeapTouchpadGesturesComponent::OnTouchpadGestureEndCallback(const FMagicLeapTouchpadGesture& GestureData)
{
	FScopeLock Lock(&CriticalSection);
	PendingTouchpadGestureEnd.Add(GestureData);
}

void UMagicLeapTouchpadGesturesComponent::BeginPlay()
{
	Super::BeginPlay();

	TSharedPtr<FMagicLeapController> controller = StaticCastSharedPtr<FMagicLeapController>(IMagicLeapControllerPlugin::Get().GetInputDevice());
	if (controller.IsValid())
	{
		controller->RegisterTouchpadGestureReceiver(this);
	}
}

void UMagicLeapTouchpadGesturesComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	{
		FScopeLock Lock(&CriticalSection);
		for (const FMagicLeapTouchpadGesture& touchpad : PendingTouchpadGestureStart)
		{
			OnTouchpadGestureStart.Broadcast(touchpad);
		}
		for (const FMagicLeapTouchpadGesture& touchpad : PendingTouchpadGestureContinue)
		{
			OnTouchpadGestureContinue.Broadcast(touchpad);
		}
		for (const FMagicLeapTouchpadGesture& touchpad : PendingTouchpadGestureEnd)
		{
			OnTouchpadGestureEnd.Broadcast(touchpad);
		}

		PendingTouchpadGestureStart.Empty();
		PendingTouchpadGestureContinue.Empty();
		PendingTouchpadGestureEnd.Empty();
	}

}

void UMagicLeapTouchpadGesturesComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	TSharedPtr<FMagicLeapController> controller = StaticCastSharedPtr<FMagicLeapController>(IMagicLeapControllerPlugin::Get().GetInputDevice());
	if (controller.IsValid())
	{
		controller->UnregisterTouchpadGestureReceiver(this);
	}

	Super::EndPlay(EndPlayReason);
}
