// Copyright (c) 2016, Entropy Game Global Limited.
// All rights reserved.

#ifndef RAIL_SDK_RAIL_ROOM_DEFINE_H
#define RAIL_SDK_RAIL_ROOM_DEFINE_H

#include "rail/sdk/base/rail_array.h"
#include "rail/sdk/rail_event.h"

namespace rail {
#pragma pack(push, RAIL_SDK_PACKING)

const int32_t RAIL_DEFAULT_MAX_ROOM_MEMBERS = 2;
// the type of room
enum EnumRoomType {
    kRailRoomTypePrivate = 0x0000,
    kRailRoomTypeWithFriends = 0x0001,
    kRailRoomTypePublic = 0x0002,
    kRailRoomTypeHidden = 0x0003,
};

// the status of room
enum EnumRoomStatus {
    kRailRoomStatusFree = 0,
    kRailRoomStatusFull = 1,
};

// the status of zone
enum EnumZoneStatus {
    kRailZoneStatusSmooth = 0,
    kRailZoneStatusNormal = 1,
    kRailZoneStatusBusy = 2,
    kRailZoneStatusFull = 3,
};

// the reason for leaving room
enum EnumLeaveRoomReason {
    kLeaveRoomReasonActive = 1,
    kLeaveRoomReasonTimeout = 2,
    kLeaveRoomReasonKick = 3,
};

// member status changed
enum EnumRoomMemberActionStatus {
    kMemberEnteredRoom = 0x0001,
    kMemberLeftRoom = 0x0002,
    kMemberDisconnectServer = 0x0004,
};

// the reason for owner changed
enum EnumRoomOwnerChangeReason {
    kRoomOwnerActiveChange = 1,
    kRoomOwnerLeave = 2,
};

struct RoomOptions {
    explicit RoomOptions(uint64_t zone) {
        type = kRailRoomTypePublic;
        max_members = RAIL_DEFAULT_MAX_ROOM_MEMBERS;
        zone_id = zone;
        enable_team_voice = true;
    }

    EnumRoomType type;
    uint32_t max_members;
    uint64_t zone_id;
    RailString password;
    bool enable_team_voice;
};

struct RoomInfoListSorter {
    RoomInfoListSorter() {
        property_value_type = kRailPropertyValueTypeString;
        property_sort_type = kRailSortTypeAsc;
        close_to_value = 0;
    }

    EnumRailPropertyValueType property_value_type;
    EnumRailSortType property_sort_type;
    RailString property_key;
    double close_to_value;  // this value is valid when property_sort_type == kRailSortTypeCloseTo
};

struct RoomInfoListFilterKey {
    RoomInfoListFilterKey() {
        value_type = kRailPropertyValueTypeString;
        comparison_type = kRailComparisonTypeEqualToOrLessThan;
    }

    RailString key_name;                   // filter key name
    EnumRailPropertyValueType value_type;  // value of 'key'(indicated by key_name), type of value
    // comparison type between value( value of 'key') and filter_value
    EnumRailComparisonType comparison_type;
    RailString filter_value;  // user define filter value
};

struct RoomInfoListFilter {
    RoomInfoListFilter() {
        filter_password = kRailOptionalAny;
        filter_friends_owned = kRailOptionalAny;
        available_slot_at_least = 0;
    }

    // all filters below are logic AND relationship
    // example:
    // filters AND
    //
    // filter_room_name(if room_name_contained is not empty) AND
    //
    // has_password_only(if filter_password = kRailOptionalYes) AND
    // not_has_password(if filter_password = kRailOptionalNo) AND
    // not_care_whether_password(if filter_password = kRailOptionalAny) AND
    //
    // friends_owned_only(if filter_friends_owned = kRailOptionalYes) AND
    // not_friends_owned(if filter_friends_owned = kRailOptionalNo) AND
    // not_care_whether_friends_owned(if filter_friends_owned = kRailOptionalAny) AND
    //
    // available_slot_at_least

