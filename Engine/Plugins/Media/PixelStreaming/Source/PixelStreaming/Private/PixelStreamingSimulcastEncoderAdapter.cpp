// Copyright Epic Games, Inc. All Rights Reserved.
#include "PixelStreamingSimulcastEncoderAdapter.h"
#include "PixelStreamingFrameBuffer.h"

namespace
{
	uint32_t SumStreamMaxBitrate(int streams, const webrtc::VideoCodec& codec)
	{
		uint32_t bitrate_sum = 0;
		for (int i = 0; i < streams; ++i)
		{
			bitrate_sum += codec.simulcastStream[i].maxBitrate;
		}
		return bitrate_sum;
	}

	int NumberOfStreams(const webrtc::VideoCodec& codec)
	{
		int streams = codec.numberOfSimulcastStreams < 1 ? 1 : codec.numberOfSimulcastStreams;
		uint32_t simulcast_max_bitrate = SumStreamMaxBitrate(streams, codec);
		if (simulcast_max_bitrate == 0)
		{
			streams = 1;
		}
		return streams;
	}

	int NumActiveStreams(const webrtc::VideoCodec& codec)
	{
		int num_configured_streams = NumberOfStreams(codec);
		int num_active_streams = 0;
		for (int i = 0; i < num_configured_streams; ++i)
		{
			if (codec.simulcastStream[i].active)
			{
				++num_active_streams;
			}
		}
		return num_active_streams;
	}

	bool StreamResolutionCompare(const webrtc::SimulcastStream& a, const webrtc::SimulcastStream& b)
	{
		return std::tie(a.height, a.width, a.maxBitrate, a.maxFramerate) < std::tie(b.height, b.width, b.maxBitrate, b.maxFramerate);
	}

	void PopulateStreamCodec(const webrtc::VideoCodec& codec_settings, int stream_index, uint32_t start_bitrate_kbps, webrtc::VideoCodec* stream_codec)
	{
		*stream_codec = codec_settings;

		// Stream specific settings.
		stream_codec->numberOfSimulcastStreams = 0;
		stream_codec->width = codec_settings.simulcastStream[stream_index].width;
		stream_codec->height = codec_settings.simulcastStream[stream_index].height;
		stream_codec->maxBitrate = codec_settings.simulcastStream[stream_index].maxBitrate;
		stream_codec->minBitrate = codec_settings.simulcastStream[stream_index].minBitrate;
		stream_codec->maxFramerate = codec_settings.simulcastStream[stream_index].maxFramerate;
		stream_codec->qpMax = codec_settings.simulcastStream[stream_index].qpMax;
		stream_codec->active = codec_settings.simulcastStream[stream_index].active;
		if (codec_settings.codecType == webrtc::kVideoCodecH264)
		{
			stream_codec->H264()->numberOfTemporalLayers = codec_settings.simulcastStream[stream_index].numberOfTemporalLayers;
		}
		stream_codec->startBitrate = start_bitrate_kbps;
	}

	// An EncodedImageCallback implementation that forwards on calls to a
	// PixelStreamingSimulcastEncoderAdapter, but with the stream index it's registered with as
	// the first parameter to Encoded.
	class AdapterEncodedImageCallback : public webrtc::EncodedImageCallback
	{
	public:
		AdapterEncodedImageCallback(PixelStreamingSimulcastEncoderAdapter* adapter,
			size_t stream_idx)
			: adapter_(adapter), stream_idx_(stream_idx)
		{
		}

		EncodedImageCallback::Result OnEncodedImage(
			const webrtc::EncodedImage& encoded_image,
			const webrtc::CodecSpecificInfo* codec_specific_info,
			const webrtc::RTPFragmentationHeader* fragmentation) override
		{
			return adapter_->OnEncodedImage(stream_idx_, encoded_image,
				codec_specific_info, fragmentation);
		}

	private:
		PixelStreamingSimulcastEncoderAdapter* const adapter_;
		const size_t stream_idx_;
	};
} // namespace

