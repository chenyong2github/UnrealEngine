// Copyright (c) 2016, Entropy Game Global Limited.
// All rights reserved.

#ifndef RAIL_SDK_RAIL_FRIENDS_H
#define RAIL_SDK_RAIL_FRIENDS_H

#include "rail/sdk/base/rail_component.h"
#include "rail/sdk/rail_friends_define.h"

namespace rail {
#pragma pack(push, RAIL_SDK_PACKING)

class IRailFriends {
  public:
    // get friends' information on the platform asynchronously,
    // such as nickname, URL address of avatar, and on-line status.
    // the user needs to register and will receive callback data is RailUsersInfoData.
    virtual RailResult AsyncGetPersonalInfo(const RailArray<RailID>& rail_ids,
                        const RailString& user_data) = 0;

    // get yours and your friend's metadata information asynchronously.
    // the user need to register and will receive call back data is RailFriendsGetMetadataResult.
    virtual RailResult AsyncGetFriendMetadata(const RailID& rail_id,
                        const RailArray<RailString>& keys,
                        const RailString& user_data) = 0;

    // configure the metadata information for yourself by batch synchronously.
    // the maximum number of keys for each metadata unit is kRailCommonMaxRepeatedKeys.
    // the maximum length for a key is kRailCommonMaxKeyLength.
    // the maximum length for a value is kRailCommonMaxValueLength.
    // the callback event is RailFriendsSetMetadataResult.
    virtual RailResult AsyncSetMyMetadata(const RailArray<RailKeyValue>& key_values,
                        const RailString& user_data) = 0;

    // clear all your metadata key-values.
    // the call-back event notification data is RailFriendsClearMetadataResult.
    virtual RailResult AsyncClearAllMyMetadata(const RailString& user_data) = 0;

    // developers can call this method to use a command line to start the friend's game,
    // in case the friend who has accepted the invitation and has not launched the game yet.
    // the callback event is RailFriendsSetMetadataResult.
    virtual RailResult AsyncSetInviteCommandLine(const RailString& command_line,
                        const RailString& user_data) = 0;

    // get the start command line set up by your friend asynchronously,
    // the call-back event notification data is RailFriendsGetInviteCommandLine.
    virtual RailResult AsyncGetInviteCommandLine(const RailID& rail_id,
                        const RailString& user_data) = 0;

    // report the player's information who played with you recently.
    // the callback event is RailFriendsReportPlayedWithUserListResult.
    virtual RailResult AsyncReportPlayedWithUserList(
                        const RailArray<RailUserPlayedWith>& player_list,
                        const RailString& user_data) = 0;

    // get the friends list of current player. Once the game client is start, we will auto
    // update friends list. If this process is done, you will receive a RailFriendsListChanged
    // callback. Or this interface will return a kErrorFriendsServerBusy result.
    virtual RailResult GetFriendsList(RailArray<RailFriendInfo>* friends_list) = 0;

    virtual RailResult AsyncQueryFriendPlayedGamesInfo(const RailID& rail_id,
                        const RailString& user_data) = 0;

    virtual RailResult AsyncQueryPlayedWithFriendsList(const RailString& user_data) = 0;

    virtual RailResult AsyncQueryPlayedWithFriendsTime(const RailArray<RailID>& rail_ids,
                        const RailString& user_data) = 0;

    virtual RailResult AsyncQueryPlayedWithFriendsGames(const RailArray<RailID>& rail_ids,
                        const RailString& user_data) = 0;

    // The callback event notification data is RailFriendsAddFriendResult.
    // The callback is triggered when the request sent successfully or failed.
    // There will be a RailFriendsBuddyListChanged callback if the other player become your friend.
    // You could then refresh the friend list after receive RailFriendsBuddyListChanged.
    virtual RailResult AsyncAddFriend(const RailFriendsAddFriendRequest& request,
                        const RailString& user_data) = 0;

    // update friends list as you need. We will query friends list again.
    // the callback event is RailFriendsListChanged.
    virtual RailResult AsyncUpdateFriendsData(const RailString& user_data) = 0;
};

#pragma pack(pop)
}  // namespace rail

#endif  // RAIL_SDK_RAIL_FRIENDS_H
