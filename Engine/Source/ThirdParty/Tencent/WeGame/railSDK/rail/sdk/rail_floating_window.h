// Copyright (c) 2016, Entropy Game Global Limited.
// All rights reserved.

#ifndef RAIL_SDK_RAIL_FLOATING_WINDOW_H
#define RAIL_SDK_RAIL_FLOATING_WINDOW_H

#include "rail/sdk/rail_floating_window_define.h"

namespace rail {
#pragma pack(push, RAIL_SDK_PACKING)

class IRailFloatingWindow {
  public:
    // display the Rail platform friends list window in the game.
    // users need to register and will received callback data is ShowFloatingWindowResult
    // when the window displayed or closed.
    virtual RailResult AsyncShowRailFloatingWindow(EnumRailWindowType window_type,
                        const RailString& user_data) = 0;

    virtual RailResult AsyncCloseRailFloatingWindow(EnumRailWindowType window_type,
                        const RailString& user_data) = 0;

    virtual RailResult SetNotifyWindowPosition(EnumRailNotifyWindowType window_type,
                        const RailWindowLayout& layout) = 0;

    // show the game or dlc store window in the game.
    // id can be GameID and DlcId
    // users need to register and will received callback data is ShowFloatingWindowResult
    // it's EnumRailWindowType is kRailWindowPurchaseProduct
    // when the window displayed or closed.
    virtual RailResult AsyncShowStoreWindow(const uint64_t& id,
                        const RailStoreOptions& options,
                        const RailString& user_data) = 0;

    // if the floating window is available, the user can access friends list window, store window
    // and other floating window in game process.
    virtual bool IsFloatingWindowAvailable() = 0;

    // show the game default store window in the game.
    // users need to register and will received callback data is ShowFloatingWindowResult
    // it's EnumRailWindowType is kRailWindowPurchaseProduct
    // when the window displayed or closed.
    virtual RailResult AsyncShowDefaultGameStoreWindow(const RailString& user_data) = 0;

    // set whether show the notify window or not. If you want show some notify message by yourself,
    // you could disable these notify window. A ShowNotifyWindow callback will be fired while
    // some system message should notify to the current player.
    virtual RailResult SetNotifyWindowEnable(EnumRailNotifyWindowType window_type, bool enable) = 0;
};

#pragma pack(pop)
}  // namespace rail

#endif  // RAIL_SDK_RAIL_FLOATING_WINDOW_H
