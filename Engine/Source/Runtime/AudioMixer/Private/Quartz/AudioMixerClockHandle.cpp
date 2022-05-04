// Copyright Epic Games, Inc. All Rights Reserved.

#include "Quartz/AudioMixerClockHandle.h"
#include "Sound/QuartzQuantizationUtilities.h"
#include "AudioDevice.h"
#include "AudioMixerDevice.h"
#include "Engine/GameInstance.h"

// FQuartzTickableObject implementation
void FQuartzTickableObject::FQuartzTickableObjectGCObjectMembers::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(QuartzSubsystem);
	Collector.AddReferencedObject(WorldPtr);
}

FQuartzTickableObject::~FQuartzTickableObject()
{
	Shutdown();

	// shutdown the shared command queue
	if (CommandQueuePtr.IsValid())
	{
		CommandQueuePtr->StopTakingCommands();
		CommandQueuePtr.Reset();
	}
}

FQuartzTickableObject* FQuartzTickableObject::Init(UWorld* InWorldPtr)
{
	if (!InWorldPtr)
	{
		// can't initialize if we don't have a valid world
		return this;
	}

	GCObjectMembers.WorldPtr = InWorldPtr;
	GCObjectMembers.QuartzSubsystem = UQuartzSubsystem::Get(GCObjectMembers.WorldPtr);
	CommandQueuePtr = GCObjectMembers.QuartzSubsystem->CreateQuartzCommandQueue();
	GCObjectMembers.QuartzSubsystem->SubscribeToQuartzTick(this);

	bHasBeenInitialized = true;

	return this;
}

int32 FQuartzTickableObject::AddCommandDelegate(const FOnQuartzCommandEventBP& InDelegate, TArray<TSharedPtr<Audio::FShareableQuartzCommandQueue, ESPMode::ThreadSafe>>& TargetSubscriberArray)
{
	TargetSubscriberArray.Add(CommandQueuePtr);

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

// called by UQuartzSubsystem::BeginDestroy()
void FQuartzTickableObject::Shutdown()
{
	if (GCObjectMembers.WorldPtr && GCObjectMembers.QuartzSubsystem)
	{
		GCObjectMembers.QuartzSubsystem->UnsubscribeFromQuartzTick(this);
		GCObjectMembers.WorldPtr = nullptr;
		GCObjectMembers.QuartzSubsystem = nullptr;
		bHasBeenInitialized = false;
	}
}

TSharedPtr<Audio::FShareableQuartzCommandQueue, ESPMode::ThreadSafe> FQuartzTickableObject::GetCommandQueue()
{
	if (!CommandQueuePtr.IsValid())
	{
		CommandQueuePtr = MakeShared<Audio::FShareableQuartzCommandQueue, ESPMode::ThreadSafe>();
	}

	return CommandQueuePtr;
}

void FQuartzTickableObject::QuartzTick(float DeltaTime)
{
	PumpCommandQueue();
}

bool FQuartzTickableObject::QuartzIsTickable() const
{
	return (CommandQueuePtr && !CommandQueuePtr->IsQueueEmpty());
}

void FQuartzTickableObject::PumpCommandQueue()
{
	if (!CommandQueuePtr.IsValid())
	{
		return;
	}

	// gather move all the current commands into our temp container
	TFunction<void(FQuartzTickableObject*)> Function;
	while (CommandQueuePtr->EventDelegateQueue.Dequeue(Function))
	{
		TempCommandQueue.Emplace(Function);
	}

	// execute the commands (may alter the original container)
	for (auto& Command : TempCommandQueue)
	{
		Command(this);
	}

	TempCommandQueue.Reset();
}



// Clock Handle implementation
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
	if (UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem())
	{
		QuartzSubsystem->UnsubscribeFromQuartzTick(this);

		Audio::FQuartzClockManager* ClockManager = QuartzSubsystem->GetManagerForClock(QuartzGetWorld(), GetClockName());

		if (ClockManager)
		{
			ClockManager->UnsubscribeFromAllTimeDivisions(CurrentClockId, GetCommandQueue());
		}
	}

	// shutdown the shared command queue
	if (CommandQueuePtr.IsValid())
	{
		CommandQueuePtr->StopTakingCommands();
		CommandQueuePtr.Reset();
	}
}

