// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Utils.h"
#include "PixelStreamingSettings.h"
#include "Tickable.h"

// Stats about Pixel Streaming that can displayed either in the in-application HUD or in the log.
struct FPixelStreamingStats : FTickableGameObject
{
	public:
		static FPixelStreamingStats& Get();
		bool GetStatsEnabled();
		void Tick(float DeltaTime);
		void Reset();
		void OnWebRTCDeliverFrameForEncode();
		void OnEncodingFinished();
		void OnCaptureFinished();
		void OnKeyframeEncoded();
		void OnFrameSubmittedToWebRTC();
		void SetCaptureToEncodeLatency(double CaptureToEncodeMs);
		void SetEncoderLatency(double EncoderLatencyMs);
		void SetEncoderBitrateMbps(double EncoderBitrateMbps);
		void SetEncoderQP(double QP);
		void SetCaptureLatency(double CaptureLatencyMs);
		void SetPostEncodeLatency(double PostEncodeLatencyMs);
		FORCEINLINE TStatId GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(PixelStreamingStats, STATGROUP_Tickables); }

	private:
		void EmitStat(int UniqueId, FString StringToEmit);

	private:

		static constexpr uint32 SmoothingPeriod = 60; // kinda 3 secs for 60FPS
		
		// Note: FSmoothedValue is thread safe.
		FSmoothedValue<SmoothingPeriod> WebRTCCaptureToEncodeLatencyMs;
		FSmoothedValue<SmoothingPeriod> EncoderLatencyMs;
		FSmoothedValue<SmoothingPeriod> CaptureLatencyMs;
		FSmoothedValue<SmoothingPeriod> EncoderBitrateMbps;
		FSmoothedValue<SmoothingPeriod> EncoderQP;
		double PostEncodeLatencyMs;

		uint64 LastEncodeTimeCycles = 0;
		FSmoothedValue<SmoothingPeriod> EncoderFPS;

		uint64 LastCaptureTimeCycles = 0;
		FSmoothedValue<SmoothingPeriod> CaptureFPS;

		uint64 LastSubmitTimeCycles = 0;
		FSmoothedValue<SmoothingPeriod> SubmitToWebRTCFPS;

		uint64 LastWebRTCEncodeAttempt = 0;
		FSmoothedValue<SmoothingPeriod> WebRTCEncodeLoopFPS;

		uint64 LastKeyFrameTimeCycles = 0;

		
};
