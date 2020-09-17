// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "Lumin/CAPIShims/LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_screens.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_screens, MLResult, MLScreensReleaseWatchHistoryThumbnail)
#define MLScreensReleaseWatchHistoryThumbnail ::LUMIN_MLSDK_API::MLScreensReleaseWatchHistoryThumbnailShim
CREATE_FUNCTION_SHIM(ml_screens, MLResult, MLScreensReleaseWatchHistoryList)
#define MLScreensReleaseWatchHistoryList ::LUMIN_MLSDK_API::MLScreensReleaseWatchHistoryListShim
CREATE_FUNCTION_SHIM(ml_screens, MLResult, MLScreensReleaseScreenInfoList)
#define MLScreensReleaseScreenInfoList ::LUMIN_MLSDK_API::MLScreensReleaseScreenInfoListShim
CREATE_FUNCTION_SHIM(ml_screens, MLResult, MLScreensReleaseScreenInfoListEx)
#define MLScreensReleaseScreenInfoListEx ::LUMIN_MLSDK_API::MLScreensReleaseScreenInfoListExShim
CREATE_FUNCTION_SHIM(ml_screens, MLResult, MLScreensInsertWatchHistoryEntry)
#define MLScreensInsertWatchHistoryEntry ::LUMIN_MLSDK_API::MLScreensInsertWatchHistoryEntryShim
CREATE_FUNCTION_SHIM(ml_screens, MLResult, MLScreensRemoveWatchHistoryEntry)
#define MLScreensRemoveWatchHistoryEntry ::LUMIN_MLSDK_API::MLScreensRemoveWatchHistoryEntryShim
CREATE_FUNCTION_SHIM(ml_screens, MLResult, MLScreensUpdateWatchHistoryEntry)
#define MLScreensUpdateWatchHistoryEntry ::LUMIN_MLSDK_API::MLScreensUpdateWatchHistoryEntryShim
CREATE_FUNCTION_SHIM(ml_screens, MLResult, MLScreensGetWatchHistoryList)
#define MLScreensGetWatchHistoryList ::LUMIN_MLSDK_API::MLScreensGetWatchHistoryListShim
CREATE_FUNCTION_SHIM(ml_screens, MLResult, MLScreensGetWatchHistoryThumbnail)
#define MLScreensGetWatchHistoryThumbnail ::LUMIN_MLSDK_API::MLScreensGetWatchHistoryThumbnailShim
CREATE_FUNCTION_SHIM(ml_screens, MLResult, MLScreensGetScreenInfo)
#define MLScreensGetScreenInfo ::LUMIN_MLSDK_API::MLScreensGetScreenInfoShim
CREATE_FUNCTION_SHIM(ml_screens, MLResult, MLScreensUpdateScreenInfo)
#define MLScreensUpdateScreenInfo ::LUMIN_MLSDK_API::MLScreensUpdateScreenInfoShim
CREATE_FUNCTION_SHIM(ml_screens, MLResult, MLScreensGetScreenInfoList)
#define MLScreensGetScreenInfoList ::LUMIN_MLSDK_API::MLScreensGetScreenInfoListShim
CREATE_FUNCTION_SHIM(ml_screens, MLResult, MLScreensGetScreenInfoListEx)
#define MLScreensGetScreenInfoListEx ::LUMIN_MLSDK_API::MLScreensGetScreenInfoListExShim
CREATE_FUNCTION_SHIM(ml_screens, MLResult, MLScreensCloseChannelAtScreen)
#define MLScreensCloseChannelAtScreen ::LUMIN_MLSDK_API::MLScreensCloseChannelAtScreenShim
CREATE_FUNCTION_SHIM(ml_screens, const char*, MLScreensGetResultString)
#define MLScreensGetResultString ::LUMIN_MLSDK_API::MLScreensGetResultStringShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
