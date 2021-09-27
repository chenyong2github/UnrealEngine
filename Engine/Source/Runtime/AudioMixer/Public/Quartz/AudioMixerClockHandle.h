// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Sound/QuartzQuantizationUtilities.h"
#include "Sound/QuartzSubscription.h"
#include "Quartz/QuartzSubsystem.h"
#include "Quartz/QuartzMetronome.h"
#include "UObject/GCObject.h"

#include "AudioMixerClockHandle.generated.h"


class UAudioComponent;


class AUDIOMIXER_API FQuartzTickableObject
{
	struct AUDIOMIXER_API FQuartzTickableObjectGCObjectMembers : public FGCObject
	{
	public:
		UQuartzSubsystem* QuartzSubsystem;
		UWorld* WorldPtr{ nullptr };

		void AddReferencedObjects(FReferenceCollector& Collector) override;
		virtual FString GetReferencerName() const override
		{
			return TEXT("FQuartzTickableObject::FQuartzTickableObjectGCObjectMembers");
		}
	};

	public:
		FQuartzTickableObject() {}
		virtual ~FQuartzTickableObject();

		FQuartzTickableObject* Init(UWorld* InWorldPtr);

		// called by the associated QuartzSubsystem
		void QuartzTick(float DeltaTime);
		bool QuartzIsTickable() const;
		UWorld* QuartzGetWorld() const { return GCObjectMembers.WorldPtr; }

		bool IsInitialized() const { return bHasBeenInitialized; }

		// access to the associated QuartzSubsystem
		UQuartzSubsystem* GetQuartzSubsystem() const { return GCObjectMembers.QuartzSubsystem; }

		TSharedPtr<Audio::FShareableQuartzCommandQueue, ESPMode::ThreadSafe> GetCommandQueue();

		int32 AddCommandDelegate(const FOnQuartzCommandEventBP& InDelegate, TArray<TSharedPtr<Audio::FShareableQuartzCommandQueue, ESPMode::ThreadSafe>>& TargetSubscriberArray);

		// virtual interface
		virtual void ProcessCommand(const Audio::FQuartzQuantizedCommandDelegateData& Data) {};

		virtual void ProcessCommand(const Audio::FQuartzMetronomeDelegateData& Data) {};

		virtual void ProcessCommand(const Audio::FQuartzQueueCommandData& Data) {};

		void Shutdown();


	protected:
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
		TArray<TFunction<void(FQuartzTickableObject*)>> TempCommandQueue;

		TArray<CommandDelegateGameThreadData> QuantizedCommandDelegates;
		MetronomeDelegateGameThreadData MetronomeDelegates[static_cast<int32>(EQuartzCommandQuantization::Count)];

	private:
		FQuartzTickableObjectGCObjectMembers GCObjectMembers;
		bool bHasBeenInitialized = false;
};

UCLASS(BlueprintType, Blueprintable, Transient, ClassGroup = Quartz, meta = (BlueprintSpawnableComponent))
class AUDIOMIXER_API UQuartzClockHandle : public UObject, public FQuartzTickableObject
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


