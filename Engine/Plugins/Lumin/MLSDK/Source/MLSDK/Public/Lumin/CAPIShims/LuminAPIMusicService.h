// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_music_service_common.h>
#include <ml_music_service.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceConnect)
#define MLMusicServiceConnect ::LUMIN_MLSDK_API::MLMusicServiceConnectShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceDisconnect)
#define MLMusicServiceDisconnect ::LUMIN_MLSDK_API::MLMusicServiceDisconnectShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceSetCallbacks)
#define MLMusicServiceSetCallbacks ::LUMIN_MLSDK_API::MLMusicServiceSetCallbacksShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceSetAuthString)
#define MLMusicServiceSetAuthString ::LUMIN_MLSDK_API::MLMusicServiceSetAuthStringShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceSetURL)
#define MLMusicServiceSetURL ::LUMIN_MLSDK_API::MLMusicServiceSetURLShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceSetPlayList)
#define MLMusicServiceSetPlayList ::LUMIN_MLSDK_API::MLMusicServiceSetPlayListShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceStart)
#define MLMusicServiceStart ::LUMIN_MLSDK_API::MLMusicServiceStartShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceStop)
#define MLMusicServiceStop ::LUMIN_MLSDK_API::MLMusicServiceStopShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServicePause)
#define MLMusicServicePause ::LUMIN_MLSDK_API::MLMusicServicePauseShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceResume)
#define MLMusicServiceResume ::LUMIN_MLSDK_API::MLMusicServiceResumeShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceSeek)
#define MLMusicServiceSeek ::LUMIN_MLSDK_API::MLMusicServiceSeekShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceNext)
#define MLMusicServiceNext ::LUMIN_MLSDK_API::MLMusicServiceNextShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServicePrevious)
#define MLMusicServicePrevious ::LUMIN_MLSDK_API::MLMusicServicePreviousShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceSetShuffle)
#define MLMusicServiceSetShuffle ::LUMIN_MLSDK_API::MLMusicServiceSetShuffleShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceSetRepeat)
#define MLMusicServiceSetRepeat ::LUMIN_MLSDK_API::MLMusicServiceSetRepeatShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceSetVolume)
#define MLMusicServiceSetVolume ::LUMIN_MLSDK_API::MLMusicServiceSetVolumeShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceGetTrackLength)
#define MLMusicServiceGetTrackLength ::LUMIN_MLSDK_API::MLMusicServiceGetTrackLengthShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceGetCurrentPosition)
#define MLMusicServiceGetCurrentPosition ::LUMIN_MLSDK_API::MLMusicServiceGetCurrentPositionShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceGetStatus)
#define MLMusicServiceGetStatus ::LUMIN_MLSDK_API::MLMusicServiceGetStatusShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceGetError)
#define MLMusicServiceGetError ::LUMIN_MLSDK_API::MLMusicServiceGetErrorShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceGetPlaybackState)
#define MLMusicServiceGetPlaybackState ::LUMIN_MLSDK_API::MLMusicServiceGetPlaybackStateShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceGetRepeatState)
#define MLMusicServiceGetRepeatState ::LUMIN_MLSDK_API::MLMusicServiceGetRepeatStateShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceGetShuffleState)
#define MLMusicServiceGetShuffleState ::LUMIN_MLSDK_API::MLMusicServiceGetShuffleStateShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceGetVolume)
#define MLMusicServiceGetVolume ::LUMIN_MLSDK_API::MLMusicServiceGetVolumeShim
CREATE_DEPRECATED_MSG_SHIM(ml_musicservice, MLResult, MLMusicServiceGetMetadata, "Replaced by MLMusicServiceGetMetadataForIndex.")
#define MLMusicServiceGetMetadata ::LUMIN_MLSDK_API::MLMusicServiceGetMetadataShim
CREATE_DEPRECATED_MSG_SHIM(ml_musicservice, MLResult, MLMusicServiceGetMetadataEx, "Replaced by MLMusicServiceGetMetadataForIndex.")
#define MLMusicServiceGetMetadataEx ::LUMIN_MLSDK_API::MLMusicServiceGetMetadataExShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceGetMetadataForIndex)
#define MLMusicServiceGetMetadataForIndex ::LUMIN_MLSDK_API::MLMusicServiceGetMetadataForIndexShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceReleaseMetadata)
#define MLMusicServiceReleaseMetadata ::LUMIN_MLSDK_API::MLMusicServiceReleaseMetadataShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
