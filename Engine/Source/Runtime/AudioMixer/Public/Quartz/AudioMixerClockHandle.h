// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Sound/QuartzQuantizationUtilities.h"
#include "Sound/QuartzSubscription.h"
#include "Quartz/QuartzSubsystem.h"
#include "Quartz/QuartzMetronome.h"

#include "AudioMixerClockHandle.generated.h"


UCLASS(BlueprintType, Blueprintable, Transient, ClassGroup = Quartz, meta = (BlueprintSpawnableComponent))
class AUDIOMIXER_API UQuartzClockHandle : public UObject
{
	GENERATED_BODY()

public:
	// ctor
	UQuartzClockHandle();

	// dtor
	~UQuartzClockHandle();

	// begin UObject interface
	void BeginDestroy() override;
	// end UObject interface

	UQuartzClockHandle* Init(UWorld* InWorldPtr);

	// called by the associated QuartzSubsystem
	void QuartzTick(float DeltaTime);
	bool QuartzIsTickable() const;

	// Register an event to respond to a specific quantization event
	void SubscribeToQuantizationEvent(EQuartzCommandQuantization InQuantizationBoundary, const FOnQuartzMetronomeEventBP& OnQuantizationEvent);

	// Register an event to respond to all quantization events
	void SubscribeToAllQuantizationEvents(const FOnQuartzMetronomeEventBP& OnQuantizationEvent);

	// access to the associated QuartzSubsystem
	UQuartzSubsystem* GetQuartzSubsystem() const{ return QuartzSubsystem; }

	UQuartzClockHandle* SubscribeToClock(const UObject* WorldContextObject, FName ClockName);

	FName GetClockName() const { return CurrentClockId; }

	FName GetHandleName() const { return ClockHandleId; }

	int32 AddCommandDelegate(const FOnQuartzCommandEventBP& InDelegate, TSharedPtr<Audio::FShareableQuartzCommandQueue, ESPMode::ThreadSafe>& OutCommandQueuePtr);

	bool DoesClockExist(const UObject* WorldContextObject) const
	{
		return QuartzSubsystem->DoesClockExist(WorldContextObject, CurrentClockId);
	}

	TSharedPtr<Audio::FShareableQuartzCommandQueue, ESPMode::ThreadSafe> GetCommandQueue()
	{
		checkSlow(CommandQueuePtr.IsValid());
		return CommandQueuePtr;
	}

	void ProcessCommand(Audio::FQuartzQuantizedCommandDelegateData Data);

	void ProcessCommand(Audio::FQuartzMetronomeDelegateData Data);


private:
	struct CommandDelegateGameThreadData
	{
		FOnQuartzCommandEvent MulticastDelegate;
		FThreadSafeCounter RefCount;
	};

	struct MetronomeDelegateGameThreadData
	{
		FOnQuartzMetronomeEvent MulticastDelegate;
	};

	void PumpCommandQueue();

	TSharedPtr<Audio::FShareableQuartzCommandQueue, ESPMode::ThreadSafe> CommandQueuePtr;

	TArray<CommandDelegateGameThreadData> QuantizedCommandDelegates;

	MetronomeDelegateGameThreadData MetronomeDelegates[static_cast<int32>(EQuartzCommandQuantization::Count)];

	UQuartzSubsystem* QuartzSubsystem;

	FName ClockHandleId;

	FName CurrentClockId;

	bool bConnectedToClock{ false };

	UWorld* WorldPtr{ nullptr };

}; // class UClockHandleBlueprintLibrary
