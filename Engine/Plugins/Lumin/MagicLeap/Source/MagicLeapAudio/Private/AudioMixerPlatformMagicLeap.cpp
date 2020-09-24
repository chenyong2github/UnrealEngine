// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioMixerPlatformMagicLeap.h"
#include "Modules/ModuleManager.h"
#include "AudioMixer.h"
#include "AudioMixerDevice.h"
#include "CoreGlobals.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/ScopeTryLock.h"
#include "VorbisAudioInfo.h"
#include "ADPCMAudioInfo.h"
#include "Async/Async.h"
#include "MagicLeapAudioModule.h"
#include "Lumin/CAPIShims/LuminAPILogging.h"
#if PLATFORM_LUMIN
#include "Lumin/LuminPlatformDelegates.h"
#endif // PLATFORM_LUMIN

DECLARE_LOG_CATEGORY_EXTERN(LogAudioMixerMagicLeap, Log, All);
DEFINE_LOG_CATEGORY(LogAudioMixerMagicLeap);

namespace Audio
{
	// ML1 currently only has stereo speakers and stereo aux support.
	constexpr uint32 DefaultNumChannels = 2;
	// TODO: @Epic check the value to be used. Setting default for now.
	constexpr uint32 DefaultSamplesPerSecond = 48000;  // presumed 48KHz and 16 bits for the sample
	// TODO: @Epic check the value to be used. Setting default for now.
	constexpr float DefaultMaxPitch = 1.0f;
	static bool bRetrievedDeviceDefaults = false;

	FMixerPlatformMagicLeap::FMixerPlatformMagicLeap()
		: bSuspended(false)
		, bInitialized(false)
		, bChangeStandby(false)
#if WITH_MLSDK
		, OutSize(0)
		, OutMinimalSize(0)
		, BufferFormat()
		, StreamHandle(ML_INVALID_HANDLE)
#endif //WITH_MLSDK
	{
#if WITH_MLSDK && PLATFORM_MAC
		// Force load ml_ext_logging before any ml_audio function is called because of REM-3398
		MLLoggingLogLevelIsEnabled(MLLogLevel_Error);
#endif
#if WITH_MLSDK
		// Note:The higher the returned 'OutSize' is, the higher the audio latency.
		//	 When NullDevice is used this latency affects termination and standby performance.
		// Note: When the OutSize is below the MLAudioGetBufferedOutputDefaults recommended size, audio will stutter when a headpose is lost
		//	 Stuttering will occur due to MLGraphicsBegineFrame. The high recommended size hides the issue.
		MLResult Result = MLAudioGetBufferedOutputDefaults(DefaultNumChannels, DefaultSamplesPerSecond, DefaultMaxPitch, &BufferFormat, &OutSize, &OutMinimalSize);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogAudioMixerMagicLeap, Error, TEXT("MLAudioGetBufferedOutputDefaults failed with error %s."), UTF8_TO_TCHAR(MLAudioGetResultString(Result)));

			// When running on the device and `MLAudioGetBufferedOutputDefaults` somehow fails, we should still proceed with known defaults.
			// It can clear its parameters on failure, so reset them here.
			OutSize = 12800;
			OutMinimalSize = 1600;
			BufferFormat.channel_count = DefaultNumChannels;
			BufferFormat.samples_per_second = DefaultSamplesPerSecond;
			BufferFormat.bits_per_sample = 16;
			BufferFormat.valid_bits_per_sample = 16;
			BufferFormat.sample_format = MLAudioSampleFormat_Int;
			BufferFormat.reserved = 0;
		}
