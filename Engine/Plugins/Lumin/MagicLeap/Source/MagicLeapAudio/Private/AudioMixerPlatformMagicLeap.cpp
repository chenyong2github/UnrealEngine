// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioMixerPlatformMagicLeap.h"
#include "Modules/ModuleManager.h"
#include "AudioMixer.h"
#include "AudioMixerDevice.h"
#include "CoreGlobals.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "VorbisAudioInfo.h"
#include "ADPCMAudioInfo.h"
#include "MagicLeapAudioModule.h"
#include "Lumin/CAPIShims/LuminAPILogging.h"
#if PLATFORM_LUMIN
#include "Lumin/LuminPlatformDelegates.h"
#endif // PLATFORM_LUMIN

DECLARE_LOG_CATEGORY_EXTERN(LogAudioMixerMagicLeap, Log, All);
DEFINE_LOG_CATEGORY(LogAudioMixerMagicLeap);

// Macro to check result for failure, get string version, log, and return false
#define MLAUDIO_RETURN_ON_FAIL(Result)						\
	if (Result != MLResult_Ok)						\
	{														\
		const TCHAR* ErrorString = FMixerPlatformMagicLeap::GetErrorString(Result);	\
		AUDIO_PLATFORM_ERROR(ErrorString);					\
		return false;										\
	}

#define MLAUDIO_CHECK_ON_FAIL(Result)						\
	if (Result != MLResult_Ok)						\
	{														\
		const TCHAR* ErrorString = FMixerPlatformMagicLeap::GetErrorString(Result);	\
		AUDIO_PLATFORM_ERROR(ErrorString);					\
		check(false);										\
	}

#define MLAUDIO_LOG_FAILURE(Result)							\
	{														\
		const TCHAR* ErrorString = FMixerPlatformMagicLeap::GetErrorString(Result);	\
		UE_LOG(LogAudioMixerMagicLeap, Error, TEXT("Error in %s, line %d: %s"), __FILE__, __LINE__, ErrorString); \
	}

namespace Audio
{
	// ML1 currently only has stereo speakers and stereo aux support.
	const uint32 DefaultNumChannels = 2;
	// TODO: @Epic check the value to be used. Setting default for now.
	const uint32 DefaultSamplesPerSecond = 48000;  // presumed 48KHz and 16 bits for the sample
	// TODO: @Epic check the value to be used. Setting default for now.
	const float DefaultMaxPitch = 1.0f;
	static bool bRetrievedDeviceDefaults = false;

#if WITH_MLSDK
	static void GetMLDeviceDefaults(MLAudioBufferFormat& OutFormat, uint32& OutSize, uint32& OutMinSize)
	{
		static uint32 CachedSize = 0;
		static uint32 CachedMinSize = 0;
		static MLAudioBufferFormat CachedBufferFormat;

		if (!bRetrievedDeviceDefaults)
		{
			MLResult Result = MLAudioGetOutputStreamDefaults(DefaultNumChannels, DefaultSamplesPerSecond, DefaultMaxPitch, &CachedBufferFormat, &CachedSize, &CachedMinSize);
			if (Result == MLResult_Ok)
			{
				bRetrievedDeviceDefaults = true;
			}
			else
			{
				bRetrievedDeviceDefaults = false;
				UE_LOG(LogAudioMixerMagicLeap, Error, TEXT("MLAudioGetOutputStreamDefaults failed with error %s."), UTF8_TO_TCHAR(MLAudioGetResultString(Result)));
			}
		}

		OutFormat = CachedBufferFormat;
		OutSize = CachedSize;
		OutMinSize = CachedMinSize;
	}
#endif //WITH_MLSDK

	FMixerPlatformMagicLeap::FMixerPlatformMagicLeap()
		: CachedBufferHandle(nullptr)
		, bSuspended(false)
		, bInitialized(false)
		, bInCallback(false)
		, FakeCallback(this)
#if WITH_MLSDK
		, StreamHandle(ML_INVALID_HANDLE)
#endif //WITH_MLSDK
	{
#if WITH_MLSDK && PLATFORM_MAC
		// Force load ml_ext_logging before any ml_audio function is called because of REM-3398
		MLLoggingLogLevelIsEnabled(MLLogLevel_Error);
#endif
	}

