// Copyright Epic Games, Inc. All Rights Reserved.
#include "VideoEncoderAdapterSimulcast.h"
#include "FrameBufferH264.h"
#include "FrameBufferI420.h"
#include "modules/video_coding/codecs/vp8/include/vp8.h"
#include "modules/video_coding/codecs/vp9/include/vp9.h"
#include "VideoEncoderFactorySimple.h"

namespace
{
	uint32_t SumStreamMaxBitrate(int Streams, const webrtc::VideoCodec& Codec)
	{
		uint32_t BitrateSum = 0;
		for (int i = 0; i < Streams; ++i)
		{
			BitrateSum += Codec.simulcastStream[i].maxBitrate;
		}
		return BitrateSum;
	}

	int GetNumberOfStreams(const webrtc::VideoCodec& Codec)
	{
		int Streams = Codec.numberOfSimulcastStreams < 1 ? 1 : Codec.numberOfSimulcastStreams;
		uint32_t SimulcastMaxBitrate = SumStreamMaxBitrate(Streams, Codec);
		if (SimulcastMaxBitrate == 0)
		{
			Streams = 1;
		}
		return Streams;
	}

	int GetNumActiveStreams(const webrtc::VideoCodec& Codec)
	{
		int NumConfiguredStreams = GetNumberOfStreams(Codec);
		int NumActiveStreams = 0;
		for (int i = 0; i < NumConfiguredStreams; ++i)
		{
			if (Codec.simulcastStream[i].active)
			{
				++NumActiveStreams;
			}
		}
		return NumActiveStreams;
	}

	bool StreamResolutionCompare(const webrtc::SpatialLayer& A, const webrtc::SpatialLayer& B)
	{
		return std::tie(A.height, A.width, A.maxBitrate, A.maxFramerate) < std::tie(B.height, B.width, B.maxBitrate, B.maxFramerate);
	}

	void PopulateStreamCodec(const webrtc::VideoCodec& CodecSettings, int StreamIndex, uint32_t StartBitrateKbps, webrtc::VideoCodec* StreamCodec)
	{
		*StreamCodec = CodecSettings;

		// Stream specific settings.
		StreamCodec->numberOfSimulcastStreams = 0;
		StreamCodec->width = CodecSettings.simulcastStream[StreamIndex].width;
		StreamCodec->height = CodecSettings.simulcastStream[StreamIndex].height;
		StreamCodec->maxBitrate = CodecSettings.simulcastStream[StreamIndex].maxBitrate;
		StreamCodec->minBitrate = CodecSettings.simulcastStream[StreamIndex].minBitrate;
		StreamCodec->maxFramerate = CodecSettings.simulcastStream[StreamIndex].maxFramerate;
		StreamCodec->qpMax = CodecSettings.simulcastStream[StreamIndex].qpMax;
		StreamCodec->active = CodecSettings.simulcastStream[StreamIndex].active;
		if (CodecSettings.codecType == webrtc::kVideoCodecH264)
		{
			StreamCodec->H264()->numberOfTemporalLayers = CodecSettings.simulcastStream[StreamIndex].numberOfTemporalLayers;
		}
		StreamCodec->startBitrate = StartBitrateKbps;
	}

	// An EncodedImageCallback implementation that forwards on calls to a
	// UE::PixelStreaming::FVideoEncoderAdapterSimulcast, but with the stream index it's registered with as
	// the first parameter to Encoded.
	class AdapterEncodedImageCallback : public webrtc::EncodedImageCallback
	{
	public:
		AdapterEncodedImageCallback(UE::PixelStreaming::FVideoEncoderAdapterSimulcast* adapter,
			size_t stream_idx)
			: adapter_(adapter), stream_idx_(stream_idx)
		{
		}

		EncodedImageCallback::Result OnEncodedImage(
			const webrtc::EncodedImage& encoded_image,
			const webrtc::CodecSpecificInfo* codec_specific_info
#if WEBRTC_VERSION == 84
			, const webrtc::RTPFragmentationHeader* fragmentation
#endif
		) override
		{
			return adapter_->OnEncodedImage(stream_idx_, encoded_image, codec_specific_info
#if WEBRTC_VERSION == 84
				, fragmentation
#endif
			);
		}

	private:
		UE::PixelStreaming::FVideoEncoderAdapterSimulcast* const adapter_;
		const size_t stream_idx_;
	};
} // namespace

