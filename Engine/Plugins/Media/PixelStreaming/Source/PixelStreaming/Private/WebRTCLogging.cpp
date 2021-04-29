// Copyright Epic Games, Inc. All Rights Reserved.

#include "WebRTCLogging.h"

#include "Utils.h"

#include "Logging/LogMacros.h"
#include "Misc/AssertionMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(PixelStreamingWebRTC, Log, All);
DEFINE_LOG_CATEGORY(PixelStreamingWebRTC);

/**
	* Receives logging from WebRTC internals, and writes it to a log file
	* and VS's Output window
	*/
class FWebRtcLogsRedirector : public rtc::LogSink
{
public:
	explicit FWebRtcLogsRedirector(rtc::LoggingSeverity Verbosity)
	{
		UE_LOG(PixelStreamingWebRTC, Verbose, TEXT("Starting WebRTC logging"));

		rtc::LogMessage::AddLogToStream(this, Verbosity);

		// Disable WebRTC's internal calls to VS's OutputDebugString, because we are calling here,
		// so we can add timestamps. Do this after `rtc::LogMessage::AddLogToStream`
		rtc::LogMessage::LogToDebug(rtc::LS_NONE);
		rtc::LogMessage::SetLogToStderr(false);
	}

	~FWebRtcLogsRedirector() override
	{
		UE_LOG(PixelStreamingWebRTC, Log, TEXT("Stopping WebRTC logging"));

		rtc::LogMessage::RemoveLogToStream(this);
	}

private:
	void OnLogMessage(const std::string& message, rtc::LoggingSeverity severity,
		const char* tag) override
	{
#if !NO_LOGGING
		static const ELogVerbosity::Type RtcToUnrealLogCategoryMap[] =
		{
			ELogVerbosity::VeryVerbose,
			ELogVerbosity::Verbose,
			ELogVerbosity::Log,
			ELogVerbosity::Warning,
			ELogVerbosity::Error
		};

		if (PixelStreamingWebRTC.IsSuppressed(RtcToUnrealLogCategoryMap[severity]))
		{
			return;
		}

		size_t Size = message.size();
		if (Size != 0 && message.back() == '\n')
		{
			--Size;
		}

		// this involves two allocations and copies
		// one allocation can be amortised by turning `FString Msg` into a member variable
		// but since logging should be multithread-safe, it should be thread-local member or explicitly synchronised
		auto StrPtr = StringCast<TCHAR>(message.c_str(), Size);
		// to zero-terminate the string
		FString Msg{ StrPtr.Length(), StrPtr.Get() };

		switch (severity)
		{
		case rtc::LS_VERBOSE:
			UE_LOG(PixelStreamingWebRTC, Verbose, TEXT("%s"), *Msg);
			break;
		case rtc::LS_INFO:
			UE_LOG(PixelStreamingWebRTC, Log, TEXT("%s"), *Msg);
			break;
		case rtc::LS_WARNING:
			UE_LOG(PixelStreamingWebRTC, Warning, TEXT("%s"), *Msg);
			break;
		case rtc::LS_ERROR:
			UE_LOG(PixelStreamingWebRTC, Error, TEXT("%s"), *Msg);
			break;
		}
#endif
	}

	void OnLogMessage(const std::string& message) override
	{
		//unimplemented();
		UE_LOG(PixelStreamingWebRTC, Verbose, TEXT("%s"), *FString(message.c_str()));
	}
};

void RedirectWebRtcLogsToUnreal(rtc::LoggingSeverity Verbosity)
{
#if !UE_BUILD_SHIPPING
	static FWebRtcLogsRedirector Redirector{ Verbosity };
#endif
}
