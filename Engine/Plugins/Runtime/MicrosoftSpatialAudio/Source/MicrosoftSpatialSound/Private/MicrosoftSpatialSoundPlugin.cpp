// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MicrosoftSpatialSoundPlugin.h"
#include "Features/IModularFeatures.h"

PRAGMA_DISABLE_OPTIMIZATION

DEFINE_LOG_CATEGORY_STATIC(LogMicrosoftSpatialSound, Verbose, All);

static const float DefaultObjectGain = 1.0f;
static const float UnrealUnitsToMeters = 1.0f;

static void LogMicrosoftSpatialAudioError(HRESULT Result, int32 LineNumber)
{
	FString ErrorString;

#if PLATFORM_WINDOWS
	switch (Result)
	{
		case REGDB_E_CLASSNOTREG:						
			ErrorString = TEXT("REGDB_E_CLASSNOTREG");
			break;

		case CLASS_E_NOAGGREGATION:
			ErrorString = TEXT("CLASS_E_NOAGGREGATION");
			break;

		case E_NOINTERFACE:
			ErrorString = TEXT("E_NOINTERFACE");
			break;

		case E_POINTER: 
			ErrorString = TEXT("E_POINTER");
			break;

		case E_INVALIDARG: 
			ErrorString = TEXT("E_INVALIDARG");
			break;

		case E_OUTOFMEMORY: 
			ErrorString = TEXT("E_OUTOFMEMORY");
			break;

		case AUDCLNT_E_UNSUPPORTED_FORMAT:
			ErrorString = TEXT("AUDCLNT_E_UNSUPPORTED_FORMAT");
			break;

		case SPTLAUDCLNT_E_DESTROYED:
			ErrorString = TEXT("SPTLAUDCLNT_E_DESTROYED");
			break;

		case SPTLAUDCLNT_E_OUT_OF_ORDER:
			ErrorString = TEXT("SPTLAUDCLNT_E_OUT_OF_ORDER");
			break;

		case SPTLAUDCLNT_E_RESOURCES_INVALIDATED:
			ErrorString = TEXT("SPTLAUDCLNT_E_RESOURCES_INVALIDATED");
			break;

		case SPTLAUDCLNT_E_NO_MORE_OBJECTS:
			ErrorString = TEXT("SPTLAUDCLNT_E_NO_MORE_OBJECTS");
			break;

		case SPTLAUDCLNT_E_PROPERTY_NOT_SUPPORTED:
			ErrorString = TEXT("SPTLAUDCLNT_E_PROPERTY_NOT_SUPPORTED");
			break;

		case SPTLAUDCLNT_E_ERRORS_IN_OBJECT_CALLS:
			ErrorString = TEXT("SPTLAUDCLNT_E_ERRORS_IN_OBJECT_CALLS");
			break;

		case SPTLAUDCLNT_E_METADATA_FORMAT_NOT_SUPPORTED:
			ErrorString = TEXT("SPTLAUDCLNT_E_METADATA_FORMAT_NOT_SUPPORTED");
			break;

		case SPTLAUDCLNT_E_STREAM_NOT_AVAILABLE:
			ErrorString = TEXT("SPTLAUDCLNT_E_STREAM_NOT_AVAILABLE");
			break;

		case SPTLAUDCLNT_E_INVALID_LICENSE:
			ErrorString = TEXT("SPTLAUDCLNT_E_INVALID_LICENSE");
			break;

		case SPTLAUDCLNT_E_STREAM_NOT_STOPPED:
			ErrorString = TEXT("SPTLAUDCLNT_E_STREAM_NOT_STOPPED");
			break;

		case SPTLAUDCLNT_E_STATIC_OBJECT_NOT_AVAILABLE:
			ErrorString = TEXT("SPTLAUDCLNT_E_STATIC_OBJECT_NOT_AVAILABLE");
			break;

		case SPTLAUDCLNT_E_OBJECT_ALREADY_ACTIVE:
			ErrorString = TEXT("SPTLAUDCLNT_E_OBJECT_ALREADY_ACTIVE");
			break;

		case SPTLAUDCLNT_E_INTERNAL:
			ErrorString = TEXT("SPTLAUDCLNT_E_INTERNAL");
			break;

		default: 
			ErrorString= FString::Printf(TEXT("UKNOWN (HRESULT=%d)"), (int32)Result);
			break;
	}
#else
	ErrorString = FString::Printf(TEXT("UKNOWN: '%d'"), (int32)Result);
#endif
	ErrorString = FString::Printf(TEXT("%s, line number %d"), *ErrorString, LineNumber);
	UE_LOG(LogMicrosoftSpatialSound, Error, TEXT("%s"), *ErrorString);
}