PixelStreamingSimulcastEncoderAdapter::PixelStreamingSimulcastEncoderAdapter(webrtc::VideoEncoderFactory* primary_factory, const webrtc::SdpVideoFormat& format)
	: Initialized(false)
	, PrimaryEncoderFactory(primary_factory)
	, VideoFormat(format)
	, EncodedCompleteCallback(nullptr)
{
	check(PrimaryEncoderFactory);
	memset(&CurrentCodec, 0, sizeof(webrtc::VideoCodec));
}

PixelStreamingSimulcastEncoderAdapter::~PixelStreamingSimulcastEncoderAdapter()
{
	check(!IsInitialized());
}

int PixelStreamingSimulcastEncoderAdapter::Release()
{
	while (!StreamInfos.empty())
	{
		std::unique_ptr<VideoEncoder> encoder = std::move(StreamInfos.back().encoder);
		// Even though it seems very unlikely, there are no guarantees that the
		// encoder will not call back after being Release()'d. Therefore, we first
		// disable the callbacks here.
		encoder->RegisterEncodeCompleteCallback(nullptr);
		encoder->Release();
		StreamInfos.pop_back(); // Deletes callback adapter.
	}

	Initialized = false;

	return WEBRTC_VIDEO_CODEC_OK;
}

int PixelStreamingSimulcastEncoderAdapter::InitEncode(const webrtc::VideoCodec* codec_settings, const webrtc::VideoEncoder::Settings& settings)
{
	int number_of_streams = NumberOfStreams(*codec_settings);
	int num_active_streams_ = NumActiveStreams(*codec_settings);

	CurrentCodec = *codec_settings;

	const auto minmax = std::minmax_element(std::begin(CurrentCodec.simulcastStream), std::begin(CurrentCodec.simulcastStream) + number_of_streams, StreamResolutionCompare);
	LowestResolutionStreamIndex = std::distance(std::begin(CurrentCodec.simulcastStream), minmax.first);
	HighestResolutionStreamIndex = std::distance(std::begin(CurrentCodec.simulcastStream), minmax.second);

	const webrtc::SdpVideoFormat format(CurrentCodec.codecType == webrtc::kVideoCodecVP8 ? "VP8" : "H264", VideoFormat.parameters);

	if (num_active_streams_ == 1)
	{
		// with one stream we just proxy the pixelstreaming encoder
		std::unique_ptr<VideoEncoder> encoder = PrimaryEncoderFactory->CreateVideoEncoder(format);
		int ret = encoder->InitEncode(&CurrentCodec, settings);
		if (ret < 0)
		{
			// Explicitly destroy the current encoder; because we haven't registered
			// a StreamInfo for it yet, Release won't do anything about it.
			encoder.reset();
			Release();
			return ret;
		}

		std::unique_ptr<webrtc::EncodedImageCallback> callback(new AdapterEncodedImageCallback(this, 0));
		encoder->RegisterEncodeCompleteCallback(callback.get());
		StreamInfos.emplace_back(
			std::move(encoder), std::move(callback),
			std::make_unique<webrtc::FramerateController>(CurrentCodec.maxFramerate),
			CurrentCodec.width, CurrentCodec.height, true);
	}
	else
	{
		webrtc::SimulcastRateAllocator rate_allocator(CurrentCodec);
		webrtc::VideoBitrateAllocation allocation = rate_allocator.Allocate(webrtc::VideoBitrateAllocationParameters(CurrentCodec.startBitrate * 1000, CurrentCodec.maxFramerate));
		std::vector<uint32_t> start_bitrates;
		for (int i = 0; i < webrtc::kMaxSimulcastStreams; ++i)
		{
			uint32_t stream_bitrate = allocation.GetSpatialLayerSum(i) / 1000;
			start_bitrates.push_back(stream_bitrate);
		}

		for (int i = 0; i < number_of_streams; ++i)
		{
			webrtc::VideoCodec stream_codec;
			uint32_t start_bitrate_kbps = std::max(CurrentCodec.simulcastStream[i].minBitrate, start_bitrates[i]);
			PopulateStreamCodec(CurrentCodec, i, start_bitrate_kbps, &stream_codec);

			std::unique_ptr<VideoEncoder> encoder = PrimaryEncoderFactory->CreateVideoEncoder(format);
			int ret = encoder->InitEncode(&stream_codec, settings);
			if (ret < 0)
			{
				// Explicitly destroy the current encoder; because we haven't registered
				// a StreamInfo for it yet, Release won't do anything about it.
				encoder.reset();
				Release();
				return ret;
			}

			std::unique_ptr<webrtc::EncodedImageCallback> callback(new AdapterEncodedImageCallback(this, i));
			encoder->RegisterEncodeCompleteCallback(callback.get());
			StreamInfos.emplace_back(
				std::move(encoder), std::move(callback),
				std::make_unique<webrtc::FramerateController>(stream_codec.maxFramerate),
				stream_codec.width, stream_codec.height, true);
		}
	}

	Initialized = true;

	return WEBRTC_VIDEO_CODEC_OK;
}

