// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "VideoEncoder.h"
#include "WebRTCIncludes.h"

// Console variables (CVars)
namespace PixelStreamingSettings
{

	extern void InitialiseSettings();

// Begin Encoder CVars
	extern TAutoConsoleVariable<int32> CVarPixelStreamingEncoderTargetBitrate;
	extern TAutoConsoleVariable<int32> CVarPixelStreamingEncoderMaxBitrate;
	extern TAutoConsoleVariable<bool> CVarPixelStreamingDebugDumpFrame;
	extern TAutoConsoleVariable<int32> CVarPixelStreamingEncoderMinQP;
	extern TAutoConsoleVariable<int32> CVarPixelStreamingEncoderMaxQP;
	extern TAutoConsoleVariable<FString> CVarPixelStreamingEncoderRateControl;
	extern TAutoConsoleVariable<bool> CVarPixelStreamingEnableFillerData;
	extern TAutoConsoleVariable<FString> CVarPixelStreamingEncoderMultipass;
	extern TAutoConsoleVariable<FString> CVarPixelStreamingH264Profile;
// End Encoder CVars

// Begin Capturer CVars
	extern TAutoConsoleVariable<bool> CVarPixelStreamingUseBackBufferCaptureSize;
	extern TAutoConsoleVariable<FString> CVarPixelStreamingCaptureSize;
	extern TAutoConsoleVariable<int32> CVarPixelStreamingMaxNumBackBuffers;
// End Capturer CVars

// Begin WebRTC CVars
	extern TAutoConsoleVariable<FString> CVarPixelStreamingDegradationPreference;
	extern TAutoConsoleVariable<int32> CVarPixelStreamingWebRTCMaxFps;
	extern TAutoConsoleVariable<int32> CVarPixelStreamingWebRTCStartBitrate;
	extern TAutoConsoleVariable<int32> CVarPixelStreamingWebRTCMinBitrate;
	extern TAutoConsoleVariable<int32> CVarPixelStreamingWebRTCMaxBitrate;
	extern TAutoConsoleVariable<int> CVarPixelStreamingWebRTCLowQpThreshold;
	extern TAutoConsoleVariable<int> CVarPixelStreamingWebRTCHighQpThreshold;
	extern TAutoConsoleVariable<bool> CVarPixelStreamingWebRTCDisableReceiveAudio;
	extern TAutoConsoleVariable<bool> CVarPixelStreamingWebRTCDisableTransmitAudio;
	extern TAutoConsoleVariable<bool> CVarPixelStreamingWebRTCDisableAudioSync;
	extern TAutoConsoleVariable<bool> CVarPixelStreamingWebRTCUseLegacyAudioDevice;
	extern TAutoConsoleVariable<bool> CVarPixelStreamingWebRTCDisableResolutionChange;
// End WebRTC CVars

// Begin Pixel Streaming Plugin CVars
	extern TAutoConsoleVariable<bool> CVarPixelStreamingOnScreenStats;
	extern TAutoConsoleVariable<bool> CVarPixelStreamingLogStats;

	extern TAutoConsoleVariable<int32> CVarPixelStreamingFreezeFrameQuality;
	extern TAutoConsoleVariable<bool> CVarSendPlayerIdAsInteger;
	extern TAutoConsoleVariable<bool> CVarPixelStreamingDisableLatencyTester;
	extern TArray<FKey> FilteredKeys;
// Ends Pixel Streaming Plugin CVars

// Begin utility functions etc.
	AVEncoder::FVideoEncoder::RateControlMode GetRateControlCVar();
	AVEncoder::FVideoEncoder::MultipassMode GetMultipassCVar();
	webrtc::DegradationPreference GetDegradationPreference();
	AVEncoder::FVideoEncoder::H264Profile GetH264Profile();
// End utility functions etc.

// Begin Command line args

	inline bool IsAllowPixelStreamingCommands()
	{
		return FParse::Param(FCommandLine::Get(), TEXT("AllowPixelStreamingCommands"));
	}

	inline bool IsPixelStreamingHideCursor()
	{
		return FParse::Param(FCommandLine::Get(), TEXT("PixelStreamingHideCursor"));
	}
	
	inline bool IsForceVP8()
	{
		return FParse::Param(FCommandLine::Get(), TEXT("PSForceVP8"));
	}

	inline bool GetSignallingServerIP(FString& OutSignallingServerIP)
	{
		return FParse::Value(FCommandLine::Get(), TEXT("PixelStreamingIP="), OutSignallingServerIP);
	}

	inline bool GetSignallingServerPort(uint16& OutSignallingServerPort)
	{
		return FParse::Value(FCommandLine::Get(), TEXT("PixelStreamingPort="), OutSignallingServerPort);
	}

	inline bool GetControlScheme(FString& OutControlScheme)
	{
		return FParse::Value(FCommandLine::Get(), TEXT("PixelStreamingControlScheme="), OutControlScheme);
	}

	inline bool GetFastPan(float& OutFastPan)
	{
		return FParse::Value(FCommandLine::Get(), TEXT("PixelStreamingFastPan="), OutFastPan);
	}

// End Command line args

}