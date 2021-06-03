// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "VideoEncoder.h"
#include "WebRTCIncludes.h"
#include "PixelStreamingSettings.generated.h"

// Console variables (CVars)
namespace PixelStreamingSettings
{

// Begin Encoder CVars
	extern TAutoConsoleVariable<int32> CVarPixelStreamingEncoderTargetBitrate;
	extern TAutoConsoleVariable<int32> CVarPixelStreamingEncoderMaxBitrate;
	extern TAutoConsoleVariable<bool> CVarPixelStreamingDebugDumpFrame;
	extern TAutoConsoleVariable<int32> CVarPixelStreamingEncoderMinQP;
	extern TAutoConsoleVariable<int32> CVarPixelStreamingEncoderMaxQP;
	extern TAutoConsoleVariable<FString> CVarPixelStreamingEncoderRateControl;
	extern TAutoConsoleVariable<bool> CVarPixelStreamingEnableFillerData;
	extern TAutoConsoleVariable<FString> CVarPixelStreamingEncoderMultipass;
// End Encoder CVars

// Begin Capturer CVars
	extern TAutoConsoleVariable<int32> CVarPixelStreamingUseBackBufferCaptureSize;
	extern TAutoConsoleVariable<FString> CVarPixelStreamingCaptureSize;
// End Capturer CVars

// Begin WebRTC CVars
	extern TAutoConsoleVariable<FString> CVarPixelStreamingDegradationPreference;
	extern TAutoConsoleVariable<int32> CVarPixelStreamingWebRTCMaxFps;
	extern TAutoConsoleVariable<int32> CVarPixelStreamingWebRTCMinBitrate;
	extern TAutoConsoleVariable<int32> CVarPixelStreamingWebRTCMaxBitrate;
	extern TAutoConsoleVariable<int> CVarPixelStreamingWebRTCLowQpThreshold;
	extern TAutoConsoleVariable<int> CVarPixelStreamingWebRTCHighQpThreshold;
// End WebRTC CVars

// Begin Pixel Streaming Plugin CVars
	extern TAutoConsoleVariable<bool> CVarPixelStreamingHudStats;
	extern TAutoConsoleVariable<int32> CVarFreezeFrameQuality;
// Ends Pixel Streaming Plugin CVars

// Begin utility functions etc.
	AVEncoder::FVideoEncoder::RateControlMode GetRateControlCVar();
	AVEncoder::FVideoEncoder::MultipassMode GetMultipassCVar();
	webrtc::DegradationPreference GetDegradationPreference();
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

// Config loaded/saved to an .ini file.
UCLASS(config = PixelStreaming, defaultconfig, meta = (DisplayName = "PixelStreaming"))
class PIXELSTREAMING_API UPixelStreamingSettings : public UDeveloperSettings
{
	GENERATED_UCLASS_BODY()

public:
	/**
	 * Pixel streaming always requires various software cursors so they will be
	 * visible in the video stream sent to the browser to allow the user to
	 * click and interact with UI elements.
	 */
	UPROPERTY(config, EditAnywhere, Category = PixelStreaming)
	FSoftClassPath PixelStreamerDefaultCursorClassName;
	UPROPERTY(config, EditAnywhere, Category = PixelStreaming)
	FSoftClassPath PixelStreamerTextEditBeamCursorClassName;

	/**
	 * Pixel Streaming can have a server-side cursor (where the cursor itself
	 * is shown as part of the video), or a client-side cursor (where the cursor
	 * is shown by the browser). In the latter case we need to turn the UE4
	 * cursor invisible.
	 */
	UPROPERTY(config, EditAnywhere, Category = PixelStreaming)
	FSoftClassPath PixelStreamerHiddenCursorClassName;
	
	/**
	 * Pixel Streaming may be running on a machine which has no physical mouse
	 * attached, and yet the browser is sending mouse positions. As such, we
	 * fake the presence of a mouse.
	 */
	UPROPERTY(config, EditAnywhere, Category = PixelStreaming)
	bool bPixelStreamerMouseAlwaysAttached = true;

	// Begin UDeveloperSettings Interface
	virtual FName GetCategoryName() const override;
#if WITH_EDITOR
	virtual FText GetSectionText() const override;
#endif
	// END UDeveloperSettings Interface

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};