namespace UE::PixelStreaming
{
	FVideoEncoderAdapterSimulcast::FVideoEncoderAdapterSimulcast(FVideoEncoderFactorySimulcast& InSimulcastFactory, const webrtc::SdpVideoFormat& format)
		: Initialized(false)
		, SimulcastEncoderFactory(InSimulcastFactory)
		, VideoFormat(format)
		, EncodedCompleteCallback(nullptr)
	{
		memset(&CurrentCodec, 0, sizeof(webrtc::VideoCodec));
	}

	FVideoEncoderAdapterSimulcast::~FVideoEncoderAdapterSimulcast()
	{
		check(!IsInitialized());
	}

	int FVideoEncoderAdapterSimulcast::Release()
	{
		{
			// Lock during deleting an encoder
			FScopeLock Lock(&StreamInfosGuard);
			while (!StreamInfos.empty())
			{
				std::unique_ptr<VideoEncoder> Encoder = std::move(StreamInfos.back().Encoder);
				// Even though it seems very unlikely, there are no guarantees that the
				// encoder will not call back after being Release()'d. Therefore, we first
				// disable the callbacks here.
				Encoder->RegisterEncodeCompleteCallback(nullptr);
				Encoder->Release();
				StreamInfos.pop_back(); // Deletes callback adapter.
			}
		}

		Initialized = false;

		return WEBRTC_VIDEO_CODEC_OK;
	}

	int FVideoEncoderAdapterSimulcast::InitEncode(const webrtc::VideoCodec* codec_settings, const webrtc::VideoEncoder::Settings& settings)
	{
		int NumberOfStreams = GetNumberOfStreams(*codec_settings);
		int NumActiveStreams = GetNumActiveStreams(*codec_settings);

		CurrentCodec = *codec_settings;

		const auto MinMax = std::minmax_element(std::begin(CurrentCodec.simulcastStream), std::begin(CurrentCodec.simulcastStream) + NumberOfStreams, StreamResolutionCompare);
		LowestResolutionStreamIndex = std::distance(std::begin(CurrentCodec.simulcastStream), MinMax.first);
		HighestResolutionStreamIndex = std::distance(std::begin(CurrentCodec.simulcastStream), MinMax.second);

		const webrtc::SdpVideoFormat Format(CurrentCodec.codecType == webrtc::kVideoCodecVP8 ? "VP8" : "H264", VideoFormat.parameters);

		if (NumActiveStreams == 1)
		{
			// with one stream we just proxy the pixelstreaming encoder
			FVideoEncoderFactorySimple* EncoderFactory = SimulcastEncoderFactory.GetOrCreateEncoderFactory(0);
			std::unique_ptr<VideoEncoder> Encoder = EncoderFactory->CreateVideoEncoder(Format);

			int ReturnCode = Encoder->InitEncode(&CurrentCodec, settings);
			if (ReturnCode < 0)
			{
				// Explicitly destroy the current encoder; because we haven't registered
				// a StreamInfo for it yet, Release won't do anything about it.
				Encoder.reset();
				Release();
				return ReturnCode;
			}

			std::unique_ptr<webrtc::EncodedImageCallback> Callback(new AdapterEncodedImageCallback(this, 0));
			Encoder->RegisterEncodeCompleteCallback(Callback.get());
			StreamInfos.emplace_back(
				std::move(Encoder), std::move(Callback),
				std::make_unique<webrtc::FramerateController>(CurrentCodec.maxFramerate),
				CurrentCodec.width, CurrentCodec.height, true, true);
		}
		else
		{
			for (int i = 0; i < NumberOfStreams; ++i)
			{
				webrtc::VideoCodec StreamCodec;
				uint32_t StartBitrateKbps = CurrentCodec.simulcastStream[i].targetBitrate;
				PopulateStreamCodec(CurrentCodec, i, StartBitrateKbps, &StreamCodec);

				FVideoEncoderFactorySimple* EncoderFactory = SimulcastEncoderFactory.GetOrCreateEncoderFactory(i);
				std::unique_ptr<VideoEncoder> Encoder = EncoderFactory->CreateVideoEncoder(Format);

				int ReturnCode = Encoder->InitEncode(&StreamCodec, settings);
				if (ReturnCode < 0)
				{
					// Explicitly destroy the current encoder; because we haven't registered
					// a StreamInfo for it yet, Release won't do anything about it.
					Encoder.reset();
					Release();
					return ReturnCode;
				}

				std::unique_ptr<webrtc::EncodedImageCallback> Callback(new AdapterEncodedImageCallback(this, i));
				Encoder->RegisterEncodeCompleteCallback(Callback.get());
				StreamInfos.emplace_back(
					std::move(Encoder), std::move(Callback),
					std::make_unique<webrtc::FramerateController>(StreamCodec.maxFramerate),
					StreamCodec.width, StreamCodec.height, true, true);
			}
		}

		Initialized = true;

		return WEBRTC_VIDEO_CODEC_OK;
	}