#endif //WITH_MLSDK
	}

	FMixerPlatformMagicLeap::~FMixerPlatformMagicLeap()
	{
		if (bInitialized)
		{
			TeardownHardware();
		}
	}

	bool FMixerPlatformMagicLeap::InitializeHardware()
	{
		if (bInitialized)
		{
			return false;
		}

		// Register application lifecycle delegates
		FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddRaw(this, &FMixerPlatformMagicLeap::SuspendContext);
		FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddRaw(this, &FMixerPlatformMagicLeap::ResumeContext);

		// Starting the NullDevice at shutdown when paused is required to avoid a two second wait sequence to flush the audio command buffers
		// Starting and stopping the NullDevice is also expensive (~60-180ms due to a non-interuptable sleep). However, it is 
		// faster than waiting for two seconds. This shutdown behavior was last confirmed with UE4.24.2
		FCoreDelegates::ApplicationWillTerminateDelegate.AddRaw(this, &FMixerPlatformMagicLeap::DevicePausedStandby);

#if PLATFORM_LUMIN		
		// When the device goes in standby mode, the MLAudioCallback function is not called.
		// As a result the audio buffers from the engine queue up and they play back once the device becomes active again.
		// This desyncs gameplay with audio and breaks the UX spec for standby mode. When in standby, the app should function normally
		// from a gameplay standpoint. It should not "pause". This spec is to ensure that when the device is active again, the "resume" time is very minimal.
		// Using this fake callback, we keep emptying the audio buffer from the engine and act as if everything is running like it normally should.
		// Another aspect this fixes is launching an app when device is already in standby mode. If during engine initialization, the audio mixer
		// does not call IAudioMixerPlatformInterface::ReadNextBuffer(), the audio engine blocks and eventually crashes.
		// This fake callback ensures a smooth initialization as well. 
		FLuminDelegates::DeviceWillGoInStandbyDelegate.AddRaw(this, &FMixerPlatformMagicLeap::DeviceStandby);

		// Note: A MLAudioEvent_UnmutedBySystem event is called when leaving reality mode.
		FLuminDelegates::DeviceHasReactivatedDelegate.AddRaw(this, &FMixerPlatformMagicLeap::DeviceActive);
#endif // PLATFORM_LUMIN

		bInitialized = true;

		return true;
	}

	bool FMixerPlatformMagicLeap::CheckAudioDeviceChange()
	{
#if WITH_MLSDK
		FScopeLock Lock(&SwapCriticalSection);

		// `bMoveAudioStreamToNewAudioDevice` is used by the parent AudioMixer to handle overrun timeouts and can't be used here.
		// The CVar `OverrunTimeoutCVar` is 1 second, which is added onto the kpi time if bMoveAudioStreamToNewAudioDevice is used
		if (bChangeStandby)
		{
			bChangeStandby = false;

			// bIsUsingNullDevice state change must be locked for `SubmitBuffer`
			FScopeLock NullLock(&DeviceSwapCriticalSection);
			if (bIsUsingNullDevice)
			{
				// 'StopRunningNullDevice' blocks until the sleeping thread wakes up
				StopRunningNullDevice();

				UE_LOG(LogAudioMixerMagicLeap, Log, TEXT("The audio device is active again."));
			}
			else 
			{
				UE_LOG(LogAudioMixerMagicLeap, Log, TEXT("The audio device is going into standby."));

				StartRunningNullDevice();
			}
			return true;
		}
#endif //WITH_MLSDK
		return false;
	}

	void FMixerPlatformMagicLeap::ResumePlaybackOnNewDevice()
	{
#if WITH_MLSDK
		// A render event needs to be triggered if the the Null Device was just removed
		if (!bIsUsingNullDevice)
		{
			// TODO : implement for 4.26 onwards
			// FlushUEBuffers();
			AudioRenderEvent->Trigger();
		}
#endif //WITH_MLSDK
	}

	bool FMixerPlatformMagicLeap::TeardownHardware()
	{
		if (!bInitialized)
		{
			return true;
		}

		// Unregister application lifecycle delegates
		FCoreDelegates::ApplicationWillEnterBackgroundDelegate.RemoveAll(this);
		FCoreDelegates::ApplicationHasEnteredForegroundDelegate.RemoveAll(this);
		FCoreDelegates::ApplicationWillTerminateDelegate.RemoveAll(this);

#if PLATFORM_LUMIN
		FLuminDelegates::DeviceWillEnterRealityModeDelegate.RemoveAll(this);
		FLuminDelegates::DeviceWillGoInStandbyDelegate.RemoveAll(this);
		FLuminDelegates::DeviceHasReactivatedDelegate.RemoveAll(this);
#endif // PLATFORM_LUMIN

		bInitialized = false;
		return true;
	}

	bool FMixerPlatformMagicLeap::IsInitialized() const
	{
		return bInitialized;
	}

	bool FMixerPlatformMagicLeap::GetNumOutputDevices(uint32& OutNumOutputDevices)
	{
		// ML1 will always have just one device.
		OutNumOutputDevices = 1;
		return true;
	}

	bool FMixerPlatformMagicLeap::GetOutputDeviceInfo(const uint32 InDeviceIndex, FAudioPlatformDeviceInfo& OutInfo)
	{
#if WITH_MLSDK
		OutInfo.Name = TEXT("Magic Leap Audio Device");
		OutInfo.DeviceId = 0;
		OutInfo.bIsSystemDefault = true;
		OutInfo.SampleRate = BufferFormat.samples_per_second;
		OutInfo.NumChannels = DefaultNumChannels;

		if (BufferFormat.sample_format == MLAudioSampleFormat_Float && BufferFormat.bits_per_sample == 32)
		{
			OutInfo.Format = EAudioMixerStreamDataFormat::Float;
		}
		else if (BufferFormat.sample_format == MLAudioSampleFormat_Int && BufferFormat.bits_per_sample == 16)
		{
			OutInfo.Format = EAudioMixerStreamDataFormat::Int16;
		}
		else
		{
			//Unknown format:
			UE_LOG(LogAudioMixerMagicLeap, Error, TEXT("Invalid sample type requested. "));
			return false;
		}

		OutInfo.OutputChannelArray.SetNum(2);
		OutInfo.OutputChannelArray[0] = EAudioMixerChannel::FrontLeft;
		OutInfo.OutputChannelArray[1] = EAudioMixerChannel::FrontRight;
#endif //WITH_MLSDK

		return true;
	}

	bool FMixerPlatformMagicLeap::GetDefaultOutputDeviceIndex(uint32& OutDefaultDeviceIndex) const
	{
		OutDefaultDeviceIndex = 0;
		return true;
	}

	bool FMixerPlatformMagicLeap::OpenAudioStream(const FAudioMixerOpenStreamParams& Params)
	{
#if WITH_MLSDK
		if (!bInitialized || AudioStreamInfo.StreamState != EAudioOutputStreamState::Closed)
		{
			return false;
		}

		if (!GetOutputDeviceInfo(AudioStreamInfo.OutputDeviceIndex, AudioStreamInfo.DeviceInfo))
		{
			return false;
		}

		OpenStreamParams = Params;

		// Number of frames is defined by the default buffer size, divided by the size of a single frame,
		// which is the number of channels times the number of bytes in a single sample.
		OpenStreamParams.NumFrames = OutSize / (DefaultNumChannels * (BufferFormat.bits_per_sample / 8));

		AudioStreamInfo.Reset();

		AudioStreamInfo.OutputDeviceIndex = 0;
		AudioStreamInfo.NumOutputFrames = OpenStreamParams.NumFrames;
		AudioStreamInfo.NumBuffers = OpenStreamParams.NumBuffers;
		AudioStreamInfo.AudioMixer = OpenStreamParams.AudioMixer;
		GetOutputDeviceInfo(0, AudioStreamInfo.DeviceInfo);

		BufferFormat.channel_count = DefaultNumChannels;

		MLResult Result = MLAudioCreateSoundWithBufferedOutput(&BufferFormat, OutSize, &MLAudioCallback, this, &StreamHandle);

		if (Result != MLResult_Ok)
		{
			UE_LOG(LogAudioMixerMagicLeap, Error, TEXT("MLAudioCreateSoundWithBufferedOutput failed with error %s"), UTF8_TO_TCHAR(MLAudioGetResultString(Result)));
			return false;
		}

		Result = MLAudioSetSoundEventCallback(StreamHandle, &MLAudioEventImplCallback, this);

		if (Result != MLResult_Ok)
		{
			UE_LOG(LogAudioMixerMagicLeap, Error, TEXT("Failed to register audio event callback with error %s"), UTF8_TO_TCHAR(MLAudioGetResultString(Result)));
		}

		AudioStreamInfo.StreamState = EAudioOutputStreamState::Open;
#endif //WITH_MLSDK

		return true;
	}

	bool FMixerPlatformMagicLeap::CloseAudioStream()
	{
#if WITH_MLSDK
		{
			// `DeviceSwapCriticalSection` Surrounds the internal `SubmitBuffer calls`
			FScopeLock Lock(&DeviceSwapCriticalSection);
			check(MLHandleIsValid(StreamHandle));

			if (!bInitialized || (AudioStreamInfo.StreamState != EAudioOutputStreamState::Open && AudioStreamInfo.StreamState != EAudioOutputStreamState::Stopped))
			{
				return false;
			}

			MLResult Result = MLAudioDestroySound(StreamHandle);

			if (Result != MLResult_Ok)
			{
				UE_LOG(LogAudioMixerMagicLeap, Error, TEXT("MLAudioDestroySound failed with error %s"), UTF8_TO_TCHAR(MLAudioGetResultString(Result)));
				return false;
			}

			AudioStreamInfo.StreamState = EAudioOutputStreamState::Closed;
			StreamHandle = ML_INVALID_HANDLE;
		}
#endif //WITH_MLSDK

		return true;
	}

	bool FMixerPlatformMagicLeap::StartAudioStream()
	{
#if WITH_MLSDK
		BeginGeneratingAudio();

		check(MLHandleIsValid(StreamHandle));

		MLResult Result = MLAudioStartSound(StreamHandle);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogAudioMixerMagicLeap, Error, TEXT("StartAudioStream MLAudioStartSound failed with error %s"), UTF8_TO_TCHAR(MLAudioGetResultString(Result)));
			return false;
		}
