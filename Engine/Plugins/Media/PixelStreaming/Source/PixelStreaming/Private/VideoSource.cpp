// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoSource.h"
#include "FixedFPSPump.h"
#include "PixelStreamingFrameBuffer.h"
#include "Stats.h"
#include "PixelStreamingStatNames.h"
#include "PlayerSessions.h"
#include "PixelStreamingModule.h"
#include "FrameBuffer.h"
#include "TextureSourceComputeI420.h"
#include "TextureSourceCPUI420.h"

namespace UE::PixelStreaming
{
	FVideoSourceBase::FVideoSourceBase()
	{
	}

	void FVideoSourceBase::Initialize()
	{
		CurrentState = webrtc::MediaSourceInterface::SourceState::kInitializing;

		IPixelStreamingModule::Get().RegisterPumpable(this);
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

	FVideoSourceP2P::FVideoSourceP2P(FName SourceType, TFunction<bool()> InIsQualityControllerFunc)
		: IsQualityControllerFunc(InIsQualityControllerFunc)
	{
		TUniquePtr<FPixelStreamingTextureSource> TextureGenerator = IPixelStreamingModule::Get().GetTextureSourceFactory().CreateTextureSource(SourceType);

		float FrameScale = Settings::CVarPixelStreamingFrameScale.GetValueOnAnyThread() <= 0 ? 1.0 : Settings::CVarPixelStreamingFrameScale.GetValueOnAnyThread();
		TextureSource = TSharedPtr<FTextureTripleBuffer, ESPMode::ThreadSafe>(new FTextureTripleBuffer(FrameScale, MoveTemp(TextureGenerator)));

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
		bool bQualityController = IsQualityControllerFunc();

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

	FVideoSourceSFU::FVideoSourceSFU(FName SourceType)
		: FVideoSourceBase()
	{
		// Make a copy of simulcast settings and sort them based on scaling.
		using FLayer = Settings::FSimulcastParameters::FLayer;

		TArray<FLayer*> SortedLayers;
		for (FLayer& Layer : Settings::SimulcastParameters.Layers)
		{
			SortedLayers.Add(&Layer);
		}
		SortedLayers.Sort([](const FLayer& LayerA, const FLayer& LayerB) { return LayerA.Scaling > LayerB.Scaling; });

		if(Settings::IsCodecVPX())
		{
			// VPX Encoding. We only keep a single texture source (the largest one) and do CPU scaling in the Encode loop
			const float Scale = SortedLayers[SortedLayers.Num() - 1]->Scaling;
			AddLayerTexture(SourceType, Scale);
		}
		else
		{
			// H264 Encoding. We store a texture source for each simulcast layer
			for (FLayer* SimulcastLayer : SortedLayers)
			{
				const float Scale = 1.0f / SimulcastLayer->Scaling;
				AddLayerTexture(SourceType, Scale);
			}
		}
	}

	void FVideoSourceSFU::AddLayerTexture(FName SourceType, float Scale) 
	{
		TUniquePtr<FPixelStreamingTextureSource> TextureGenerator = IPixelStreamingModule::Get().GetTextureSourceFactory().CreateTextureSource(SourceType);
		TSharedPtr<FTextureTripleBuffer> TextureSource = MakeShared<FTextureTripleBuffer, ESPMode::ThreadSafe>(Scale, MoveTemp(TextureGenerator));
		TextureSource->SetEnabled(true);
		LayerTextures.Add(TextureSource);
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
		rtc::scoped_refptr<webrtc::VideoFrameBuffer> FrameBuffer;
		if(Settings::IsCodecVPX())
		{
			// VPX Encoding. We only ever have a single layer
			FrameBuffer = new rtc::RefCountedObject<FFrameBufferI420>(LayerTextures[0]);
		}
		else
		{
			FrameBuffer = new rtc::RefCountedObject<FSimulcastFrameBuffer>(LayerTextures);
		}
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