	int FVideoEncoderAdapterSimulcast::EncodeVP8(const webrtc::VideoFrame& InputImage, const std::vector<webrtc::VideoFrameType>* FrameTypes, bool bSendKeyFrame)
	{
		// TODO Currently this does not get hit because the encoder factory will just return a vpx encoder but if we change it to use this encoder
		// webrtc will try to call ToI420() on the simulcast buffer which is currently unimplemented because the buffers are pre created.
		// we either need to short circuit that behaviour or change what webrtc is trying to do. info.supports_native_handle from the vpx encoders
		// is the culprit.

		const FFrameBufferI420Simulcast* FrameBuffer = static_cast<FFrameBufferI420Simulcast*>(InputImage.video_frame_buffer().get());
		check(FrameBuffer->GetFrameBufferType() == EPixelStreamingFrameBufferType::Simulcast);

		rtc::scoped_refptr<webrtc::I420BufferInterface> SourceBuffer;
		for (size_t StreamIdx = 0; StreamIdx < StreamInfos.size(); ++StreamIdx)
		{
			// Don't encode frames in resolutions that we don't intend to send.
			if (!StreamInfos[StreamIdx].bSendStream)
			{
				continue;
			}

			// extract the specific layer frame buffer
			webrtc::VideoFrame NewFrame(InputImage);
			const int LayerIndex = StreamInfos.size() == 1 ? FrameBuffer->GetNumLayers() - 1 : StreamIdx;
			rtc::scoped_refptr<FFrameBufferI420> LayerFrameBuffer = new rtc::RefCountedObject<FFrameBufferI420>(FrameBuffer->GetFrameAdapter(), LayerIndex);
			NewFrame.set_video_frame_buffer(LayerFrameBuffer);

			const uint32_t FrameTimestampMs = 1000 * InputImage.timestamp() / 90000; // kVideoPayloadTypeFrequency;
			// If adapter is passed through and only one sw encoder does simulcast,
			// frame types for all streams should be passed to the encoder unchanged.
			// Otherwise a single per-encoder frame type is passed.
			std::vector<webrtc::VideoFrameType> StreamFrameTypes(StreamInfos.size() == 1 ? GetNumberOfStreams(CurrentCodec) : 1);
			if (bSendKeyFrame)
			{
				std::fill(StreamFrameTypes.begin(), StreamFrameTypes.end(), webrtc::VideoFrameType::kVideoFrameKey);
				StreamInfos[StreamIdx].KeyFrameRequest = false;
			}
			else
			{
#if WEBRTC_VERSION == 84
				if (StreamInfos[StreamIdx].FramerateController->DropFrame(FrameTimestampMs))
#elif WEBRTC_VERSION == 96
				if (StreamInfos[StreamIdx].FramerateController->ShouldDropFrame(FrameTimestampMs))
#endif
				{
					continue;
				}
				std::fill(StreamFrameTypes.begin(), StreamFrameTypes.end(), webrtc::VideoFrameType::kVideoFrameDelta);
			}
#if WEBRTC_VERSION == 84
			StreamInfos[StreamIdx].FramerateController->AddFrame(FrameTimestampMs);
#elif WEBRTC_VERSION == 96
			StreamInfos[StreamIdx].FramerateController->KeepFrame(FrameTimestampMs);
#endif

			int RtcError = StreamInfos[StreamIdx].Encoder->Encode(NewFrame, &StreamFrameTypes);
			if (RtcError != WEBRTC_VIDEO_CODEC_OK)
			{
				return RtcError;
			}
		}
		return WEBRTC_VIDEO_CODEC_OK;
	}

	int FVideoEncoderAdapterSimulcast::EncodeVP9(const webrtc::VideoFrame& InputImage, const std::vector<webrtc::VideoFrameType>* FrameTypes, bool bSendKeyFrame)
	{
		return WEBRTC_VIDEO_CODEC_OK;
	}

