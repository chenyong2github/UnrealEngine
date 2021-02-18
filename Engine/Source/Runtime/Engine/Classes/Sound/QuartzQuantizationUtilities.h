// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/Class.h"
#include "Containers/Map.h"
#include "Containers/Queue.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "QuartzQuantizationUtilities.generated.h"

ENGINE_API DECLARE_LOG_CATEGORY_EXTERN(LogAudioQuartz, Log, All);

// forwards
struct FQuartzClockTickRate;
struct FQuartzQuantizationBoundary;
struct FQuartzTimeSignature;


namespace Audio
{
	// forwards (Audio::)
	class IAudioMixerQuantizedEventListener;
	class IQuartzQuantizedCommand;
	class FQuartzClock;
	class FShareableQuartzCommandQueue;

	class FMixerDevice;

	struct FQuartzQuantizedCommandDelegateData;
	struct FQuartzMetronomeDelegateData;
	struct FQuartzQuantizedCommandInitInfo;
} // namespace Audio




// UOBJECT LAYER:

// An enumeration for specifying quantization for Quartz commands
UENUM(BlueprintType)
enum class EQuartzCommandQuantization : uint8
{
	Bar						UMETA(DisplayName = "Bar", ToolTip = "(dependent on time signature)"),
	Beat					UMETA(DisplayName = "Beat", ToolTip = "(dependent on time signature and Pulse Override)"),

	ThirtySecondNote		UMETA(DisplayName = "1/32"),
	SixteenthNote			UMETA(DisplayName = "1/16"),
	EighthNote				UMETA(DisplayName = "1/8"),
	QuarterNote				UMETA(DisplayName = "1/4"),
	HalfNote				UMETA(DisplayName = "Half"),
	WholeNote				UMETA(DisplayName = "Whole"),

	DottedSixteenthNote		UMETA(DisplayName = "(dotted) 1/16"),
	DottedEighthNote		UMETA(DisplayName = "(dotted) 1/8"),
	DottedQuarterNote		UMETA(DisplayName = "(dotted) 1/4"),
	DottedHalfNote			UMETA(DisplayName = "(dotted) Half"),
	DottedWholeNote			UMETA(DisplayName = "(dotted) Whole"),

	SixteenthNoteTriplet	UMETA(DisplayName = "1/16 (triplet)"),
	EighthNoteTriplet		UMETA(DisplayName = "1/8 (triplet)"),
	QuarterNoteTriplet		UMETA(DisplayName = "1/4 (triplet)"),
	HalfNoteTriplet			UMETA(DisplayName = "1/2 (triplet)"),

	Tick					UMETA(DisplayName = "On Tick (Smallest Value, same as 1/32)", ToolTip = "(same as 1/32)"),

	Count					UMETA(Hidden),

	None					UMETA(DisplayName = "None", ToolTip = "(Execute as son as possible)"),
	// (when using "Count" in various logic, we don't want to account for "None")
};

// An enumeration for specifying the denominator of time signatures
UENUM(BlueprintType)
enum class EQuartzTimeSignatureQuantization : uint8
{
	HalfNote				UMETA(DisplayName = "/2"),
	QuarterNote				UMETA(DisplayName = "/4"),
	EighthNote				UMETA(DisplayName = "/8"),
	SixteenthNote			UMETA(DisplayName = "/16"),
	ThirtySecondNote		UMETA(DisplayName = "/32"),

	Count				UMETA(Hidden),
};

// Allows the user to specify non-uniform beat durations in odd meters
USTRUCT(BlueprintType)
struct ENGINE_API FQuartzPulseOverrideStep
{
	GENERATED_BODY()

	// The number of pulses for this beat duration
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quantized Audio Clock Time Signature")
	int32 NumberOfPulses = 0;

	// This Beat duration
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quantized Audio Clock Time Signature")
	EQuartzCommandQuantization PulseDuration = EQuartzCommandQuantization::Beat;
};


// Quartz Time Signature
USTRUCT(BlueprintType)
struct ENGINE_API FQuartzTimeSignature
{
	GENERATED_BODY()

	// default ctor
	FQuartzTimeSignature() {};

	// numerator
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quantized Audio Clock Time Signature")
	int32 NumBeats { 4 };

	// denominator
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quantized Audio Clock Time Signature")
	EQuartzTimeSignatureQuantization BeatType { EQuartzTimeSignatureQuantization::QuarterNote };

	// beat override
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quantized Audio Clock Time Signature")
	TArray<FQuartzPulseOverrideStep> OptionalPulseOverride { };


	// copy ctor
	FQuartzTimeSignature(const FQuartzTimeSignature& Other);

