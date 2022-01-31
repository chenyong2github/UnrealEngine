// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WebRTCIncludes.h"
#include "PixelStreamingPlayerId.h"
#include "FixedFPSPump.h"
#include "TextureSource.h"
#include "IPixelStreamingSessions.h"

namespace UE
{
	namespace PixelStreaming
	{
		class FVideoSourceFactory;

		/*
		* Base class for pumped Pixel Streaming video sources. Video sources are WebRTC video sources that populate WebRTC tracks
		* and pass WebRTC video frames to `OnFrame`, which eventually gets passed to a WebRTC video encoder, encoded, and transmitted.
		*/
		class FVideoSourceBase : public rtc::AdaptedVideoTrackSource, public IPumpedVideoSource
		{
		public:
			FVideoSourceBase(FPixelStreamingPlayerId InPlayerId);
			virtual ~FVideoSourceBase();
			FPixelStreamingPlayerId GetPlayerId() const { return PlayerId; }
			virtual void Initialize();

			/* Begin rtc::AdaptedVideoTrackSource overrides */
			virtual webrtc::MediaSourceInterface::SourceState state() const override { return CurrentState; }
			virtual bool remote() const override { return false; }
			virtual bool is_screencast() const override { return false; }
			virtual absl::optional<bool> needs_denoising() const override { return false; }
			/* End rtc::AdaptedVideoTrackSource overrides */

			/* Begin IPumpedVideoSource */
			virtual void OnPump(int32 FrameId) override;
			virtual bool IsReadyForPump() const = 0;
			virtual void AddRef() const override;
			virtual rtc::RefCountReleaseStatus Release() const override;
			virtual bool HasOneRef() const { return RefCount.HasOneRef(); }
			/* End IPumpedVideoSource */

		protected:
			virtual bool AdaptCaptureFrame(const int64 TimestampUs, FIntPoint Resolution);
			virtual webrtc::VideoFrame CreateFrame(int32 FrameId) = 0;

		private:
			mutable webrtc::webrtc_impl::RefCounter RefCount{ 0 };

		protected:
			webrtc::MediaSourceInterface::SourceState CurrentState;
			FPixelStreamingPlayerId PlayerId;
		};

		/*
		* A video source for P2P peers.
		*/
		class FVideoSourceP2P : public FVideoSourceBase
		{
		public:
			FVideoSourceP2P(FPixelStreamingPlayerId InPlayerId, IPixelStreamingSessions* InSessions);

		protected:
			IPixelStreamingSessions* Sessions;
			TSharedPtr<ITextureSource> TextureSource;
			Settings::ECodec Codec;

		protected:
			/* Begin FVideoSourceBase */
			virtual webrtc::VideoFrame CreateFrame(int32 FrameId) override;
			virtual bool IsReadyForPump() const override;
			/* End FVideoSourceBase */

			virtual webrtc::VideoFrame CreateFrameH264(int32 FrameId);
			virtual webrtc::VideoFrame CreateFrameVPX(int32 FrameId);
		};

		/*
		* A video source for the SFU.
		*/
		class FVideoSourceSFU : public FVideoSourceBase
		{
		public:
			FVideoSourceSFU();

		protected:
			TArray<TSharedPtr<ITextureSource>> LayerTextures;

		protected:
			/* Begin FVideoSourceBase */
			virtual webrtc::VideoFrame CreateFrame(int32 FrameId) override;
			virtual bool IsReadyForPump() const override;
			/* End FVideoSourceBase */
		};

	} // namespace PixelStreaming
} // namespace UE
