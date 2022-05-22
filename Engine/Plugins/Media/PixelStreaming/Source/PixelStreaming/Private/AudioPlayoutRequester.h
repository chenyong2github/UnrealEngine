// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPixelStreamingAudioPlayoutRequester.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"

namespace UE::PixelStreaming
{
	// Requests audio from WebRTC at a regular interval (10ms)
	// This is required so that WebRTC audio sinks actually have
	// some audio data for their sinks. Without this WebRTC assumes
	// there is no demand for audio and does not populate the sinks.
	class FAudioPlayoutRequester : public IPixelStreamingAudioPlayoutRequester
	{
	public:
		class Runnable : public FRunnable
		{
		public:
			Runnable(TFunction<void()> RequestPlayoutFunc);
			virtual ~Runnable() = default;

			// Begin FRunnable interface.
			virtual bool Init() override;
			virtual uint32 Run() override;
			virtual void Stop() override;
			virtual void Exit() override;
			// End FRunnable interface

		private:
			bool bIsRunning;
			int64_t LastAudioRequestTimeMs;
			TFunction<void()> RequestPlayoutFunc;
		};

		FAudioPlayoutRequester();
		virtual ~FAudioPlayoutRequester() = default;

		virtual void InitPlayout() override;
		virtual void StartPlayout() override;
		virtual void StopPlayout() override;
		virtual bool Playing() const override;
		virtual bool PlayoutIsInitialized() const override;
		virtual void Uninitialise() override;
		virtual void RegisterAudioCallback(webrtc::AudioTransport* AudioCallback) override;

	public:
		static int16_t const RequestIntervalMs = 10;

	private:
		FThreadSafeBool bIsPlayoutInitialised;
		FThreadSafeBool bIsPlaying;
		uint32 SampleRate;
		uint8 NumChannels;
		TUniquePtr<FAudioPlayoutRequester::Runnable> RequesterRunnable;
		TUniquePtr<FRunnableThread> RequesterThread;
		webrtc::AudioTransport* AudioCallback;
		FCriticalSection PlayoutCriticalSection;
		TArray<int16_t> PlayoutBuffer;
	};
} // namespace UE::PixelStreaming