int PixelStreamingSimulcastEncoderAdapter::EncodeStream(const webrtc::VideoFrame& input_image, FPixelStreamingLayerFrameSource* layer_frame_source, size_t stream_idx, bool send_key_frame)
{
	// grab the simulcast frame source, extract the frame source for this layer and wrap that in a new frame buffer
	rtc::scoped_refptr<FPixelStreamingLayerFrameBuffer> layer_frame_buffer = new rtc::RefCountedObject<FPixelStreamingLayerFrameBuffer>(layer_frame_source);
	webrtc::VideoFrame new_frame(input_image);
	new_frame.set_video_frame_buffer(layer_frame_buffer);

	const uint32_t frame_timestamp_ms = 1000 * new_frame.timestamp() / 90000;

	// If adapter is passed through and only one sw encoder does simulcast,
	// frame types for all streams should be passed to the encoder unchanged.
	// Otherwise a single per-encoder frame type is passed.
	std::vector<webrtc::VideoFrameType> stream_frame_types(StreamInfos.size() == 1 ? NumberOfStreams(CurrentCodec) : 1);
	if (send_key_frame)
	{
		std::fill(stream_frame_types.begin(), stream_frame_types.end(), webrtc::VideoFrameType::kVideoFrameKey);
		StreamInfos[stream_idx].key_frame_request = false;
	}
	else
	{
		if (StreamInfos[stream_idx].framerate_controller->DropFrame(frame_timestamp_ms))
		{
			return WEBRTC_VIDEO_CODEC_OK;
		}
		std::fill(stream_frame_types.begin(), stream_frame_types.end(), webrtc::VideoFrameType::kVideoFrameDelta);
	}
	StreamInfos[stream_idx].framerate_controller->AddFrame(frame_timestamp_ms);

	return StreamInfos[stream_idx].encoder->Encode(new_frame, &stream_frame_types);
}

int PixelStreamingSimulcastEncoderAdapter::Encode(const webrtc::VideoFrame& input_image, const std::vector<webrtc::VideoFrameType>* frame_types)
{
	if (!IsInitialized())
	{
		return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
	}

	if (EncodedCompleteCallback == nullptr)
	{
		return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
	}

	// ignore any init frames
	// NOTE: It seems most of the time they never even get here because the internals of WebRTC decide to drop it. It seems it thinks
	// the encoder is paused or something.
	const FPixelStreamingFrameBuffer* frame_buffer = static_cast<FPixelStreamingFrameBuffer*>(input_image.video_frame_buffer().get());
	if (frame_buffer->GetFrameBufferType() == FPixelStreamingFrameBufferType::Initialize)
	{
		return WEBRTC_VIDEO_CODEC_OK;
	}

	// separate out our pixelstreaming image sources
	check(frame_buffer->GetFrameBufferType() == FPixelStreamingFrameBufferType::Simulcast);
	const FPixelStreamingSimulcastFrameBuffer* simulcast_frame_buffer = static_cast<const FPixelStreamingSimulcastFrameBuffer*>(frame_buffer);

	// All active streams should generate a key frame if
	// a key frame is requested by any stream.
	bool send_key_frame = false;
	if (frame_types)
	{
		for (size_t i = 0; i < frame_types->size(); ++i)
		{
			if (frame_types->at(i) == webrtc::VideoFrameType::kVideoFrameKey)
			{
				send_key_frame = true;
				break;
			}
		}
	}

	for (size_t stream_idx = 0; stream_idx < StreamInfos.size(); ++stream_idx)
	{
		if (StreamInfos[stream_idx].key_frame_request && StreamInfos[stream_idx].send_stream)
		{
			send_key_frame = true;
			break;
		}
	}

	for (size_t stream_idx = 0; stream_idx < StreamInfos.size(); ++stream_idx)
	{
		// Don't encode frames in resolutions that we don't intend to send.
		if (!StreamInfos[stream_idx].send_stream)
		{
			continue;
		}

		FPixelStreamingLayerFrameSource* layer_frame_source = simulcast_frame_buffer->GetLayerFrameSource(StreamInfos.size() == 1 ? simulcast_frame_buffer->GetNumLayers() - 1 : stream_idx);
		int ret = EncodeStream(input_image, layer_frame_source, stream_idx, send_key_frame);
		if (ret != WEBRTC_VIDEO_CODEC_OK)
		{
			return ret;
		}
	}
	return WEBRTC_VIDEO_CODEC_OK;
}

