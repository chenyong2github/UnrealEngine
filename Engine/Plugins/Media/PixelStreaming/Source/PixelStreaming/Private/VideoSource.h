// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingPumpable.h"
#include "AdaptedVideoTrackSource.h"
#include "Settings.h"
#include "TextureTripleBuffer.h"
#include "PixelStreamingPlayerId.h"

namespace UE::PixelStreaming
{
	class FVideoSourceFactory;
	class FPlayerSessions;

	/*
	* Video sources are WebRTC video sources that populate WebRTC tracks
	* and pass WebRTC video frames to `OnFrame`, which eventually gets passed to a WebRTC video encoder, encoded, and transmitted.
	*/
	class FVideoSourceBase : public AdaptedVideoTrackSource, public FPixelStreamingPumpable
	{
	public:
		FVideoSourceBase();
		virtual ~FVideoSourceBase();
		virtual void Initialize();

		/* Begin UE::PixelStreaming::AdaptedVideoTrackSource overrides */
		virtual webrtc::MediaSourceInterface::SourceState state() const override { return CurrentState; }
		virtual bool remote() const override { return false; }
		virtual bool is_screencast() const override { return false; }
		virtual absl::optional<bool> needs_denoising() const override { return false; }
		void AddRef() const override { FPixelStreamingPumpable::AddRef(); }
		virtual rtc::RefCountReleaseStatus Release() const override { return FPixelStreamingPumpable::Release(); }
		/* End UE::PixelStreaming::AdaptedVideoTrackSource overrides */

		/* Begin FPixelStreamingPumpable */
		virtual void OnPump(int32 FrameId) override;
		virtual bool IsReadyForPump() const = 0;
		/* End FPixelStreamingPumpable */

	protected:
		virtual bool AdaptCaptureFrame(const int64 TimestampUs, FIntPoint Resolution);
		virtual webrtc::VideoFrame CreateFrame(int32 FrameId) = 0;

	protected:
		webrtc::MediaSourceInterface::SourceState CurrentState;
	};

	/*
		* A video source for P2P peers.
		*/
	class FVideoSourceP2P : public FVideoSourceBase
	{
	public:
		FVideoSourceP2P(FName SourceType, TFunction<bool()> InIsQualityControllerFunc);

	protected:
		TSharedPtr<FTextureTripleBuffer> TextureSource;
		Settings::ECodec Codec;
		TFunction<bool()> IsQualityControllerFunc;

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
		FVideoSourceSFU(FName SourceType);

	protected:
		TArray<TSharedPtr<FTextureTripleBuffer>> LayerTextures;

	protected:
		/* Begin FVideoSourceBase */
		virtual webrtc::VideoFrame CreateFrame(int32 FrameId) override;
		virtual bool IsReadyForPump() const override;
		/* End FVideoSourceBase */

		void AddLayerTexture(FName SourceType, float Scale);
	};

} // namespace UE::PixelStreaming