	FMixerPlatformMagicLeap::~FMixerPlatformMagicLeap()
	{
		if (bInitialized)
		{
			TeardownHardware();
		}
	}

#if WITH_MLSDK
	const TCHAR* FMixerPlatformMagicLeap::GetErrorString(MLResult Result)
	{
		switch (Result)
		{
		case MLResult_UnspecifiedFailure:			 return TEXT("MLResult_UnspecifiedFailure");
		case MLResult_InvalidParam:					 return TEXT("MLResult_InvalidParam");
		case MLResult_AllocFailed:					 return TEXT("MLResult_AllocFailed");
		case MLAudioResult_NotImplemented:			 return TEXT("MLAudioResult_NotImplemented");
		case MLAudioResult_HandleNotFound:           return TEXT("MLAudioResult_HandleNotFound");
		case MLAudioResult_InvalidSampleRate:        return TEXT("MLAudioResult_InvalidSampleRate");
		case MLAudioResult_InvalidBitsPerSample:     return TEXT("MLAudioResult_InvalidBitsPerSample");
		case MLAudioResult_InvalidValidBits:         return TEXT("MLAudioResult_InvalidValidBits");
		case MLAudioResult_InvalidSampleFormat:      return TEXT("MLAudioResult_InvalidSampleFormat");
		case MLAudioResult_InvalidChannelCount:      return TEXT("MLAudioResult_InvalidChannelCount");
		case MLAudioResult_InvalidBufferSize:        return TEXT("MLAudioResult_InvalidBufferSize");
		case MLAudioResult_BufferNotReady:           return TEXT("MLAudioResult_BufferNotReady");
		case MLAudioResult_FileNotFound:             return TEXT("MLAudioResult_FileNotFound");
		case MLAudioResult_FileNotRecognized:        return TEXT("MLAudioResult_FileNotRecognized");
		default:                                return TEXT("MlAudioResult_UnknownError");
		}
	}
#endif //WITH_MLSDK

	bool FMixerPlatformMagicLeap::InitializeHardware()
	{
		if (bInitialized)
		{
			return false;
		}

		// Register application lifecycle delegates
		FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddRaw(this, &FMixerPlatformMagicLeap::SuspendContext);
		FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddRaw(this, &FMixerPlatformMagicLeap::ResumeContext);

#if PLATFORM_LUMIN		
		FLuminDelegates::DeviceWillEnterRealityModeDelegate.AddRaw(this, &FMixerPlatformMagicLeap::DeviceStandby);
		FLuminDelegates::DeviceWillGoInStandbyDelegate.AddRaw(this, &FMixerPlatformMagicLeap::DeviceStandby);
		FLuminDelegates::DeviceHasReactivatedDelegate.AddRaw(this, &FMixerPlatformMagicLeap::DeviceActive);
#endif // PLATFORM_LUMIN

		bInitialized = true;

		return true;
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
		MLAudioBufferFormat DesiredBufferFormat;
		uint32 OutSize;
		uint32 OutMinimalSize;

		GetMLDeviceDefaults(DesiredBufferFormat, OutSize, OutMinimalSize);

		check(bRetrievedDeviceDefaults == true);
		OutInfo.Name = TEXT("Magic Leap Audio Device");
		OutInfo.DeviceId = 0;
		OutInfo.bIsSystemDefault = true;
		OutInfo.SampleRate = DesiredBufferFormat.samples_per_second;
		OutInfo.NumChannels = DefaultNumChannels;

		if (DesiredBufferFormat.sample_format == MLAudioSampleFormat_Float && DesiredBufferFormat.bits_per_sample == 32)
		{
			OutInfo.Format = EAudioMixerStreamDataFormat::Float;
		}
		else if (DesiredBufferFormat.sample_format == MLAudioSampleFormat_Int && DesiredBufferFormat.bits_per_sample == 16)
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

		MLAudioBufferFormat DesiredBufferFormat;
		uint32 OutSize;
		uint32 OutMinimalSize;

		GetMLDeviceDefaults(DesiredBufferFormat, OutSize, OutMinimalSize);
		check(bRetrievedDeviceDefaults == true);

		OpenStreamParams = Params;

		// Number of frames is defined by the default buffer size, divided by the size of a single frame,
		// which is the number of channels times the number of bytes in a single sample.
		OpenStreamParams.NumFrames = OutSize / (DefaultNumChannels * (DesiredBufferFormat.bits_per_sample / 8));

		AudioStreamInfo.Reset();

		AudioStreamInfo.OutputDeviceIndex = 0;
		AudioStreamInfo.NumOutputFrames = OpenStreamParams.NumFrames;
		AudioStreamInfo.NumBuffers = OpenStreamParams.NumBuffers;
		AudioStreamInfo.AudioMixer = OpenStreamParams.AudioMixer;
		GetOutputDeviceInfo(0, AudioStreamInfo.DeviceInfo);

		DesiredBufferFormat.channel_count = DefaultNumChannels;

		MLResult Result = MLAudioCreateSoundWithOutputStream(&DesiredBufferFormat, OutSize, &MLAudioCallback, this, &StreamHandle);

		if (Result != MLResult_Ok)
		{
			MLAUDIO_LOG_FAILURE(Result);
			return false;
		}

		AudioStreamInfo.StreamState = EAudioOutputStreamState::Open;
#endif //WITH_MLSDK

		return true;
	}

