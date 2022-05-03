// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Subsystems/WorldSubsystem.h"
#include "Quartz/AudioMixerClockManager.h"
#include "Sound/QuartzQuantizationUtilities.h"

#include "QuartzSubsystem.generated.h"

// forwards
namespace Audio
{
	class FMixerDevice;
	class FQuartzClockManager;

	template<class ListenerType>
	class TQuartzShareableCommandQueue;
}

class FQuartzTickableObject;
using MetronomeCommandQueuePtr = TSharedPtr<Audio::TQuartzShareableCommandQueue<FQuartzTickableObject>, ESPMode::ThreadSafe>;

// GetManagerForClock() logic will need to be updated if more entries are present
UENUM(BlueprintType)
enum class EQuarztClockManagerType : uint8
{
	AudioEngine		UMETA(DisplayName = "Audio Engine", ToolTip = "Sample-accurate clock managment by the audio renderer"),
	QuartzSubsystem	UMETA(DisplayName = "Transport Relative", ToolTip = "Loose clock management by the Quartz Subsystem in UObjectTick.  (not sample-accurate. Used automatically when no Audio Device is present)"),
	Count			UMETA(Hidden)
};


UCLASS(DisplayName = "Quartz")
class AUDIOMIXER_API UQuartzSubsystem : public UTickableWorldSubsystem, public FQuartLatencyTracker
{
	GENERATED_BODY()

public:
	// ctor/dtor
	UQuartzSubsystem();
	~UQuartzSubsystem();

	//~ Begin UWorldSubsystem Interface
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	void BeginDestroy() override;
	//~ End UWorldSubsystem Interface

	//~ Begin FTickableGameObject Interface
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

	void SubscribeToQuartzTick(FQuartzTickableObject* InObjectToTick);
	void UnsubscribeFromQuartzTick(FQuartzTickableObject* InObjectToTick);
	//~ End FTickableGameObject Interface

	// static methods
	static UQuartzSubsystem* Get(UWorld* World);

	// create a new command queue to be shared between ClockHandles and other threads
	static TSharedPtr<Audio::TQuartzShareableCommandQueue<FQuartzTickableObject>, ESPMode::ThreadSafe> CreateQuartzCommandQueue();

	// Helper functions for initializing quantized command initialization struct (to consolidate eyesore)
	static Audio::FQuartzQuantizedRequestData CreateRequestDataForTickRateChange(UQuartzClockHandle* InClockHandle, const FOnQuartzCommandEventBP& InDelegate, const Audio::FQuartzClockTickRate& InNewTickRate, const FQuartzQuantizationBoundary& InQuantizationBoundary);
	static Audio::FQuartzQuantizedRequestData CreateRequestDataForTransportReset(UQuartzClockHandle* InClockHandle, const FQuartzQuantizationBoundary& InQuantizationBoundary, const FOnQuartzCommandEventBP& InDelegate);
	static Audio::FQuartzQuantizedRequestData CreateRequestDataForStartOtherClock(UQuartzClockHandle* InClockHandle, FName InClockToStart, const FQuartzQuantizationBoundary& InQuantizationBoundary, const FOnQuartzCommandEventBP& InDelegate);
	static Audio::FQuartzQuantizedRequestData CreateRequestDataForSchedulePlaySound(UQuartzClockHandle* InClockHandle, const FOnQuartzCommandEventBP& InDelegate, const FQuartzQuantizationBoundary& InQuantizationBoundary);

	// DEPRECATED HELPERS: non-static versions of the above CreateDataFor...() functions
	UE_DEPRECATED(5.1, "Use the static (CreateRequestDataFor) version of this function instead")
	Audio::FQuartzQuantizedRequestData CreateDataForTickRateChange(UQuartzClockHandle* InClockHandle, const FOnQuartzCommandEventBP& InDelegate, const Audio::FQuartzClockTickRate& InNewTickRate, const FQuartzQuantizationBoundary& InQuantizationBoundary);

	UE_DEPRECATED(5.1, "Use the static (CreateRequestDataFor) version of this function instead")
	Audio::FQuartzQuantizedRequestData CreateDataForTransportReset(UQuartzClockHandle* InClockHandle, const FQuartzQuantizationBoundary& InQuantizationBoundary, const FOnQuartzCommandEventBP& InDelegate);
	
	UE_DEPRECATED(5.1, "Use the static (CreateRequestDataFor) version of this function instead")
	Audio::FQuartzQuantizedRequestData CreateDataForStartOtherClock(UQuartzClockHandle* InClockHandle, FName InClockToStart, const FQuartzQuantizationBoundary& InQuantizationBoundary, const FOnQuartzCommandEventBP& InDelegate);

	UE_DEPRECATED(5.1, "Use the static (CreateRequestDataFor) version of this function instead")
	Audio::FQuartzQuantizedRequestData CreateDataDataForSchedulePlaySound(UQuartzClockHandle* InClockHandle, const FOnQuartzCommandEventBP& InDelegate, const FQuartzQuantizationBoundary& InQuantizationBoundary);