	// assignment
	FQuartzTimeSignature& operator=(const FQuartzTimeSignature& Other);

	// comparison
	bool operator==(const FQuartzTimeSignature& Other);
};

// Transport Time stamp, used for tracking the musical time stamp on a clock
USTRUCT(BlueprintType)
struct ENGINE_API FQuartzTransportTimeStamp
{
	GENERATED_BODY()

	int32 Bars { 0 };

	int32 Beat{ 0 };

	float BeatFraction{ 0.f };

	bool IsZero() const;

	void Reset();
};


// An enumeration for specifying different TYPES of delegates
UENUM(BlueprintType)
enum class EQuartzDelegateType : uint8
{
	MetronomeTick				UMETA(DisplayName = "Metronome Tick"), // uses EAudioMixerCommandQuantization to select subdivision
	CommandEvent				UMETA(DisplayName = "Command Event"),

	Count					UMETA(Hidden)
};


// An enumeration for specifying quantization boundary reference frame
UENUM(BlueprintType)
enum class EQuarztQuantizationReference : uint8
{
	BarRelative				UMETA(DisplayName = "Bar Relative", ToolTip = "Will occur on the next occurence of this duration from the start of a bar (i.e. On beat 3)"),
	TransportRelative		UMETA(DisplayName = "Transport Relative", ToolTip = "Will occur on the next multiple of this duration since the clock started ticking (i.e. on the next 4 bar boundary)"),
	CurrentTimeRelative		UMETA(DisplayName = "Current Time Relative", ToolTip = "Will occur on the next multiple of this duration from the current time (i.e. In three beats)"),

	Count					UMETA(Hidden)
};

// An enumeration for specifying different TYPES of delegates
UENUM(BlueprintType)
enum class EQuartzCommandDelegateSubType : uint8
{
	CommandOnFailedToQueue		UMETA(DisplayName = "Failed To Queue", ToolTip = "The command will not execute (i.e. Clock doesn't exist or PlayQuantized failed concurrency)"),
	CommandOnQueued				UMETA(DisplayName = "Queued", ToolTip = "The command has been passed to the Audio Render Thread"),
	CommandOnCanceled			UMETA(DisplayName = "Canceled", ToolTip = "The command was stopped before it could execute"),
	CommandOnAboutToStart		UMETA(DisplayName = "About To Start", ToolTip = "execute off this to be in sync w/ sound starting"),
	CommandOnStarted			UMETA(DisplayName = "Started", ToolTip = "the command was just executed on the Audio Render Thrtead"),
//	CommandCompleted			UMETA(DisplayName = "Completed", ToolTip = "same as 'Started' unless command is looping"),

	Count					UMETA(Hidden)
};


// Delegate Declarations
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FOnQuartzMetronomeEvent, FName, ClockName, EQuartzCommandQuantization, QuantizationType, int32, NumBars, int32, Beat, float, BeatFraction);
DECLARE_DYNAMIC_DELEGATE_FiveParams(FOnQuartzMetronomeEventBP, FName, ClockName, EQuartzCommandQuantization, QuantizationType, int32, NumBars, int32, Beat, float, BeatFraction);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnQuartzCommandEvent, EQuartzCommandDelegateSubType, EventType, FName, Name);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnQuartzCommandEventBP, EQuartzCommandDelegateSubType, EventType, FName, Name);

// struct used to specify the quantization boundary of an event
USTRUCT(BlueprintType)
struct ENGINE_API FQuartzQuantizationBoundary
{
	GENERATED_BODY()

	// resolution we are interested in
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quantized Audio Clock Settings")
	EQuartzCommandQuantization Quantization;

	// how many "Resolutions" to wait before the onset we care about
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quantized Audio Clock Settings", meta = (ClampMin = "1.0"))
	float Multiplier;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quantized Audio Clock Settings")
	EQuarztQuantizationReference CountingReferencePoint;

	// ctor
	FQuartzQuantizationBoundary(
		EQuartzCommandQuantization InQuantization = EQuartzCommandQuantization::Tick
		, float InMultiplier = 1.f
		, EQuarztQuantizationReference InReferencePoint = EQuarztQuantizationReference::BarRelative
	)
		: Quantization(InQuantization)
		, Multiplier(InMultiplier)
		, CountingReferencePoint(InReferencePoint)
	{}
}; // struct FQuartzQuantizationBoundary

// UStruct version of settings struct used to initialized a clock
USTRUCT(BlueprintType)
struct ENGINE_API FQuartzClockSettings
{
	GENERATED_BODY()