	bool FMixerPlatformMagicLeap::CloseAudioStream()
	{
#if WITH_MLSDK
		{
			FScopeLock Lock(&StreamHandleCriticalSection);
			check(MLHandleIsValid(StreamHandle));

			if (!bInitialized || (AudioStreamInfo.StreamState != EAudioOutputStreamState::Open && AudioStreamInfo.StreamState != EAudioOutputStreamState::Stopped))
			{
				return false;
			}

			MLResult Result = MLAudioDestroySound(StreamHandle);

			if (Result != MLResult_Ok)
			{
				MLAUDIO_LOG_FAILURE(Result);
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

		MLResult Result = MLResult_Ok;

		//Pre buffer with two zeroed buffers:
		static const int32 NumberOfBuffersToPrecache = 2;
		for (int32 BufferIndex = 0; BufferIndex < NumberOfBuffersToPrecache; BufferIndex++)
		{
			MLAudioBuffer PrecacheBuffer;
			Result = MLAudioGetOutputStreamBuffer(StreamHandle, &PrecacheBuffer);
			if (Result != MLResult_Ok)
			{
				MLAUDIO_LOG_FAILURE(Result);
				break;
			}

			FMemory::Memzero(PrecacheBuffer.ptr, PrecacheBuffer.size);
			Result = MLAudioReleaseOutputStreamBuffer(StreamHandle);
			if (Result != MLResult_Ok)
			{
				MLAUDIO_LOG_FAILURE(Result);
				break;
			}
		}

		Result = MLAudioStartSound(StreamHandle);
		if (Result != MLResult_Ok)
		{
			MLAUDIO_LOG_FAILURE(Result);
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
			MLAUDIO_LOG_FAILURE(Result);
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
		MLAudioBufferFormat DesiredBufferFormat;
		uint32 OutSize;
		uint32 OutMinimalSize;

		GetMLDeviceDefaults(DesiredBufferFormat, OutSize, OutMinimalSize);

		check(bRetrievedDeviceDefaults == true);
		FAudioPlatformSettings PlatformSettings;
		PlatformSettings.CallbackBufferFrameSize = OutSize / (DefaultNumChannels * (DesiredBufferFormat.bits_per_sample / 8));
		PlatformSettings.MaxChannels = 0;
		PlatformSettings.NumBuffers = 2;
		PlatformSettings.SampleRate = DesiredBufferFormat.samples_per_second;

		return PlatformSettings;
#else
		return FAudioPlatformSettings();
#endif //WITH_MLSDK
	}

	void FMixerPlatformMagicLeap::SubmitBuffer(const uint8* Buffer)
	{
		CachedBufferHandle = (uint8*)Buffer;
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
		if (bSuspended)
		{
			if (MLHandleIsValid(StreamHandle))
			{
				MLResult Result = MLAudioStartSound(StreamHandle);
				if (Result != MLResult_Ok)
				{
					MLAUDIO_LOG_FAILURE(Result);
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
		MLAudioBufferFormat DesiredBufferFormat;
		uint32 OutSize;
		uint32 OutMinimalSize;

		GetMLDeviceDefaults(DesiredBufferFormat, OutSize, OutMinimalSize);
		check(bRetrievedDeviceDefaults == true);
		return OutSize / (DefaultNumChannels * (DesiredBufferFormat.bits_per_sample / 8));
#else
		return 0;
#endif //WITH_MLSDK
	}

	void FMixerPlatformMagicLeap::SuspendContext()
	{
#if WITH_MLSDK
		if (!bSuspended)
		{
			if (MLHandleIsValid(StreamHandle))
			{
				MLResult Result = MLAudioStopSound(StreamHandle);
				if (Result != MLResult_Ok)
				{
					MLAUDIO_LOG_FAILURE(Result);
					return;
				}
			}

			bSuspended = true;
		}
#endif //WITH_MLSDK
	}

	void FMixerPlatformMagicLeap::DeviceStandby()
	{
		// When the device goes in standby mode, the MLAudioCallback function is not called.
		// As a result the audio buffers from the engine queue up and they play back once the device becomes active again.
		// This desyncs gameplay with audio and breaks the UX spec for standby mode. When in standby, the app should function normally
		// from a gameplay standpoint. It should not "pause". This spec is to ensure that when the device is active again, the "resume" time is very minimal.
		// Using this fake callback, we keep emptying the audio buffer from the engine and act as if everything is running like it normally should.
		// Another aspect this fixes is launching an app when device is already in standby mode. If during engine initialization, the audio mixer
		// does not call IAudioMixerPlatformInterface::ReadNextBuffer(), the audio engine blocks and eventually crashes.
		// This fake callback ensures a smooth initialization as well. 
		FakeCallback.SetShouldUseCallback(true);
	}

	void FMixerPlatformMagicLeap::DeviceActive()
	{
		FakeCallback.SetShouldUseCallback(false);
	}

#if WITH_MLSDK
	void FMixerPlatformMagicLeap::MLAudioCallback(MLHandle Handle, void* CallbackContext)
	{
		FMixerPlatformMagicLeap* InPlatform = reinterpret_cast<FMixerPlatformMagicLeap*>(CallbackContext);

		if (InPlatform == nullptr)
		{
			UE_LOG(LogAudioMixerMagicLeap, Error, TEXT("The callback context to MLAudioBufferCallback for MLAudioCreateSoundWithOutputStream was null!"));
			return;
		}
		check(MLHandleIsValid(Handle));

		// This handle may be used even after being invalidated in CloseAudioStream's call to MLAudioDestroySound
		{
			FScopeLock Lock(&InPlatform->StreamHandleCriticalSection);
			if (!MLHandleIsValid(InPlatform->StreamHandle))
			{
				return;
			}

			MLAudioBuffer CallbackBuffer;
			// Get the callback buffer from MLAudio
			MLResult Result = MLAudioGetOutputStreamBuffer(InPlatform->StreamHandle, &CallbackBuffer);
			if (Result != MLResult_Ok)
			{
				MLAUDIO_LOG_FAILURE(Result);
				return;
			}

			if (InPlatform->CachedBufferHandle == nullptr)
			{
				InPlatform->ReadNextBuffer();
			}

			// It is possible that ReadNextBuffer() doesn't call SubmitBuffer()
			// in which case CachedBufferHandle will still be null and memcpy will segfault.
			if (InPlatform->CachedBufferHandle != nullptr)
			{
				// Fill the callback buffer:
				FMemory::Memcpy(CallbackBuffer.ptr, InPlatform->CachedBufferHandle, CallbackBuffer.size);
			}

			Result = MLAudioReleaseOutputStreamBuffer(InPlatform->StreamHandle);
			if (Result != MLResult_Ok)
			{
				MLAUDIO_LOG_FAILURE(Result);
				return;
			}

			InPlatform->CachedBufferHandle = nullptr;
		}
	}
#endif //WITH_MLSDK
}