// Macro to check result code for XAudio2 failure, get the string version, log, and goto a cleanup
#define MS_SPATIAL_AUDIO_RETURN_ON_FAIL(Result)					\
	if (FAILED(Result))											\
	{															\
		LogMicrosoftSpatialAudioError(Result, __LINE__);		\
		return;													\
	}

#define MS_SPATIAL_AUDIO_CLEANUP_ON_FAIL(Result)				\
	if (FAILED(Result))											\
	{															\
		LogMicrosoftSpatialAudioError(Result, __LINE__);		\
		goto Cleanup;											\
	}

#define MS_SPATIAL_AUDIO_LOG_ON_FAIL(Result)					\
	if (FAILED(Result))											\
	{															\
		LogMicrosoftSpatialAudioError(Result, __LINE__);		\
	}

// Function which maps unreal coordinates to MS Spatial sound coordinates
static FORCEINLINE FVector UnrealToMicrosoftSpatialSoundCoordinates(const FVector& Input)
{
	return { UnrealUnitsToMeters * Input.Y, UnrealUnitsToMeters * Input.X, -UnrealUnitsToMeters * Input.Z };
}

FMicrosoftSpatialSound::FMicrosoftSpatialSound()
	: MinFramesRequiredPerObjectUpdate(0)
	, DeviceEnumerator(nullptr)
	, DefaultDevice(nullptr)
	, SpatialAudioClient(nullptr)
	, SpatialAudioStream(nullptr)
	, BufferCompletionEvent(0)
	, SpatialAudioRenderThread(nullptr)
	, bIsInitialized(false)
{
}

FMicrosoftSpatialSound::~FMicrosoftSpatialSound()
{
	Shutdown();
}

void FMicrosoftSpatialSound::Initialize(const FAudioPluginInitializationParams InitParams)
{
	InitializationParams = InitParams;

	SpatialAudioRenderThread = FRunnableThread::Create(this, TEXT("MicrosoftSpatialAudioThread"), 0, TPri_TimeCritical, FPlatformAffinity::GetAudioThreadMask());
}

void FMicrosoftSpatialSound::Shutdown()
{
	if (bIsInitialized)
	{
		// Flag that we're no longer rendering
		bIsRendering = false;

		check(SpatialAudioRenderThread != nullptr);

		SpatialAudioRenderThread->Kill(true);

		delete SpatialAudioRenderThread;
		SpatialAudioRenderThread = nullptr;
	}

	bIsInitialized = false;
}

void FMicrosoftSpatialSound::OnInitSource(const uint32 SourceId, const FName& AudioComponentUserId, USpatializationPluginSourceSettingsBase* InSettings)
{
	if (!bIsInitialized)
	{
		return;
	}

	FScopeLock ScopeLock(&Objects[SourceId].ObjectCritSect);

	FSpatialSoundSourceObjectData& ObjectData = Objects[SourceId];
	check(!ObjectData.bActive);
}

void FMicrosoftSpatialSound::OnReleaseSource(const uint32 SourceId)
{
	if (!bIsInitialized)
	{
		return;
	}

	FScopeLock ScopeLock(&Objects[SourceId].ObjectCritSect);

	Objects[SourceId].bActive = false;
}

void FMicrosoftSpatialSound::ProcessAudio(const FAudioPluginSourceInputData& InputData, FAudioPluginSourceOutputData& OutputData)
{
	FScopeLock ScopeLock(&Objects[InputData.SourceId].ObjectCritSect);

	FSpatialSoundSourceObjectData& ObjectData = Objects[InputData.SourceId];
	check(InputData.AudioBuffer != nullptr);

	if (!ObjectData.bActive && !ObjectData.bBuffering)
	{
		ObjectData.AudioBuffer.SetCapacity(4096 * 50);
		ObjectData.bBuffering = true;
	}

	int32 NumSamplesToPush = InputData.AudioBuffer->Num();
	int32 SamplesWritten = ObjectData.AudioBuffer.Push(InputData.AudioBuffer->GetData(), NumSamplesToPush);
	checkf(SamplesWritten == NumSamplesToPush, TEXT("Source circular buffers should be bigger!"));
	check(InputData.SpatializationParams != nullptr);

	FVector NewPosition = UnrealToMicrosoftSpatialSoundCoordinates(InputData.SpatializationParams->EmitterPosition);

	if (ObjectData.bBuffering && ObjectData.AudioBuffer.Num() > MinFramesRequiredPerObjectUpdate * 5)
	{
		ObjectData.bBuffering = false;
	}

	bool bIsFirstPosition = false;
	if (!ObjectData.bBuffering && !ObjectData.bActive)
	{
		bIsFirstPosition = true;
		ObjectData.bActive = true;
		ObjectData.ObjectHandle = nullptr;

		HRESULT Result = SpatialAudioStream->ActivateSpatialAudioObject(AudioObjectType::AudioObjectType_Dynamic, &ObjectData.ObjectHandle);
		MS_SPATIAL_AUDIO_LOG_ON_FAIL(Result);
	}


	if (bIsFirstPosition || !FMath::IsNearlyEqual(ObjectData.TargetPosition.X, NewPosition.X) || !FMath::IsNearlyEqual(ObjectData.TargetPosition.Y, NewPosition.Y) || !FMath::IsNearlyEqual(ObjectData.TargetPosition.Z, NewPosition.Z))
	{
		if (bIsFirstPosition)
		{
			ObjectData.StartingPosition = ObjectData.TargetPosition;
		}
		else
		{
			ObjectData.StartingPosition = ObjectData.CurrentPosition;
		}
		ObjectData.TargetPosition = NewPosition;
		ObjectData.CurrentFrameLerpPosition = 0;
		ObjectData.NumberOfLerpFrames = 4*NumSamplesToPush;
	}
}

