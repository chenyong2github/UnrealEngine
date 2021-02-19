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

// Begin Blueprint Interface

	// Clock manipulation
	UFUNCTION(BlueprintCallable, Category = "Quartz Clock", meta = (WorldContext = "WorldContextObject"))
	void StartClock(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "Quartz Subsystem", meta = (WorldContext = "WorldContextObject"))
	void StopClock(const UObject* WorldContextObject, bool CancelPendingEvents);

	UFUNCTION(BlueprintCallable, Category = "Quartz Subsystem", meta = (WorldContext = "WorldContextObject"))
	void PauseClock(const UObject* WorldContextObject);

 	UFUNCTION(BlueprintCallable, Category = "Quartz Clock", meta = (WorldContext = "WorldContextObject"))
 	void ResumeClock(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "Quartz Clock", meta = (WorldContext = "WorldContextObject", AutoCreateRefTerm = "InDelegate", Keywords = "Transport, Counter"))
	void ResetTransport(const UObject* WorldContextObject, FQuartzQuantizationBoundary InQuantizationBoundary, const FOnQuartzCommandEventBP& InDelegate);

	UFUNCTION(BlueprintCallable, Category = "Quartz Clock", meta = (WorldContext = "WorldContextObject", Keywords = "Transport, Counter"))
	bool IsClockRunning(const UObject* WorldContextObject);

	// "other" clock manipulation
	UFUNCTION(BlueprintCallable, Category = "Quartz Clock", meta = (WorldContext = "WorldContextObject", AutoCreateRefTerm = "InDelegate", Keywords = "Transport, Counter"))
	void StartOtherClock(const UObject* WorldContextObject, FName OtherClockName, FQuartzQuantizationBoundary InQuantizationBoundary, const FOnQuartzCommandEventBP& InDelegate);

	// Metronome subscription
	UFUNCTION(BlueprintCallable, Category = "Quartz Clock", meta = (WorldContext = "WorldContextObject"))
	void SubscribeToQuantizationEvent(const UObject* WorldContextObject, EQuartzCommandQuantization InQuantizationBoundary, const FOnQuartzMetronomeEventBP& OnQuantizationEvent);

	UFUNCTION(BlueprintCallable, Category = "Quartz Clock", meta = (WorldContext = "WorldContextObject"))
	void SubscribeToAllQuantizationEvents(const UObject* WorldContextObject, const FOnQuartzMetronomeEventBP& OnQuantizationEvent);

	UFUNCTION(BlueprintCallable, Category = "Quartz Clock", meta = (WorldContext = "WorldContextObject"))
	void UnsubscribeFromTimeDivision(const UObject* WorldContextObject, EQuartzCommandQuantization InQuantizationBoundary);

	UFUNCTION(BlueprintCallable, Category = "Quartz Clock", meta = (WorldContext = "WorldContextObject"))
	void UnsubscribeFromAllTimeDivisions(const UObject* WorldContextObject);


	// Metronome Alteration (setters)
	UFUNCTION(BlueprintCallable, Category = "Quantization", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "QuantizationBoundary, Delegate", AutoCreateRefTerm = "QuantizationBoundary, Delegate", Keywords = "BPM, Tempo"))
	void SetMillisecondsPerTick(const UObject* WorldContextObject, UPARAM(ref) const FQuartzQuantizationBoundary& QuantizationBoundary, const FOnQuartzCommandEventBP& Delegate, UQuartzClockHandle*& ClockHandle, float MillisecondsPerTick = 100.f);

	UFUNCTION(BlueprintCallable, Category = "Quantization", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "QuantizationBoundary, Delegate", AutoCreateRefTerm = "QuantizationBoundary, Delegate", Keywords = "BPM, Tempo"))
	void SetTicksPerSecond(const UObject* WorldContextObject, UPARAM(ref) const FQuartzQuantizationBoundary& QuantizationBoundary, const FOnQuartzCommandEventBP& Delegate, UQuartzClockHandle*& ClockHandle, float TicksPerSecond = 10.f);

	UFUNCTION(BlueprintCallable, Category = "Quantization", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "QuantizationBoundary, Delegate", AutoCreateRefTerm = "QuantizationBoundary, Delegate", Keywords = "BPM, Tempo"))
	void SetSecondsPerTick(const UObject* WorldContextObject, UPARAM(ref) const FQuartzQuantizationBoundary& QuantizationBoundary, const FOnQuartzCommandEventBP& Delegate, UQuartzClockHandle*& ClockHandle, float SecondsPerTick = 0.25f);

	UFUNCTION(BlueprintCallable, Category = "Quantization", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "QuantizationBoundary, Delegate", AutoCreateRefTerm = "QuantizationBoundary, Delegate", Keywords = "BPM, Tempo"))
	void SetThirtySecondNotesPerMinute(const UObject* WorldContextObject, UPARAM(ref) const FQuartzQuantizationBoundary& QuantizationBoundary, const FOnQuartzCommandEventBP& Delegate, UQuartzClockHandle*& ClockHandle, float ThirtySecondsNotesPerMinute = 960.f);

	UFUNCTION(BlueprintCallable, Category = "Quantization", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "QuantizationBoundary, Delegate", AutoCreateRefTerm = "QuantizationBoundary, Delegate", Keywords = "BPM, Tempo"))
	void SetBeatsPerMinute(const UObject* WorldContextObject, UPARAM(ref) const FQuartzQuantizationBoundary& QuantizationBoundary, const FOnQuartzCommandEventBP& Delegate, UQuartzClockHandle*& ClockHandle, float BeatsPerMinute = 60.f);


	// Metronome getters
	UFUNCTION(BlueprintCallable, Category = "Quantization", meta = (WorldContext = "WorldContextObject", AutoCreateRefTerm = "InDelegate", Keywords = "BPM, Tempo"))
	float GetMillisecondsPerTick(const UObject* WorldContextObject) const;

	UFUNCTION(BlueprintCallable, Category = "Quantization", meta = (WorldContext = "WorldContextObject", AutoCreateRefTerm = "InDelegate", Keywords = "BPM, Tempo"))
	float GetTicksPerSecond(const UObject* WorldContextObject) const;

	UFUNCTION(BlueprintCallable, Category = "Quantization", meta = (WorldContext = "WorldContextObject", AutoCreateRefTerm = "InDelegate", Keywords = "BPM, Tempo"))
	float GetSecondsPerTick(const UObject* WorldContextObject) const;

	UFUNCTION(BlueprintCallable, Category = "Quantization", meta = (WorldContext = "WorldContextObject", AutoCreateRefTerm = "InDelegate", Keywords = "BPM, Tempo"))
	float GetThirtySecondNotesPerMinute(const UObject* WorldContextObject) const;

	UFUNCTION(BlueprintCallable, Category = "Quantization", meta = (WorldContext = "WorldContextObject", AutoCreateRefTerm = "InDelegate", Keywords = "BPM, Tempo"))
	float GetBeatsPerMinute(const UObject* WorldContextObject) const;

// End Blueprint Interface



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
		check(CommandQueuePtr.IsValid());
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
	
	bool GetCurrentTickRate(const UObject* WorldContextObject, Audio::FQuartzClockTickRate& OutTickRate) const;

	TSharedPtr<Audio::FShareableQuartzCommandQueue, ESPMode::ThreadSafe> CommandQueuePtr;

	TArray<CommandDelegateGameThreadData> QuantizedCommandDelegates;

	MetronomeDelegateGameThreadData MetronomeDelegates[static_cast<int32>(EQuartzCommandQuantization::Count)];

	UPROPERTY(Transient)
	UQuartzSubsystem* QuartzSubsystem;

	FName ClockHandleId;

	FName CurrentClockId;

	bool bConnectedToClock{ false };

	UPROPERTY(Transient)
	UWorld* WorldPtr{ nullptr };

}; // class UClockHandleBlueprintLibrary