// Begin Blueprint Interface

	// Clock manipulation
	UFUNCTION(BlueprintCallable, Category = "Quartz Clock", meta = (WorldContext = "WorldContextObject"))
	void StartClock(const UObject* WorldContextObject, UQuartzClockHandle*& ClockHandle);

	UFUNCTION(BlueprintCallable, Category = "Quartz Subsystem", meta = (WorldContext = "WorldContextObject"))
	void StopClock(const UObject* WorldContextObject, bool CancelPendingEvents, UQuartzClockHandle*& ClockHandle);

	UFUNCTION(BlueprintCallable, Category = "Quartz Subsystem", meta = (WorldContext = "WorldContextObject"))
	void PauseClock(const UObject* WorldContextObject, UQuartzClockHandle*& ClockHandle);

	UFUNCTION(BlueprintCallable, Category = "Quartz Clock", meta = (WorldContext = "WorldContextObject"))
	void ResumeClock(const UObject* WorldContextObject, UQuartzClockHandle*& ClockHandle);
	UFUNCTION(BlueprintCallable, Category = "Quartz Clock", meta = (DeprecatedFunction, DeprecationMessage="Please use ResetTransportQuantized instead", WorldContext = "WorldContextObject", AutoCreateRefTerm = "InDelegate", Keywords = "Transport, Counter"))
	void ResetTransport(const UObject* WorldContextObject, const FOnQuartzCommandEventBP& InDelegate);

	UFUNCTION(BlueprintCallable, Category = "Quartz Clock", meta = (WorldContext = "WorldContextObject", AutoCreateRefTerm = "InDelegate", Keywords = "Transport, Counter"))
	void ResetTransportQuantized(const UObject* WorldContextObject, FQuartzQuantizationBoundary InQuantizationBoundary, const FOnQuartzCommandEventBP& InDelegate, UQuartzClockHandle*& ClockHandle);

	UFUNCTION(BlueprintCallable, Category = "Quartz Clock", meta = (WorldContext = "WorldContextObject", Keywords = "Transport, Counter"))
	bool IsClockRunning(const UObject* WorldContextObject);

	/** Returns the duration in seconds of the given Quantization Type
	 *
	 * @param The Quantization type to measure
	 * @param The quantity of the Quantization Type to calculate the time of
	 * @return The duration, in seconds, of a multiplier amount of the Quantization Type, or -1 in the case the clock is invalid
	 */
	UFUNCTION(BlueprintCallable, Category = "Quartz Clock", meta = (WorldContext = "WorldContextObject"))
	float GetDurationOfQuantizationTypeInSeconds(const UObject* WorldContextObject, const EQuartzCommandQuantization& QuantizationType, float Multiplier = 1.0f);

	//Retrieves a timestamp for the clock
	UFUNCTION(BlueprintCallable, Category = "Quartz Clock Handle", meta = (WorldContext = "WorldContextObject"))
	FQuartzTransportTimeStamp GetCurrentTimestamp(const UObject* WorldContextObject);

	// Returns the amount of time, in seconds, the clock has been running. Caution: due to latency, this will not be perfectly accurate
	UFUNCTION(BlueprintCallable, Category = "Quartz Clock Handle", meta = (WorldContext = "WorldContextObject"))
	float GetEstimatedRunTime(const UObject* WorldContextObject);

	// "other" clock manipulation
	UFUNCTION(BlueprintCallable, Category = "Quartz Clock", meta = (WorldContext = "WorldContextObject", AutoCreateRefTerm = "InDelegate", Keywords = "Transport, Counter"))
	void StartOtherClock(const UObject* WorldContextObject, FName OtherClockName, FQuartzQuantizationBoundary InQuantizationBoundary, const FOnQuartzCommandEventBP& InDelegate);

	// Metronome subscription
	UFUNCTION(BlueprintCallable, Category = "Quartz Clock", meta = (WorldContext = "WorldContextObject"))
	void SubscribeToQuantizationEvent(const UObject* WorldContextObject, EQuartzCommandQuantization InQuantizationBoundary, const FOnQuartzMetronomeEventBP& OnQuantizationEvent, UQuartzClockHandle*& ClockHandle);

	UFUNCTION(BlueprintCallable, Category = "Quartz Clock", meta = (WorldContext = "WorldContextObject"))
	void SubscribeToAllQuantizationEvents(const UObject* WorldContextObject, const FOnQuartzMetronomeEventBP& OnQuantizationEvent, UQuartzClockHandle*& ClockHandle);

	UFUNCTION(BlueprintCallable, Category = "Quartz Clock", meta = (WorldContext = "WorldContextObject"))
	void UnsubscribeFromTimeDivision(const UObject* WorldContextObject, EQuartzCommandQuantization InQuantizationBoundary, UQuartzClockHandle*& ClockHandle);

	UFUNCTION(BlueprintCallable, Category = "Quartz Clock", meta = (WorldContext = "WorldContextObject"))
	void UnsubscribeFromAllTimeDivisions(const UObject* WorldContextObject, UQuartzClockHandle*& ClockHandle);

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
	void QueueQuantizedSound(const UObject* WorldContextObject, UQuartzClockHandle*& ClockHandle, const FAudioComponentCommandInfo& AudioComponentData, const FOnQuartzCommandEventBP& InDelegate, const FQuartzQuantizationBoundary& InTargetBoundary);

	UQuartzClockHandle* SubscribeToClock(const UObject* WorldContextObject, FName ClockName);

	FName GetClockName() const { return CurrentClockId; }

	FName GetHandleName() const { return ClockHandleId; }
	
	bool DoesClockExist(const UObject* WorldContextObject) const
	{
		if (UQuartzSubsystem* QuartzSubsystem = GetQuartzSubsystem())
		{
			return QuartzSubsystem->DoesClockExist(WorldContextObject, CurrentClockId);
		}

		return false;
	}

	virtual void ProcessCommand(const Audio::FQuartzQuantizedCommandDelegateData& Data) override;

	virtual void ProcessCommand(const Audio::FQuartzMetronomeDelegateData& Data) override;

	bool GetCurrentTickRate(const UObject* WorldContextObject, Audio::FQuartzClockTickRate& OutTickRate) const;

private:
	
	FName CurrentClockId;

	FName ClockHandleId;

	bool bConnectedToClock{ false };

}; // class UQuartzClockHandle