    // user define filter condition
    RailArray<RoomInfoListFilterKey> key_filters;  // filter by all conditions in key_filters array
    // filters[0] AND filters[1] AND ... AND filters[N]
    RailString room_name_contained;
    // filter rooms whether have password or not
    EnumRailOptionalValue filter_password;
    // filter rooms whether created by friends or not
    EnumRailOptionalValue filter_friends_owned;
    uint32_t available_slot_at_least;
};

struct ZoneInfo {
    ZoneInfo() {
        zone_id = 0;
        idc_id = 0;
        country_code = 0;
        status = kRailZoneStatusNormal;
    }
    uint64_t zone_id;
    uint64_t idc_id;
    uint32_t country_code;
    EnumZoneStatus status;
    RailString name;
    RailString description;
};

struct RoomInfo {
    RoomInfo() {
        zone_id = 0;
        room_id = 0;
        owner_id = 0;
        room_state = kRailRoomStatusFree;
        max_members = 0;
        current_members = 0;
        create_time = 0;
        has_password = false;
        is_joinable = true;
        type = kRailRoomTypePrivate;
        game_server_rail_id = 0;
    }

    uint64_t zone_id;
    uint64_t room_id;
    RailID owner_id;
    EnumRoomStatus room_state;
    uint32_t max_members;
    uint32_t current_members;
    uint32_t create_time;
    RailString room_name;
    bool has_password;
    bool is_joinable;
    EnumRoomType type;
    uint64_t game_server_rail_id;
    RailArray<RailKeyValue> room_kvs;
};

struct MemberInfo {
    MemberInfo() {
        room_id = 0;
        member_id = 0;
        member_index = 0;
    }

    uint64_t room_id;
    uint64_t member_id;
    uint32_t member_index;
    RailString member_name;
};

namespace rail_event {

// response for the request of zone info
struct ZoneInfoList : public RailEvent<kRailEventRoomZoneListResult> {
    ZoneInfoList() {}

    RailArray<ZoneInfo> zone_info;
};

// request for room info list
struct RoomInfoList : public RailEvent<kRailEventRoomListResult> {
    RoomInfoList() {
        zone_id = 0;
        begin_index = 0;
        end_index = 0;
        total_room_num_in_zone = 0;
    }

    uint64_t zone_id;
    uint32_t begin_index;
    uint32_t end_index;
    uint32_t total_room_num_in_zone;
    RailArray<RoomInfo> room_info;
};

struct RoomAllData : public RailEvent<kRailEventRoomGetAllDataResult> {
    RoomAllData() {}

    RoomInfo room_info;
};

// request for creating room info
struct CreateRoomInfo : public RailEvent<kRailEventRoomCreated> {
    CreateRoomInfo() {
        zone_id = 0;
        room_id = 0;
    }

    uint64_t zone_id;
    uint64_t room_id;
};

struct RoomMembersInfo : public RailEvent<kRailEventRoomGotRoomMembers> {
    RoomMembersInfo() {
        room_id = 0;
        member_num = 0;
    }

    uint64_t room_id;
    uint32_t member_num;
    RailArray<MemberInfo> member_info;
};

// info for joining a room
struct JoinRoomInfo : public RailEvent<kRailEventRoomJoinRoomResult> {
    JoinRoomInfo() {
        room_id = 0;
        zone_id = 0;
    }

    uint64_t zone_id;
    uint64_t room_id;
};

// info for kick member
struct KickOffMemberInfo : public RailEvent<kRailEventRoomKickOffMemberResult> {
    KickOffMemberInfo() {
        room_id = 0;
        kicked_id = 0;
    }

    uint64_t room_id;
    uint64_t kicked_id;
};

struct SetRoomMetadataInfo : public RailEvent<kRailEventRoomSetRoomMetadataResult> {
    SetRoomMetadataInfo() { room_id = 0; }

    uint64_t room_id;
};

struct GetRoomMetadataInfo : public RailEvent<kRailEventRoomGetRoomMetadataResult> {
    GetRoomMetadataInfo() { room_id = 0; }