void UQuartzClockHandle::StartClock(const UObject* WorldContextObject, UQuartzClockHandle*& ClockHandle)
{
	ClockHandle = this;
	ResumeClock(WorldContextObject, ClockHandle);
}

void UQuartzClockHandle::StopClock(const UObject* WorldContextObject, bool CancelPendingEvents, UQuartzClockHandle*& ClockHandle)
{
	ClockHandle = this;
	if (UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem())
	{
		Audio::FQuartzClockManager* ClockManager = QuartzSubsystem->GetManagerForClock(WorldContextObject, GetClockName());

		if (ClockManager)
		{
			ClockManager->StopClock(CurrentClockId, CancelPendingEvents);
		}
	}
}

void UQuartzClockHandle::PauseClock(const UObject* WorldContextObject, UQuartzClockHandle*& ClockHandle)
{
	ClockHandle = this;
	if (UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem())
	{
		Audio::FQuartzClockManager* ClockManager = QuartzSubsystem->GetManagerForClock(WorldContextObject, GetClockName());
		if (ClockManager)
		{
			ClockManager->PauseClock(CurrentClockId);
		}
	}
}

// Begin BP interface
void UQuartzClockHandle::ResumeClock(const UObject* WorldContextObject, UQuartzClockHandle*& ClockHandle)
{
	ClockHandle = this;
	if (UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem())
	{
		Audio::FQuartzClockManager* ClockManager = QuartzSubsystem->GetManagerForClock(WorldContextObject, GetClockName());

		if (ClockManager)
		{
			ClockManager->ResumeClock(CurrentClockId);
		}
	}
}

void UQuartzClockHandle::QueueQuantizedSound(const UObject* WorldContextObject, UQuartzClockHandle*& InClockHandle, const FAudioComponentCommandInfo& InAudioComponentData, const FOnQuartzCommandEventBP& InDelegate, const FQuartzQuantizationBoundary& InTargetBoundary)
{
	InClockHandle = this;
	FName ClockName = GetClockName();

	if (UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem())
	{
		//Create a Queue Command, and give it the additional data that it needs
		TSharedPtr<Audio::FQuantizedQueueCommand> QueueCommandPtr = MakeShared<Audio::FQuantizedQueueCommand>();
		QueueCommandPtr->SetQueueCommand(InAudioComponentData);

		//Set up initial command info
		Audio::FQuartzQuantizedCommandInitInfo CommandInitInfo = QuartzSubsystem->CreateDataDataForSchedulePlaySound(InClockHandle, InDelegate, InTargetBoundary);

		//(Queue's setup is identical to PlaySound except for the command ptr, so fix that here)
		CommandInitInfo.QuantizedCommandPtr.Reset();
		CommandInitInfo.QuantizedCommandPtr = QueueCommandPtr;

		QuartzSubsystem->AddCommandToClock(WorldContextObject, CommandInitInfo, ClockName);	
	}
}

// deprecated: use ResetTransportQuantized
void UQuartzClockHandle::ResetTransport(const UObject* WorldContextObject, const FOnQuartzCommandEventBP& InDelegate)
{
	if (UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem())
	{
		Audio::FQuartzQuantizedCommandInitInfo Data(QuartzSubsystem->CreateDataForTransportReset(this, FQuartzQuantizationBoundary(EQuartzCommandQuantization::Bar), InDelegate));
		QuartzSubsystem->AddCommandToClock(WorldContextObject, Data, GetClockName());
	}
}

void UQuartzClockHandle::ResetTransportQuantized(const UObject* WorldContextObject, FQuartzQuantizationBoundary InQuantizationBoundary, const FOnQuartzCommandEventBP& InDelegate, UQuartzClockHandle*& ClockHandle)
{
	ClockHandle = this;
	if (UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem())
	{
		Audio::FQuartzQuantizedCommandInitInfo Data(QuartzSubsystem->CreateDataForTransportReset(this, InQuantizationBoundary, InDelegate));
		QuartzSubsystem->AddCommandToClock(WorldContextObject, Data, GetClockName());
	}
}



bool UQuartzClockHandle::IsClockRunning(const UObject* WorldContextObject)
{
	if (UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem())
	{
		return QuartzSubsystem->IsClockRunning(WorldContextObject, CurrentClockId);
	}

	return false;
}

