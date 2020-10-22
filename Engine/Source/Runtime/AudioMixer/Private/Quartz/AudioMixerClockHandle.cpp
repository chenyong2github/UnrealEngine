// Copyright Epic Games, Inc. All Rights Reserved.

#include "Quartz/AudioMixerClockHandle.h"
#include "Sound/QuartzQuantizationUtilities.h"

#include "AudioDevice.h"
#include "AudioMixerDevice.h"
#include "Engine/GameInstance.h"

// TODO: Don't access clock manager directly, go through the subsystem

UQuartzClockHandle::UQuartzClockHandle()
{
}

UQuartzClockHandle::~UQuartzClockHandle()
{

}

void UQuartzClockHandle::BeginDestroy()
{
	Super::BeginDestroy();

	if (CommandQueuePtr.IsValid())
	{
		CommandQueuePtr->StopTakingCommands();
		CommandQueuePtr.Reset();
	}

	if (QuartzSubsystem)
	{
		QuartzSubsystem->UnsubscribeFromQuartzTick(this);

		if (WorldPtr)
		{
			QuartzSubsystem->UnsubscribeFromAllTimeDivisionsInternal(WorldPtr, *this);
		}
	}
}

UQuartzClockHandle* UQuartzClockHandle::Init(UWorld* InWorldPtr)
{
	checkSlow(InWorldPtr);

	WorldPtr = InWorldPtr;

	QuartzSubsystem = UQuartzSubsystem::Get(WorldPtr);

	CommandQueuePtr = QuartzSubsystem->CreateQuartzCommandQueue();

	QuartzSubsystem->SubscribeToQuartzTick(this);

	return this;
}

void UQuartzClockHandle::SubscribeToQuantizationEvent(EQuartzCommandQuantization InQuantizationBoundary, const FOnQuartzMetronomeEventBP& OnQuantizationEvent)
{
	if (!OnQuantizationEvent.IsBound())
	{
		return;
	}

	MetronomeDelegates[static_cast<int32>(InQuantizationBoundary)].MulticastDelegate.AddUnique(OnQuantizationEvent);
}

void UQuartzClockHandle::SubscribeToAllQuantizationEvents(const FOnQuartzMetronomeEventBP& OnQuantizationEvent)
{
	if (!OnQuantizationEvent.IsBound())
	{
		return;
	}

	for (int32 i = 0; i < static_cast<int32>(EQuartzCommandQuantization::Count) - 1; ++i)
	{
		MetronomeDelegates[i].MulticastDelegate.AddUnique(OnQuantizationEvent);
	}
}

UQuartzClockHandle* UQuartzClockHandle::SubscribeToClock(const UObject* WorldContextObject, FName ClockName)
{
	// create ID
	CurrentClockId = ClockName;

	FString TempId = WorldContextObject->GetFName().ToString();
	TempId.Append(CurrentClockId.ToString());
	ClockHandleId = FName(*TempId);

	// TODO: subscribe to clock w/ ClockHandleId
	bConnectedToClock = true;

	return this;
}

int32 UQuartzClockHandle::AddCommandDelegate(const FOnQuartzCommandEventBP& InDelegate, TSharedPtr<Audio::FShareableQuartzCommandQueue, ESPMode::ThreadSafe>& OutCommandQueuePtr)
{
	OutCommandQueuePtr = CommandQueuePtr;

	const int32 Num = QuantizedCommandDelegates.Num();
	int32 SlotId = 0;

	for (; SlotId < Num; ++SlotId)
	{
		if (!QuantizedCommandDelegates[SlotId].MulticastDelegate.IsBound())
		{
			QuantizedCommandDelegates[SlotId].MulticastDelegate.AddUnique(InDelegate);
			return SlotId;
		}
	}

	// need a new slot
	QuantizedCommandDelegates.AddDefaulted_GetRef().MulticastDelegate.AddUnique(InDelegate);
	return SlotId;
}

void UQuartzClockHandle::QuartzTick(float DeltaTime)
{
	PumpCommandQueue();
}

bool UQuartzClockHandle::QuartzIsTickable() const
{
	return (CommandQueuePtr && !CommandQueuePtr->IsQueueEmpty());
}

void UQuartzClockHandle::PumpCommandQueue()
{
	if (!CommandQueuePtr.IsValid())
	{
		return;
	}

	TFunction<void(UQuartzClockHandle*)> Command;
	while (CommandQueuePtr->EventDelegateQueue.Dequeue(Command))
	{
		Command(this);
	}
}

void UQuartzClockHandle::ProcessCommand(Audio::FQuartzQuantizedCommandDelegateData Data)
{
	checkSlow(Data.DelegateSubType < EQuartzCommandDelegateSubType::Count && (Data.DelegateID < QuantizedCommandDelegates.Num()));

	QuartzSubsystem->PushLatencyTrackerResult(Data.RequestRecieved());

	CommandDelegateGameThreadData& GameThreadEntry = QuantizedCommandDelegates[Data.DelegateID];

	GameThreadEntry.MulticastDelegate.Broadcast(Data.DelegateSubType, "Sample Payload");

	// track the number of active QuantizedCommands that may be sending info back to us.
	// (new command)
	if (Data.DelegateSubType == EQuartzCommandDelegateSubType::CommandOnQueued)
	{
		GameThreadEntry.RefCount.Increment();
	}

	// (end of a command)
	if ((Data.DelegateSubType == EQuartzCommandDelegateSubType::CommandCompleted)
		|| (Data.DelegateSubType == EQuartzCommandDelegateSubType::CommandOnCanceled))
	{
		// are all the commands done?
		if (GameThreadEntry.RefCount.Decrement() == 0)
		{
			GameThreadEntry.MulticastDelegate.Clear();
		}
	}
}

void UQuartzClockHandle::ProcessCommand(Audio::FQuartzMetronomeDelegateData Data)
{
	QuartzSubsystem->PushLatencyTrackerResult(Data.RequestRecieved());

	MetronomeDelegates[static_cast<int32>(Data.Quantization)].MulticastDelegate.Broadcast(CurrentClockId, Data.Quantization, Data.Bar, Data.Beat, Data.BeatFraction);
}
