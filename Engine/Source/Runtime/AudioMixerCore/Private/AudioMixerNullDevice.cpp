// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioMixerNullDevice.h"
#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"

namespace Audio
{
	uint32 FMixerNullCallback::Run()
	{
		//
		// To simulate an audio device requesting for more audio, we sleep between callbacks.
		// The problem with this is that Sleep is not accurate. It will always be slightly higher than requested,
		// which means that audio will be generated slightly slower than the stated sample rate.
		// To correct this, we keep track of the real time passed, and adjust the sleep time accordingly so the audio clock
		// stays as close to the real time clock as possible.

		double AudioClock = FPlatformTime::Seconds();

		float SleepTime = CallbackTime; 
		while (!bShouldShutdown)
		{
			Callback();
			FPlatformProcess::Sleep(FMath::Max(0.f, SleepTime));

			AudioClock += CallbackTime;
			double RealClock = FPlatformTime::Seconds();
			double AudioVsReal = RealClock - AudioClock;
			// For the next sleep, we adjust the sleep duration to try and keep the audio clock as close
			// to the real time clock as possible
			SleepTime = CallbackTime - AudioVsReal;
		}

		return 0;
	}

	FMixerNullCallback::FMixerNullCallback(float BufferDuration, TFunction<void()> InCallback, EThreadPriority ThreadPriority)
		: Callback(InCallback)
		, CallbackTime(BufferDuration)
		, bShouldShutdown(false)
	{
		CallbackThread.Reset(FRunnableThread::Create(this, TEXT("AudioMixerNullCallbackThread"), 0, ThreadPriority, FPlatformAffinity::GetAudioThreadMask()));
	}

	FMixerNullCallback::~FMixerNullCallback()
	{
		bShouldShutdown = true;
		CallbackThread->Kill(true);
	}
}
