// Copyright (c) 2016, Entropy Game Global Limited.
// All rights reserved.

#ifndef RAIL_SDK_RAIL_DLC_HELPER_H
#define RAIL_SDK_RAIL_DLC_HELPER_H

#include "rail/sdk/rail_dlc_define.h"

namespace rail {
#pragma pack(push, RAIL_SDK_PACKING)

class IRailDlcHelper {
  public:
    // Query Owned DLCs
    // event kRailEventDlcQueryIsOwnedDlcsResult will be triggered
    // if dlc_ids' size is 0 return all dlcs' ownership
    virtual RailResult AsyncQueryIsOwnedDlcsOnServer(const RailArray<RailDlcID>& dlc_ids,
                        const RailString& user_data) = 0;

    // -/////////////////////////////////////////-

    // trigger event when all dlcs' states are ready
    // Do not call IsDlcInstalled,IsOwnedDlc,GetDlcCount and GetDlcInfo
    // when dlcs' states are not ready
    virtual RailResult AsyncCheckAllDlcsStateReady(const RailString& user_data) = 0;

    // query the dlc installed or not, and retrieve installed path
    virtual bool IsDlcInstalled(RailDlcID dlc_id, RailString* installed_path = NULL) = 0;

    // query the dlc is owned or not
    virtual bool IsOwnedDlc(RailDlcID dlc_id) = 0;

    // all dlcs of game
    virtual uint32_t GetDlcCount() = 0;
    virtual bool GetDlcInfo(uint32_t index, RailDlcInfo* dlc_info) = 0;

    // -/////////////////////////////////////////-

    // AsyncInstallDlc will cause dlc files download and install.
    // First event kRailEventDlcInstallStartResult will be triggered
    // Then event kRailEventDlcInstallProgress will be triggered,
    // if the dlc files have been downloaded before,kRailEventDlcInstallProgress won't be triggered
    // Last event kRailEventAppsDlcInstallFinished will be triggered.
    // if the dlc is already installed,no event will be triggered.
    virtual bool AsyncInstallDlc(RailDlcID dlc_id, const RailString& user_data) = 0;

    // AsyncRemoveDlc will delete dlc files.
    // if the dlc has its own uninstall.exe, it will be executed when remove dlc.
    virtual bool AsyncRemoveDlc(RailDlcID dlc_id, const RailString& user_data) = 0;
};

#pragma pack(pop)
}  // namespace rail

#endif  // RAIL_SDK_RAIL_DLC_HELPER_H
