// Copyright (c) 2018, Entropy Game Global Limited.
// All rights reserved.
// Rail Small Object Service

#ifndef RAIL_SDK_RAIL_SMALL_OBJECT_SERVICE_DEFINE
#define RAIL_SDK_RAIL_SMALL_OBJECT_SERVICE_DEFINE

#include "rail/sdk/base/rail_define.h"
#include "rail/sdk/rail_result.h"
#include "rail/sdk/rail_event.h"

namespace rail {

enum EnumRailSmallObjectUpdateState {
    kRailSmallObjectUnknwonState = 0,
    kRailSmallObjectNotExist = 1,
    kRailSmallObjectDownloading = 2,
    kRailSmallObjectNeedUpdate = 3,
    kRailSmallObjectUpToDate = 4,
};

struct RailSmallObjectState {
    RailSmallObjectState() {
        update_state = kRailSmallObjectUnknwonState;
        index = 0;
    }
    EnumRailSmallObjectUpdateState update_state;
    uint32_t index;
};

struct RailSmallObjectDownloadInfo {
    RailSmallObjectDownloadInfo() {
        index = 0;
        result = kFailure;
    }

    uint32_t index;
    RailResult result;
};

namespace rail_event {
struct RailSmallObjectStateQueryResult
    : public RailEvent<kRailEventSmallObjectServiceQueryObjectStateResult> {
    RailArray<RailSmallObjectState> objects_state;
};

struct RailSmallObjectDownloadResult
    : public RailEvent<kRailEventSmallObjectServiceDownloadResult> {
    RailArray<RailSmallObjectDownloadInfo> download_infos;
};
};  // namespace rail_event
};  // namespace rail

#endif  // RAIL_SDK_RAIL_SMALL_OBJECT_SERVICE_DEFINE