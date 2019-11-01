// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_music_service_common.h>
#include <ml_music_service.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceConnect)
#define MLMusicServiceConnect ::MLSDK_API::MLMusicServiceConnectShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceDisconnect)
#define MLMusicServiceDisconnect ::MLSDK_API::MLMusicServiceDisconnectShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceSetCallbacks)
#define MLMusicServiceSetCallbacks ::MLSDK_API::MLMusicServiceSetCallbacksShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceSetAuthString)
#define MLMusicServiceSetAuthString ::MLSDK_API::MLMusicServiceSetAuthStringShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceSetURL)
#define MLMusicServiceSetURL ::MLSDK_API::MLMusicServiceSetURLShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceSetPlayList)
#define MLMusicServiceSetPlayList ::MLSDK_API::MLMusicServiceSetPlayListShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceStart)
#define MLMusicServiceStart ::MLSDK_API::MLMusicServiceStartShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceStop)
#define MLMusicServiceStop ::MLSDK_API::MLMusicServiceStopShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServicePause)
#define MLMusicServicePause ::MLSDK_API::MLMusicServicePauseShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceResume)
#define MLMusicServiceResume ::MLSDK_API::MLMusicServiceResumeShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceSeek)
#define MLMusicServiceSeek ::MLSDK_API::MLMusicServiceSeekShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceNext)
#define MLMusicServiceNext ::MLSDK_API::MLMusicServiceNextShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServicePrevious)
#define MLMusicServicePrevious ::MLSDK_API::MLMusicServicePreviousShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceSetShuffle)
#define MLMusicServiceSetShuffle ::MLSDK_API::MLMusicServiceSetShuffleShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceSetRepeat)
#define MLMusicServiceSetRepeat ::MLSDK_API::MLMusicServiceSetRepeatShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceSetVolume)
#define MLMusicServiceSetVolume ::MLSDK_API::MLMusicServiceSetVolumeShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceGetTrackLength)
#define MLMusicServiceGetTrackLength ::MLSDK_API::MLMusicServiceGetTrackLengthShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceGetCurrentPosition)
#define MLMusicServiceGetCurrentPosition ::MLSDK_API::MLMusicServiceGetCurrentPositionShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceGetStatus)
#define MLMusicServiceGetStatus ::MLSDK_API::MLMusicServiceGetStatusShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceGetError)
#define MLMusicServiceGetError ::MLSDK_API::MLMusicServiceGetErrorShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceGetPlaybackState)
#define MLMusicServiceGetPlaybackState ::MLSDK_API::MLMusicServiceGetPlaybackStateShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceGetRepeatState)
#define MLMusicServiceGetRepeatState ::MLSDK_API::MLMusicServiceGetRepeatStateShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceGetShuffleState)
#define MLMusicServiceGetShuffleState ::MLSDK_API::MLMusicServiceGetShuffleStateShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceGetVolume)
#define MLMusicServiceGetVolume ::MLSDK_API::MLMusicServiceGetVolumeShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceGetMetadata)
#define MLMusicServiceGetMetadata ::MLSDK_API::MLMusicServiceGetMetadataShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceGetMetadataEx)
#define MLMusicServiceGetMetadataEx ::MLSDK_API::MLMusicServiceGetMetadataExShim
CREATE_FUNCTION_SHIM(ml_musicservice, MLResult, MLMusicServiceReleaseMetadata)
#define MLMusicServiceReleaseMetadata ::MLSDK_API::MLMusicServiceReleaseMetadataShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
