// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AudioMixerNullDevice.h"
#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"

namespace Audio
{
	uint32 FMixerNullCallback::Run()
	{
		while (!bShouldShutdown)
		{
			Callback();
			FPlatformProcess::Sleep(CallbackTime);
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