#endif //WITH_MLSDK

		return true;
	}

	bool FMixerPlatformMagicLeap::StopAudioStream()
	{
#if WITH_MLSDK
		if (!bInitialized || AudioStreamInfo.StreamState != EAudioOutputStreamState::Running)
		{
			return false;
		}

		MLResult Result = MLAudioStopSound(StreamHandle);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogAudioMixerMagicLeap, Error, TEXT("MLAudioStopSound failed with error %s"), UTF8_TO_TCHAR(MLAudioGetResultString(Result)));
			return false;
		}

		if (AudioStreamInfo.StreamState == EAudioOutputStreamState::Running)
		{
			StopGeneratingAudio();
		}

		check(AudioStreamInfo.StreamState == EAudioOutputStreamState::Stopped);
#endif //WITH_MLSDK

		return true;
	}

	FAudioPlatformDeviceInfo FMixerPlatformMagicLeap::GetPlatformDeviceInfo() const
	{
		check(AudioStreamInfo.DeviceInfo.NumChannels == 2);
		return AudioStreamInfo.DeviceInfo;
	}

	FAudioPlatformSettings FMixerPlatformMagicLeap::GetPlatformSettings() const
	{
#if WITH_MLSDK
		FAudioPlatformSettings PlatformSettings;
		PlatformSettings.CallbackBufferFrameSize = OutSize / (DefaultNumChannels * (BufferFormat.bits_per_sample / 8));
		PlatformSettings.MaxChannels = 0;
		PlatformSettings.NumBuffers = 2;
		PlatformSettings.SampleRate = BufferFormat.samples_per_second;

		return PlatformSettings;
#else
		return FAudioPlatformSettings();
#endif //WITH_MLSDK
	}

	void FMixerPlatformMagicLeap::SubmitBuffer(const uint8* Buffer)
	{
#if WITH_MLSDK
		// The audio handle may be used even after being invalidated in CloseAudioStream's call to MLAudioDestroySound
		// By using `bIsUsingNullDevice` as an early out, no submission will ever be triggered by the Null Device.
		// Any MLAudioCallback that happens during bIsUsingNullDevice may or may not submit, either case is OK.
		if (bIsUsingNullDevice || !MLHandleIsValid(StreamHandle))
		{
			return;
		}

		MLAudioBuffer CallbackBuffer;
		MLResult Result = MLAudioGetOutputBuffer(StreamHandle, &CallbackBuffer);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogAudioMixerMagicLeap, Error, TEXT("SubmitBuffer MLAudioGetOutputBuffer failed with error %s"), UTF8_TO_TCHAR(MLAudioGetResultString(Result)));
			return;
		}

		// Fill the callback buffer
		FMemory::Memcpy(CallbackBuffer.ptr, Buffer, CallbackBuffer.size);

		Result = MLAudioReleaseOutputBuffer(StreamHandle);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogAudioMixerMagicLeap, Error, TEXT("SubmitBuffer MLAudioReleaseOutputBuffer failed with error %s"), UTF8_TO_TCHAR(MLAudioGetResultString(Result)));
			return;
		}