	// Time Signature (defaults to 4/4)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quantized Audio Clock Settings")
	FQuartzTimeSignature TimeSignature;

	// should the clock start Ticking
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quantized Audio Clock Settings")
	bool bIgnoreLevelChange{ false };

}; // struct FQuartzClockSettings

// ---------

// Class to track latency trends
// will lazily calculate running average on the correct thread
class ENGINE_API FQuartLatencyTracker
{
public:
	FQuartLatencyTracker();

	void PushLatencyTrackerResult(const double& InResult);

	float GetLifetimeAverageLatency();

	float GetMinLatency();

	float GetMaxLatency();

private:
	void PushSingleResult(const double& InResult);

	void DigestQueue();

	TQueue<float, EQueueMode::Mpsc> ResultQueue;

	int64 NumEntries{ 0 };

	float LifetimeAverage{ 0.f };

	float Min;

	float Max;
};

// NON-UOBJECT LAYER:
namespace Audio
{
	class FAudioMixer;

	// Utility class to set/get/convert tick rate
// In this context "Tick Rate" refers to the duration of smallest temporal resolution we may care about
// in musical time, this is locked to a 1/32nd note

	struct ENGINE_API FQuartzClockTickRate
	{

	public:
		// ctor
		FQuartzClockTickRate();

		// Setters
		void SetFramesPerTick(int32 InNewFramesPerTick);

		void SetMillisecondsPerTick(float InNewMillisecondsPerTick);

		void SetSecondsPerTick(float InNewSecondsPerTick);

		void SetThirtySecondNotesPerMinute(float InNewThirtySecondNotesPerMinute);

		void SetBeatsPerMinute(float InNewBeatsPerMinute);

		void SetSampleRate(float InNewSampleRate);

		// Getters
		int32 GetFramesPerTick() const { return FramesPerTick; }

		float GetMillisecondsPerTick() const { return MillisecondsPerTick; }

		float GetSecondsPerTick() const { return SecondsPerTick; }

		float GetThirtySecondNotesPerMinute() const { return ThirtySecondNotesPerMinute; }

		float GetBeatsPerMinute() const { return BeatsPerMinute; }

		float GetSampleRate() const { return SampleRate; }

		int64 GetFramesPerDuration(EQuartzCommandQuantization InDuration) const;

		int64 GetFramesPerDuration(EQuartzTimeSignatureQuantization InDuration) const;

		bool IsValid(int32 InEventResolutionThreshold = 1) const;

		bool IsSameTickRate(const FQuartzClockTickRate& Other, bool bAccountForDifferentSampleRates = true) const;



	private:
		// FramesPerTick is our ground truth 
		// update FramesPerTick and call RecalculateDurationsBasedOnFramesPerTick() to update other members
		int32 FramesPerTick{ 1 };
		float MillisecondsPerTick{ 1.f };
		float SecondsPerTick{ 1.f };
		float ThirtySecondNotesPerMinute{ 1.f };
		float BeatsPerMinute{ 1 };
		float SampleRate{ 44100.f };

		void RecalculateDurationsBasedOnFramesPerTick();

	}; // class FAudioMixerClockTickRate

	// Simple class to track latency as a request/action propagates from GT to ART (or vice versa)
	class ENGINE_API FQuartzLatencyTimer
	{
	public:
		// ctor
		FQuartzLatencyTimer();

		// record the start time
		void StartTimer();

		// reset the start time
		void ResetTimer();

		// stop the timer
		void StopTimer();

		// get the current value of a running timer
		double GetCurrentTimePassedMs();

		// get the final time of a stopped timer
		double GetResultsMilliseconds();

		// returns true if the Timer was started (could be running or stopped)
		bool HasTimerStarted();

		// returns true if the timer has been run and stopped
		bool HasTimerStopped();

		// returns true if the timer is running
		bool IsTimerRunning();

		// returns true if the timer has completed (we can get the results)
		bool HasTimerRun();

	private:
		int64 JourneyStartCycles;

		int64 JourneyEndCycles;
	};

	// class to track time a QuartzMessage takes to get from one thread to another
	class ENGINE_API FQuartzCrossThreadMessage : public FQuartzLatencyTimer
	{
	public:
		FQuartzCrossThreadMessage(bool bAutoStartTimer = true);

		void RequestSent();

		double RequestRecieved();

		double GetResultsMilliseconds();

		double GetCurrentTimeMilliseconds();

	private:
		FQuartzLatencyTimer Timer;
	};


