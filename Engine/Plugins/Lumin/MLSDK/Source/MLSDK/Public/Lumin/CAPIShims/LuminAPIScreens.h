// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_screens.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_screens, MLResult, MLScreensReleaseWatchHistoryThumbnail)
#define MLScreensReleaseWatchHistoryThumbnail ::MLSDK_API::MLScreensReleaseWatchHistoryThumbnailShim
CREATE_FUNCTION_SHIM(ml_screens, MLResult, MLScreensReleaseWatchHistoryList)
#define MLScreensReleaseWatchHistoryList ::MLSDK_API::MLScreensReleaseWatchHistoryListShim
CREATE_DEPRECATED_MSG_SHIM(ml_screens, MLResult, MLScreensReleaseScreenInfoList, "Replaced by MLScreensReleaseScreenInfoListEx.")
#define MLScreensReleaseScreenInfoList ::MLSDK_API::MLScreensReleaseScreenInfoListShim
CREATE_FUNCTION_SHIM(ml_screens, MLResult, MLScreensReleaseScreenInfoListEx)
#define MLScreensReleaseScreenInfoListEx ::MLSDK_API::MLScreensReleaseScreenInfoListExShim
CREATE_FUNCTION_SHIM(ml_screens, MLResult, MLScreensInsertWatchHistoryEntry)
#define MLScreensInsertWatchHistoryEntry ::MLSDK_API::MLScreensInsertWatchHistoryEntryShim
CREATE_FUNCTION_SHIM(ml_screens, MLResult, MLScreensRemoveWatchHistoryEntry)
#define MLScreensRemoveWatchHistoryEntry ::MLSDK_API::MLScreensRemoveWatchHistoryEntryShim
CREATE_FUNCTION_SHIM(ml_screens, MLResult, MLScreensUpdateWatchHistoryEntry)
#define MLScreensUpdateWatchHistoryEntry ::MLSDK_API::MLScreensUpdateWatchHistoryEntryShim
CREATE_FUNCTION_SHIM(ml_screens, MLResult, MLScreensGetWatchHistoryList)
#define MLScreensGetWatchHistoryList ::MLSDK_API::MLScreensGetWatchHistoryListShim
CREATE_FUNCTION_SHIM(ml_screens, MLResult, MLScreensGetWatchHistoryThumbnail)
#define MLScreensGetWatchHistoryThumbnail ::MLSDK_API::MLScreensGetWatchHistoryThumbnailShim
CREATE_FUNCTION_SHIM(ml_screens, MLResult, MLScreensGetScreenInfo)
#define MLScreensGetScreenInfo ::MLSDK_API::MLScreensGetScreenInfoShim
CREATE_FUNCTION_SHIM(ml_screens, MLResult, MLScreensUpdateScreenInfo)
#define MLScreensUpdateScreenInfo ::MLSDK_API::MLScreensUpdateScreenInfoShim
CREATE_DEPRECATED_MSG_SHIM(ml_screens, MLResult, MLScreensGetScreenInfoList, "Replaced by MLScreensGetScreenInfoListEx.")
#define MLScreensGetScreenInfoList ::MLSDK_API::MLScreensGetScreenInfoListShim
CREATE_FUNCTION_SHIM(ml_screens, MLResult, MLScreensGetScreenInfoListEx)
#define MLScreensGetScreenInfoListEx ::MLSDK_API::MLScreensGetScreenInfoListExShim
CREATE_FUNCTION_SHIM(ml_screens, MLResult, MLScreensCloseChannelAtScreen)
#define MLScreensCloseChannelAtScreen ::MLSDK_API::MLScreensCloseChannelAtScreenShim
CREATE_FUNCTION_SHIM(ml_screens, const char*, MLScreensGetResultString)
#define MLScreensGetResultString ::MLSDK_API::MLScreensGetResultStringShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
