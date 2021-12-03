// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "WebRTCIncludes.h"
//#include <memory>

//#include <string>
//#include <utility>
//#include <vector>

//#include "absl/types/optional.h"
//#include "api/fec_controller_override.h"
//#include "api/video_codecs/sdp_video_format.h"
//#include "api/video_codecs/video_encoder.h"
//#include "modules/video_coding/include/video_codec_interface.h"
//#include "modules/video_coding/utility/framerate_controller.h"
//#include "rtc_base/atomic_ops.h"
//#include "rtc_base/synchronization/sequence_checker.h"
//#include "rtc_base/system/rtc_export.h"

namespace webrtc {

class SimulcastRateAllocator;
class VideoEncoderFactory;

}


// This is duplicated from webrtc::simulcast_encoder_adapter
// Something about allocating within the library was causing memory corruption and
// so duplicating it here makes sure its allocating the correct way.

class PixelStreamingSimulcastEncoderAdapter : public webrtc::VideoEncoder {
 public:
  // TODO(bugs.webrtc.org/11000): Remove when downstream usage is gone.
  PixelStreamingSimulcastEncoderAdapter(webrtc::VideoEncoderFactory* primarty_factory,
                          const webrtc::SdpVideoFormat& format);
  // |primary_factory| produces the first-choice encoders to use.
  // |fallback_factory|, if non-null, is used to create fallback encoder that
  // will be used if InitEncode() fails for the primary encoder.
  PixelStreamingSimulcastEncoderAdapter(webrtc::VideoEncoderFactory* primary_factory,
                          webrtc::VideoEncoderFactory* fallback_factory,
                          const webrtc::SdpVideoFormat& format);
  ~PixelStreamingSimulcastEncoderAdapter() override;

  // Implements VideoEncoder.
  void SetFecControllerOverride(
      webrtc::FecControllerOverride* fec_controller_override) override;
  int Release() override;
  int InitEncode(const webrtc::VideoCodec* codec_settings,
                 const webrtc::VideoEncoder::Settings& settings) override;
  int Encode(const webrtc::VideoFrame& input_image,
             const std::vector<webrtc::VideoFrameType>* frame_types) override;
  int RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* callback) override;
  void SetRates(const RateControlParameters& parameters) override;
  void OnPacketLossRateUpdate(float packet_loss_rate) override;
  void OnRttUpdate(int64_t rtt_ms) override;
  void OnLossNotification(const LossNotification& loss_notification) override;

  // Eventual handler for the contained encoders' EncodedImageCallbacks, but
  // called from an internal helper that also knows the correct stream
  // index.
  webrtc::EncodedImageCallback::Result OnEncodedImage(
      size_t stream_idx,
      const webrtc::EncodedImage& encoded_image,
      const webrtc::CodecSpecificInfo* codec_specific_info,
      const webrtc::RTPFragmentationHeader* fragmentation);

  EncoderInfo GetEncoderInfo() const override;

 private:
  struct StreamInfo {
    StreamInfo(std::unique_ptr<webrtc::VideoEncoder> encoder,
               std::unique_ptr<webrtc::EncodedImageCallback> callback,
               std::unique_ptr<webrtc::FramerateController> framerate_controller,
               uint16_t width,
               uint16_t height,
               bool send_stream)
        : encoder(std::move(encoder)),
          callback(std::move(callback)),
          framerate_controller(std::move(framerate_controller)),
          width(width),
          height(height),
          key_frame_request(false),
          send_stream(send_stream) {}
    std::unique_ptr<webrtc::VideoEncoder> encoder;
    std::unique_ptr<webrtc::EncodedImageCallback> callback;
    std::unique_ptr<webrtc::FramerateController> framerate_controller;
    uint16_t width;
    uint16_t height;
    bool key_frame_request;
    bool send_stream;
  };

  enum class StreamResolution {
    OTHER,
    HIGHEST,
    LOWEST,
  };

  // Populate the codec settings for each simulcast stream.
  void PopulateStreamCodec(const webrtc::VideoCodec& inst,
                           int stream_index,
                           uint32_t start_bitrate_kbps,
                           StreamResolution stream_resolution,
                           webrtc::VideoCodec* stream_codec);

  bool Initialized() const;

  void DestroyStoredEncoders();

  volatile int inited_;  // Accessed atomically.
  webrtc::VideoEncoderFactory* const primary_encoder_factory_;
  webrtc::VideoEncoderFactory* const fallback_encoder_factory_;
  const webrtc::SdpVideoFormat video_format_;
  webrtc::VideoCodec codec_;
  std::vector<StreamInfo> streaminfos_;
  webrtc::EncodedImageCallback* encoded_complete_callback_;

  // Used for checking the single-threaded access of the encoder interface.
  webrtc::SequenceChecker encoder_queue_;

  // Store encoders in between calls to Release and InitEncode, so they don't
  // have to be recreated. Remaining encoders are destroyed by the destructor.
  std::stack<std::unique_ptr<webrtc::VideoEncoder>> stored_encoders_;

  const absl::optional<unsigned int> experimental_boosted_screenshare_qp_;
  const bool boost_base_layer_quality_;
  const bool prefer_temporal_support_on_base_layer_;
};

