// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "HAL/ThreadSafeBool.h"
#include "DSP/Delay.h"
#include "DSP/EnvelopeFollower.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Generators/AudioGenerator.h"

#include "AudioCapture.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAudioCapture, Log, All);

namespace Audio
{

	struct FCaptureDeviceInfo
	{
		FString DeviceName;
		int32 InputChannels;
		int32 PreferredSampleRate;
	};

	class FAudioCaptureImpl;

	typedef TFunction<void(const float* InAudio, int32 NumFrames, int32 NumChannels, double StreamTime, bool bOverFlow)> FOnCaptureFunction;

	// Class which handles audio capture internally, implemented with a back-end per platform
	class AUDIOCAPTURE_API FAudioCapture
	{
	public:
		FAudioCapture();
		~FAudioCapture();

		// Returns the audio capture device information at the given Id.
		bool GetDefaultCaptureDeviceInfo(FCaptureDeviceInfo& OutInfo);

		// Opens the audio capture stream with the given parameters
		bool OpenDefaultCaptureStream(FOnCaptureFunction OnCapture, uint32 NumFramesDesired);

		// Closes the audio capture stream
		bool CloseStream();

		// Start the audio capture stream
		bool StartStream();

		// Stop the audio capture stream
		bool StopStream();

		// Abort the audio capture stream (stop and close)
		bool AbortStream();

		// Get the stream time of the audio capture stream
		bool GetStreamTime(double& OutStreamTime) const;

		// Get the sample rate in use by the stream.
		int32 GetSampleRate() const;

		// Returns if the audio capture stream has been opened.
		bool IsStreamOpen() const;

		// Returns true if the audio capture stream is currently capturing audio
		bool IsCapturing() const;

	private:

		TUniquePtr<FAudioCaptureImpl> CreateImpl();
		TUniquePtr<FAudioCaptureImpl> Impl;
	};


	/** Class which contains an FAudioCapture object and performs analysis on the audio stream, only outputing audio if it matches a detection criteria. */
	class FAudioCaptureSynth
	{
	public:
		FAudioCaptureSynth();
		virtual ~FAudioCaptureSynth();

		// Gets the default capture device info
		bool GetDefaultCaptureDeviceInfo(FCaptureDeviceInfo& OutInfo);

		// Opens up a stream to the default capture device
		bool OpenDefaultStream();

		// Starts capturing audio
		bool StartCapturing();

		// Stops capturing audio
		void StopCapturing();

		// Immediately stop capturing audio
		void AbortCapturing();

		// Returned if the capture synth is closed
		bool IsStreamOpen() const;

		// Returns true if the capture synth is capturing audio
		bool IsCapturing() const;

		// Retrieves audio data from the capture synth.
		// This returns audio only if there was non-zero audio since this function was last called.
		bool GetAudioData(TArray<float>& OutAudioData);

		// Returns the number of samples enqueued in the capture synth
		int32 GetNumSamplesEnqueued();

	private:

		// Number of samples enqueued
		int32 NumSamplesEnqueued;

		// Information about the default capture device we're going to use
		FCaptureDeviceInfo CaptureInfo;

		// Audio capture object dealing with getting audio callbacks
		FAudioCapture AudioCapture;

		// Critical section to prevent reading and writing from the captured buffer at the same time
		FCriticalSection CaptureCriticalSection;
		
		// Buffer of audio capture data, yet to be copied to the output 
		TArray<float> AudioCaptureData;


		// If the object has been initialized
		bool bInitialized;

		// If we're capturing data
		bool bIsCapturing;
	};

} // namespace Audio

class FAudioCaptureModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

// Struct defining the time synth global quantization settings
USTRUCT(BlueprintType)
struct AUDIOCAPTURE_API FAudioCaptureDeviceInfo
{
	GENERATED_USTRUCT_BODY()

	// The name of the audio capture device
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AudioCapture")
	FName DeviceName;

	// The number of input channels
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AudioCapture")
	int32 NumInputChannels;

	// The sample rate of the audio capture device
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AudioCapture")
	int32 SampleRate;
};

// Class which opens up a handle to an audio capture device.
// Allows other objects to get audio buffers from the capture device.
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class AUDIOCAPTURE_API UAudioCapture : public UAudioGenerator
{
	GENERATED_BODY()

public:
	UAudioCapture();
	~UAudioCapture();

	bool OpenDefaultAudioStream();

	// Returns the audio capture device info
	UFUNCTION(BlueprintCallable, Category = "AudioCapture")
	bool GetAudioCaptureDeviceInfo(FAudioCaptureDeviceInfo& OutInfo);

	// Starts capturing audio
	UFUNCTION(BlueprintCallable, Category = "AudioCapture")
	void StartCapturingAudio();

	// Stops capturing audio
	UFUNCTION(BlueprintCallable, Category = "AudioCapture")
	void StopCapturingAudio();

	// Returns true if capturing audio
	UFUNCTION(BlueprintCallable, Category = "AudioCapture")
	bool IsCapturingAudio();

protected:

	Audio::FAudioCapture AudioCapture;
};

UCLASS()
class AUDIOCAPTURE_API UAudioCaptureFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, Category = "Audio Capture")
	static class UAudioCapture* CreateAudioCapture();
};