#endif //WITH_MLSDK
	}

	FName FMixerPlatformMagicLeap::GetRuntimeFormat(USoundWave* InSoundWave)
	{
#if WITH_OGGVORBIS
		static FName NAME_OGG(TEXT("OGG"));
		if (InSoundWave->HasCompressedData(NAME_OGG))
		{
			return NAME_OGG;
		}
#endif

		static FName NAME_ADPCM(TEXT("ADPCM"));

		return NAME_ADPCM;
	}

	bool FMixerPlatformMagicLeap::HasCompressedAudioInfoClass(USoundWave* InSoundWave)
	{
		return true;
	}

	ICompressedAudioInfo* FMixerPlatformMagicLeap::CreateCompressedAudioInfo(USoundWave* InSoundWave)
	{
		if (InSoundWave->IsSeekableStreaming())
		{
			return new FADPCMAudioInfo();
		}

#if WITH_OGGVORBIS
		static FName NAME_OGG(TEXT("OGG"));
		if (InSoundWave->HasCompressedData(NAME_OGG))
		{
			return new FVorbisAudioInfo();
		}
#endif
		static FName NAME_ADPCM(TEXT("ADPCM"));
		return new FADPCMAudioInfo();
	}

	FString FMixerPlatformMagicLeap::GetDefaultDeviceName()
	{
		return FString(TEXT("MLAudio"));
	}

	void FMixerPlatformMagicLeap::ResumeContext()
	{
#if WITH_MLSDK
		FScopeLock ScopeLock(&SuspendedCriticalSection);
		
		if (bSuspended)
		{
			if (MLHandleIsValid(StreamHandle))
			{
				MLResult Result = MLAudioStartSound(StreamHandle);
				if (Result != MLResult_Ok)
				{
					UE_LOG(LogAudioMixerMagicLeap, Error, TEXT("Resuming with MLAudioStartSound failed with error %s"), UTF8_TO_TCHAR(MLAudioGetResultString(Result)));
					return;
				}
			}

			bSuspended = false;
		}
#endif //WITH_MLSDK
	}

	void FMixerPlatformMagicLeap::OnHardwareUpdate()
	{
		GetMagicLeapAudioModule().CheckOutputDevice();
	}

	int32 FMixerPlatformMagicLeap::GetNumFrames(const int32 InNumReqestedFrames)
	{
#if WITH_MLSDK
		return OutSize / (DefaultNumChannels * (BufferFormat.bits_per_sample / 8));
#else
		return 0;
#endif //WITH_MLSDK
	}

	void FMixerPlatformMagicLeap::SuspendContext()
	{
#if WITH_MLSDK
		FScopeLock ScopeLock(&SuspendedCriticalSection);
		
		if (!bSuspended)
		{
			if (MLHandleIsValid(StreamHandle))
			{
				MLResult Result = MLAudioStopSound(StreamHandle);
				if (Result != MLResult_Ok)
				{
					UE_LOG(LogAudioMixerMagicLeap, Error, TEXT("Suspending with MLAudioStopSound failed with error %s"), UTF8_TO_TCHAR(MLAudioGetResultString(Result)));
					return;
				}
			}

			bSuspended = true;
		}
#endif //WITH_MLSDK
	}

	// TODO : implement for 4.26 onwards
	// Flush the Unreal audio buffers
	// void FMixerPlatformMagicLeap::FlushUEBuffers()
	// {
	// 	for (int32 Index = 0; Index < OutputBuffers.Num(); ++Index)
	// 	{
	// 		OutputBuffers[Index].Reset(OpenStreamParams.NumFrames * AudioStreamInfo.DeviceInfo.NumChannels);
	// 	}
	// }

	void FMixerPlatformMagicLeap::DeviceStandby() 
	{
		check(IsInGameThread());

		FScopeLock Lock(&SwapCriticalSection);
		bChangeStandby = static_cast<bool>(!bIsUsingNullDevice);
	}

	// Conditionally start the null device to consume UE audio output if the device is suspended
	void FMixerPlatformMagicLeap::DevicePausedStandby()
	{
		FScopeLock ScopeLock(&SuspendedCriticalSection);

		// If the device is going to terminate we can avoid the 1 sec timeout to trigger `CheckAudioDeviceChange` by
		// manually starting the null device.
		if (bSuspended)
		{
			// bIsUsingNullDevice state change must be locked for `SubmitBuffer`
			FScopeLock NullLock(&DeviceSwapCriticalSection);
			StartRunningNullDevice();
		}
	}

	void FMixerPlatformMagicLeap::DeviceActive() 
	{
		check(IsInGameThread());

		FScopeLock Lock(&SwapCriticalSection);
		bChangeStandby = static_cast<bool>(bIsUsingNullDevice);
	}