    uint64_t room_id;
    RailArray<RailKeyValue> key_value;
};

struct ClearRoomMetadataInfo : public RailEvent<kRailEventRoomClearRoomMetadataResult> {
    ClearRoomMetadataInfo() { room_id = 0; }

    uint64_t room_id;
};

struct GetMemberMetadataInfo : public RailEvent<kRailEventRoomGetMemberMetadataResult> {
    GetMemberMetadataInfo() {
        room_id = 0;
        member_id = 0;
    }

    uint64_t room_id;
    uint64_t member_id;
    RailArray<RailKeyValue> key_value;
};

struct SetMemberMetadataInfo : public RailEvent<kRailEventRoomSetMemberMetadataResult> {
    SetMemberMetadataInfo() {
        room_id = 0;
        member_id = 0;
    }

    uint64_t room_id;
    uint64_t member_id;
};

struct LeaveRoomInfo : public RailEvent<kRailEventRoomLeaveRoomResult> {
    LeaveRoomInfo() {
        room_id = 0;
        reason = kLeaveRoomReasonActive;
    }

    uint64_t room_id;
    EnumLeaveRoomReason reason;
};

struct UserRoomListInfo : public RailEvent<kRailEventRoomGetUserRoomListResult> {
    UserRoomListInfo() {}

    RailArray<RoomInfo> room_info;
};

// info of room property changed
struct NotifyMetadataChange : public RailEvent<kRailEventRoomNotifyMetadataChanged> {
    NotifyMetadataChange() {
        room_id = 0;
        changer_id = 0;
    }

    uint64_t room_id;
    uint64_t changer_id;
};

// info of room member changed
struct NotifyRoomMemberChange : public RailEvent<kRailEventRoomNotifyMemberChanged> {
    NotifyRoomMemberChange() {
        room_id = 0;
        changer_id = 0;
        id_for_making_change = 0;
        state_change = kMemberEnteredRoom;
    }

    uint64_t room_id;
    uint64_t changer_id;
    uint64_t id_for_making_change;
    EnumRoomMemberActionStatus state_change;
};

// info of member being kicked
struct NotifyRoomMemberKicked : public RailEvent<kRailEventRoomNotifyMemberkicked> {
    NotifyRoomMemberKicked() {
        room_id = 0;
        id_for_making_kick = 0;
        kicked_id = 0;
        due_to_kicker_lost_connect = 0;
    }

    uint64_t room_id;
    uint64_t id_for_making_kick;
    uint64_t kicked_id;
    uint32_t due_to_kicker_lost_connect;
};

// info of room destroyed
struct NotifyRoomDestroy : public RailEvent<kRailEventRoomNotifyRoomDestroyed> {
    NotifyRoomDestroy() { room_id = 0; }

    uint64_t room_id;
};

struct RoomDataReceived : public RailEvent<kRailEventRoomNotifyRoomDataReceived> {
    RoomDataReceived() {
        remote_peer = 0;
        message_type = 0;
        data_len = 0;
    }

    RailID remote_peer;
    uint32_t message_type;
    uint32_t data_len;
    RailString data_buf;
};

// info of room owner changed
struct NotifyRoomOwnerChange : public RailEvent<kRailEventRoomNotifyRoomOwnerChanged> {
    NotifyRoomOwnerChange() {
        room_id = 0;
        old_owner_id = 0;
        new_owner_id = 0;
        reason = kRoomOwnerActiveChange;
    }
    uint64_t room_id;
    uint64_t old_owner_id;
    uint64_t new_owner_id;
    EnumRoomOwnerChangeReason reason;
};

// info of room game server changed
struct NotifyRoomGameServerChange : public RailEvent<kRailEventRoomNotifyRoomGameServerChanged> {
    NotifyRoomGameServerChange() {
        room_id = 0;
        game_server_rail_id = 0;
        game_server_channel_id = 0;
    }

    uint64_t room_id;
    uint64_t game_server_rail_id;
    uint64_t game_server_channel_id;
};

}  // namespace rail_event
#pragma pack(pop)

}  // namespace rail

#endif  // RAIL_SDK_RAIL_ROOM_DEFINE_H
