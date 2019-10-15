// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "FakeDeviceCallbackRunnable.h"
#include "CoreGlobals.h"
#include "HAL/Event.h"
#include "GenericPlatform/GenericPlatformProcess.h"

namespace Audio
{
	FFakeDeviceCallbackRunnable::FFakeDeviceCallbackRunnable(IAudioMixerPlatformInterface* InContext)
	: Context(InContext)
	, Thread(nullptr)
	, StopTaskCounter(0)
	, Semaphore(nullptr)
	, bShouldUseCallback(false)
	{
	}

	FFakeDeviceCallbackRunnable::~FFakeDeviceCallbackRunnable()
	{
		StopTaskCounter.Increment();

		if (Semaphore)
		{
			Semaphore->Trigger();
			Thread->WaitForCompletion();
			FGenericPlatformProcess::ReturnSynchEventToPool(Semaphore);
			Semaphore = nullptr;
		}

		delete Thread;
		Thread = nullptr;
	}

	uint32 FFakeDeviceCallbackRunnable::Run()
	{
		while (StopTaskCounter.GetValue() == 0)
		{
			if (bShouldUseCallback == false)
			{
				Semaphore->Wait();
			}

			Context->ReadNextBuffer();
			Semaphore->Wait(16);
		}

		return 0;
	}

	void FFakeDeviceCallbackRunnable::SetShouldUseCallback(bool bUseCallback)
	{
		if (bShouldUseCallback != bUseCallback)
		{
			bShouldUseCallback = bUseCallback;

			if (bShouldUseCallback)
			{
				WakeUp();
			}
		}
	}

	void FFakeDeviceCallbackRunnable::WakeUp()
	{
		if (Semaphore == nullptr)
		{
			Semaphore = FGenericPlatformProcess::GetSynchEventFromPool();
			Thread = FRunnableThread::Create(this, TEXT("FakeDeviceCallbackRunnable"), 0, TPri_BelowNormal);
		}

		Semaphore->Trigger();
	}
}