#if WITH_MLSDK
	void FMixerPlatformMagicLeap::MLAudioCallback(MLHandle Handle, void* CallbackContext)
	{

		FMixerPlatformMagicLeap* InPlatform = reinterpret_cast<FMixerPlatformMagicLeap*>(CallbackContext);

		if (InPlatform == nullptr)
		{
			UE_LOG(LogAudioMixerMagicLeap, Error, TEXT("The callback context to MLAudioBufferCallback for MLAudioCreateSoundWithBufferedOutput was null!"));
			return;
		}
		check(MLHandleIsValid(Handle));

		InPlatform->ReadNextBuffer();
	}

	void FMixerPlatformMagicLeap::MLAudioEventImplCallback(MLHandle Handle, MLAudioEvent Event, void* CallbackContext)
	{

		FMixerPlatformMagicLeap* InPlatform = reinterpret_cast<FMixerPlatformMagicLeap*>(CallbackContext);

		switch (Event)
		{
			case MLAudioEvent_End:
			{
				// TODO: The MLAudioEvent callback for 'End' has no implementation

				break;
			}
			case MLAudioEvent_Loop:
			{
				// TODO: The MLAudioEvent callback for 'Loop' has no implementation

				break;
			}
			case MLAudioEvent_MutedBySystem:
			{
				float Volume;
				MLResult Result = MLAudioGetMasterVolume(&Volume);

				if (Result != MLResult_Ok)
				{
					UE_LOG(LogAudioMixerMagicLeap, Error, TEXT("Mute with MLAudioGetMasterVolume failed with error %s"), UTF8_TO_TCHAR(MLAudioGetResultString(Result)));
					return;
				}

				AsyncTask(ENamedThreads::GameThread, [Volume]()
				{
					// 'MLAudioGetMasterVolume' returns 0-10. AudioMuteDelegate expects 0-100
					FCoreDelegates::AudioMuteDelegate.Broadcast(true, Volume * 10);
				});

				break;
			}
			case MLAudioEvent_UnmutedBySystem:
			{
				float Volume;
				MLResult Result = MLAudioGetMasterVolume(&Volume);

				if (Result != MLResult_Ok)
				{
					UE_LOG(LogAudioMixerMagicLeap, Error, TEXT("Unmute with MLAudioGetMasterVolume failed with error %s"), UTF8_TO_TCHAR(MLAudioGetResultString(Result)));
					return;
				}

				AsyncTask(ENamedThreads::GameThread, [Volume]()
				{
					// 'MLAudioGetMasterVolume' returns 0-10. AudioMuteDelegate expects 0-100
					FCoreDelegates::AudioMuteDelegate.Broadcast(false, Volume * 10);
				});

				break;
			}
			case MLAudioEvent_DuckedBySystem:
			{
				AsyncTask(ENamedThreads::GameThread, []()
				{
					FCoreDelegates::UserMusicInterruptDelegate.Broadcast(true);
				});

				break;
			}
			case MLAudioEvent_UnduckedBySystem:
			{
				AsyncTask(ENamedThreads::GameThread, []()
				{
					FCoreDelegates::UserMusicInterruptDelegate.Broadcast(false);
				});

				break;
			}
			case MLAudioEvent_ResourceDestroyed:
			{
				// TODO: The MLAudioEvent callback for 'ResourceDestroyed' has no implementation

				break;
			}
			default:
			{
				UE_LOG(LogAudioMixerMagicLeap, Warning, TEXT("Unhandled MLAudioEvent."));

				break;
			}
		}
	}
#endif //WITH_MLSDK
}