float UQuartzClockHandle::GetDurationOfQuantizationTypeInSeconds(const UObject* WorldContextObject, const EQuartzCommandQuantization& QuantizationType, float Multiplier)
{
	if (UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem())
	{
		return QuartzSubsystem->GetDurationOfQuantizationTypeInSeconds(WorldContextObject, CurrentClockId, QuantizationType, Multiplier);
	}
	else
	{
		return INDEX_NONE;
	}
}

FQuartzTransportTimeStamp UQuartzClockHandle::GetCurrentTimestamp(const UObject* WorldContextObject)
{
	if (UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem())
	{
		return QuartzSubsystem->GetCurrentClockTimestamp(WorldContextObject, CurrentClockId);
	}
	else
	{
		return FQuartzTransportTimeStamp();
	}
}

float UQuartzClockHandle::GetEstimatedRunTime(const UObject* WorldContextObject)
{
	if (UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem())
	{
		return QuartzSubsystem->GetEstimatedClockRunTime(WorldContextObject, CurrentClockId);
	}
	else
	{
		return INDEX_NONE;
	}
}

void UQuartzClockHandle::StartOtherClock(const UObject* WorldContextObject, FName OtherClockName, FQuartzQuantizationBoundary InQuantizationBoundary, const FOnQuartzCommandEventBP& InDelegate)
{
	if (OtherClockName == CurrentClockId)
	{
		UE_LOG(LogAudioQuartz, Warning, TEXT("Clock: (%s) is attempting to start itself on a quantization boundary.  Ignoring command"), *CurrentClockId.ToString());
		return;
	}

	if (UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem())
	{
		Audio::FQuartzQuantizedCommandInitInfo Data(QuartzSubsystem->CreateDataForStartOtherClock(this, OtherClockName, InQuantizationBoundary, InDelegate));
		QuartzSubsystem->AddCommandToClock(WorldContextObject, Data, GetClockName());
	}
}

void UQuartzClockHandle::SubscribeToQuantizationEvent(const UObject* WorldContextObject, EQuartzCommandQuantization InQuantizationBoundary, const FOnQuartzMetronomeEventBP& OnQuantizationEvent, UQuartzClockHandle*& ClockHandle)
{
	ClockHandle = this;

	if (InQuantizationBoundary == EQuartzCommandQuantization::None)
	{
		UE_LOG(LogAudioQuartz, Warning, TEXT("Clock: (%s) is attempting to subscribe to 'NONE' as a Quantization Boundary.  Ignoring request"), *CurrentClockId.ToString());
		return;
	}

	if (UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem())
	{
		Audio::FQuartzClockManager* ClockManager = QuartzSubsystem->GetManagerForClock(WorldContextObject, GetClockName());
		if (ClockManager && ClockManager->DoesClockExist(CurrentClockId) && OnQuantizationEvent.IsBound())
		{
			MetronomeDelegates[static_cast<int32>(InQuantizationBoundary)].MulticastDelegate.AddUnique(OnQuantizationEvent);
			ClockManager->SubscribeToTimeDivision(CurrentClockId, GetCommandQueue(), InQuantizationBoundary);
		}
	}
}