	// data that is gathered by the AudioThread to get passed from FActiveSound->FMixerSourceVoice
	// eventually converted to IQuartzQuantizedCommand for the Quantized Command itself
	struct ENGINE_API FQuartzQuantizedRequestData
	{
		// shared with FQuartzQuantizedCommandInitInfo:
		FName ClockName;
		FName ClockHandleName;
		FName OtherClockName;
		TSharedPtr<IQuartzQuantizedCommand> QuantizedCommandPtr;
		FQuartzQuantizationBoundary QuantizationBoundary{ /* InQuantization */ EQuartzCommandQuantization::Tick, /* InMultiplier */ 1.f };
		TSharedPtr<FShareableQuartzCommandQueue, ESPMode::ThreadSafe> GameThreadCommandQueue{ nullptr };
		int32 GameThreadDelegateID{ -1 };
	};


	// data that is passed into IQuartzQuantizedCommand::OnQueued
	// info that derived classes need can be added here
	struct ENGINE_API FQuartzQuantizedCommandInitInfo
	{
		// default ctor
		FQuartzQuantizedCommandInitInfo() {};

		// conversion ctor from FQuartzQuantizedRequestData
		FQuartzQuantizedCommandInitInfo(
			const FQuartzQuantizedRequestData& RHS
			, int32 InSourceID = -1
		);

		void SetOwningClockPtr(TSharedPtr<Audio::FQuartzClock> InClockPointer)
		{
			OwningClockPointer = InClockPointer;
		}

		// shared with FQuartzQuantizedRequestData
		FName ClockName;
		FName ClockHandleName;
		FName OtherClockName;
		TSharedPtr<IQuartzQuantizedCommand> QuantizedCommandPtr;
		FQuartzQuantizationBoundary QuantizationBoundary;
		TSharedPtr<FShareableQuartzCommandQueue, ESPMode::ThreadSafe> GameThreadCommandQueue;
		int32 GameThreadDelegateID{ -1 };

		// Audio Render thread-specific data:
		TSharedPtr<Audio::FQuartzClock> OwningClockPointer{ nullptr };
		int32 SourceID{ -1 };
	};


	// base class for quantized commands. Virtual methods called by owning clock.
	class ENGINE_API IQuartzQuantizedCommand : public FQuartzCrossThreadMessage
	{
	public:

		// ctor
		IQuartzQuantizedCommand() {};

		// dtor
		virtual ~IQuartzQuantizedCommand() {};

		// allocate a copy of the derived class
		virtual TSharedPtr<IQuartzQuantizedCommand> GetDeepCopyOfDerivedObject() const;

		// Command has reached the AudioRenderThread
		void OnQueued(const FQuartzQuantizedCommandInitInfo& InCommandInitInfo);

		// Perhaps the associated sound failed concurrency and will not be playing
		void FailedToQueue();

		// Called 2x Assumed thread latency before OnFinalCallback()
		void AboutToStart();

		// Called on the final callback of this event boundary.
		// InNumFramesLeft is the number of frames into the callback the exact quantized event should take place
		void OnFinalCallback(int32 InNumFramesLeft);

		// Called if the owning clock gets stopped
		void OnClockPaused();

		// Called if the owning clock gets started
		void OnClockStarted();

		// Called if the event is cancelled before OnFinalCallback() is called
		void Cancel();

		virtual bool IsLooping() { return false; }
		virtual bool IsClockAltering() { return false; }

		virtual FName GetCommandName() const = 0;


	protected:
		// base classes can override these to add extra functionality
		virtual void OnQueuedCustom(const FQuartzQuantizedCommandInitInfo& InCommandInitInfo) {}
		virtual void FailedToQueueCustom() {}
		virtual void AboutToStartCustom() {}
		virtual void OnFinalCallbackCustom(int32 InNumFramesLeft) {}
		virtual void OnClockPausedCustom() {}
		virtual void OnClockStartedCustom() {}
		virtual void CancelCustom() {}

	private:
		TSharedPtr<FShareableQuartzCommandQueue, ESPMode::ThreadSafe> GameThreadCommandQueue{ nullptr };
		int32 GameThreadDelegateID{ -1 };
		bool bAboutToStartHasBeenCalled{ false };
	}; // class IAudioMixerQuantizedCommandBase

	// Audio Render Thread Handle to a queued command
	// Used by AudioMixerSourceVoices to access a pending associated command
	struct ENGINE_API FQuartzQuantizedCommandHandle
	{
		FName OwningClockName;
		TSharedPtr<IQuartzQuantizedCommand> CommandPtr{ nullptr };
		FMixerDevice* MixerDevice{ nullptr };

		// attempts to cancel the command. Returns true if the cancellation was successful.
		bool Cancel();
	};

} // namespace Audio
