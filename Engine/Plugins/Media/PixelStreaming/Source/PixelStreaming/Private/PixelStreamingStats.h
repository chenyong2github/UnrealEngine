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
		void SetCaptureLatency(double CaptureLatencyMs);
		void OnCaptureFinished();
		void OnKeyframeEncoded(uint64 encoderId);
		void OnWebRTCDeliverFrameForEncode(uint64 encoderId);
		void OnEncodingFinished(uint64 encoderId);
		void SetEncoderLatency(uint64 encoderId, double EncoderLatencyMs);
		void SetEncoderBitrateMbps(uint64 encoderId, double EncoderBitrateMbps);
		void SetEncoderQP(uint64 encoderId, double QP);

		FORCEINLINE TStatId GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(PixelStreamingStats, STATGROUP_Tickables); }
		
	private:
		void EmitStat(int UniqueId, FString StringToEmit);

	private:

		static constexpr uint32 SmoothingPeriod = 3 * 60; // kinda 3 secs for 60FPS

		// Note: FSmoothedValue is thread safe.

		struct FEncoderStats
		{
			FSmoothedValue<SmoothingPeriod> WebRTCCaptureToEncodeLatencyMs;
			FSmoothedValue<SmoothingPeriod> EncoderLatencyMs;
			FSmoothedValue<SmoothingPeriod> EncoderBitrateMbps;
			FSmoothedValue<SmoothingPeriod> EncoderQP;
			uint64 LastEncodeTimeCycles = 0;
			FSmoothedValue<SmoothingPeriod> EncoderFPS;
			uint64 LastKeyFrameTimeCycles = 0;
		};

		FSmoothedValue<SmoothingPeriod> CaptureLatencyMs;

		uint64 LastCaptureTimeCycles = 0;
		FSmoothedValue<SmoothingPeriod> CaptureFPS;

		// unique ptr because FSmoothedValue is not trivially copyable 
		TMap<uint64, TUniquePtr<FEncoderStats>> EncoderStats;

		FEncoderStats& GetEncoderStats(uint64 encoderId);
};
