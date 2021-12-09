// Copyright Epic Games, Inc. All Rights Reserved.

#include "BufferedSubmixListener.h"

/** Buffered Submix Listener. */
FBufferedSubmixListener::FBufferedSubmixListener(int32 InDefaultCircularBufferSize, bool bInZeroInputBuffer)
	: FBufferedListenerBase{ InDefaultCircularBufferSize }
	, bZeroInputBuffer{ bInZeroInputBuffer }
{
}

FBufferedSubmixListener::~FBufferedSubmixListener()
{
	//Stop(); // FIXME! Virtual call in destructor.	
	//StopInternal();
	// Perhaps we should assert that we're still registered.
}

bool FBufferedSubmixListener::Start(FAudioDevice* InAudioDevice)
{
	if (ensure(!bStarted))
	{		
		if (ensure(InAudioDevice))
		{
			// Inputs look valid, we can start.
			DeviceId = InAudioDevice->DeviceID;
			InAudioDevice->RegisterSubmixBufferListener(this);
			bStarted = true;
			return true;
		}
	}
	return false;
}

void FBufferedSubmixListener::Stop(FAudioDevice* InAudioDevice)
{
	StopInternal(InAudioDevice);
}

void FBufferedSubmixListener::StopInternal(FAudioDevice* InAudioDevice)
{
	if (bStarted)
	{
		if (ensure(InAudioDevice))
		{
			if (ensure(InAudioDevice->DeviceID == DeviceId))
			{
				InAudioDevice->UnregisterSubmixBufferListener(this);
				DeviceId = bStarted = false;
			}
		}
	}
}

void FBufferedSubmixListener::OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 InNumSamples, int32 InNumChannels, const int32 InSampleRate, double)
{
	if(bStarted)
	{
		// Call to base class to handle.
		FBufferFormat NewFormat;
		NewFormat.NumChannels = InNumChannels;
		NewFormat.NumSamplesPerBlock = InNumSamples;
		NewFormat.NumSamplesPerSec = InSampleRate;
		OnBufferRecieved(NewFormat, MakeArrayView(AudioData, InNumSamples));

		// Optionally, zero the buffer if we're asked to. This in the case where we're running both Unreal+Consumer renderers at once.
		// NOTE: this is dangerous as there's a chance we're not the only listener registered on this Submix. And will cause
		// listeners after us to have a silent buffer. Use with caution. 

		if (bZeroInputBuffer)
		{
			FMemory::Memzero(AudioData, sizeof(float) * InNumSamples);
		}	
	}
}