	int FVideoEncoderAdapterSimulcast::EncodeH264(const webrtc::VideoFrame& InputImage, const std::vector<webrtc::VideoFrameType>* FrameTypes, bool bSendKeyFrame)
	{
		// ignore any init frames
		// NOTE: It seems most of the time they never even get here because the internals of WebRTC decide to drop it. It seems it thinks
		// the encoder is paused or something.
		const IPixelStreamingFrameBuffer* FrameBuffer = static_cast<IPixelStreamingFrameBuffer*>(InputImage.video_frame_buffer().get());
		if (FrameBuffer->GetFrameBufferType() == EPixelStreamingFrameBufferType::Initialize)
		{
			return WEBRTC_VIDEO_CODEC_OK;
		}
		// separate out our pixelstreaming image sources
		check(FrameBuffer->GetFrameBufferType() == EPixelStreamingFrameBufferType::Simulcast);
		const FFrameBufferH264Simulcast* SimulcastFrameBuffer = static_cast<const FFrameBufferH264Simulcast*>(FrameBuffer);
		for (size_t StreamIdx = 0; StreamIdx < StreamInfos.size(); ++StreamIdx)
		{
			// Don't encode frames in resolutions that we don't intend to send.
			if (!StreamInfos[StreamIdx].bSendStream)
			{
				continue;
			}
			webrtc::VideoFrame NewFrame(InputImage);
			// grab the simulcast frame source, extract the frame source for this layer and wrap that in a new frame buffer appropriate for H264
			const int LayerIndex = StreamInfos.size() == 1 ? SimulcastFrameBuffer->GetNumLayers() - 1 : StreamIdx;
			rtc::scoped_refptr<FFrameBufferH264> LayerFrameBuffer = new rtc::RefCountedObject<FFrameBufferH264>(SimulcastFrameBuffer->GetFrameSource(), LayerIndex);
			NewFrame.set_video_frame_buffer(LayerFrameBuffer);

			const uint32_t FrameTimestampMs = 1000 * NewFrame.timestamp() / 90000;
			// If adapter is passed through and only one sw encoder does simulcast,
			// frame types for all streams should be passed to the encoder unchanged.
			// Otherwise a single per-encoder frame type is passed.
			std::vector<webrtc::VideoFrameType> StreamFrameTypes(StreamInfos.size() == 1 ? GetNumberOfStreams(CurrentCodec) : 1);
			if (bSendKeyFrame)
			{
				std::fill(StreamFrameTypes.begin(), StreamFrameTypes.end(), webrtc::VideoFrameType::kVideoFrameKey);
				StreamInfos[StreamIdx].KeyFrameRequest = false;
			}
			else
			{
#if WEBRTC_VERSION == 84
				if (StreamInfos[StreamIdx].FramerateController->DropFrame(FrameTimestampMs))
#elif WEBRTC_VERSION == 96
				if (StreamInfos[StreamIdx].FramerateController->ShouldDropFrame(FrameTimestampMs))
#endif
				{
					return WEBRTC_VIDEO_CODEC_OK;
				}
				std::fill(StreamFrameTypes.begin(), StreamFrameTypes.end(), webrtc::VideoFrameType::kVideoFrameDelta);
			}
#if WEBRTC_VERSION == 84
			StreamInfos[StreamIdx].FramerateController->AddFrame(FrameTimestampMs);
#elif WEBRTC_VERSION == 96
			StreamInfos[StreamIdx].FramerateController->KeepFrame(FrameTimestampMs);
#endif
			int RtcError = StreamInfos[StreamIdx].Encoder->Encode(NewFrame, &StreamFrameTypes);
			if (RtcError != WEBRTC_VIDEO_CODEC_OK)
			{
				return RtcError;
			}
		}
		return WEBRTC_VIDEO_CODEC_OK;
	}

	int FVideoEncoderAdapterSimulcast::Encode(const webrtc::VideoFrame& input_image, const std::vector<webrtc::VideoFrameType>* frame_types)
	{
		if (!IsInitialized())
		{
			return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
		}
		if (EncodedCompleteCallback == nullptr)
		{
			return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
		}

		// All active streams should generate a key frame if
		// a key frame is requested by any stream.
		bool bSendKeyFrame = false;
		if (frame_types)
		{
			for (size_t i = 0; i < frame_types->size(); ++i)
			{
				if (frame_types->at(i) == webrtc::VideoFrameType::kVideoFrameKey)
				{
					bSendKeyFrame = true;
					break;
				}
			}
		}

		for (size_t StreamIdx = 0; StreamIdx < StreamInfos.size(); ++StreamIdx)
		{
			if (StreamInfos[StreamIdx].KeyFrameRequest && StreamInfos[StreamIdx].bSendStream)
			{
				bSendKeyFrame = true;
				break;
			}
		}

		switch (CurrentCodec.codecType)
		{
			case webrtc::kVideoCodecVP8:
				return EncodeVP8(input_image, frame_types, bSendKeyFrame);
			case webrtc::kVideoCodecVP9:
				return EncodeVP9(input_image, frame_types, bSendKeyFrame);
			case webrtc::kVideoCodecH264:
				return EncodeH264(input_image, frame_types, bSendKeyFrame);
			default:
				return WEBRTC_VIDEO_CODEC_ERROR;
		}
	}

