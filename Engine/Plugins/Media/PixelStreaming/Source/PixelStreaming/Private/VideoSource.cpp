// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoSource.h"
#include "FixedFPSPump.h"
#include "PixelStreamingFrameBuffer.h"
#include "Stats.h"
#include "PixelStreamingStatNames.h"
#include "PlayerSessions.h"
#include "IPixelStreamingTextureSource.h"

namespace UE::PixelStreaming
{
	FVideoSourceBase::FVideoSourceBase(FPixelStreamingPlayerId InPlayerId)
		: PlayerId(InPlayerId)
	{
	}

	void FVideoSourceBase::AddRef() const
	{
		RefCount.IncRef();
	}

	rtc::RefCountReleaseStatus FVideoSourceBase::Release() const
	{
		const rtc::RefCountReleaseStatus Status = RefCount.DecRef();
		if (Status == rtc::RefCountReleaseStatus::kDroppedLastRef)
		{
			FFixedFPSPump::Get()->UnregisterVideoSource(PlayerId);
			delete this;
		}
		return Status;
	}

	void FVideoSourceBase::Initialize()
	{
		CurrentState = webrtc::MediaSourceInterface::SourceState::kInitializing;

		FFixedFPSPump::Get()->RegisterVideoSource(PlayerId, this);
	}

	FVideoSourceBase::~FVideoSourceBase()
	{
		CurrentState = webrtc::MediaSourceInterface::SourceState::kEnded;
	}

	void FVideoSourceBase::OnPump(int32 FrameId)
	{

		// `OnPump` prepares a frame to be passed to WebRTC's `OnFrame`.
		// we dont call `OnFrame` outside this method so we can call AdaptCaptureFrame and possibly
		// drop a frame if requested.

		CurrentState = webrtc::MediaSourceInterface::SourceState::kLive;

		webrtc::VideoFrame Frame = CreateFrame(FrameId);

		// might want to allow source frames to be scaled here?
		const FIntPoint Resolution(Frame.width(), Frame.height());
		if (!AdaptCaptureFrame(Frame.timestamp_us(), Resolution))
		{
			return;
		}

		OnFrame(Frame);
	}

	bool FVideoSourceBase::AdaptCaptureFrame(const int64 TimestampUs, FIntPoint Resolution)
	{
		int outWidth, outHeight, cropWidth, cropHeight, cropX, cropY;
		if (!AdaptFrame(Resolution.X, Resolution.Y, TimestampUs, &outWidth, &outHeight, &cropWidth, &cropHeight, &cropX, &cropY))
		{
			return false;
		}

		return true;
	}

	/*
* ------------------ FPlayerVideSource ------------------
*/

	FVideoSourceP2P::FVideoSourceP2P(FPixelStreamingPlayerId InPlayerId, FPlayerSessions* InSessions)
		: FVideoSourceBase(InPlayerId)
		, Sessions(InSessions)
	{
		if (Settings::IsCodecVPX())
		{
			TextureSource = TSharedPtr<IPixelStreamingTextureSource, ESPMode::ThreadSafe>(new FBackBufferToCPUTextureSource(Settings::CVarPixelStreamingFrameScale.GetValueOnAnyThread() <= 0 ? 1.0 : Settings::CVarPixelStreamingFrameScale.GetValueOnAnyThread()));
		}
		else
		{
			TextureSource = TSharedPtr<IPixelStreamingTextureSource, ESPMode::ThreadSafe>(new FBackBufferTextureSource(Settings::CVarPixelStreamingFrameScale.GetValueOnAnyThread() <= 0 ? 1.0 : Settings::CVarPixelStreamingFrameScale.GetValueOnAnyThread()));
		}

		// We store the codec during construction as querying it everytime seem overly wasteful
		// especially considering we don't actually support switching codec mid-stream.
		Codec = Settings::GetSelectedCodec();
	}

	bool FVideoSourceP2P::IsReadyForPump() const
	{
		return TextureSource.IsValid() && TextureSource->IsAvailable();
	}

