// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Subsystems/WorldSubsystem.h"
#include "Sound/QuartzQuantizationUtilities.h"
#include "Tickable.h"

#include "QuartzSubsystem.generated.h"

// forwards
namespace Audio
{
	class FMixerDevice;
	class FQuartzClockManager;
	class FShareableQuartzCommandQueue;
}

using MetronomeCommandQueuePtr = TSharedPtr<Audio::FShareableQuartzCommandQueue, ESPMode::ThreadSafe>;


UCLASS(DisplayName = "Quartz")
class AUDIOMIXER_API UQuartzSubsystem : public UWorldSubsystem, public FQuartLatencyTracker, public FTickableGameObject
{
	GENERATED_BODY()

public:
	// ctor/dtor
	UQuartzSubsystem();
	~UQuartzSubsystem();

	//~ Begin FTickableGameObject Interface
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject Interface

	// if we have another stakeholder later we can use polymorphism here.
	// in the mean time, we are avoiding the virtual overhead
	void SubscribeToQuartzTick(UQuartzClockHandle* InObjectToTick);
	void UnsubscribeFromQuartzTick(UQuartzClockHandle* InObjectToTick);

	// static methods
	static UQuartzSubsystem* Get(UWorld* World);

	// create a new command queue to be shared between ClockHandles and other threads
	static TSharedPtr<Audio::FShareableQuartzCommandQueue, ESPMode::ThreadSafe> CreateQuartzCommandQueue();

	// Helper functions for initializing quantized command initialization struct (to consolidate eyesore)
	static Audio::FQuartzQuantizedRequestData CreateDataForTickRateChange(UQuartzClockHandle* InClockHandle, const FOnQuartzCommandEventBP& InDelegate, const FQuartzClockTickRate& InNewTickRate, FQuartzQuantizationBoundary& InQuantizationBoundary);
	static Audio::FQuartzQuantizedRequestData CreateDataForTransportReset(UQuartzClockHandle* InClockHandle, const FOnQuartzCommandEventBP& InDelegate);
	static Audio::FQuartzQuantizedRequestData CreateDataDataForSchedulePlaySound(UQuartzClockHandle* InClockHandle, const FOnQuartzCommandEventBP& InDelegate, FQuartzQuantizationBoundary& InQuantizationBoundary);

	// Clock Creation
	// create a new clock (or return handle if clock already exists)
	UFUNCTION(BlueprintCallable, Category = "Quartz Clock Handle", meta = (WorldContext = "WorldContextObject"))
	UQuartzClockHandle* CreateNewClock(const UObject* WorldContextObject, FName ClockName, FQuartzClockSettings InSettings, bool bOverrideSettingsIfClockExists = false) const;

	// get handle for existing clock
	UFUNCTION(BlueprintCallable, Category = "Quartz Clock Handle", meta = (WorldContext = "WorldContextObject"))
	UQuartzClockHandle* GetHandleForClock(const UObject* WorldContextObject, FName ClockName) const;

	// returns true if the clock exists
	UFUNCTION(BlueprintCallable, Category = "Quartz Clock Handle", meta = (WorldContext = "WorldContextObject"))
	bool DoesClockExist(const UObject* WorldContextObject, FName ClockName) const;

	// latency data (Game thread -> Audio Render Thread)
	UFUNCTION(BlueprintCallable, Category = "Quartz Subsystem", meta = (WorldContext = "WorldContextObject"))
	float GetGameThreadToAudioRenderThreadAverageLatency(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "Quartz Subsystem", meta = (WorldContext = "WorldContextObject"))
	float GetGameThreadToAudioRenderThreadMinLatency(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "Quartz Subsystem", meta = (WorldContext = "WorldContextObject"))
	float GetGameThreadToAudioRenderThreadMaxLatency(const UObject* WorldContextObject);

	// latency data (Audio Render Thread -> Game thread)
	UFUNCTION(BlueprintCallable, Category = "Quartz Subsystem")
	float GetAudioRenderThreadToGameThreadAverageLatency();

	UFUNCTION(BlueprintCallable, Category = "Quartz Subsystem")
	float GetAudioRenderThreadToGameThreadMinLatency();

	UFUNCTION(BlueprintCallable, Category = "Quartz Subsystem")
	float GetAudioRenderThreadToGameThreadMaxLatency();

