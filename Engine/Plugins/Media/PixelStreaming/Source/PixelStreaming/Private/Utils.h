// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WebRTCIncludes.h"
#include "Async/Async.h"

namespace UE::PixelStreaming
{
	template <typename T>
	void DoOnGameThread(T&& Func)
	{
		if (IsInGameThread())
		{
			Func();
		}
		else
		{
			AsyncTask(ENamedThreads::GameThread, [Func]() { Func(); });
		}
	}

	template <typename T>
	void DoOnGameThreadAndWait(uint32 Timeout, T&& Func)
	{
		if (IsInGameThread())
		{
			Func();
		}
		else
		{
			FEvent* TaskEvent = FPlatformProcess::GetSynchEventFromPool();
			AsyncTask(ENamedThreads::GameThread, [Func, TaskEvent]() {
				Func();
				TaskEvent->Trigger();
			});
			TaskEvent->Wait(Timeout);
			FPlatformProcess::ReturnSynchEventToPool(TaskEvent);
		}
	}
#if WEBRTC_VERSION == 84
	inline webrtc::SdpVideoFormat CreateH264Format(webrtc::H264::Profile profile, webrtc::H264::Level level)
	{
		const absl::optional<std::string> ProfileString =
			webrtc::H264::ProfileLevelIdToString(webrtc::H264::ProfileLevelId(profile, level));
		check(ProfileString);
		return webrtc::SdpVideoFormat(
			cricket::kH264CodecName,
			{ { cricket::kH264FmtpProfileLevelId, *ProfileString },
				{ cricket::kH264FmtpLevelAsymmetryAllowed, "1" },
				{ cricket::kH264FmtpPacketizationMode, "1" } });

	}
#elif WEBRTC_VERSION == 96
	inline webrtc::SdpVideoFormat CreateH264Format(webrtc::H264Profile profile, webrtc::H264Level level)
	{
		const absl::optional<std::string> ProfileString =
			webrtc::H264ProfileLevelIdToString(webrtc::H264ProfileLevelId(profile, level));
		check(ProfileString);
		return webrtc::SdpVideoFormat(
			cricket::kH264CodecName,
			{ { cricket::kH264FmtpProfileLevelId, *ProfileString },
				{ cricket::kH264FmtpLevelAsymmetryAllowed, "1" },
				{ cricket::kH264FmtpPacketizationMode, "1" } });

	}
#endif
inline void MemCpyStride(void* Dest, const void* Src, size_t DestStride, size_t SrcStride, size_t Height)
	{
		char* DestPtr = static_cast<char*>(Dest);
		const char* SrcPtr = static_cast<const char*>(Src);
		size_t Row = Height;
		while (Row--)
		{
			FMemory::Memcpy(DestPtr + DestStride * Row, SrcPtr + SrcStride * Row, DestStride);
		}
	}

	inline size_t SerializeToBuffer(rtc::CopyOnWriteBuffer& Buffer, size_t Pos, const void* Data, size_t DataSize)
	{
#if WEBRTC_VERSION == 84
		FMemory::Memcpy(&Buffer[Pos], Data, DataSize);
#elif WEBRTC_VERSION == 96
		FMemory::Memcpy(Buffer.MutableData() + Pos, Data, DataSize);
#endif
		return Pos + DataSize;
	}
} // namespace UE::PixelStreaming