	webrtc::VideoFrame FVideoSourceP2P::CreateFrameH264(int32 FrameId)
	{
		bool bQualityController = Sessions->IsQualityController(PlayerId);
		TextureSource->SetEnabled(bQualityController);

		const int64 TimestampUs = rtc::TimeMicros();

		// Based on quality control we either send a frame buffer with a texture or an empty frame
		// buffer to indicate that this frame should be skipped by the encoder as this peer is having its
		// frames transmitted by some other peer (the quality controlling peer).
		rtc::scoped_refptr<webrtc::VideoFrameBuffer> FrameBuffer;

		if (bQualityController)
		{
			FrameBuffer = new rtc::RefCountedObject<FLayerFrameBuffer>(TextureSource);
		}
		else
		{
			FrameBuffer = new rtc::RefCountedObject<FInitializeFrameBuffer>(TextureSource);
		}

		webrtc::VideoFrame Frame = webrtc::VideoFrame::Builder()
									   .set_video_frame_buffer(FrameBuffer)
									   .set_timestamp_us(TimestampUs)
									   .set_rotation(webrtc::VideoRotation::kVideoRotation_0)
									   .set_id(FrameId)
									   .build();

		return Frame;
	}

	webrtc::VideoFrame FVideoSourceP2P::CreateFrameVPX(int32 FrameId)
	{
		// We always send the the `FLayerFrameBuffer` as our usage of VPX has no notion of a "quality controller"
		// for hacky encoder sharing - unlike what we do for H264.

		TextureSource->SetEnabled(true);
		const int64 TimestampUs = rtc::TimeMicros();

		rtc::scoped_refptr<webrtc::VideoFrameBuffer> FrameBuffer = new rtc::RefCountedObject<FFrameBufferI420>(TextureSource);

		webrtc::VideoFrame Frame = webrtc::VideoFrame::Builder()
									   .set_video_frame_buffer(FrameBuffer)
									   .set_timestamp_us(TimestampUs)
									   .set_rotation(webrtc::VideoRotation::kVideoRotation_0)
									   .set_id(FrameId)
									   .build();

		return Frame;
	}

	webrtc::VideoFrame FVideoSourceP2P::CreateFrame(int32 FrameId)
	{
		switch (Codec)
		{
			case Settings::ECodec::VP8:
			case Settings::ECodec::VP9:
				return CreateFrameVPX(FrameId);
			case Settings::ECodec::H264:
			default:
				return CreateFrameH264(FrameId);
		}
	}

	/*
* ------------------ FVideoSourceSFU ------------------
*/

	FVideoSourceSFU::FVideoSourceSFU()
		: FVideoSourceBase(SFU_PLAYER_ID)
	{
		// Make a copy of simulcast settings and sort them based on scaling.
		using FLayer = Settings::FSimulcastParameters::FLayer;

		TArray<FLayer*> SortedLayers;
		for (FLayer& Layer : Settings::SimulcastParameters.Layers)
		{
			SortedLayers.Add(&Layer);
		}
		SortedLayers.Sort([](const FLayer& LayerA, const FLayer& LayerB) { return LayerA.Scaling > LayerB.Scaling; });

		for (FLayer* SimulcastLayer : SortedLayers)
		{
			const float Scale = 1.0f / SimulcastLayer->Scaling;
			TSharedPtr<FBackBufferTextureSource> TextureSource = MakeShared<FBackBufferTextureSource>(Scale);
			TextureSource->SetEnabled(true);
			LayerTextures.Add(TextureSource);
		}
	}

	bool FVideoSourceSFU::IsReadyForPump() const
	{
		// Check all texture sources are ready.
		int NumReady = 0;

		for (int i = 0; i < LayerTextures.Num(); i++)
		{
			bool bIsReady = LayerTextures[i].IsValid() && LayerTextures[i]->IsAvailable();
			NumReady += bIsReady ? 1 : 0;
		}

		return NumReady == LayerTextures.Num();
	}

	webrtc::VideoFrame FVideoSourceSFU::CreateFrame(int32 FrameId)
	{
		rtc::scoped_refptr<webrtc::VideoFrameBuffer> FrameBuffer = new rtc::RefCountedObject<FSimulcastFrameBuffer>(LayerTextures);
		const int64 TimestampUs = rtc::TimeMicros();

		webrtc::VideoFrame Frame = webrtc::VideoFrame::Builder()
									   .set_video_frame_buffer(FrameBuffer)
									   .set_timestamp_us(TimestampUs)
									   .set_rotation(webrtc::VideoRotation::kVideoRotation_0)
									   .set_id(FrameId)
									   .build();

		return Frame;
	}
} // namespace UE::PixelStreaming
