// Copyright (c) 2016, Entropy Game Global Limited.
// All rights reserved.

#ifndef RAIL_SDK_RAIL_ROOM_H
#define RAIL_SDK_RAIL_ROOM_H

#include "rail/sdk/base/rail_component.h"
#include "rail/sdk/rail_room_define.h"

namespace rail {
#pragma pack(push, RAIL_SDK_PACKING)

// zone helper singleton
class IRailZoneHelper {
  public:
    // get zone list
    virtual RailResult AsyncGetZoneList(const RailString& user_data) = 0;

    // get room list, range[start_index, end_index)
    virtual RailResult AsyncGetRoomListInZone(uint64_t zone_id,
                        uint32_t start_index,
                        uint32_t end_index,
                        const RailArray<RoomInfoListSorter>& sorter,
                        const RailArray<RoomInfoListFilter>& filter,
                        const RailString& user_data) = 0;
};

class IRailRoom;
// room helper singleton
class IRailRoomHelper {
  public:
    IRailRoomHelper() : current_zone_id_(0) {}
    // set zone id
    virtual void set_current_zone_id(uint64_t zone_id) { current_zone_id_ = zone_id; }

    // get zone id
    virtual uint64_t get_current_zone_id() const { return current_zone_id_; }

    // create room directly
    virtual IRailRoom* CreateRoom(const RoomOptions& options,
                        const RailString& room_name,
                        RailResult* result) = 0;

    // async create room
    virtual IRailRoom* AsyncCreateRoom(const RoomOptions& options,
                        const RailString& room_name,
                        const RailString& user_data) = 0;

    // get a room object
    virtual IRailRoom* OpenRoom(uint64_t zone_id, uint64_t room_id, RailResult* result) = 0;

    virtual RailResult AsyncGetUserRoomList(const RailString& user_data) = 0;

  private:
    uint64_t current_zone_id_;
};

// room class
class IRailRoom : public IRailComponent {
  public:
    // get room id
    virtual uint64_t GetRoomId() = 0;
    // get room name
    virtual RailResult GetRoomName(RailString* name) = 0;

    // get zone id
    virtual uint64_t GetZoneId() = 0;

    // get owner id of room
    virtual RailID GetOwnerId() = 0;

    // get whether there has protection with password
    virtual RailResult GetHasPassword(bool* has_password) = 0;

    // get room type
    virtual EnumRoomType GetType() = 0;

    // set a new owner for the room
    virtual bool SetNewOwner(const RailID& new_owner_id) = 0;

    // async get all the members in the room
    virtual RailResult AsyncGetRoomMembers(const RailString& user_data) = 0;

    // leave room
    virtual void Leave() = 0;

    // join room with password, if no password,just input "".
    virtual RailResult AsyncJoinRoom(const rail::RailString& password,
                        const RailString& user_data) = 0;

    // get all data of the room
    virtual RailResult AsyncGetAllRoomData(const RailString& user_data) = 0;

    // kick one member out of the room
    virtual RailResult AsyncKickOffMember(const RailID& member_id, const RailString& user_data) = 0;

    // get the value of the room with key-value
    virtual bool GetRoomMetadata(const RailString& key, RailString* value) = 0;

    //  set the value of the room with key-value
    virtual bool SetRoomMetadata(const RailString& key, const RailString& value) = 0;

    // async set the values of the room with key-value
    virtual RailResult AsyncSetRoomMetadata(const RailArray<RailKeyValue>& key_values,
                        const RailString& user_data) = 0;

    // async get the values of the room with key-value
    virtual RailResult AsyncGetRoomMetadata(const RailArray<RailString>& keys,
                        const RailString& user_data) = 0;

    // async clear the values of the room with keys
    virtual RailResult AsyncClearRoomMetadata(const RailArray<RailString>& keys,
                        const RailString& user_data) = 0;

    // get the value of the member with key-value
    virtual bool GetMemberMetadata(const RailID& member_id,
                    const RailString& key,
                    RailString* value) = 0;

    // set the value of the member with key-value
    virtual bool SetMemberMetadata(const RailID& member_id,
                    const RailString& key,
                    const RailString& value) = 0;

    //  async get the values of the member with key-value
    virtual RailResult AsyncGetMemberMetadata(const RailID& member_id,
                        const RailArray<RailString>& keys,
                        const RailString& user_data) = 0;

    // async set the values of the member with key-value
    virtual RailResult AsyncSetMemberMetadata(const RailID& member_id,
                        const RailArray<RailKeyValue>& key_values,
                        const RailString& user_data) = 0;

    // send messages to other members in the room. Message_type is a
    // self-define param. It will broadcast to all members if remote_peer is 0.
    // *NOTE: this interface is in unreliable mode, and maximum of data_len is 1200 bytes.
    virtual RailResult SendDataToMember(const RailID& remote_peer,
                        const void* data_buf,
                        uint32_t data_len,
                        uint32_t message_type = 0) = 0;

    // get number of members in the room
    virtual uint32_t GetNumOfMembers() = 0;

    // get member by index
    virtual RailID GetMemberByIndex(uint32_t index) = 0;

    // get member's name by index
    virtual RailResult GetMemberNameByIndex(uint32_t index, RailString* name) = 0;

    // get max number of the room
    virtual uint32_t GetMaxMembers() = 0;

    // set game server rail id for room
    virtual bool SetGameServerID(uint64_t game_server_rail_id) = 0;

    // get game server rail id for room
    virtual bool GetGameServerID(uint64_t* game_server_rail_id) = 0;

    virtual bool SetRoomJoinable(bool is_joinable) = 0;

    virtual bool GetRoomJoinable() = 0;

    virtual RailResult GetFriendsInRoom(RailArray<RailID>* friend_ids) = 0;

    virtual bool IsUserInRoom(const RailID& user_rail_id) = 0;

    virtual RailResult EnableTeamVoice(bool enable) = 0;
};

#pragma pack(pop)
}  // namespace rail

#endif  // RAIL_SDK_RAIL_ROOM_H
