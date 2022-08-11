// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/SessionsNull.h"

#include "Online/OnlineServicesNull.h"
#include "Online/NboSerializerNullSvc.h"

namespace UE::Online {

/** FOnlineSessionIdRegistryNull */

FOnlineSessionIdRegistryNull::FOnlineSessionIdRegistryNull()
	: FOnlineSessionIdRegistryLAN(EOnlineServices::Null)
{
}

FOnlineSessionIdRegistryNull& FOnlineSessionIdRegistryNull::Get()
{
	static FOnlineSessionIdRegistryNull Instance;
	return Instance;
}

/** FSessionsNull */

FSessionsNull::FSessionsNull(FOnlineServicesNull& InServices)
	: Super(InServices)
{

}

/** FSessionsLAN */

void FSessionsNull::AppendSessionToPacket(FNboSerializeToBuffer& Packet, const FSessionLAN& Session)
{
	using namespace NboSerializerNullSvc;

	// We won't save CurrentState in the packet as all advertised sessions will be Valid
	SerializeToBuffer(Packet, Session.OwnerUserId);
	SerializeToBuffer(Packet, Session.SessionId);
	Packet << *Session.OwnerInternetAddr;

	// TODO: Write session settings to packet, after SchemaVariant work
}

void FSessionsNull::ReadSessionFromPacket(FNboSerializeFromBuffer& Packet, FSessionLAN& Session)
{
	using namespace NboSerializerNullSvc;

	SerializeFromBuffer(Packet, Session.OwnerUserId);
	SerializeFromBuffer(Packet, Session.SessionId);
	Packet >> *Session.OwnerInternetAddr;

	// We'll set the connect address for the remote session as a custom parameter, so it can be read in OnlineServices' GetResolvedConnectString
	FCustomSessionSetting ConnectString;
	ConnectString.Data.Set<FString>(Session.OwnerInternetAddr->ToString(true));
	ConnectString.Visibility = ECustomSessionSettingVisibility::ViaOnlineService;
	Session.SessionSettings.CustomSettings.Add(CONNECT_STRING_TAG, ConnectString);

	// TODO:: Read session settings from packet, after SchemaVariant work
}

/* UE::Online */ }