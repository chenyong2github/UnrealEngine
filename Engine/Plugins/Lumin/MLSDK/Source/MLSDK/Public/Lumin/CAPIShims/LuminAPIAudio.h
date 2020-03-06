// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_audio.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_audio, const char*, MLAudioGetResultString)
#define MLAudioGetResultString ::MLSDK_API::MLAudioGetResultStringShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioCreateLoadedResource)
#define MLAudioCreateLoadedResource ::MLSDK_API::MLAudioCreateLoadedResourceShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioCreateStreamedResource)
#define MLAudioCreateStreamedResource ::MLSDK_API::MLAudioCreateStreamedResourceShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioCheckResource)
#define MLAudioCheckResource ::MLSDK_API::MLAudioCheckResourceShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetResourceSize)
#define MLAudioCheckReMLAudioGetResourceSizesource ::MLSDK_API::MLAudioGetResourceSizeShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioRefreshResource)
#define MLAudioRefreshResource ::MLSDK_API::MLAudioRefreshResourceShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioDestroyResource)
#define MLAudioDestroyResource ::MLSDK_API::MLAudioDestroyResourceShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioCreateSoundWithLoadedResource)
#define MLAudioCreateSoundWithLoadedResource ::MLSDK_API::MLAudioCreateSoundWithLoadedResourceShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioCreateSoundWithStreamedResource)
#define MLAudioCreateSoundWithStreamedResource ::MLSDK_API::MLAudioCreateSoundWithStreamedResourceShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioCreateSoundWithLoadedFile)
#define MLAudioCreateSoundWithLoadedFile ::MLSDK_API::MLAudioCreateSoundWithLoadedFileShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioCreateSoundWithStreamedFile)
#define MLAudioCreateSoundWithStreamedFile ::MLSDK_API::MLAudioCreateSoundWithStreamedFileShim
CREATE_DEPRECATED_MSG_SHIM(ml_audio, MLResult, MLAudioCreateSoundWithOutputStream, "Replaced by MLCreateSoundWithBufferedOutput.")
#define MLAudioCreateSoundWithOutputStream ::MLSDK_API::MLAudioCreateSoundWithOutputStreamShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioCreateSoundWithBufferedOutput)
#define MLAudioCreateSoundWithBufferedOutput ::MLSDK_API::MLAudioCreateSoundWithBufferedOutputShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioDestroySound)
#define MLAudioDestroySound ::MLSDK_API::MLAudioDestroySoundShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioStartSound)
#define MLAudioStartSound ::MLSDK_API::MLAudioStartSoundShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioStopSound)
#define MLAudioStopSound ::MLSDK_API::MLAudioStopSoundShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioPauseSound)
#define MLAudioPauseSound ::MLSDK_API::MLAudioPauseSoundShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioResumeSound)
#define MLAudioResumeSound ::MLSDK_API::MLAudioResumeSoundShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSoundState)
#define MLAudioGetSoundState ::MLSDK_API::MLAudioGetSoundStateShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSoundFormat)
#define MLAudioGetSoundFormat ::MLSDK_API::MLAudioGetSoundFormatShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSoundEventCallback)
#define MLAudioSetSoundEventCallback ::MLSDK_API::MLAudioSetSoundEventCallbackShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSpatialSoundEnable)
#define MLAudioSetSpatialSoundEnable ::MLSDK_API::MLAudioSetSpatialSoundEnableShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSpatialSoundEnable)
#define MLAudioGetSpatialSoundEnable ::MLSDK_API::MLAudioGetSpatialSoundEnableShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSpatialSoundPosition)
#define MLAudioSetSpatialSoundPosition ::MLSDK_API::MLAudioSetSpatialSoundPositionShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSpatialSoundPosition)
#define MLAudioGetSpatialSoundPosition ::MLSDK_API::MLAudioGetSpatialSoundPositionShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSpatialSoundDirection)
#define MLAudioSetSpatialSoundDirection ::MLSDK_API::MLAudioSetSpatialSoundDirectionShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSpatialSoundDirection)
#define MLAudioGetSpatialSoundDirection ::MLSDK_API::MLAudioGetSpatialSoundDirectionShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSpatialSoundDistanceProperties)
#define MLAudioSetSpatialSoundDistanceProperties ::MLSDK_API::MLAudioSetSpatialSoundDistancePropertiesShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSpatialSoundDistanceProperties)
#define MLAudioGetSpatialSoundDistanceProperties ::MLSDK_API::MLAudioGetSpatialSoundDistancePropertiesShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSpatialSoundRadiationProperties)
#define MLAudioSetSpatialSoundRadiationProperties ::MLSDK_API::MLAudioSetSpatialSoundRadiationPropertiesShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSpatialSoundRadiationProperties)
#define MLAudioGetSpatialSoundRadiationProperties ::MLSDK_API::MLAudioGetSpatialSoundRadiationPropertiesShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSpatialSoundDirectSendLevels)
#define MLAudioSetSpatialSoundDirectSendLevels ::MLSDK_API::MLAudioSetSpatialSoundDirectSendLevelsShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSpatialSoundDirectSendLevels)
#define MLAudioGetSpatialSoundDirectSendLevels ::MLSDK_API::MLAudioGetSpatialSoundDirectSendLevelsShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSpatialSoundRoomSendLevels)
#define MLAudioSetSpatialSoundRoomSendLevels ::MLSDK_API::MLAudioSetSpatialSoundRoomSendLevelsShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSpatialSoundRoomSendLevels)
#define MLAudioGetSpatialSoundRoomSendLevels ::MLSDK_API::MLAudioGetSpatialSoundRoomSendLevelsShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSpatialSoundRoomProperties)
#define MLAudioSetSpatialSoundRoomProperties ::MLSDK_API::MLAudioSetSpatialSoundRoomPropertiesShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSpatialSoundRoomProperties)
#define MLAudioGetSpatialSoundRoomProperties ::MLSDK_API::MLAudioGetSpatialSoundRoomPropertiesShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSpatialSoundControlFrequencies)
#define MLAudioSetSpatialSoundControlFrequencies ::MLSDK_API::MLAudioSetSpatialSoundControlFrequenciesShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSpatialSoundControlFrequencies)
#define MLAudioGetSpatialSoundControlFrequencies ::MLSDK_API::MLAudioGetSpatialSoundControlFrequenciesShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSpatialSoundHeadRelative)
#define MLAudioSetSpatialSoundHeadRelative ::MLSDK_API::MLAudioSetSpatialSoundHeadRelativeShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioIsSpatialSoundHeadRelative)
#define MLAudioIsSpatialSoundHeadRelative ::MLSDK_API::MLAudioIsSpatialSoundHeadRelativeShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSoundVolumeLinear)
#define MLAudioSetSoundVolumeLinear ::MLSDK_API::MLAudioSetSoundVolumeLinearShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSoundVolumeLinear)
#define MLAudioGetSoundVolumeLinear ::MLSDK_API::MLAudioGetSoundVolumeLinearShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSoundVolumeDb)
#define MLAudioSetSoundVolumeDb ::MLSDK_API::MLAudioSetSoundVolumeDbShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSoundVolumeDb)
#define MLAudioGetSoundVolumeDb ::MLSDK_API::MLAudioGetSoundVolumeDbShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSoundPitch)
#define MLAudioSetSoundPitch ::MLSDK_API::MLAudioSetSoundPitchShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetSoundPitch)
#define MLAudioGetSoundPitch ::MLSDK_API::MLAudioGetSoundPitchShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSoundMute)
#define MLAudioSetSoundMute ::MLSDK_API::MLAudioSetSoundMuteShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioIsSoundMuted)
#define MLAudioIsSoundMuted ::MLSDK_API::MLAudioIsSoundMutedShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetSoundLooping)
#define MLAudioSetSoundLooping ::MLSDK_API::MLAudioSetSoundLoopingShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioIsSoundLooping)
#define MLAudioIsSoundLooping ::MLSDK_API::MLAudioIsSoundLoopingShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetStreamedFileOffset)
#define MLAudioSetStreamedFileOffset ::MLSDK_API::MLAudioSetStreamedFileOffsetShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetStreamedFileOffset)
#define MLAudioGetStreamedFileOffset ::MLSDK_API::MLAudioGetStreamedFileOffsetShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetOutputDevice)
#define MLAudioGetOutputDevice ::MLSDK_API::MLAudioGetOutputDeviceShim
CREATE_DEPRECATED_MSG_SHIM(ml_audio, MLResult, MLAudioGetOutputStreamDefaults, "Replaced by MLAudioGetBufferedOutputDefaults.")
#define MLAudioGetOutputStreamDefaults ::MLSDK_API::MLAudioGetOutputStreamDefaultsShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetBufferedOutputDefaults)
#define MLAudioGetBufferedOutputDefaults ::MLSDK_API::MLAudioGetBufferedOutputDefaultsShim
CREATE_DEPRECATED_MSG_SHIM(ml_audio, MLResult, MLAudioGetOutputStreamLatency, "Replaced by MLAudioGetBufferedOutputLatency.")
#define MLAudioGetOutputStreamLatency ::MLSDK_API::MLAudioGetOutputStreamLatencyShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetBufferedOutputLatency)
#define MLAudioGetBufferedOutputLatency ::MLSDK_API::MLAudioGetBufferedOutputLatencyShim
CREATE_DEPRECATED_MSG_SHIM(ml_audio, MLResult, MLAudioGetOutputStreamFramesPlayed, "Replaced by MLAudioGetBufferedOutputFramesPlayed.")
#define MLAudioGetOutputStreamFramesPlayed ::MLSDK_API::MLAudioGetOutputStreamFramesPlayedShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetBufferedOutputFramesPlayed)
#define MLAudioGetBufferedOutputFramesPlayed ::MLSDK_API::MLAudioGetBufferedOutputFramesPlayedShim
CREATE_DEPRECATED_MSG_SHIM(ml_audio, MLResult, MLAudioGetOutputStreamBuffer, "Replaced by MLAudioGetOutputBuffer.")
#define MLAudioGetOutputStreamBuffer ::MLSDK_API::MLAudioGetOutputStreamBufferShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetOutputBuffer)
#define MLAudioGetOutputBuffer ::MLSDK_API::MLAudioGetOutputBufferShim
CREATE_DEPRECATED_MSG_SHIM(ml_audio, MLResult, MLAudioReleaseOutputStreamBuffer, "Replaced by MLAudioReleaseOutputBuffer.")
#define MLAudioReleaseOutputStreamBuffer ::MLSDK_API::MLAudioReleaseOutputStreamBufferShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioReleaseOutputBuffer)
#define MLAudioReleaseOutputBuffer ::MLSDK_API::MLAudioReleaseOutputBufferShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetMasterVolume)
#define MLAudioGetMasterVolume ::MLSDK_API::MLAudioGetMasterVolumeShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetMasterVolumeCallback)
#define MLAudioSetMasterVolumeCallback ::MLSDK_API::MLAudioSetMasterVolumeCallbackShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioCreateInputFromVoiceComm)
#define MLAudioCreateInputFromVoiceComm ::MLSDK_API::MLAudioCreateInputFromVoiceCommShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioCreateInputFromWorldCapture)
#define MLAudioCreateInputFromWorldCapture ::MLSDK_API::MLAudioCreateInputFromWorldCaptureShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioDestroyInput)
#define MLAudioDestroyInput ::MLSDK_API::MLAudioDestroyInputShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioStartInput)
#define MLAudioStartInput ::MLSDK_API::MLAudioStartInputShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioStopInput)
#define MLAudioStopInput ::MLSDK_API::MLAudioStopInputShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetInputState)
#define MLAudioGetInputState ::MLSDK_API::MLAudioGetInputStateShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetInputEventCallback)
#define MLAudioSetInputEventCallback ::MLSDK_API::MLAudioSetInputEventCallbackShim
CREATE_DEPRECATED_MSG_SHIM(ml_audio, MLResult, MLAudioGetInputStreamDefaults, "Replaced by MLAudioGetBufferedInputDefaults.")
#define MLAudioGetInputStreamDefaults ::MLSDK_API::MLAudioGetInputStreamDefaultsShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetBufferedInputDefaults)
#define MLAudioGetBufferedInputDefaults ::MLSDK_API::MLAudioGetBufferedInputDefaultsShim
CREATE_DEPRECATED_MSG_SHIM(ml_audio, MLResult, MLAudioGetInputStreamLatency, "Replaced by MLAudioGetBufferedInputLatency.")
#define MLAudioGetInputStreamLatency ::MLSDK_API::MLAudioGetInputStreamLatencyShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetBufferedInputLatency)
#define MLAudioGetBufferedInputLatency ::MLSDK_API::MLAudioGetBufferedInputLatencyShim
CREATE_DEPRECATED_MSG_SHIM(ml_audio, MLResult, MLAudioGetInputStreamBuffer, "Replaced by MLAudioGetInputBuffer.")
#define MLAudioGetInputStreamBuffer ::MLSDK_API::MLAudioGetInputStreamBufferShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioGetInputBuffer)
#define MLAudioGetInputBuffer ::MLSDK_API::MLAudioGetInputBufferShim
CREATE_DEPRECATED_MSG_SHIM(ml_audio, MLResult, MLAudioReleaseInputStreamBuffer, "Replaced by MLAudioReleaseInputBuffer.")
#define MLAudioReleaseInputStreamBuffer ::MLSDK_API::MLAudioReleaseInputStreamBufferShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioReleaseInputBuffer)
#define MLAudioReleaseInputBuffer ::MLSDK_API::MLAudioReleaseInputBufferShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetMicMute)
#define MLAudioSetMicMute ::MLSDK_API::MLAudioSetMicMuteShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioIsMicMuted)
#define MLAudioIsMicMuted ::MLSDK_API::MLAudioIsMicMutedShim
CREATE_FUNCTION_SHIM(ml_audio, MLResult, MLAudioSetMicMuteCallback)
#define MLAudioSetMicMuteCallback ::MLSDK_API::MLAudioSetMicMuteCallbackShim
CREATE_DEPRECATED_SHIM(ml_audio, MLResult, MLAudioCreateInputFromVirtualCapture)
#define MLAudioCreateInputFromVirtualCapture ::MLSDK_API::MLAudioCreateInputFromVirtualCaptureShim
CREATE_DEPRECATED_SHIM(ml_audio, MLResult, MLAudioCreateInputFromMixedCapture)
#define MLAudioCreateInputFromMixedCapture ::MLSDK_API::MLAudioCreateInputFromMixedCaptureShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
