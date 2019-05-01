// Copyright (c) 2016, Entropy Game Global Limited.
// All rights reserved.

#ifndef RAIL_SDK_RAIL_NETWORK_H
#define RAIL_SDK_RAIL_NETWORK_H

#include "rail/sdk/rail_network_define.h"

namespace rail {
#pragma pack(push, RAIL_SDK_PACKING)

// IRailNetwork is a network function that supports P2P communication.
class IRailNetwork {
  public:
    // Accept a network session request from a remote player. When a player wants to establish
    // a network session with you, you will receive a CreateSessionRequest callback event,
    // which tells you the remote player's rail_id and the local rail_id. Only after calling the
    // AcceptSessionRequest function, can you establish a network session with the other player.
    virtual RailResult AcceptSessionRequest(const RailID& local_peer,
                        const RailID& remote_peer) = 0;

    // Send data to the specified player. If you want to create a network session with a player,
    // you can directly call the SendData interface to send data to the player. If the request to
    // establish a network session with the other player failed, you will receive a
    // CreateSessionFailed callback event.
    // *NOTE: this api is in unreliable mode, and maximum of data_len is 1200 bytes.
    virtual RailResult SendData(const RailID& local_peer,
                        const RailID& remote_peer,
                        const void* data_buf,
                        uint32_t data_len,
                        uint32_t message_type = 0) = 0;

    // Send data in reliable mode, the maximum of data_len is 1M byte.
    // The CloseSession API should be called after communication end.
    virtual RailResult SendReliableData(const RailID& local_peer,
                        const RailID& remote_peer,
                        const void* data_buf,
                        uint32_t data_len,
                        uint32_t message_type = 0) = 0;

    // Determine whether readable data is available. You need to call the IsDataReady method in
    // the frame cycle to constantly check whether you have received data from other players.
    virtual bool IsDataReady(RailID* local_peer,
                    uint32_t* data_len,
                    uint32_t* message_type = NULL) = 0;

    // Read data send from remote player.
    virtual RailResult ReadData(const RailID& local_peer,
                        RailID* remote_peer,
                        void* data_buf,
                        uint32_t data_len,
                        uint32_t message_type = 0) = 0;

    // Blocks receiving certain message types of data.
    virtual RailResult BlockMessageType(const RailID& local_peer, uint32_t message_type) = 0;

    // Restores receiving certain message types of data.
    virtual RailResult UnblockMessageType(const RailID& local_peer, uint32_t message_type) = 0;

    // After communication is over, you should call this api to end the session, otherwise
    // the second communication perhaps is abnormal.
    virtual RailResult CloseSession(const RailID& local_peer, const RailID& remote_peer) = 0;

    virtual RailResult ResolveHostname(const RailString& domain,
                        RailArray<RailString>* ip_list) = 0;

    virtual RailResult GetSessionState(const RailID& remote_peer,
                        RailNetworkSessionState* session_state) = 0;
};

#pragma pack(pop)
}  // namespace rail

#endif  // RAIL_SDK_RAIL_NETWORK_H
