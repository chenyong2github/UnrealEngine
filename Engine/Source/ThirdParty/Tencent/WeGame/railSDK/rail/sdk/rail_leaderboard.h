// Copyright (c) 2016, Entropy Game Global Limited.
// All rights reserved.

#ifndef RAIL_SDK_RAIL_LEADERBOARD_H
#define RAIL_SDK_RAIL_LEADERBOARD_H

#include "rail/sdk/base/rail_component.h"
#include "rail/sdk/rail_user_space_define.h"

namespace rail {
#pragma pack(push, RAIL_SDK_PACKING)

enum LeaderboardType {
    kLeaderboardUnknown = 0,
    kLeaderboardAllZone = 1,
    kLeaderboardMyZone = 2,
    kLeaderboardMyServer = 3,
    kLeaderboardFriends = 4,
};

enum LeaderboardUploadType {
    kLeaderboardUploadInvalid = 0,
    kLeaderboardUploadRewrite = 1,     // rewrite unconditionally
    kLeaderboardUploadChooseBest = 2,  // choose the best score
};

enum LeaderboardSortType {
    kLeaderboardSortTypeNone = 0,
    kLeaderboardSortTypeAsc = 1,   // ascending
    kLeaderboardSortTypeDesc = 2,  // descending
};

enum LeaderboardDisplayType {
    kLeaderboardDisplayTypeNone = 0,
    kLeaderboardDisplayTypeDouble = 1,
    kLeaderboardDisplayTypeSeconds = 2,
    kLeaderboardDisplayTypeMilliSeconds = 3,
};

struct LeaderboardParameters {
    LeaderboardParameters() {}

    RailString param;  // JSON format, configured at back-end server
};

struct RequestLeaderboardEntryParam {
    RequestLeaderboardEntryParam() {
        type = kLeaderboardUnknown;
        range_start = 0;
        range_end = 0;
        user_coordinate = false;
    }

    LeaderboardType type;
    int32_t range_start;
    // normally, range_end >= range_start. set range_end to -1 via request to the last one.
    int32_t range_end;
    bool user_coordinate;
};

struct LeaderboardData {
    LeaderboardData() {
        score = 0.0;
        rank = 0;
    }

    double score;
    int32_t rank;
    SpaceWorkID spacework_id;
    RailString additional_infomation;
};

struct LeaderboardEntry {
    LeaderboardEntry() { player_id = 0; }

    RailID player_id;
    LeaderboardData data;
};

struct UploadLeaderboardParam {
    UploadLeaderboardParam() { type = kLeaderboardUploadInvalid; }

    LeaderboardUploadType type;
    LeaderboardData data;
};

class IRailLeaderboardEntries;
class IRailLeaderboard;

class IRailLeaderboardHelper {
  public:
    virtual ~IRailLeaderboardHelper() {}

    virtual IRailLeaderboard* OpenLeaderboard(const RailString& leaderboard_name) = 0;

    // trigger event LeaderboardCreated
    virtual IRailLeaderboard* AsyncCreateLeaderboard(const RailString& leaderboard_name,
                                LeaderboardSortType sort_type,
                                LeaderboardDisplayType display_type,
                                const RailString& user_data,
                                RailResult* result) = 0;
};

class IRailLeaderboard : public IRailComponent {
  public:
    virtual RailString GetLeaderboardName() = 0;

    virtual int32_t GetTotalEntriesCount() = 0;

    // trigger event LeaderboardReceived
    virtual RailResult AsyncGetLeaderboard(const RailString& user_data) = 0;

    virtual RailResult GetLeaderboardParameters(LeaderboardParameters* param) = 0;

    virtual IRailLeaderboardEntries* CreateLeaderboardEntries() = 0;

    // trigger event LeaderboardUploaded
    // don't support to attach spacework_id with uploaded score
    // you should call AsyncAttachSpaceWork to update the attached spacework_id
    virtual RailResult AsyncUploadLeaderboard(const UploadLeaderboardParam& update_param,
                        const RailString& user_data) = 0;

    virtual RailResult GetLeaderboardSortType(LeaderboardSortType* sort_type) = 0;

    virtual RailResult GetLeaderboardDisplayType(LeaderboardDisplayType* display_type) = 0;

    // trigger event LeaderboardAttachSpaceWork
    // only supports one spacework_id attached to the leaderboard
    // the new spacework_id will replace the old one
    virtual RailResult AsyncAttachSpaceWork(SpaceWorkID spacework_id,
                        const RailString& user_data) = 0;
};

class IRailLeaderboardEntries : public IRailComponent {
  public:
    virtual RailID GetRailID() = 0;

    virtual RailString GetLeaderboardName() = 0;

    // range_start could be little than zero.
    // if player is not zero, when param.user_coordinate equals true,
    // the actual range is [player_pos + range_start, player_pos + range_end].
    // otherwise, the actual range is [max(1, player_pos) + range_start, max(0, player_pos) +
    // range_end]. for example: if player_pos is 6, range_start is -2, range_end is 2, then the
    // actual range is [4, 8]. if player is RailID(0), range_start is -2, range_end is 2, then the
    // actual range is [1, 2] trigger event LeaderboardEntryReceived
    virtual RailResult AsyncRequestLeaderboardEntries(const RailID& player,
                        const RequestLeaderboardEntryParam& param,
                        const RailString& user_data) = 0;

    virtual RequestLeaderboardEntryParam GetEntriesParam() = 0;

    virtual int32_t GetEntriesCount() = 0;

    // index's range is [0, entries_count)
    virtual RailResult GetLeaderboardEntry(int32_t index, LeaderboardEntry* leaderboard_entry) = 0;
};

namespace rail_event {

struct LeaderboardReceived : public RailEvent<kRailEventLeaderboardReceived> {
    LeaderboardReceived() { does_exist = false; }

    RailString leaderboard_name;
    bool does_exist;
};

struct LeaderboardCreated : public RailEvent<kRailEventLeaderboardAsyncCreated> {
    LeaderboardCreated() {}

    RailString leaderboard_name;
};

struct LeaderboardEntryReceived : public RailEvent<kRailEventLeaderboardEntryReceived> {
    LeaderboardEntryReceived() {}

    RailString leaderboard_name;
};

struct LeaderboardUploaded : public RailEvent<kRailEventLeaderboardUploaded> {
    LeaderboardUploaded() {
        score = 0.0;
        better_score = false;
        new_rank = 0;
        old_rank = 0;
    }

    RailString leaderboard_name;
    double score;
    bool better_score;
    int32_t new_rank;
    int32_t old_rank;
};

struct LeaderboardAttachSpaceWork : public RailEvent<kRailEventLeaderboardAttachSpaceWork> {
    LeaderboardAttachSpaceWork() {}

    RailString leaderboard_name;
    SpaceWorkID spacework_id;
};

}  // namespace rail_event

#pragma pack(pop)
}  // namespace rail

#endif  // RAIL_SDK_RAIL_LEADERBOARD_H