int PixelStreamingSimulcastEncoderAdapter::RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* callback)
{
	EncodedCompleteCallback = callback;
	return WEBRTC_VIDEO_CODEC_OK;
}

void PixelStreamingSimulcastEncoderAdapter::SetRates(const RateControlParameters& parameters)
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
		StreamInfos[0].encoder->SetRates(parameters);
		return;
	}

	for (size_t stream_idx = 0; stream_idx < StreamInfos.size(); ++stream_idx)
	{
		uint32_t stream_bitrate_kbps = parameters.bitrate.GetSpatialLayerSum(stream_idx) / 1000;

		// Need a key frame if we have not sent this stream before.
		if (stream_bitrate_kbps > 0 && !StreamInfos[stream_idx].send_stream)
		{
			StreamInfos[stream_idx].key_frame_request = true;
		}
		StreamInfos[stream_idx].send_stream = stream_bitrate_kbps > 0;

		// Slice the temporal layers out of the full allocation and pass it on to
		// the encoder handling the current simulcast stream.
		RateControlParameters stream_parameters = parameters;
		stream_parameters.bitrate = webrtc::VideoBitrateAllocation();
		for (int i = 0; i < webrtc::kMaxTemporalStreams; ++i)
		{
			if (parameters.bitrate.HasBitrate(stream_idx, i))
			{
				stream_parameters.bitrate.SetBitrate(0, i, parameters.bitrate.GetBitrate(stream_idx, i));
			}
		}

		// Assign link allocation proportionally to spatial layer allocation.
		if (!parameters.bandwidth_allocation.IsZero() && parameters.bitrate.get_sum_bps() > 0)
		{
			stream_parameters.bandwidth_allocation =
				webrtc::DataRate::BitsPerSec((parameters.bandwidth_allocation.bps() * stream_parameters.bitrate.get_sum_bps()) / parameters.bitrate.get_sum_bps());
			// Make sure we don't allocate bandwidth lower than target bitrate.
			if (stream_parameters.bandwidth_allocation.bps() < stream_parameters.bitrate.get_sum_bps())
			{
				stream_parameters.bandwidth_allocation =
					webrtc::DataRate::BitsPerSec(stream_parameters.bitrate.get_sum_bps());
			}
		}

		stream_parameters.framerate_fps = std::min<double>(parameters.framerate_fps, StreamInfos[stream_idx].framerate_controller->GetTargetRate());

		StreamInfos[stream_idx].encoder->SetRates(stream_parameters);
	}
}

void PixelStreamingSimulcastEncoderAdapter::OnPacketLossRateUpdate(float packet_loss_rate)
{
	for (StreamInfo& info : StreamInfos)
	{
		info.encoder->OnPacketLossRateUpdate(packet_loss_rate);
	}
}

void PixelStreamingSimulcastEncoderAdapter::OnRttUpdate(int64_t rtt_ms)
{
	for (StreamInfo& info : StreamInfos)
	{
		info.encoder->OnRttUpdate(rtt_ms);
	}
}

