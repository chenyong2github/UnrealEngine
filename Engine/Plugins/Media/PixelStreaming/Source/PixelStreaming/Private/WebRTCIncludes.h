// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

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

// C4582/3: constructor/desctructor is not implicitly called in "api/rtcerror.h", treated as an error by UE4
// for some unknown reasons we have to disable it inside those UE4's windows-related includes
// C6323: Use of arithmetic operator on Boolean type(s).
#pragma warning(push)
#pragma warning(disable: 4582 4583 6323)

#include "rtc_base/win32.h"
#include "rtc_base/win32socketinit.h"
#include "rtc_base/win32socketserver.h"

// uses Win32 Interlocked* functions
#include "rtc_base/refcountedobject.h"

#pragma warning(pop)

#if defined(UNDEF_INCL_EXTRA_HTON_FUNCTIONS)
#	undef UNDEF_INCL_EXTRA_HTON_FUNCTIONS
#	undef INCL_EXTRA_HTON_FUNCTIONS
#endif

THIRD_PARTY_INCLUDES_END

#include "Windows/HideWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformTypes.h"

THIRD_PARTY_INCLUDES_START

// C4582: constructor is not implicitly called in "api/rtcerror.h", treated as an error by UE4
// C6319: Use of the comma-operator in a tested expression causes the left argument to be ignored when it has no side-effects.
// C6323: Use of arithmetic operator on Boolean type(s).
#pragma warning(push)
#pragma warning(disable: 4582 6319 6323)

#include "api/mediastreaminterface.h"
#include "api/peerconnectioninterface.h"
#include "api/audio_codecs/audio_format.h"
#include "api/audio_codecs/audio_decoder_factory_template.h"
#include "api/audio_codecs/audio_encoder_factory_template.h"
#include "api/audio_codecs/opus/audio_decoder_opus.h"
#include "api/audio_codecs/opus/audio_encoder_opus.h"
#include "api/test/fakeconstraints.h"
#include "api/video_codecs/video_decoder.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_sink_interface.h"

#include "rtc_base/thread.h"
#include "rtc_base/logging.h"
#include "rtc_base/flags.h"
#include "rtc_base/ssladapter.h"
#include "rtc_base/arraysize.h"
#include "rtc_base/nethelpers.h"
#include "rtc_base/stringutils.h"
#include "rtc_base/signalthread.h"

#include "pc/sessiondescription.h"

#include "media/base/videocapturer.h"
#include "media/engine/webrtcvideocapturerfactory.h"
#include "media/engine/internaldecoderfactory.h"
#include "media/engine/internalencoderfactory.h"
#include "media/base/h264_profile_level_id.h"
#include "media/engine/webrtcvideoencoderfactory.h"
#include "media/base/adaptedvideotracksource.h"
#include "media/base/mediachannel.h"
#include "media/base/videocommon.h"

#include "modules/video_capture/video_capture_factory.h"
#include "modules/audio_device/include/audio_device.h"
#include "modules/audio_device/audio_device_buffer.h"
#include "modules/audio_processing/include/audio_processing.h"
#include "modules/video_coding/codecs/h264/include/h264.h"

#include "common_video/h264/h264_bitstream_parser.h"
#include "common_video/h264/h264_common.h"

#include "media/base/videobroadcaster.h"

#pragma warning(pop)

 // because WebRTC uses STL
#include <string>
#include <memory>

THIRD_PARTY_INCLUDES_END