// bool FMicrosoftSpatialSound::ReadyToUpdateAudioObjects() const
// {
// 	for (const FSpatialSoundSourceObjectData& Object : Objects)
// 	{
// 		if (Object.bActive)
// 		{
// 			return Object.AudioBuffer.Num() >= MinFramesRequiredPerObjectUpdate;
// 		}
// 	}
// 	return false;
// }

void FMicrosoftSpatialSound::OnAllSourcesProcessed()
{
}

uint32 FMicrosoftSpatialSound::Run()
{
	HRESULT Result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&DeviceEnumerator);
	MS_SPATIAL_AUDIO_CLEANUP_ON_FAIL(Result);

	Result = DeviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &DefaultDevice);
	MS_SPATIAL_AUDIO_CLEANUP_ON_FAIL(Result);

	Result = DefaultDevice->Activate(__uuidof(ISpatialAudioClient), CLSCTX_INPROC_SERVER, nullptr, (void**)&SpatialAudioClient);
	MS_SPATIAL_AUDIO_CLEANUP_ON_FAIL(Result);

	WAVEFORMATEX Format;
	Format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
	Format.wBitsPerSample = 32;
	Format.nChannels = 1;
	Format.nSamplesPerSec = InitializationParams.SampleRate;
	Format.nBlockAlign = (Format.wBitsPerSample >> 3) * Format.nChannels;
	Format.nAvgBytesPerSec = Format.nBlockAlign * Format.nSamplesPerSec;
	Format.cbSize = 0;

	Result = SpatialAudioClient->IsAudioObjectFormatSupported(&Format);
	MS_SPATIAL_AUDIO_CLEANUP_ON_FAIL(Result);

	uint32 MaxObjects;
	Result = SpatialAudioClient->GetMaxDynamicObjectCount(&MaxObjects);
	MS_SPATIAL_AUDIO_CLEANUP_ON_FAIL(Result);

	if (MaxObjects < InitializationParams.NumSources)
	{
		UE_LOG(LogMicrosoftSpatialSound, Error, TEXT("Trying to use more spatial audio sources than is allowed by the device."));
		goto Cleanup;
	}

	// Spatial sound processes 1% of the frames per callback
	MinFramesRequiredPerObjectUpdate = Format.nSamplesPerSec / 100;

	// Event used to signal spatial audio buffer completion (to signal that it's time to render more audio)
	BufferCompletionEvent = CreateEvent(nullptr, 0, 0, nullptr);

	// Set the maximum number of dynamic audio objects that will be used
	SpatialAudioObjectRenderStreamActivationParams StreamActivationParams;
	StreamActivationParams.ObjectFormat = &Format;
	StreamActivationParams.StaticObjectTypeMask = AudioObjectType_None;
	StreamActivationParams.MinDynamicObjectCount = 0;
	StreamActivationParams.MaxDynamicObjectCount = InitializationParams.NumSources;
	StreamActivationParams.Category = AudioCategory_GameEffects;
	StreamActivationParams.EventHandle = BufferCompletionEvent;
	StreamActivationParams.NotifyObject = nullptr;

	// Create a property to set the format for the stream 
	PROPVARIANT SpatialAudioStreamProperty;
	PropVariantInit(&SpatialAudioStreamProperty);
	SpatialAudioStreamProperty.vt = VT_BLOB;
	SpatialAudioStreamProperty.blob.cbSize = sizeof(StreamActivationParams);
	SpatialAudioStreamProperty.blob.pBlobData = (BYTE *)&StreamActivationParams;

	// Activate the spatial audio stream
	Result = SpatialAudioClient->ActivateSpatialAudioStream(&SpatialAudioStreamProperty, __uuidof(SpatialAudioStream), (void**)&SpatialAudioStream);
	MS_SPATIAL_AUDIO_CLEANUP_ON_FAIL(Result);

	// Prepare our own record keeping for the number of spatial sources we expect to render
	Objects.Reset();
	Objects.AddDefaulted(InitializationParams.NumSources);

	Result = SpatialAudioStream->Start();
	MS_SPATIAL_AUDIO_CLEANUP_ON_FAIL(Result);

	// Flag that we're rendering
	bIsRendering = true;
	bIsInitialized = true;

	// The render loop
	while (bIsRendering)
	{
		// Wait for a signal from the audio-engine to start the next processing pass
		if (WaitForSingleObject(BufferCompletionEvent, 100) != WAIT_OBJECT_0)
		{
			UE_LOG(LogMicrosoftSpatialSound, Warning, TEXT("Microsoft Spatial Sound buffer completion event timed out."));

			Result = SpatialAudioStream->Reset();

			if (Result != S_OK)
			{
				MS_SPATIAL_AUDIO_LOG_ON_FAIL(Result);
				break;
			}
		}

		PumpSpatialAudioCommandQueue();

		// We need to lock while updating the spatial renderer		
	 	uint32 FrameCount = 0;
	 	uint32 AvailableObjects = 0;

		Result = SpatialAudioStream->BeginUpdatingAudioObjects(&AvailableObjects, &FrameCount);
		MS_SPATIAL_AUDIO_CLEANUP_ON_FAIL(Result);

		for (FSpatialSoundSourceObjectData& Object : Objects)
		{
			FScopeLock ScopeLock(&Object.ObjectCritSect);

			if (Object.bActive)
			{
				if (Object.AudioBuffer.Num() > FrameCount)
				{
					// Get the buffer for the audio object
					float* OutBuffer = nullptr;
					uint32 BufferLength = 0;
					Result = Object.ObjectHandle->GetBuffer((BYTE**)&OutBuffer, &BufferLength);
					MS_SPATIAL_AUDIO_LOG_ON_FAIL(Result);

					// Fill that buffer from the circular buffer queue
					uint32 NumSamples = BufferLength / sizeof(float);
					Object.AudioBuffer.Pop(OutBuffer, NumSamples);

					Result = Object.ObjectHandle->SetVolume(1.0f);
					MS_SPATIAL_AUDIO_LOG_ON_FAIL(Result);

					float LerpFraction = FMath::Clamp((float)Object.CurrentFrameLerpPosition / Object.NumberOfLerpFrames, 0.0f, 1.0f);

					Object.CurrentPosition = FMath::Lerp(Object.StartingPosition, Object.TargetPosition, LerpFraction);
					// Update the object's meta-data 
					Result = Object.ObjectHandle->SetPosition(Object.CurrentPosition.X, Object.CurrentPosition.Y, Object.CurrentPosition.Z);
					MS_SPATIAL_AUDIO_LOG_ON_FAIL(Result);

					Object.CurrentFrameLerpPosition += NumSamples;
				}
				else
				{
					UE_LOG(LogTemp, Log, TEXT("Foo"));
					float* OutBuffer = nullptr;
					uint32 BufferLength = 0;
					Result = Object.ObjectHandle->GetBuffer((BYTE**)&OutBuffer, &BufferLength);
					MS_SPATIAL_AUDIO_LOG_ON_FAIL(Result);
				}
			}
		}

		Result = SpatialAudioStream->EndUpdatingAudioObjects();
		MS_SPATIAL_AUDIO_CLEANUP_ON_FAIL(Result);
	}

Cleanup:

	// Shutting down
	if (SpatialAudioStream)
	{
		Result = SpatialAudioStream->Stop();
		check(SUCCEEDED(Result));

		Result = SpatialAudioStream->Reset();
		check(SUCCEEDED(Result));

		SAFE_RELEASE(SpatialAudioStream);
	}

	SAFE_RELEASE(SpatialAudioClient);
	SAFE_RELEASE(DefaultDevice);
	SAFE_RELEASE(DeviceEnumerator);

	if (BufferCompletionEvent)
	{
		CloseHandle(BufferCompletionEvent);
		BufferCompletionEvent = 0;
	}

	return 0;
}


void FMicrosoftSpatialSoundModule::StartupModule()
{
	IModularFeatures::Get().RegisterModularFeature(FMicrosoftSpatialSoundPluginFactory::GetModularFeatureName(), &PluginFactory);
}

void FMicrosoftSpatialSoundModule::ShutdownModule()
{
}

PRAGMA_ENABLE_OPTIMIZATION

IMPLEMENT_MODULE(FMicrosoftSpatialSoundModule, MicrosoftSpatialSound)