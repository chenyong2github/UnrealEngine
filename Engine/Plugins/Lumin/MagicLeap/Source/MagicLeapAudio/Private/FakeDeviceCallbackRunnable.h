// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioMixer.h"
#include "Containers/Ticker.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/ThreadSafeCounter64.h"

namespace Audio
{
	// When the device goes in standby mode, the MLAudioCallback function is not called.
	// As a result the audio buffers from the engine queue up and they play back once the device becomes active again.
	// This desyncs gameplay with audio and breaks the UX spec for standby mode. When in standby, the app should function normally
	// from a gameplay standpoint. It should not "pause". This spec is to ensure that when the device is active again, the "resume" time is very minimal.
	// Using this fake callback, we keep emptying the audio buffer from the engine and act as if everything is running like it normally should.
	// Another aspect this fixes is launching an app when device is already in standby mode. If during engine initialization, the audio mixer
	// does not call IAudioMixerPlatformInterface::ReadNextBuffer(), the audio engine blocks and eventually crashes.
	// This fake callback ensures a smooth initialization as well.
	class FFakeDeviceCallbackRunnable : public FRunnable
	{
	public:
		FFakeDeviceCallbackRunnable(class IAudioMixerPlatformInterface* InContext);
		virtual ~FFakeDeviceCallbackRunnable();
		virtual uint32 Run() override;

		void SetShouldUseCallback(bool bUseCallback);

	private:
		void WakeUp();

		class IAudioMixerPlatformInterface* Context;
		FRunnableThread* Thread;
		FThreadSafeCounter StopTaskCounter;
		FEvent* Semaphore;

		FThreadSafeBool bShouldUseCallback;
	};
}