void UQuartzClockHandle::SubscribeToAllQuantizationEvents(const UObject* WorldContextObject, const FOnQuartzMetronomeEventBP& OnQuantizationEvent, UQuartzClockHandle*& ClockHandle)
{
	ClockHandle = this;
	if (UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem())
	{
		Audio::FQuartzClockManager* ClockManager = QuartzSubsystem->GetManagerForClock(WorldContextObject, GetClockName());
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

void UQuartzClockHandle::UnsubscribeFromTimeDivision(const UObject* WorldContextObject, EQuartzCommandQuantization InQuantizationBoundary, UQuartzClockHandle*& ClockHandle)
{
	ClockHandle = this;
	if (UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem())
	{
		Audio::FQuartzClockManager* ClockManager = QuartzSubsystem->GetManagerForClock(WorldContextObject, GetClockName());
		if (ClockManager && ClockManager->DoesClockExist(CurrentClockId))
		{
			ClockManager->UnsubscribeFromTimeDivision(CurrentClockId, GetCommandQueue(), InQuantizationBoundary);
		}
	}
}

void UQuartzClockHandle::UnsubscribeFromAllTimeDivisions(const UObject* WorldContextObject, UQuartzClockHandle*& ClockHandle)
{
	ClockHandle = this;
	if (UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem())
	{
		Audio::FQuartzClockManager* ClockManager = QuartzSubsystem->GetManagerForClock(WorldContextObject, GetClockName());
		if (ClockManager && ClockManager->DoesClockExist(CurrentClockId))
		{
			ClockManager->UnsubscribeFromAllTimeDivisions(CurrentClockId, GetCommandQueue());
		}
	}
}

// Metronome Alteration (setters)
void UQuartzClockHandle::SetMillisecondsPerTick(const UObject* WorldContextObject, const FQuartzQuantizationBoundary& InQuantizationBoundary, const FOnQuartzCommandEventBP& InDelegate, UQuartzClockHandle*& ClockHandle, float MillisecondsPerTick)
{
	ClockHandle = this;
	if (MillisecondsPerTick < 0 || FMath::IsNearlyZero(MillisecondsPerTick))
	{
		UE_LOG(LogAudioQuartz, Warning, TEXT("Ignoring invalid request on Clock: %s: MillisecondsPerTick was %f"), *this->CurrentClockId.ToString(), MillisecondsPerTick);
		return;
	}

	if (UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem())
	{
		Audio::FQuartzClockTickRate TickRate;
		TickRate.SetMillisecondsPerTick(MillisecondsPerTick);

		Audio::FQuartzQuantizedCommandInitInfo Data(QuartzSubsystem->CreateDataForTickRateChange(this, InDelegate, TickRate, InQuantizationBoundary));
		QuartzSubsystem->AddCommandToClock(WorldContextObject, Data, GetClockName());
	}
}

void UQuartzClockHandle::SetTicksPerSecond(const UObject* WorldContextObject, const FQuartzQuantizationBoundary& InQuantizationBoundary, const FOnQuartzCommandEventBP& InDelegate, UQuartzClockHandle*& ClockHandle, float TicksPerSecond)
{
	ClockHandle = this;
	if (TicksPerSecond < 0 || FMath::IsNearlyZero(TicksPerSecond))
	{
		UE_LOG(LogAudioQuartz, Warning, TEXT("Ignoring invalid request on Clock: %s: TicksPerSecond was %f"), *this->CurrentClockId.ToString(), TicksPerSecond);
		return;
	}

	if (UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem())
	{
		Audio::FQuartzClockTickRate TickRate;
		TickRate.SetSecondsPerTick(1.f / TicksPerSecond);

		Audio::FQuartzQuantizedCommandInitInfo Data(QuartzSubsystem->CreateDataForTickRateChange(this, InDelegate, TickRate, InQuantizationBoundary));
		QuartzSubsystem->AddCommandToClock(WorldContextObject, Data, GetClockName());
	}
}

void UQuartzClockHandle::SetSecondsPerTick(const UObject* WorldContextObject, const FQuartzQuantizationBoundary& InQuantizationBoundary, const FOnQuartzCommandEventBP& InDelegate, UQuartzClockHandle*& ClockHandle, float SecondsPerTick)
{
	ClockHandle = this;
	if (SecondsPerTick < 0 || FMath::IsNearlyZero(SecondsPerTick))
	{
		UE_LOG(LogAudioQuartz, Warning, TEXT("Ignoring invalid request on Clock: %s: SecondsPerTick was %f"), *this->CurrentClockId.ToString(), SecondsPerTick);
		return;
	}

	if (UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem())
	{
		Audio::FQuartzClockTickRate TickRate;
		TickRate.SetSecondsPerTick(SecondsPerTick);

		Audio::FQuartzQuantizedCommandInitInfo Data(QuartzSubsystem->CreateDataForTickRateChange(this, InDelegate, TickRate, InQuantizationBoundary));
		QuartzSubsystem->AddCommandToClock(WorldContextObject, Data, GetClockName());
	}
}

void UQuartzClockHandle::SetThirtySecondNotesPerMinute(const UObject* WorldContextObject, const FQuartzQuantizationBoundary& InQuantizationBoundary, const FOnQuartzCommandEventBP& InDelegate, UQuartzClockHandle*& ClockHandle, float ThirtySecondsNotesPerMinute)
{
	ClockHandle = this;
	if (ThirtySecondsNotesPerMinute < 0 || FMath::IsNearlyZero(ThirtySecondsNotesPerMinute))
	{
		UE_LOG(LogAudioQuartz, Warning, TEXT("Ignoring invalid request on Clock: %s: ThirtySecondsNotesPerMinute was %f"), *this->CurrentClockId.ToString(), ThirtySecondsNotesPerMinute);
		return;
	}

	if (UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem())
	{
		Audio::FQuartzClockTickRate TickRate;
		TickRate.SetThirtySecondNotesPerMinute(ThirtySecondsNotesPerMinute);

		Audio::FQuartzQuantizedCommandInitInfo Data(QuartzSubsystem->CreateDataForTickRateChange(this, InDelegate, TickRate, InQuantizationBoundary));
		QuartzSubsystem->AddCommandToClock(WorldContextObject, Data, GetClockName());
	}
}

void UQuartzClockHandle::SetBeatsPerMinute(const UObject* WorldContextObject, const FQuartzQuantizationBoundary& InQuantizationBoundary, const FOnQuartzCommandEventBP& InDelegate, UQuartzClockHandle*& ClockHandle, float BeatsPerMinute)
{
	ClockHandle = this;
	if (BeatsPerMinute < 0 || FMath::IsNearlyZero(BeatsPerMinute))
	{
		UE_LOG(LogAudioQuartz, Warning, TEXT("Ignoring invalid request on Clock: %s: BeatsPerMinute was %f"), *this->CurrentClockId.ToString(), BeatsPerMinute);
		return;
	}

	if (UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem())
	{
		Audio::FQuartzClockTickRate TickRate;
		TickRate.SetBeatsPerMinute(BeatsPerMinute);

		Audio::FQuartzQuantizedCommandInitInfo Data(QuartzSubsystem->CreateDataForTickRateChange(this, InDelegate, TickRate, InQuantizationBoundary));
		QuartzSubsystem->AddCommandToClock(WorldContextObject, Data, GetClockName());
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


// returns true if OutTickRate is valid and was updated
bool UQuartzClockHandle::GetCurrentTickRate(const UObject* WorldContextObject, Audio::FQuartzClockTickRate& OutTickRate) const
{
	if (UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem())
	{
		Audio::FQuartzClockManager* ClockManager = QuartzSubsystem->GetManagerForClock(WorldContextObject, GetClockName());

		if (ClockManager)
		{
			OutTickRate = ClockManager->GetTickRateForClock(CurrentClockId);
			return true;
		}
	}

	OutTickRate = {};
	return false;
}

void UQuartzClockHandle::ProcessCommand(const Audio::FQuartzQuantizedCommandDelegateData& Data)
{
	checkSlow(Data.DelegateSubType < EQuartzCommandDelegateSubType::Count && (Data.DelegateID < QuantizedCommandDelegates.Num()));
	if (UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem())
	{
		QuartzSubsystem->PushLatencyTrackerResult(Data.RequestRecieved());
	}

	CommandDelegateGameThreadData& GameThreadEntry = QuantizedCommandDelegates[Data.DelegateID];

	GameThreadEntry.MulticastDelegate.Broadcast(Data.DelegateSubType, "Sample Payload");

	// track the number of active QuantizedCommands that may be sending info back to us.
	// (new command)
	if (Data.DelegateSubType == EQuartzCommandDelegateSubType::CommandOnQueued)
	{
		GameThreadEntry.RefCount.Increment();
	}

	// (end of a command)
	if (Data.DelegateSubType == EQuartzCommandDelegateSubType::CommandOnCanceled)
	{
		// are all the commands done?
		if (GameThreadEntry.RefCount.Decrement() == 0)
		{
			GameThreadEntry.MulticastDelegate.Clear();
		}
	}
}

void UQuartzClockHandle::ProcessCommand(const Audio::FQuartzMetronomeDelegateData& Data)
{
	if (UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem())
	{
		QuartzSubsystem->PushLatencyTrackerResult(Data.RequestRecieved());
	}

	MetronomeDelegates[static_cast<int32>(Data.Quantization)].MulticastDelegate.Broadcast(CurrentClockId, Data.Quantization, Data.Bar, Data.Beat, Data.BeatFraction);
}