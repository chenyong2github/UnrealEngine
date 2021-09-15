// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingSettings.h"
#include "PixelStreamingPrivate.h"

template<typename T>
void CommandLineParseValue(const TCHAR* Match, TAutoConsoleVariable<T>& CVar)
{
	T Value;
	if (FParse::Value(FCommandLine::Get(), Match, Value))
		CVar->Set(Value, ECVF_SetByCommandline);
};

void CommandLineParseValue(const TCHAR* Match, TAutoConsoleVariable<FString>& CVar, bool StopOnSeparator = false)
{
	FString Value;
	if (FParse::Value(FCommandLine::Get(), Match, Value, StopOnSeparator))
		CVar->Set(*Value, ECVF_SetByCommandline);
};

void CommandLineParseOption(const TCHAR* Match, TAutoConsoleVariable<bool>& CVar)
{
	FString ValueMatch(Match);
	ValueMatch.Append(TEXT("="));
	FString Value;
	if (FParse::Value(FCommandLine::Get(), *ValueMatch, Value)) {
		if (Value.Equals(FString(TEXT("true")), ESearchCase::IgnoreCase)) {
			CVar->Set(true, ECVF_SetByCommandline);
		}
		else if (Value.Equals(FString(TEXT("false")), ESearchCase::IgnoreCase)) {
			CVar->Set(false, ECVF_SetByCommandline);
		}
	}
	else if (FParse::Param(FCommandLine::Get(), Match))
	{
		CVar->Set(true, ECVF_SetByCommandline);
	}
}