	// latency data (Round trip)
	UFUNCTION(BlueprintCallable, Category = "Quartz Subsystem", meta = (WorldContext = "WorldContextObject"))
	float GetRoundTripAverageLatency(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "Quartz Subsystem", meta = (WorldContext = "WorldContextObject"))
	float GetRoundTripMinLatency(const UObject* WorldContextObject);

	UFUNCTION(BlueprintCallable, Category = "Quartz Subsystem", meta = (WorldContext = "WorldContextObject"))
	float GetRoundTripMaxLatency(const UObject* WorldContextObject);

	// C++ interface. (most of these methods are exposed to BP via UClockHandle)
		// Clock Manipulation
	UFUNCTION(BlueprintCallable, Category = "Quartz Subsystem", meta = (WorldContext = "WorldContextObject"))
	void PauseClock(const UObject* WorldContextObject, UPARAM(ref) UQuartzClockHandle*& InClockHandle);

	UFUNCTION(BlueprintCallable, Category = "Quartz Subsystem", meta = (WorldContext = "WorldContextObject"))
	void ResumeClock(const UObject* WorldContextObject, UPARAM(ref) UQuartzClockHandle*& InClockHandle);

	UFUNCTION(BlueprintCallable, Category = "Quartz Subsystem", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = 0, AutoCreateRefTerm = "InDelegate", Keywords = "Transport, Counter"))
	void ResetTransport(const UObject* WorldContextObject, UPARAM(ref) UQuartzClockHandle*& InClockHandle, const FOnQuartzCommandEventBP& InDelegate);

	// change the tick rate of the clock
	UFUNCTION(BlueprintCallable, Category = "Quantization", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = 2, AutoCreateRefTerm = "InDelegate", Keywords = "BPM, Tempo"))
	void ChangeTickRate(const UObject* WorldContextObject, UPARAM(ref) UQuartzClockHandle*& InClockHandle, const FQuartzClockTickRate& InNewTickRate, UPARAM(ref) FQuartzQuantizationBoundary& InQuantizationBoundary, const FOnQuartzCommandEventBP& InDelegate);

	// Metronome Subscription
	UFUNCTION(BlueprintCallable, Category = "Quartz Subsystem", meta = (WorldContext = "WorldContextObject"))
	void SubscribeToQuantizationEvent(const UObject* WorldContextObject, UPARAM(ref) UQuartzClockHandle*& InClockHandle, EQuartzCommandQuantization InQuantizationBoundary, const FOnQuartzMetronomeEventBP& OnQuantizationEvent);

	UFUNCTION(BlueprintCallable, Category = "Quartz Subsystem", meta = (WorldContext = "WorldContextObject"))
	void SubscribeToAllQuantizationEvents(const UObject* WorldContextObject, UPARAM(ref) UQuartzClockHandle*& InClockHandle, const FOnQuartzMetronomeEventBP& OnQuantizationEvent);

	UFUNCTION(BlueprintCallable, Category = "Quartz Subsystem", meta = (WorldContext = "WorldContextObject"))
	void UnsubscribeFromTimeDivision(const UObject* WorldContextObject, UPARAM(ref) UQuartzClockHandle*& InClockHandle, EQuartzCommandQuantization InQuantizationBoundary);

	UFUNCTION(BlueprintCallable, Category = "Quartz Subsystem", meta = (WorldContext = "WorldContextObject"))
	void UnsubscribeFromAllTimeDivisions(const UObject* WorldContextObject, UPARAM(ref) UQuartzClockHandle*& InClockHandle);

	// non-BP version of above, called from begin destroy on clock handle
	void UnsubscribeFromAllTimeDivisionsInternal(const UObject* WorldContextObject, UQuartzClockHandle& InClockHandle);

	void AddCommandToClock(const UObject* WorldContextObject, Audio::FQuartzQuantizedCommandInitInfo& InQuantizationCommandInitInfo);


private:
	Audio::FQuartzClockManager* GetClockManager(const UObject* WorldContextObject) const;

	// list of objects needing to be ticked by Quartz
	TArray<UQuartzClockHandle*> QuartzTickSubscribers;

	// index to track the next clock handle to tick (if updates are being amortized across multiple UObject Ticks)
	int32 UpdateIndex{ 0 };

}; // class UQuartzGameSubsystem 