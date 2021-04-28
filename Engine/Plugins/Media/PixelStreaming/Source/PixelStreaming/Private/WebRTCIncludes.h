// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if PLATFORM_WINDOWS || PLATFORM_XBOXONE
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/AllowWindowsPlatformAtomics.h"

THIRD_PARTY_INCLUDES_START

// WebRTC requires `htonll` to be defined, which depends either on `NTDDI_VERION` value or
// `INCL_EXTRA_HTON_FUNCTIONS` to be defined
#if !defined(INCL_EXTRA_HTON_FUNCTIONS)
#	if defined(UNDEF_INCL_EXTRA_HTON_FUNCTIONS)
#		pragma message(": Error: `UNDEF_INCL_EXTRA_HTON_FUNCTIONS` already defined, use another name")
#	endif
#	define UNDEF_INCL_EXTRA_HTON_FUNCTIONS
#	define INCL_EXTRA_HTON_FUNCTIONS
#endif

// C4582/3: constructor/desctructor is not implicitly called in "api/rtcerror.h", treated as an error by UnrealEngine
// for some unknown reasons we have to disable it inside those Unreal's windows-related includes
// C6323: Use of arithmetic operator on Boolean type(s).
#pragma warning(push)
#pragma warning(disable: 4582 4596 6323)

#include "rtc_base/win32.h"
#include "rtc_base/win32_socket_init.h"
#include "rtc_base/win32_socket_server.h"

// uses Win32 Interlocked* functions
#include "rtc_base/ref_counted_object.h"

#pragma warning(pop)

#if defined(UNDEF_INCL_EXTRA_HTON_FUNCTIONS)
#	undef UNDEF_INCL_EXTRA_HTON_FUNCTIONS
#	undef INCL_EXTRA_HTON_FUNCTIONS
#endif

THIRD_PARTY_INCLUDES_END

#include "Windows/HideWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformTypes.h"

#endif // PLATFORM_WINDOWS || PLATFORM_XBOXONE

THIRD_PARTY_INCLUDES_START

#if PLATFORM_LINUX
#include "rtc_base/physical_socket_server.h"
#endif

// C4582: constructor is not implicitly called in "api/rtcerror.h", treated as an error by UnrealEngine
// C6319: Use of the comma-operator in a tested expression causes the left argument to be ignored when it has no side-effects.
// C6323: Use of arithmetic operator on Boolean type(s).
#pragma warning(push)
#pragma warning(disable: 4582 4583 6319 6323)

#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/create_peerconnection_factory.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "api/audio_codecs/audio_format.h"
#include "api/audio_codecs/audio_decoder_factory_template.h"
#include "api/audio_codecs/audio_encoder_factory_template.h"
#include "api/audio_codecs/opus/audio_decoder_opus.h"
#include "api/audio_codecs/opus/audio_encoder_opus.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/video_codecs/video_decoder.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video/video_frame.h"
#include "api/video/video_rotation.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_sink_interface.h"

#include "rtc_base/thread.h"
#include "rtc_base/logging.h"
#include "rtc_base/ssl_adapter.h"
#include "rtc_base/arraysize.h"
#include "rtc_base/net_helpers.h"
#include "rtc_base/string_utils.h"
#include "rtc_base/signal_thread.h"

#include "pc/session_description.h"
#include "pc/video_track_source.h"

#include "media/engine/internal_decoder_factory.h"
#include "media/engine/internal_encoder_factory.h"
#include "media/base/h264_profile_level_id.h"
#include "media/base/adapted_video_track_source.h"
#include "media/base/media_channel.h"
#include "media/base/video_common.h"

#include "modules/video_capture/video_capture_factory.h"
#include "modules/audio_device/include/audio_device.h"
#include "modules/audio_device/audio_device_buffer.h"
#include "modules/audio_processing/include/audio_processing.h"
#include "modules/video_coding/codecs/h264/include/h264.h"

#include "common_video/h264/h264_bitstream_parser.h"
#include "common_video/h264/h264_common.h"

#include "media/base/video_broadcaster.h"

#pragma warning(pop)

 // because WebRTC uses STL
#include <string>
#include <memory>

THIRD_PARTY_INCLUDES_END
