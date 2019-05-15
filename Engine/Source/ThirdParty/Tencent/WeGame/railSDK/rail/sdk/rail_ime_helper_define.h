// Copyright (C) 2017, Entropy Game Global Limited.
// All rights reserved.

#ifndef RAIL_SDK_RAIL_IME_HELPER_DEFINE_H
#define RAIL_SDK_RAIL_IME_HELPER_DEFINE_H

#include "rail/sdk/rail_event.h"

namespace rail {
#pragma pack(push, RAIL_SDK_PACKING)

struct RailWindowPosition {
    RailWindowPosition() {
        position_left = 0;
        position_top = 0;
    }
    uint32_t position_left;  // x position relative to foreground window left
    uint32_t position_top;   // y position relative to foreground window top
};

namespace rail_event {

struct RailIMEHelperTextInputSelectedResult
    : public RailEvent<kRailEventIMEHelperTextInputSelectedResult> {
    RailIMEHelperTextInputSelectedResult() {
        result = kFailure;
        user_data = "";
    }

    RailString content;
};

}  // namespace rail_event

#pragma pack(pop)
}  // namespace rail

#endif  // RAIL_SDK_RAIL_UTILS_DEFINE_H
