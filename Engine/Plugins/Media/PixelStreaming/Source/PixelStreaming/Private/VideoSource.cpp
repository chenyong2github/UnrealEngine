// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoSource.h"
#include "FixedFPSPump.h"
#include "FrameBuffer.h"
#include "Stats.h"
#include "PixelStreamingStatNames.h"

UE::PixelStreaming::FVideoSourceBase::FVideoSourceBase(FPixelStreamingPlayerId InPlayerId)
	: PlayerId(InPlayerId)
{
}

void UE::PixelStreaming::FVideoSourceBase::AddRef() const
{
	RefCount.IncRef();
}

rtc::RefCountReleaseStatus UE::PixelStreaming::FVideoSourceBase::Release() const
{
	const rtc::RefCountReleaseStatus Status = RefCount.DecRef();
	if (Status == rtc::RefCountReleaseStatus::kDroppedLastRef)
	{
		UE::PixelStreaming::FFixedFPSPump::Get()->UnregisterVideoSource(PlayerId);
		delete this;
	}
	return Status;
}

void UE::PixelStreaming::FVideoSourceBase::Initialize()
{
	CurrentState = webrtc::MediaSourceInterface::SourceState::kInitializing;

	UE::PixelStreaming::FFixedFPSPump::Get()->RegisterVideoSource(PlayerId, this);
}

UE::PixelStreaming::FVideoSourceBase::~FVideoSourceBase()
{
	CurrentState = webrtc::MediaSourceInterface::SourceState::kEnded;
}

void UE::PixelStreaming::FVideoSourceBase::OnPump(int32 FrameId)
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

bool UE::PixelStreaming::FVideoSourceBase::AdaptCaptureFrame(const int64 TimestampUs, FIntPoint Resolution)
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

UE::PixelStreaming::FVideoSourceP2P::FVideoSourceP2P(FPixelStreamingPlayerId InPlayerId, UE::PixelStreaming::IPixelStreamingSessions* InSessions)
	: UE::PixelStreaming::FVideoSourceBase(InPlayerId)
	, Sessions(InSessions)
{
	if (UE::PixelStreaming::Settings::IsCodecVPX())
	{
		TextureSource = MakeShared<FTextureSourceBackBufferToCPU>(1.0);
	}
	else
	{
		TextureSource = MakeShared<FTextureSourceBackBuffer>(1.0);
	}

	// We store the codec during construction as querying it everytime seem overly wasteful
	// especially considering we don't actually support switching codec mid-stream.
	Codec = Settings::GetSelectedCodec();
}

bool UE::PixelStreaming::FVideoSourceP2P::IsReadyForPump() const
{
	return TextureSource.IsValid() && TextureSource->IsAvailable();
}

webrtc::VideoFrame UE::PixelStreaming::FVideoSourceP2P::CreateFrameH264(int32 FrameId)
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
		FrameBuffer = new rtc::RefCountedObject<UE::PixelStreaming::FLayerFrameBuffer>(TextureSource);
	}
	else
	{
		FrameBuffer = new rtc::RefCountedObject<UE::PixelStreaming::FInitializeFrameBuffer>(TextureSource);
	}

	webrtc::VideoFrame Frame = webrtc::VideoFrame::Builder()
								   .set_video_frame_buffer(FrameBuffer)
								   .set_timestamp_us(TimestampUs)
								   .set_rotation(webrtc::VideoRotation::kVideoRotation_0)
								   .set_id(FrameId)
								   .build();

	return Frame;
}

webrtc::VideoFrame UE::PixelStreaming::FVideoSourceP2P::CreateFrameVPX(int32 FrameId)
{
	// We always send the the `FLayerFrameBuffer` as our usage of VPX has no notion of a "quality controller"
	// for hacky encoder sharing - unlike what we do for H264.

	TextureSource->SetEnabled(true);
	const int64 TimestampUs = rtc::TimeMicros();

	rtc::scoped_refptr<webrtc::VideoFrameBuffer> FrameBuffer = new rtc::RefCountedObject<UE::PixelStreaming::FFrameBufferI420>(TextureSource);

	webrtc::VideoFrame Frame = webrtc::VideoFrame::Builder()
								   .set_video_frame_buffer(FrameBuffer)
								   .set_timestamp_us(TimestampUs)
								   .set_rotation(webrtc::VideoRotation::kVideoRotation_0)
								   .set_id(FrameId)
								   .build();

	return Frame;
}

webrtc::VideoFrame UE::PixelStreaming::FVideoSourceP2P::CreateFrame(int32 FrameId)
{
	switch (Codec)
	{
		case UE::PixelStreaming::Settings::ECodec::VP8:
		case UE::PixelStreaming::Settings::ECodec::VP9:
			return CreateFrameVPX(FrameId);
		case UE::PixelStreaming::Settings::ECodec::H264:
		default:
			return CreateFrameH264(FrameId);
	}
}

/*
* ------------------ FVideoSourceSFU ------------------
*/

UE::PixelStreaming::FVideoSourceSFU::FVideoSourceSFU()
	: FVideoSourceBase(SFU_PLAYER_ID)
{
	// Make a copy of simulcast settings and sort them based on scaling.
	using FLayer = UE::PixelStreaming::Settings::FSimulcastParameters::FLayer;

	TArray<FLayer*> SortedLayers;
	for (FLayer& Layer : UE::PixelStreaming::Settings::SimulcastParameters.Layers)
	{
		SortedLayers.Add(&Layer);
	}
	SortedLayers.Sort([](const FLayer& LayerA, const FLayer& LayerB) { return LayerA.Scaling > LayerB.Scaling; });

	for (FLayer* SimulcastLayer : SortedLayers)
	{
		const float Scale = 1.0f / SimulcastLayer->Scaling;
		TSharedPtr<FTextureSourceBackBuffer> TextureSource = MakeShared<FTextureSourceBackBuffer>(Scale);
		TextureSource->SetEnabled(true);
		LayerTextures.Add(TextureSource);
	}
}

bool UE::PixelStreaming::FVideoSourceSFU::IsReadyForPump() const
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

webrtc::VideoFrame UE::PixelStreaming::FVideoSourceSFU::CreateFrame(int32 FrameId)
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