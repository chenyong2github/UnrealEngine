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

	if(PixelStreamingSettings::CVarPixelStreamingOnScreenStats.GetValueOnGameThread())
	{
		GAreScreenMessagesEnabled = true;
	}

	double NowMillis = FTimespan::FromSeconds(FPlatformTime::Seconds()).GetTotalMilliseconds();
	double KeyframeDeltaSecs = LastKeyFrameTimeCycles > 0 ? FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - LastKeyFrameTimeCycles) : 0;

	int Id = 0;
	EmitStat(++Id, FString::Printf(TEXT("Timestamp: %.0f ms"), NowMillis));
	EmitStat(++Id, FString::Printf(TEXT("Capture latency: %.5f ms"), CaptureLatencyMs.Get()));
	EmitStat(++Id, FString::Printf(TEXT("Encoder latency: %.5f ms"), EncoderLatencyMs.Get()));
	EmitStat(++Id, FString::Printf(TEXT("Post encode latency: %.5f ms"), PostEncodeLatencyMs));
	EmitStat(++Id, FString::Printf(TEXT("WebRTC Capture to Pre-Encode latency: %.5f ms"), WebRTCCaptureToEncodeLatencyMs.Get()));
	EmitStat(++Id, FString::Printf(TEXT("GameThread FPS: %.0f"), 1.0f / FApp::GetDeltaTime()));
	EmitStat(++Id, FString::Printf(TEXT("Captured FPS: %.0f"), CaptureFPS.Get()));
	EmitStat(++Id, FString::Printf(TEXT("Submit to WebRTC FPS: %.0f"), SubmitToWebRTCFPS.Get()));
	EmitStat(++Id, FString::Printf(TEXT("WebRTC encode loop FPS: %.0f"), WebRTCEncodeLoopFPS.Get()));
	EmitStat(++Id, FString::Printf(TEXT("Encoded FPS: %.0f"), EncoderFPS.Get()));
	EmitStat(++Id, FString::Printf(TEXT("Encoder bitrate: %.3f Mbps"), EncoderBitrateMbps.Get()));
	EmitStat(++Id, FString::Printf(TEXT("QP: %.0f"), EncoderQP.Get()));
	EmitStat(++Id, FString::Printf(TEXT("Time since last keyframe: %.3f secs"), KeyframeDeltaSecs));
	EmitStat(++Id, "------------ Pixel Streaming Stats ------------");

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

void FPixelStreamingStats::SetPostEncodeLatency(double InPostEncodeLatencyMs)
{
	this->PostEncodeLatencyMs = InPostEncodeLatencyMs;
}

void FPixelStreamingStats::SetCaptureToEncodeLatency(double CaptureToEncodeMs)
{
	this->WebRTCCaptureToEncodeLatencyMs.Update(CaptureToEncodeMs);
}

void FPixelStreamingStats::OnEncodingFinished()
{
	uint64 NowCycles = FPlatformTime::Cycles64();
	if(this->LastEncodeTimeCycles != 0)
	{
		uint64 DeltaCycles = NowCycles - this->LastEncodeTimeCycles;
		double DeltaSeconds = FPlatformTime::ToSeconds64(DeltaCycles);
		double EncodesPerSecond = 1.0 / DeltaSeconds;
		this->EncoderFPS.Update(EncodesPerSecond);
	}
	this->LastEncodeTimeCycles = NowCycles;
}

void FPixelStreamingStats::OnCaptureFinished()
{
	uint64 NowCycles = FPlatformTime::Cycles64();
	if(this->LastCaptureTimeCycles != 0)
	{
		uint64 DeltaCycles = NowCycles - this->LastCaptureTimeCycles;
		double DeltaSeconds = FPlatformTime::ToSeconds64(DeltaCycles);
		double CapturesPerSecond = 1.0 / DeltaSeconds;
		this->CaptureFPS.Update(CapturesPerSecond);
	}
	this->LastCaptureTimeCycles = NowCycles;
}

void FPixelStreamingStats::OnWebRTCDeliverFrameForEncode()
{
	uint64 NowCycles = FPlatformTime::Cycles64();
	if(this->LastWebRTCEncodeAttempt != 0)
	{
		uint64 DeltaCycles = NowCycles - this->LastWebRTCEncodeAttempt;
		double DeltaSeconds = FPlatformTime::ToSeconds64(DeltaCycles);
		double LoopsPerSecond = 1.0 / DeltaSeconds;
		this->WebRTCEncodeLoopFPS.Update(LoopsPerSecond);
	}
	this->LastWebRTCEncodeAttempt = NowCycles;
}

void FPixelStreamingStats::OnFrameSubmittedToWebRTC()
{
	uint64 NowCycles = FPlatformTime::Cycles64();
	if(this->LastSubmitTimeCycles != 0)
	{
		uint64 DeltaCycles = NowCycles - this->LastSubmitTimeCycles;
		double DeltaSeconds = FPlatformTime::ToSeconds64(DeltaCycles);
		double SubmitsPerSecond = 1.0 / DeltaSeconds;
		this->SubmitToWebRTCFPS.Update(SubmitsPerSecond);
	}
	this->LastSubmitTimeCycles = NowCycles;
}

void FPixelStreamingStats::SetEncoderLatency(double InEncoderLatencyMs)
{
	this->EncoderLatencyMs.Update(InEncoderLatencyMs);
}

void FPixelStreamingStats::SetEncoderBitrateMbps(double InEncoderBitrateMbps)
{
	this->EncoderBitrateMbps.Update(InEncoderBitrateMbps);
}

void FPixelStreamingStats::SetEncoderQP(double QP)
{
	this->EncoderQP.Update(QP);
}

void FPixelStreamingStats::SetCaptureLatency(double InCaptureLatencyMs)
{
	this->CaptureLatencyMs.Update(InCaptureLatencyMs);
}

void FPixelStreamingStats::OnKeyframeEncoded()
{
	uint64 NowCycles = FPlatformTime::Cycles64();
	this->LastKeyFrameTimeCycles = NowCycles;
}

void FPixelStreamingStats::Reset()
{
	EncoderLatencyMs.Reset();
	EncoderBitrateMbps.Reset();
	EncoderQP.Reset();
	EncoderFPS.Reset();

	CaptureFPS.Reset();
	CaptureLatencyMs.Reset();

	SubmitToWebRTCFPS.Reset();
}