namespace PixelStreamingSettings
{
// Begin Encoder CVars
	TAutoConsoleVariable<int32> CVarPixelStreamingEncoderTargetBitrate(
		TEXT("PixelStreaming.Encoder.TargetBitrate"),
		-1,
		TEXT("Target bitrate (bps). Ignore the bitrate WebRTC wants (not recommended). Set to -1 to disable. Default -1."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarPixelStreamingEncoderMaxBitrate(
		TEXT("PixelStreaming.Encoder.MaxBitrateVBR"),
		20000000,
		TEXT("Max bitrate (bps). Does not work in CBR rate control mode with NVENC."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<bool> CVarPixelStreamingDebugDumpFrame(
		TEXT("PixelStreaming.Encoder.DumpDebugFrames"),
		false,
		TEXT("Dumps frames from the encoder to a file on disk for debugging purposes."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarPixelStreamingEncoderMinQP(
		TEXT("PixelStreaming.Encoder.MinQP"),
		-1,
		TEXT("0-51, lower values result in better quality but higher bitrate. Default -1. -1 will disable any hard limit on a minimum QP."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarPixelStreamingEncoderMaxQP(
		TEXT("PixelStreaming.Encoder.MaxQP"),
		-1,
		TEXT("0-51, lower values result in better quality but higher bitrate. Default -1. -1 will disable any hard limit on a maximum QP."),
		ECVF_Default);

	TAutoConsoleVariable<FString> CVarPixelStreamingEncoderRateControl(
		TEXT("PixelStreaming.Encoder.RateControl"),
		TEXT("CBR"),
		TEXT("PixelStreaming video encoder RateControl mode. Supported modes are `ConstQP`, `VBR`, `CBR`. Default: CBR, which we recommend."),
		ECVF_Default);

	TAutoConsoleVariable<bool> CVarPixelStreamingEnableFillerData(
		TEXT("PixelStreaming.Encoder.EnableFillerData"),
		false,
		TEXT("Maintains constant bitrate by filling with junk data. Note: Should not be required with CBR and MinQP = -1. Default: false."),
		ECVF_Default);

	TAutoConsoleVariable<FString> CVarPixelStreamingEncoderMultipass(
		TEXT("PixelStreaming.Encoder.Multipass"),
		TEXT("FULL"),
		TEXT("PixelStreaming encoder multipass. Supported modes are `DISABLED`, `QUARTER`, `FULL`"),
		ECVF_Default);

	TAutoConsoleVariable<FString> CVarPixelStreamingH264Profile(
		TEXT("PixelStreaming.Encoder.H264Profile"),
		TEXT("BASELINE"),
		TEXT("PixelStreaming encoder profile. Supported modes are `AUTO`, `BASELINE`, `MAIN`, `HIGH`, `HIGH444`, `STEREO`, `SVC_TEMPORAL_SCALABILITY`, `PROGRESSIVE_HIGH`, `CONSTRAINED_HIGH`"),
		ECVF_Default);
// End Encoder CVars

// Begin Capturer CVars
	TAutoConsoleVariable<bool> CVarPixelStreamingUseBackBufferCaptureSize(
		TEXT("PixelStreaming.Capturer.UseBackBufferSize"),
		true,
		TEXT("Whether to use back buffer size or custom size"),
		ECVF_Default);

	TAutoConsoleVariable<FString> CVarPixelStreamingCaptureSize(
		TEXT("PixelStreaming.Capturer.CaptureSize"),
		TEXT("1920x1080"),
		TEXT("Capture size in format widthxheight. Recommended to UseBackBufferSize instead."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarPixelStreamingMaxNumBackBuffers(
		TEXT("PixelStreaming.Capturer.MaxNumBackBuffers"),
		3,
		TEXT("Maximum number of back buffers to use. A value of 0 will not limit the number of back buffers."),
		ECVF_Default);
// End Capturer CVars

// Begin WebRTC CVars
	TAutoConsoleVariable<FString> CVarPixelStreamingDegradationPreference(
		TEXT("PixelStreaming.WebRTC.DegradationPreference"),
		TEXT("MAINTAIN_FRAMERATE"),
		TEXT("PixelStreaming degradation preference. Supported modes are `BALANCED`, `MAINTAIN_FRAMERATE`, `MAINTAIN_RESOLUTION`"),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarPixelStreamingWebRTCMaxFps(
		TEXT("PixelStreaming.WebRTC.MaxFps"),
		60,
		TEXT("Maximum fps WebRTC will try to request. Default: 60"),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarPixelStreamingWebRTCStartBitrate(
		TEXT("PixelStreaming.WebRTC.StartBitrate"),
		10000000,
		TEXT("Start bitrate (bps) that WebRTC will try begin the stream with. Must be between Min/Max bitrates. Default: 10000000"),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarPixelStreamingWebRTCMinBitrate(
		TEXT("PixelStreaming.WebRTC.MinBitrate"),
		100000,
		TEXT("Min bitrate (bps) that WebRTC will not request below. Careful not to set too high otherwise WebRTC will just drop frames. Default: 100000"),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarPixelStreamingWebRTCMaxBitrate(
		TEXT("PixelStreaming.WebRTC.MaxBitrate"),
		20000000,
		TEXT("Max bitrate (bps) that WebRTC will not request above. Careful not to set too high otherwise because a local (ideal network) will actually reach this. Default: 20000000"),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int> CVarPixelStreamingWebRTCLowQpThreshold(
		TEXT("PixelStreaming.WebRTC.LowQpThreshold"),
		25,
		TEXT("Only useful when MinQP=-1. Value between 1-51 (default: 25). If WebRTC is getting frames below this QP it will try to increase resolution when not in MAINTAIN_RESOLUTION mode."),
		ECVF_Default);

	TAutoConsoleVariable<int> CVarPixelStreamingWebRTCHighQpThreshold(
		TEXT("PixelStreaming.WebRTC.HighQpThreshold"),
		37,
		TEXT("Only useful when MinQP=-1. Value between 1-51 (default: 37). If WebRTC is getting frames above this QP it will decrease resolution when not in MAINTAIN_RESOLUTION mode."),
		ECVF_Default);

	TAutoConsoleVariable<bool> CVarPixelStreamingWebRTCDisableReceiveAudio(
		TEXT("PixelStreaming.WebRTC.DisableReceiveAudio"),
		false,
		TEXT("Disables receiving audio from the browser into UE."),
		ECVF_Default);

	TAutoConsoleVariable<bool> CVarPixelStreamingWebRTCDisableTransmitAudio(
		TEXT("PixelStreaming.WebRTC.DisableTransmitAudio"),
		false,
		TEXT("Disables transmission of UE audio to the browser."),
		ECVF_Default);

	TAutoConsoleVariable<bool> CVarPixelStreamingWebRTCDisableAudioSync(
		TEXT("PixelStreaming.WebRTC.DisableAudioSync"),
		true,
		TEXT("Disables the synchronization of audio and video tracks in WebRTC. This can be useful in low latency usecases where synchronization is not required."),
		ECVF_Default);

	TAutoConsoleVariable<bool> CVarPixelStreamingWebRTCUseLegacyAudioDevice(
		TEXT("PixelStreaming.WebRTC.UseLegacyAudioDevice"),
		false,
		TEXT("Whether put audio and video in the same stream (which will make WebRTC try to sync them)."),
		ECVF_Default);

	TAutoConsoleVariable<bool> CVarPixelStreamingWebRTCDisableResolutionChange(
		TEXT("PixelStreaming.WebRTC.DisableResolutionChange"),
		true,
		TEXT("Disables whether WebRTC can trigger a resolution a change of the stream (regardless of it wants to)."),
		ECVF_Default);

// End WebRTC CVars

// Begin Pixel Streaming Plugin CVars
	TAutoConsoleVariable<bool> CVarPixelStreamingHudStats(
		TEXT("PixelStreaming.HUDStats"),
		false,
		TEXT("Whether to show PixelStreaming stats on the in-game HUD."),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarFreezeFrameQuality(
		TEXT("PixelStreaming.FreezeFrameQuality"),
		100,
		TEXT("Compression quality of the freeze frame"),
		ECVF_Default);

	TAutoConsoleVariable<bool> CVarSendPlayerIdAsInteger(
		TEXT("PixelStreaming.SendPlayerIdAsInteger"),
		true,
		TEXT("If true transmit the player id as an integer (for backward compatibility) or as a string."),
		ECVF_Default);

	TAutoConsoleVariable<bool> CVarPixelStreamingDisableLatencyTester(
		TEXT("PixelStreaming.DisableLatencyTester"),
		false,
		TEXT("If true disables latency tester being triggerable."),
		ECVF_Default);

	 TAutoConsoleVariable<FString> CVarPixelStreamingKeyFilter(
	 	TEXT("PixelStreaming.KeyFilter"),
		"",
		TEXT("Comma separated list of keys to ignore from streaming clients."),
		ECVF_Default);

	TArray<FKey> FilteredKeys;

	void OnFilteredKeysChanged(IConsoleVariable* Var)
	{
		FString CommaList = Var->GetString();
		TArray<FString> KeyStringArray;
		CommaList.ParseIntoArray(KeyStringArray, TEXT(","), true);
		FilteredKeys.Empty();
		for (auto&& KeyString : KeyStringArray)
		{
			FilteredKeys.Add(FKey(*KeyString));
		}
	}
// Ends Pixel Streaming Plugin CVars

// Begin utility functions etc.
	std::map<FString, AVEncoder::FVideoEncoder::RateControlMode> const RateControlCVarMap{
		{"ConstQP", AVEncoder::FVideoEncoder::RateControlMode::CONSTQP },
		{"VBR", AVEncoder::FVideoEncoder::RateControlMode::VBR },
		{"CBR", AVEncoder::FVideoEncoder::RateControlMode::CBR },
	};

	std::map<FString, AVEncoder::FVideoEncoder::MultipassMode> const MultipassCVarMap{
		{"DISABLED", AVEncoder::FVideoEncoder::MultipassMode::DISABLED },
		{"QUARTER", AVEncoder::FVideoEncoder::MultipassMode::QUARTER },
		{"FULL", AVEncoder::FVideoEncoder::MultipassMode::FULL },
	};

	std::map<FString, AVEncoder::FVideoEncoder::H264Profile> const H264ProfileMap{
		{"AUTO", AVEncoder::FVideoEncoder::H264Profile::AUTO},
		{"BASELINE", AVEncoder::FVideoEncoder::H264Profile::BASELINE},
		{"MAIN", AVEncoder::FVideoEncoder::H264Profile::MAIN},
		{"HIGH", AVEncoder::FVideoEncoder::H264Profile::HIGH},
		{"HIGH444", AVEncoder::FVideoEncoder::H264Profile::HIGH444},
		{"STEREO", AVEncoder::FVideoEncoder::H264Profile::STEREO},
		{"SVC_TEMPORAL_SCALABILITY", AVEncoder::FVideoEncoder::H264Profile::SVC_TEMPORAL_SCALABILITY},
		{"PROGRESSIVE_HIGH", AVEncoder::FVideoEncoder::H264Profile::PROGRESSIVE_HIGH},
		{"CONSTRAINED_HIGH", AVEncoder::FVideoEncoder::H264Profile::CONSTRAINED_HIGH},
	};

	AVEncoder::FVideoEncoder::RateControlMode GetRateControlCVar()
	{
		auto const cvarStr = CVarPixelStreamingEncoderRateControl.GetValueOnAnyThread();
		auto const it = RateControlCVarMap.find(cvarStr);
		if (it == std::end(RateControlCVarMap))
			return AVEncoder::FVideoEncoder::RateControlMode::CBR;
		return it->second;
	}

	AVEncoder::FVideoEncoder::MultipassMode GetMultipassCVar()
	{
		auto const cvarStr = CVarPixelStreamingEncoderMultipass.GetValueOnAnyThread();
		auto const it = MultipassCVarMap.find(cvarStr);
		if (it == std::end(MultipassCVarMap))
			return AVEncoder::FVideoEncoder::MultipassMode::FULL;
		return it->second;
	}

	webrtc::DegradationPreference GetDegradationPreference()
	{
		FString DegradationPrefStr = CVarPixelStreamingDegradationPreference.GetValueOnAnyThread();
		if(DegradationPrefStr == "MAINTAIN_FRAMERATE")
		{
			return webrtc::DegradationPreference::MAINTAIN_FRAMERATE;
		}
		else if(DegradationPrefStr == "MAINTAIN_RESOLUTION")
		{
			return webrtc::DegradationPreference::MAINTAIN_RESOLUTION;
		}
		// Everything else, return balanced.
		return webrtc::DegradationPreference::BALANCED;
	}

	AVEncoder::FVideoEncoder::H264Profile GetH264Profile()
	{
		auto const cvarStr = CVarPixelStreamingH264Profile.GetValueOnAnyThread();
		auto const it = H264ProfileMap.find(cvarStr);
		if (it == std::end(H264ProfileMap))
			return AVEncoder::FVideoEncoder::H264Profile::BASELINE;
		return it->second;
	}
// End utility functions etc.

	void InitialiseSettings()
	{

		UE_LOG(PixelStreamer, Log, TEXT("Initialising Pixel Streaming settings."));

		PixelStreamingSettings::CVarPixelStreamingKeyFilter.AsVariable()->SetOnChangedCallback(FConsoleVariableDelegate::CreateStatic(&PixelStreamingSettings::OnFilteredKeysChanged));

		// Values parse from commands line
		CommandLineParseValue(TEXT("PixelStreamingEncoderTargetBitrate="), PixelStreamingSettings::CVarPixelStreamingEncoderTargetBitrate);
		CommandLineParseValue(TEXT("PixelStreamingEncoderMaxBitrate="), PixelStreamingSettings::CVarPixelStreamingEncoderMaxBitrate);
		CommandLineParseValue(TEXT("PixelStreamingEncoderMinQP="), PixelStreamingSettings::CVarPixelStreamingEncoderMinQP);
		CommandLineParseValue(TEXT("PixelStreamingEncoderMaxQP="), PixelStreamingSettings::CVarPixelStreamingEncoderMaxQP);
		CommandLineParseValue(TEXT("PixelStreamingEncoderRateControl="), PixelStreamingSettings::CVarPixelStreamingEncoderRateControl);
		CommandLineParseValue(TEXT("PixelStreamingEncoderMultipass="), PixelStreamingSettings::CVarPixelStreamingEncoderMultipass);
		CommandLineParseValue(TEXT("PixelStreamingH264Profile="), PixelStreamingSettings::CVarPixelStreamingH264Profile);
		CommandLineParseValue(TEXT("PixelStreamingCaptureSize="), PixelStreamingSettings::CVarPixelStreamingCaptureSize);
		CommandLineParseValue(TEXT("PixelStreamingMaxNumBackBuffers="), PixelStreamingSettings::CVarPixelStreamingMaxNumBackBuffers);
		CommandLineParseValue(TEXT("PixelStreamingDegradationPreference="), PixelStreamingSettings::CVarPixelStreamingDegradationPreference);
		CommandLineParseValue(TEXT("PixelStreamingWebRTCMaxFps="), PixelStreamingSettings::CVarPixelStreamingWebRTCMaxFps);
		CommandLineParseValue(TEXT("PixelStreamingWebRTCStartBitrate="), PixelStreamingSettings::CVarPixelStreamingWebRTCStartBitrate);
		CommandLineParseValue(TEXT("PixelStreamingWebRTCMinBitrate="), PixelStreamingSettings::CVarPixelStreamingWebRTCMinBitrate);
		CommandLineParseValue(TEXT("PixelStreamingWebRTCMaxBitrate="), PixelStreamingSettings::CVarPixelStreamingWebRTCMaxBitrate);
		CommandLineParseValue(TEXT("PixelStreamingWebRTCLowQpThreshold="), PixelStreamingSettings::CVarPixelStreamingWebRTCLowQpThreshold);
		CommandLineParseValue(TEXT("PixelStreamingWebRTCHighQpThreshold="), PixelStreamingSettings::CVarPixelStreamingWebRTCHighQpThreshold);
		CommandLineParseValue(TEXT("FreezeFrameQuality="), PixelStreamingSettings::CVarFreezeFrameQuality);
		CommandLineParseValue(TEXT("PixelStreamingKeyFilter="), PixelStreamingSettings::CVarPixelStreamingKeyFilter);

		// Options parse (if these exist they are set to true)
		CommandLineParseOption(TEXT("PixelStreamingHudStats"), PixelStreamingSettings::CVarPixelStreamingHudStats);
		CommandLineParseOption(TEXT("PixelStreamingDebugDumpFrame"), PixelStreamingSettings::CVarPixelStreamingDebugDumpFrame);
		CommandLineParseOption(TEXT("PixelStreamingEnableFillerData"), PixelStreamingSettings::CVarPixelStreamingEnableFillerData);
		CommandLineParseOption(TEXT("PixelStreamingUseBackBufferCaptureSize"), PixelStreamingSettings::CVarPixelStreamingUseBackBufferCaptureSize);
		CommandLineParseOption(TEXT("PixelStreamingWebRTCDisableReceiveAudio"), PixelStreamingSettings::CVarPixelStreamingWebRTCDisableReceiveAudio);
		CommandLineParseOption(TEXT("PixelStreamingWebRTCDisableTransmitAudio"), PixelStreamingSettings::CVarPixelStreamingWebRTCDisableTransmitAudio);
		CommandLineParseOption(TEXT("PixelStreamingWebRTCDisableAudioSync"), PixelStreamingSettings::CVarPixelStreamingWebRTCDisableAudioSync);
		CommandLineParseOption(TEXT("PixelStreamingSendPlayerIdAsInteger"), PixelStreamingSettings::CVarSendPlayerIdAsInteger);
		CommandLineParseOption(TEXT("PixelStreamingWebRTCUseLegacyAudioDevice"), PixelStreamingSettings::CVarPixelStreamingWebRTCUseLegacyAudioDevice);
		CommandLineParseOption(TEXT("PixelStreamingDisableLatencyTester"), PixelStreamingSettings::CVarPixelStreamingDisableLatencyTester);

	}

}
