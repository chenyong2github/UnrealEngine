// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingStats.h"
#include "PixelStreamingPrivate.h"
#include "Engine/Engine.h"


FPixelStreamingStats& FPixelStreamingStats::Get()
{
	static FPixelStreamingStats Stats;
	return Stats;
}

bool FPixelStreamingStats::GetStatsEnabled()
{
	return PixelStreamingSettings::CVarPixelStreamingOnScreenStats.GetValueOnAnyThread() || PixelStreamingSettings::CVarPixelStreamingLogStats.GetValueOnAnyThread();
}

void FPixelStreamingStats::Tick(float DeltaTime)
{
	bool bStatsEnabled = this->GetStatsEnabled();

	if (!GEngine || !bStatsEnabled)
	{
		return;
	}

	if (PixelStreamingSettings::CVarPixelStreamingOnScreenStats.GetValueOnGameThread())
	{
		GAreScreenMessagesEnabled = true;
	}

	const double NowMillis = FTimespan::FromSeconds(FPlatformTime::Seconds()).GetTotalMilliseconds();

	int Id = 0;

	for (auto&& Kvp : EncoderStats)
	{
		auto EncoderId = Kvp.Key;
		auto& Stats = Kvp.Value;

		const double KeyframeDeltaSecs = Stats->LastKeyFrameTimeCycles > 0 ? FPlatformTime::ToSeconds64(FPlatformTime::Cycles() - Stats->LastKeyFrameTimeCycles) : 0;

		EmitStat(++Id, FString::Printf(TEXT("Encoder latency: %.5f ms"), Stats->EncoderLatencyMs.Get()));
		EmitStat(++Id, FString::Printf(TEXT("WebRTC Capture->Encode latency: %.5f ms"), Stats->WebRTCCaptureToEncodeLatencyMs.Get()));
		EmitStat(++Id, FString::Printf(TEXT("Time since last keyframe: %.3f secs"), KeyframeDeltaSecs));
		EmitStat(++Id, FString::Printf(TEXT("Encoded FPS: %.0f"), Stats->EncoderFPS.Get()));
		EmitStat(++Id, FString::Printf(TEXT("Encoder bitrate: %.3f Mbps"), Stats->EncoderBitrateMbps.Get()));
		EmitStat(++Id, FString::Printf(TEXT("QP: %.0f"), Stats->EncoderQP.Get()));
		EmitStat(++Id, FString::Printf(TEXT("Encoder ID: 0x%llX"), EncoderId));
		EmitStat(++Id, "------------");
	}

	EmitStat(++Id, FString::Printf(TEXT("Capture latency: %.5f ms"), CaptureLatencyMs.Get()));
	EmitStat(++Id, FString::Printf(TEXT("Captured FPS: %.0f"), CaptureFPS.Get()));
	EmitStat(++Id, FString::Printf(TEXT("GameThread FPS: %.0f"), 1.0f / FApp::GetDeltaTime()));
	EmitStat(++Id, FString::Printf(TEXT("Timestamp: %.0f ms"), NowMillis));

	EmitStat(++Id, "------------ Pixel Streaming Stats ------------");

}

void FPixelStreamingStats::Reset()
{
	for (auto&& Kvp : EncoderStats)
	{
		auto& Stats = Kvp.Value;
		Stats->EncoderLatencyMs.Reset();
		Stats->EncoderBitrateMbps.Reset();
		Stats->EncoderQP.Reset();
		Stats->EncoderFPS.Reset();
	}

	CaptureFPS.Reset();
	CaptureLatencyMs.Reset();
}

void FPixelStreamingStats::SetCaptureLatency(double InCaptureLatencyMs)
{
	this->CaptureLatencyMs.Update(InCaptureLatencyMs);
}

void FPixelStreamingStats::OnCaptureFinished()
{
	uint64 NowCycles = FPlatformTime::Cycles();
	if(this->LastCaptureTimeCycles != 0)
	{
		uint64 DeltaCycles = NowCycles - this->LastCaptureTimeCycles;
		double DeltaSeconds = FPlatformTime::ToSeconds64(DeltaCycles);
		double CapturesPerSecond = 1.0 / DeltaSeconds;
		this->CaptureFPS.Update(CapturesPerSecond);
	}
	this->LastCaptureTimeCycles = NowCycles;
}

void FPixelStreamingStats::OnKeyframeEncoded(uint64 encoderId)
{
	FEncoderStats& encoderStats = GetEncoderStats(encoderId);
	encoderStats.LastKeyFrameTimeCycles = FPlatformTime::Cycles();;
}

void FPixelStreamingStats::OnWebRTCDeliverFrameForEncode(uint64 encoderId)
{
	uint64 NowCycles = FPlatformTime::Cycles();
	if(this->LastCaptureTimeCycles != 0)
	{
		uint64 DeltaCycles = NowCycles - this->LastCaptureTimeCycles;
		double DeltaMs = FPlatformTime::ToMilliseconds64(DeltaCycles);
		FEncoderStats& encoderStats = GetEncoderStats(encoderId);
		encoderStats.WebRTCCaptureToEncodeLatencyMs.Update(DeltaMs);
	}
}

void FPixelStreamingStats::OnEncodingFinished(uint64 encoderId)
{
	uint64 NowCycles = FPlatformTime::Cycles();
	FEncoderStats& encoderStats = GetEncoderStats(encoderId);
	if(encoderStats.LastEncodeTimeCycles != 0)
	{
		uint64 DeltaCycles = NowCycles - encoderStats.LastEncodeTimeCycles;
		double DeltaSeconds = FPlatformTime::ToSeconds64(DeltaCycles);
		double EncodesPerSecond = 1.0 / DeltaSeconds;
		encoderStats.EncoderFPS.Update(EncodesPerSecond);
	}
	encoderStats.LastEncodeTimeCycles = NowCycles;
}

void FPixelStreamingStats::SetEncoderLatency(uint64 encoderId, double InEncoderLatencyMs)
{
	GetEncoderStats(encoderId).EncoderLatencyMs.Update(InEncoderLatencyMs);
}

void FPixelStreamingStats::SetEncoderBitrateMbps(uint64 encoderId, double InEncoderBitrateMbps)
{
	GetEncoderStats(encoderId).EncoderBitrateMbps.Update(InEncoderBitrateMbps);
}

void FPixelStreamingStats::SetEncoderQP(uint64 encoderId, double QP)
{
	GetEncoderStats(encoderId).EncoderQP.Update(QP);
}

void FPixelStreamingStats::EmitStat(int UniqueId, FString StringToEmit)
{

	if(PixelStreamingSettings::CVarPixelStreamingOnScreenStats.GetValueOnGameThread())
	{
		GEngine->AddOnScreenDebugMessage(UniqueId, 0, FColor::Green, StringToEmit, false /* newer on top */);
	}

	if(PixelStreamingSettings::CVarPixelStreamingLogStats.GetValueOnGameThread())
	{
		UE_LOG(PixelStreamer, Log, TEXT("%s"), *StringToEmit);
	}

}

FPixelStreamingStats::FEncoderStats& FPixelStreamingStats::GetEncoderStats(uint64 encoderId)
{
	if (TUniquePtr<FEncoderStats>* Stats = EncoderStats.Find(encoderId))
	{
		return *Stats->Get();
	}

	TUniquePtr<FEncoderStats>& Stats = EncoderStats.Add(encoderId, MakeUnique<FEncoderStats>());
	return *Stats.Get();
}
