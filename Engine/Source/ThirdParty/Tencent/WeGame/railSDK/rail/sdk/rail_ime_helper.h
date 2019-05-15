// Copyright (c) 2017, Entropy Game Global Limited.
// All rights reserved.

#ifndef RAIL_SDK_RAIL_IME_HELPER_H
#define RAIL_SDK_RAIL_IME_HELPER_H

#include "rail/sdk/base/rail_define.h"
#include "rail/sdk/rail_ime_helper_define.h"

namespace rail {
#pragma pack(push, RAIL_SDK_PACKING)

class IRailIMEHelper {
  public:
    //  when enable is true, SDK will handle the none-English text input, for example Chinese input,
    //  and the game should register the event of kRailEventIMEHelperTextInputSelectedResult,
    //  when player selects a text,SDK will pop up this event.
    virtual RailResult EnableIMEHelperTextInputWindow(bool enable,
                        const RailWindowPosition& position) = 0;

    //  set the position of CandidateList Window,
    //  only used when the parameter of enable is true in EnableIMEHelperTextInputWindow
    virtual RailResult UpdateIMEHelperTextInputWindowPosition(
                        const RailWindowPosition& position) = 0;
};

#pragma pack(pop)
}  // namespace rail

#endif  // RAIL_SDK_RAIL_UTILS_H
