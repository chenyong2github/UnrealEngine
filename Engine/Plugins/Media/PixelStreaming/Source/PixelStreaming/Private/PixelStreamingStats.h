// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Utils.h"
#include "PixelStreamingSettings.h"

// Stats about Pixel Streaming that can displayed either in the in-application HUD or in the log.
struct FPixelStreamingStats
{
	public:
		static FPixelStreamingStats& Get();
		bool GetStatsEnabled();
		void Tick();
		void Reset();
		void OnWebRTCDeliverFrameForEncode();
		void OnEncodingFinished();
		void OnCaptureFinished();
		void SetEncoderLatency(double EncoderLatencyMs);
		void SetEncoderBitrateMbps(double EncoderBitrateMbps);
		void SetEncoderQP(double QP);
		void SetCaptureLatency(double CaptureLatencyMs);

	private:
		void EmitStat(int UniqueId, FString StringToEmit);

	private:

		static constexpr uint32 SmoothingPeriod = 3 * 60; // kinda 3 secs for 60FPS
		
		// Note: FSmoothedValue is thread safe.
		FSmoothedValue<SmoothingPeriod> WebRTCCaptureToEncodeLatencyMs;
		FSmoothedValue<SmoothingPeriod> EncoderLatencyMs;
		FSmoothedValue<SmoothingPeriod> CaptureLatencyMs;
		FSmoothedValue<SmoothingPeriod> EncoderBitrateMbps;
		FSmoothedValue<SmoothingPeriod> EncoderQP;

		uint64 LastEncodeTimeCycles = 0;
		FSmoothedValue<SmoothingPeriod> EncoderFPS;

		uint64 LastCaptureTimeCycles = 0;
		FSmoothedValue<SmoothingPeriod> CaptureFPS;

		
};