	UFUNCTION(BlueprintCallable, Category = "Quartz Clock Handle", meta = (DeprecatedFunction, DeprecationMessage = "Quartz is always enabled. This function will always return true"))
	bool IsQuartzEnabled();

	// Clock Creation
	// create a new clock (or return handle if clock already exists)
	UFUNCTION(BlueprintCallable, Category = "Quartz Clock Handle", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "bUseAudioEngineClockManager"))
	UQuartzClockHandle* CreateNewClock(const UObject* WorldContextObject, FName ClockName, FQuartzClockSettings InSettings, bool bOverrideSettingsIfClockExists = false, bool bUseAudioEngineClockManager = true);

	// delete an existing clock given its name
	UFUNCTION(BlueprintCallable, Category = "Quartz Clock Handle", meta = (WorldContext = "WorldContextObject"))
	void DeleteClockByName(const UObject* WorldContextObject, FName ClockName);

	// delete an existing clock given its clock handle
	UFUNCTION(BlueprintCallable, Category = "Quartz Clock Handle", meta = (WorldContext = "WorldContextObject"))
	void DeleteClockByHandle(const UObject* WorldContextObject, UPARAM(ref) UQuartzClockHandle*& InClockHandle);

	// get handle for existing clock
	UFUNCTION(BlueprintCallable, Category = "Quartz Clock Handle", meta = (WorldContext = "WorldContextObject"))
	UQuartzClockHandle* GetHandleForClock(const UObject* WorldContextObject, FName ClockName);

	// returns true if the clock exists
	UFUNCTION(BlueprintCallable, Category = "Quartz Clock Handle", meta = (WorldContext = "WorldContextObject"))
	bool DoesClockExist(const UObject* WorldContextObject, FName ClockName);

	// returns true if the clock is running
	UFUNCTION(BlueprintCallable, Category = "Quartz Clock Handle", meta = (WorldContext = "WorldContextObject", DeprecatedFunction, DeprecationMessage = "Obtain and Query a UQuartzClockHandle instead"))
	bool IsClockRunning(const UObject* WorldContextObject, FName ClockName);

	/** Returns the duration in seconds of the given Quantization Type
	 * 
	 * @param The Quantization type to measure
	 * @param The quantity of the Quantization Type to calculate the time of
	 * @return The duration, in seconds, of a multiplier amount of the Quantization Type, or -1 in the case the clock is invalid
	 */
	UFUNCTION(BlueprintCallable, Category = "Quartz Clock Handle", meta = (WorldContext = "WorldContextObject", DeprecatedFunction, DeprecationMessage = "Obtain and Query a UQuartzClockHandle instead"))
	float GetDurationOfQuantizationTypeInSeconds(const UObject* WorldContextObject, FName ClockName, const EQuartzCommandQuantization& QuantizationType, float Multiplier = 1.0f);

	// Retrieves a timestamp for the clock
	UFUNCTION(BlueprintCallable, Category = "Quartz Clock Handle", meta = (WorldContext = "WorldContextObject", DeprecatedFunction, DeprecationMessage = "Obtain and Query a UQuartzClockHandle instead"))
	FQuartzTransportTimeStamp GetCurrentClockTimestamp(const UObject* WorldContextObject, const FName& InClockName);

	// Returns the amount of time, in seconds, the clock has been running. Caution: due to latency, this will not be perfectly accurate
	UFUNCTION(BlueprintCallable, Category = "Quartz Clock Handle", meta = (WorldContext = "WorldContextObject", DeprecatedFunction, DeprecationMessage = "Obtain and Query a UQuartzClockHandle instead"))
	float GetEstimatedClockRunTime(const UObject* WorldContextObject, const FName& InClockName);

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

	UE_DEPRECATED(5.1, "Obtain and use a UQuartzClockHandle / FQuartzClockProxy instead")
	void AddCommandToClock(const UObject* WorldContextObject, Audio::FQuartzQuantizedCommandInitInfo& InQuantizationCommandInitInfo, FName ClockName);

	// maxtodo: deprecate this? or make private?
	Audio::FQuartzClockManager* GetManagerForClock(const UObject* WorldContextObject, FName ExistingClockName = FName());

private:
	Audio::FQuartzClockManager SubsystemClockManager;

	// list of objects needing to be ticked by Quartz
	TArray<FQuartzTickableObject *> QuartzTickSubscribers;

	// index to track the next clock handle to tick (if updates are being amortized across multiple UObject Ticks)
	int32 UpdateIndex{ 0 };

	// Tracks which system is managing a clock (given that clock's FName)
	// currently only this subsystem or the FMixerDevice
	TMap<FName, EQuarztClockManagerType> ClockManagerTypeMap;

}; // class UQuartzGameSubsystem 