void PixelStreamingSimulcastEncoderAdapter::OnLossNotification(const LossNotification& loss_notification)
{
	for (StreamInfo& info : StreamInfos)
	{
		info.encoder->OnLossNotification(loss_notification);
	}
}

webrtc::EncodedImageCallback::Result PixelStreamingSimulcastEncoderAdapter::OnEncodedImage(
	size_t stream_idx,
	const webrtc::EncodedImage& encodedImage,
	const webrtc::CodecSpecificInfo* codecSpecificInfo,
	const webrtc::RTPFragmentationHeader* fragmentation)
{
	webrtc::EncodedImage stream_image(encodedImage);
	webrtc::CodecSpecificInfo stream_codec_specific = *codecSpecificInfo;

	stream_image.SetSpatialIndex(stream_idx);

	return EncodedCompleteCallback->OnEncodedImage(stream_image, &stream_codec_specific, fragmentation);
}

bool PixelStreamingSimulcastEncoderAdapter::IsInitialized() const
{
	return Initialized;
}

webrtc::VideoEncoder::EncoderInfo PixelStreamingSimulcastEncoderAdapter::GetEncoderInfo() const
{
	if (StreamInfos.size() == 1)
	{
		// Not using simulcast adapting functionality, just pass through.
		return StreamInfos[0].encoder->GetEncoderInfo();
	}

	VideoEncoder::EncoderInfo encoder_info;
	encoder_info.implementation_name = "PixelStreamingSimulcastEncoderAdapter";
	encoder_info.requested_resolution_alignment = 1;
	encoder_info.supports_native_handle = true;
	encoder_info.scaling_settings.thresholds = absl::nullopt;
	if (StreamInfos.empty())
	{
		return encoder_info;
	}

	encoder_info.scaling_settings = VideoEncoder::ScalingSettings::kOff;
	int num_active_streams = NumActiveStreams(CurrentCodec);

	for (size_t i = 0; i < StreamInfos.size(); ++i)
	{
		VideoEncoder::EncoderInfo encoder_impl_info = StreamInfos[i].encoder->GetEncoderInfo();

		if (i == 0)
		{
			// Encoder name indicates names of all sub-encoders.
			encoder_info.implementation_name += " (";
			encoder_info.implementation_name += encoder_impl_info.implementation_name;

			encoder_info.supports_native_handle = encoder_impl_info.supports_native_handle;
			encoder_info.has_trusted_rate_controller = encoder_impl_info.has_trusted_rate_controller;
			encoder_info.is_hardware_accelerated = encoder_impl_info.is_hardware_accelerated;
			encoder_info.has_internal_source = encoder_impl_info.has_internal_source;
		}
		else
		{
			encoder_info.implementation_name += ", ";
			encoder_info.implementation_name += encoder_impl_info.implementation_name;

			// Native handle supported if any encoder supports it.
			encoder_info.supports_native_handle |= encoder_impl_info.supports_native_handle;

			// Trusted rate controller only if all encoders have it.
			encoder_info.has_trusted_rate_controller &= encoder_impl_info.has_trusted_rate_controller;

			// Uses hardware support if any of the encoders uses it.
			// For example, if we are having issues with down-scaling due to
			// pipelining delay in HW encoders we need higher encoder usage
			// thresholds in CPU adaptation.
			encoder_info.is_hardware_accelerated |= encoder_impl_info.is_hardware_accelerated;

			// Has internal source only if all encoders have it.
			encoder_info.has_internal_source &= encoder_impl_info.has_internal_source;
		}
		encoder_info.fps_allocation[i] = encoder_impl_info.fps_allocation[0];
		encoder_info.requested_resolution_alignment = cricket::LeastCommonMultiple(encoder_info.requested_resolution_alignment, encoder_impl_info.requested_resolution_alignment);
		if (num_active_streams == 1 && CurrentCodec.simulcastStream[i].active)
		{
			encoder_info.scaling_settings = encoder_impl_info.scaling_settings;
		}
	}
	encoder_info.implementation_name += ")";

	return encoder_info;
}
