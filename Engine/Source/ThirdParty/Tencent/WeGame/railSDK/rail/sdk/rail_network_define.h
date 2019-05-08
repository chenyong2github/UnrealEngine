// Copyright (c) 2016, Entropy Game Global Limited.
// All rights reserved.

#ifndef RAIL_SDK_RAIL_NETWORK_DEFINE_H
#define RAIL_SDK_RAIL_NETWORK_DEFINE_H

#include "rail/sdk/base/rail_define.h"
#include "rail/sdk/rail_event.h"

namespace rail {
#pragma pack(push, RAIL_SDK_PACKING)

struct RailNetworkSessionState {
    RailNetworkSessionState() {
        is_connection_active = false;
        is_connecting = false;
        is_using_relay = false;
        bytes_in_send_buffer = 0;
        packets_in_send_buffer = 0;
        remote_ip = 0;
        remote_port = 0;
        session_error = kSuccess;
    }
    bool is_connection_active;
    bool is_connecting;
    bool is_using_relay;
    RailResult session_error;
    uint32_t bytes_in_send_buffer;
    uint32_t packets_in_send_buffer;
    uint32_t remote_ip;
    uint16_t remote_port;
};

namespace rail_event {

// This callback event is returned when a network session is requested to connect from
// the other player, which specifies the other player��s rail_id and the local rail_id.
struct CreateSessionRequest : public RailEvent<kRailEventNetworkCreateSessionRequest> {
    CreateSessionRequest() {
        local_peer = 0;
        remote_peer = 0;
    }

    RailID local_peer;
    RailID remote_peer;
};

// This callback event is returned if the request to establish a network session with
// the other player fails.
struct CreateSessionFailed : public RailEvent<kRailEventNetworkCreateSessionFailed> {
    CreateSessionFailed() {
        result = kFailure;
        local_peer = 0;
        remote_peer = 0;
    }

    RailID local_peer;
    RailID remote_peer;
};

}  // namespace rail_event

#pragma pack(pop)
}  // namespace rail

#endif  // RAIL_SDK_RAIL_NETWORK_DEFINE_H
