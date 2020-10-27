// Copyright Epic Games, Inc. All Rights Reserved.

#include "Quartz/AudioMixerClockHandle.h"
#include "Sound/QuartzQuantizationUtilities.h"

#include "AudioDevice.h"
#include "AudioMixerDevice.h"
#include "Engine/GameInstance.h"


UQuartzClockHandle::UQuartzClockHandle()
{
}

UQuartzClockHandle::~UQuartzClockHandle()
{

}

void UQuartzClockHandle::BeginDestroy()
{
	Super::BeginDestroy();

	// un-subscribe from Subsystem tick and metronome events
	if (QuartzSubsystem)
	{
		QuartzSubsystem->UnsubscribeFromQuartzTick(this);

		if (WorldPtr)
		{
			Audio::FQuartzClockManager* ClockManager = QuartzSubsystem->GetClockManager(WorldPtr);

			if (ClockManager)
			{
				ClockManager->UnsubscribeFromAllTimeDivisions(CurrentClockId, GetCommandQueue());
			}
		}
	}

	// shutdown the shared command queue
	if (CommandQueuePtr.IsValid())
	{
		CommandQueuePtr->StopTakingCommands();
		CommandQueuePtr.Reset();
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

void UQuartzClockHandle::PauseClock(const UObject* WorldContextObject)
{
	if (QuartzSubsystem)
	{
		Audio::FQuartzClockManager* ClockManager = QuartzSubsystem->GetClockManager(WorldContextObject);
		if (ClockManager)
		{
			ClockManager->PauseClock(CurrentClockId);
		}
	}
}

// Begin BP interface
void UQuartzClockHandle::ResumeClock(const UObject* WorldContextObject)
{
	if (QuartzSubsystem)
	{
		Audio::FQuartzClockManager* ClockManager = QuartzSubsystem->GetClockManager(WorldContextObject);

		if (ClockManager)
		{
			ClockManager->ResumeClock(CurrentClockId);
		}
	}
}

void UQuartzClockHandle::ResetTransport(const UObject* WorldContextObject, const FOnQuartzCommandEventBP& InDelegate)
{
	if (QuartzSubsystem != nullptr)
	{
		Audio::FQuartzQuantizedCommandInitInfo Data(QuartzSubsystem->CreateDataForTransportReset(this, InDelegate));
		QuartzSubsystem->AddCommandToClock(WorldContextObject, Data);
	}
}

void UQuartzClockHandle::SubscribeToQuantizationEvent(const UObject* WorldContextObject, EQuartzCommandQuantization InQuantizationBoundary, const FOnQuartzMetronomeEventBP& OnQuantizationEvent)
{
	if (QuartzSubsystem)
	{
		Audio::FQuartzClockManager* ClockManager = QuartzSubsystem->GetClockManager(WorldContextObject);
		if (ClockManager && ClockManager->DoesClockExist(CurrentClockId) && OnQuantizationEvent.IsBound())
		{
			MetronomeDelegates[static_cast<int32>(InQuantizationBoundary)].MulticastDelegate.AddUnique(OnQuantizationEvent);
			ClockManager->SubscribeToTimeDivision(CurrentClockId, GetCommandQueue(), InQuantizationBoundary);
		}
	}
}

void UQuartzClockHandle::SubscribeToAllQuantizationEvents(const UObject* WorldContextObject, const FOnQuartzMetronomeEventBP& OnQuantizationEvent)
{
	if (QuartzSubsystem)
	{
		Audio::FQuartzClockManager* ClockManager = QuartzSubsystem->GetClockManager(WorldContextObject);
		if (ClockManager && ClockManager->DoesClockExist(CurrentClockId) && OnQuantizationEvent.IsBound())
		{
			for (int32 i = 0; i < static_cast<int32>(EQuartzCommandQuantization::Count) - 1; ++i)
			{
				MetronomeDelegates[i].MulticastDelegate.AddUnique(OnQuantizationEvent);
			}

			ClockManager->SubscribeToAllTimeDivisions(CurrentClockId, GetCommandQueue());
		}
	}
}

void UQuartzClockHandle::UnsubscribeFromTimeDivision(const UObject* WorldContextObject, EQuartzCommandQuantization InQuantizationBoundary)
{
	if (QuartzSubsystem)
	{
		Audio::FQuartzClockManager* ClockManager = QuartzSubsystem->GetClockManager(WorldContextObject);
		if (ClockManager && ClockManager->DoesClockExist(CurrentClockId))
		{
			ClockManager->UnsubscribeFromTimeDivision(CurrentClockId, GetCommandQueue(), InQuantizationBoundary);
		}
	}
}

void UQuartzClockHandle::UnsubscribeFromAllTimeDivisions(const UObject* WorldContextObject)
{
	if (QuartzSubsystem)
	{
		Audio::FQuartzClockManager* ClockManager = QuartzSubsystem->GetClockManager(WorldContextObject);
		if (ClockManager && ClockManager->DoesClockExist(CurrentClockId))
		{
			ClockManager->UnsubscribeFromAllTimeDivisions(CurrentClockId, GetCommandQueue());
		}
	}
}

// Metronome Alteration (setters)
void UQuartzClockHandle::SetMillisecondsPerTick(const UObject* WorldContextObject, float MillisecondsPerTick, const FQuartzQuantizationBoundary& InQuantizationBoundary, const FOnQuartzCommandEventBP& InDelegate)
{
	if (QuartzSubsystem)
	{
		Audio::FQuartzClockTickRate TickRate;
		TickRate.SetMillisecondsPerTick(MillisecondsPerTick);

		Audio::FQuartzQuantizedCommandInitInfo Data(QuartzSubsystem->CreateDataForTickRateChange(this, InDelegate, TickRate, InQuantizationBoundary));
		QuartzSubsystem->AddCommandToClock(WorldContextObject, Data);
	}
}

void UQuartzClockHandle::SetTicksPerSecond(const UObject* WorldContextObject, float TicksPerSecond, const FQuartzQuantizationBoundary& InQuantizationBoundary, const FOnQuartzCommandEventBP& InDelegate)
{
	if (QuartzSubsystem)
	{
		Audio::FQuartzClockTickRate TickRate;
		TickRate.SetSecondsPerTick(1.f / TicksPerSecond);

		Audio::FQuartzQuantizedCommandInitInfo Data(QuartzSubsystem->CreateDataForTickRateChange(this, InDelegate, TickRate, InQuantizationBoundary));
		QuartzSubsystem->AddCommandToClock(WorldContextObject, Data);
	}
}

void UQuartzClockHandle::SetSecondsPerTick(const UObject* WorldContextObject, float SecondsPerTick, const FQuartzQuantizationBoundary& InQuantizationBoundary, const FOnQuartzCommandEventBP& InDelegate)
{
	if (QuartzSubsystem)
	{
		Audio::FQuartzClockTickRate TickRate;
		TickRate.SetSecondsPerTick(SecondsPerTick);

		Audio::FQuartzQuantizedCommandInitInfo Data(QuartzSubsystem->CreateDataForTickRateChange(this, InDelegate, TickRate, InQuantizationBoundary));
		QuartzSubsystem->AddCommandToClock(WorldContextObject, Data);
	}
}

void UQuartzClockHandle::SetThirtySecondNotesPerMinute(const UObject* WorldContextObject, float ThirtySecondsNotesPerMinute, const FQuartzQuantizationBoundary& InQuantizationBoundary, const FOnQuartzCommandEventBP& InDelegate)
{
	if (QuartzSubsystem)
	{
		Audio::FQuartzClockTickRate TickRate;
		TickRate.SetThirtySecondNotesPerMinute(ThirtySecondsNotesPerMinute);

		Audio::FQuartzQuantizedCommandInitInfo Data(QuartzSubsystem->CreateDataForTickRateChange(this, InDelegate, TickRate, InQuantizationBoundary));
		QuartzSubsystem->AddCommandToClock(WorldContextObject, Data);
	}
}

void UQuartzClockHandle::SetBeatsPerMinute(const UObject* WorldContextObject, float BeatsPerMinute, const FQuartzQuantizationBoundary& InQuantizationBoundary, const FOnQuartzCommandEventBP& InDelegate)
{
	if (QuartzSubsystem)
	{
		Audio::FQuartzClockTickRate TickRate;
		TickRate.SetBeatsPerMinute(BeatsPerMinute);

		Audio::FQuartzQuantizedCommandInitInfo Data(QuartzSubsystem->CreateDataForTickRateChange(this, InDelegate, TickRate, InQuantizationBoundary));
		QuartzSubsystem->AddCommandToClock(WorldContextObject, Data);
	}
}

// Metronome getters
float UQuartzClockHandle::GetMillisecondsPerTick(const UObject* WorldContextObject) const
{
	Audio::FQuartzClockTickRate OutTickRate;

	if (GetCurrentTickRate(WorldContextObject, OutTickRate))
	{
		return OutTickRate.GetMillisecondsPerTick();
	}

	return 0.f;
}

float UQuartzClockHandle::GetTicksPerSecond(const UObject* WorldContextObject) const
{
	Audio::FQuartzClockTickRate OutTickRate;

	if (GetCurrentTickRate(WorldContextObject, OutTickRate))
	{
		const float SecondsPerTick = OutTickRate.GetSecondsPerTick();
		
		if (!FMath::IsNearlyZero(SecondsPerTick))
		{
			return 1.f / SecondsPerTick;
		}
	}

	return 0.f;
}

float UQuartzClockHandle::GetSecondsPerTick(const UObject* WorldContextObject) const
{
	Audio::FQuartzClockTickRate OutTickRate;

	if (GetCurrentTickRate(WorldContextObject, OutTickRate))
	{
		return OutTickRate.GetSecondsPerTick();
	}

	return 0.f;
}

float UQuartzClockHandle::GetThirtySecondNotesPerMinute(const UObject* WorldContextObject) const
{
	Audio::FQuartzClockTickRate OutTickRate;

	if (GetCurrentTickRate(WorldContextObject, OutTickRate))
	{
		return OutTickRate.GetThirtySecondNotesPerMinute();
	}

	return 0.f;
}

float UQuartzClockHandle::GetBeatsPerMinute(const UObject* WorldContextObject) const
{
	Audio::FQuartzClockTickRate OutTickRate;

	if (GetCurrentTickRate(WorldContextObject, OutTickRate))
	{
		return OutTickRate.GetBeatsPerMinute();
	}

	return 0.f;
}
// End BP interface


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

// returns true if OutTickRate is valid and was updated
bool UQuartzClockHandle::GetCurrentTickRate(const UObject* WorldContextObject, Audio::FQuartzClockTickRate& OutTickRate) const
{
	if (QuartzSubsystem)
	{
		Audio::FQuartzClockManager* ClockManager = QuartzSubsystem->GetClockManager(WorldContextObject);

		if (ClockManager)
		{
			OutTickRate = ClockManager->GetTickRateForClock(CurrentClockId);
			return true;
		}
	}

	OutTickRate = {};
	return false;
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