	int FVideoEncoderAdapterSimulcast::RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* callback)
	{
		EncodedCompleteCallback = callback;
		return WEBRTC_VIDEO_CODEC_OK;
	}

	void FVideoEncoderAdapterSimulcast::SetRates(const RateControlParameters& parameters)
	{
		if (!IsInitialized())
		{
			RTC_LOG(LS_WARNING) << "SetRates while not initialized";
			return;
		}

		if (parameters.framerate_fps < 1.0)
		{
			RTC_LOG(LS_WARNING) << "Invalid framerate: " << parameters.framerate_fps;
			return;
		}

		CurrentCodec.maxFramerate = static_cast<uint32_t>(parameters.framerate_fps + 0.5);

		if (StreamInfos.size() == 1)
		{
			// Not doing simulcast.
			StreamInfos[0].Encoder->SetRates(parameters);
			return;
		}

		for (size_t StreamIdx = 0; StreamIdx < StreamInfos.size(); ++StreamIdx)
		{
			uint32_t StreamBitrateKbps = parameters.bitrate.GetSpatialLayerSum(StreamIdx) / 1000;

			// Need a key frame if we have not sent this stream before.
			if (StreamBitrateKbps > 0 && !StreamInfos[StreamIdx].bSendStream)
			{
				StreamInfos[StreamIdx].KeyFrameRequest = true;
			}
			StreamInfos[StreamIdx].bSendStream = StreamBitrateKbps > 0;

			// Slice the temporal layers out of the full allocation and pass it on to
			// the encoder handling the current simulcast stream.
			RateControlParameters StreamParameters = parameters;
			StreamParameters.bitrate = webrtc::VideoBitrateAllocation();
			for (int i = 0; i < webrtc::kMaxTemporalStreams; ++i)
			{
				if (parameters.bitrate.HasBitrate(StreamIdx, i))
				{
					StreamParameters.bitrate.SetBitrate(0, i, parameters.bitrate.GetBitrate(StreamIdx, i));
				}
			}

			// Assign link allocation proportionally to spatial layer allocation.
			if (!parameters.bandwidth_allocation.IsZero() && parameters.bitrate.get_sum_bps() > 0)
			{
				StreamParameters.bandwidth_allocation =
					webrtc::DataRate::BitsPerSec((parameters.bandwidth_allocation.bps() * StreamParameters.bitrate.get_sum_bps()) / parameters.bitrate.get_sum_bps());
				// Make sure we don't allocate bandwidth lower than target bitrate.
				if (StreamParameters.bandwidth_allocation.bps() < StreamParameters.bitrate.get_sum_bps())
				{
					StreamParameters.bandwidth_allocation =
						webrtc::DataRate::BitsPerSec(StreamParameters.bitrate.get_sum_bps());
				}
			}
#if WEBRTC_VERSION == 84
			StreamParameters.framerate_fps = std::min<double>(parameters.framerate_fps, StreamInfos[StreamIdx].FramerateController->GetTargetRate());
#elif WEBRTC_VERSION == 96
			StreamParameters.framerate_fps = std::min<double>(parameters.framerate_fps, StreamInfos[StreamIdx].FramerateController->GetMaxFramerate());
#endif

			StreamInfos[StreamIdx].Encoder->SetRates(StreamParameters);
		}
	}

	void FVideoEncoderAdapterSimulcast::OnPacketLossRateUpdate(float packet_loss_rate)
	{
		for (StreamInfo& Info : StreamInfos)
		{
			Info.Encoder->OnPacketLossRateUpdate(packet_loss_rate);
		}
	}

	void FVideoEncoderAdapterSimulcast::OnRttUpdate(int64_t rtt_ms)
	{
		for (StreamInfo& Info : StreamInfos)
		{
			Info.Encoder->OnRttUpdate(rtt_ms);
		}
	}

	void FVideoEncoderAdapterSimulcast::OnLossNotification(const LossNotification& loss_notification)
	{
		for (StreamInfo& Info : StreamInfos)
		{
			Info.Encoder->OnLossNotification(loss_notification);
		}
	}

	webrtc::EncodedImageCallback::Result FVideoEncoderAdapterSimulcast::OnEncodedImage(
		size_t stream_idx,
		const webrtc::EncodedImage& encodedImage,
		const webrtc::CodecSpecificInfo* codecSpecificInfo
#if WEBRTC_VERSION == 84
		, const webrtc::RTPFragmentationHeader* fragmentation
#endif
	)
	{
		webrtc::EncodedImage StreamImage(encodedImage);
		webrtc::CodecSpecificInfo StreamCodecSpecific = *codecSpecificInfo;

		StreamImage.SetSpatialIndex(stream_idx);

		return EncodedCompleteCallback->OnEncodedImage(StreamImage, &StreamCodecSpecific
#if WEBRTC_VERSION == 84
			, fragmentation
#endif
		);
	}

	bool FVideoEncoderAdapterSimulcast::IsInitialized() const
	{
		return Initialized;
	}

	webrtc::VideoEncoder::EncoderInfo FVideoEncoderAdapterSimulcast::GetEncoderInfo() const
	{
		if (StreamInfos.size() == 1)
		{
			// Not using simulcast adapting functionality, just pass through.
			return StreamInfos[0].Encoder->GetEncoderInfo();
		}

		VideoEncoder::EncoderInfo EncoderInfo;
		EncoderInfo.implementation_name = "PixelStreamingSimulcastEncoderAdapter";
		EncoderInfo.requested_resolution_alignment = 1;
		EncoderInfo.supports_native_handle = true;
		EncoderInfo.scaling_settings.thresholds = absl::nullopt;
		if (StreamInfos.empty())
		{
			return EncoderInfo;
		}

		EncoderInfo.scaling_settings = VideoEncoder::ScalingSettings::kOff;
		int NumActiveStreams = GetNumActiveStreams(CurrentCodec);

		for (size_t i = 0; i < StreamInfos.size(); ++i)
		{
			VideoEncoder::EncoderInfo EncoderImplInfo = StreamInfos[i].Encoder->GetEncoderInfo();

			if (i == 0)
			{
				// Encoder name indicates names of all sub-encoders.
				EncoderInfo.implementation_name += " (";
				EncoderInfo.implementation_name += EncoderImplInfo.implementation_name;

				EncoderInfo.supports_native_handle = EncoderImplInfo.supports_native_handle;
				EncoderInfo.has_trusted_rate_controller = EncoderImplInfo.has_trusted_rate_controller;
				EncoderInfo.is_hardware_accelerated = EncoderImplInfo.is_hardware_accelerated;
				EncoderInfo.has_internal_source = EncoderImplInfo.has_internal_source;
			}
			else
			{
				EncoderInfo.implementation_name += ", ";
				EncoderInfo.implementation_name += EncoderImplInfo.implementation_name;

				// Native handle supported if any encoder supports it.
				EncoderInfo.supports_native_handle |= EncoderImplInfo.supports_native_handle;

				// Trusted rate controller only if all encoders have it.
				EncoderInfo.has_trusted_rate_controller &= EncoderImplInfo.has_trusted_rate_controller;

				// Uses hardware support if any of the encoders uses it.
				// For example, if we are having issues with down-scaling due to
				// pipelining delay in HW encoders we need higher encoder usage
				// thresholds in CPU adaptation.
				EncoderInfo.is_hardware_accelerated |= EncoderImplInfo.is_hardware_accelerated;

				// Has internal source only if all encoders have it.
				EncoderInfo.has_internal_source &= EncoderImplInfo.has_internal_source;
			}

			// Nasty hack to allow us to manually convert VPX frames to I420 later in the encode block
			if (CurrentCodec.codecType == webrtc::kVideoCodecVP8)
			{
				EncoderInfo.supports_native_handle = true;
			}

			EncoderInfo.fps_allocation[i] = EncoderImplInfo.fps_allocation[0];
			EncoderInfo.requested_resolution_alignment = cricket::LeastCommonMultiple(EncoderInfo.requested_resolution_alignment, EncoderImplInfo.requested_resolution_alignment);
			if (NumActiveStreams == 1 && CurrentCodec.simulcastStream[i].active)
			{
				EncoderInfo.scaling_settings = EncoderImplInfo.scaling_settings;
			}
		}
		EncoderInfo.implementation_name += ")";

		return EncoderInfo;
	}
} // namespace UE::PixelStreaming
