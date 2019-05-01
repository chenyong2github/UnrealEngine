// Copyright (c) 2016, Entropy Game Global Limited.
// All rights reserved.

#ifndef RAIL_SDK_RAIL_APPS_H
#define RAIL_SDK_RAIL_APPS_H

#include "rail/sdk/rail_game_define.h"

namespace rail {
#pragma pack(push, RAIL_SDK_PACKING)

class IRailApps {
  public:
    // check if the specified game is installed
    virtual bool IsGameInstalled(const RailGameID& game_id) = 0;

    // check if the specified game is subscribed, callback is QuerySubscribeWishPlayState
    virtual RailResult AsyncQuerySubscribeWishPlayState(const RailGameID& game_id,
                        const RailString& user_data) = 0;
};

#pragma pack(pop)
}  // namespace rail

#endif  // RAIL_SDK_RAIL_APPS_H
