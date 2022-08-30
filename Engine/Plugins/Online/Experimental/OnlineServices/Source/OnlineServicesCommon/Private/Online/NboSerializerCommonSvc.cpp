// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/NboSerializerCommonSvc.h"

/**
 * Serializes data in network byte order form into a buffer
 */
namespace UE::Online {

namespace NboSerializerCommonSvc {

/** SerializeToBuffer methods */

void SerializeToBuffer(FNboSerializeToBuffer& Packet, const FSchemaVariant& Data)
{
	Packet << (uint8)Data.VariantType;

	switch (Data.VariantType)
	{
	case ESchemaAttributeType::Bool: Packet << Data.GetBoolean(); break;
	case ESchemaAttributeType::Double: Packet << Data.GetDouble(); break;
	case ESchemaAttributeType::Int64: Packet << (int32)Data.GetInt64(); break;
	case ESchemaAttributeType::String: Packet << Data.GetString(); break;
	}
}

void SerializeToBuffer(FNboSerializeToBuffer& Packet, const FCustomSessionSetting& CustomSessionSetting)
{
	SerializeToBuffer(Packet, CustomSessionSetting.Data);
	Packet << CustomSessionSetting.ID;
	Packet << (uint8)CustomSessionSetting.Visibility;
}

void SerializeToBuffer(FNboSerializeToBuffer& Packet, const FCustomSessionSettingsMap& CustomSessionSettingsMap)
{
	// First count the number of advertised keys
	int32 NumAdvertisedProperties = 0;
	for (const TPair<FSchemaAttributeId, FCustomSessionSetting>& Entry : CustomSessionSettingsMap)
	{
		const FCustomSessionSetting& Setting = Entry.Value;
		if (Setting.Visibility == ESchemaAttributeVisibility::Public)
		{
			NumAdvertisedProperties++;
		}
	}

	// Add the count of advertised keys and the data
	Packet << NumAdvertisedProperties;

	for (const TPair<FSchemaAttributeId, FCustomSessionSetting>& Entry : CustomSessionSettingsMap)
	{
		const FCustomSessionSetting& Setting = Entry.Value;
		if (Setting.Visibility == ESchemaAttributeVisibility::Public)
		{
			Packet << Entry.Key;
			SerializeToBuffer(Packet, Setting);
		}
	}
}

void SerializeToBuffer(FNboSerializeToBuffer& Packet, const FSessionMember& SessionMember)
{
	SerializeToBuffer(Packet, SessionMember.MemberSettings);
}

void SerializeToBuffer(FNboSerializeToBuffer& Packet, const FSessionSettings& SessionSettings)
{
	Packet << SessionSettings.bAllowNewMembers;
	Packet << SessionSettings.bAllowSanctionedPlayers;
	Packet << SessionSettings.bAntiCheatProtected;
	Packet << SessionSettings.bIsDedicatedServerSession;
	Packet << SessionSettings.bIsLANSession;
	Packet << SessionSettings.bPresenceEnabled;
	SerializeToBuffer(Packet, SessionSettings.CustomSettings);
	Packet << (uint8)SessionSettings.JoinPolicy;
	Packet << SessionSettings.NumMaxPrivateConnections;;
	Packet << SessionSettings.NumMaxPublicConnections;
	Packet << SessionSettings.NumOpenPrivateConnections;
	Packet << SessionSettings.NumOpenPublicConnections;
	// SessionSettings.RegisteredPlayers will be serialized in implementations as user id types will vary
	Packet << SessionSettings.SchemaName;
	Packet << SessionSettings.SessionIdOverride;
	// SessionSettings.SessionMembers will be serialized in implementations as user id types will vary
}

void SerializeToBuffer(FNboSerializeToBuffer& Packet, const FSessionCommon& Session)
{
	// Session.OwnerUserId will be serialized in implementations as user id types will vary
	// Session.SessionId will be serialized in implementations as user id types will vary
	// Session.CurrentState won't be serialized as it should always be Valid
	SerializeToBuffer(Packet, Session.SessionSettings);
}

/** SerializeFromBuffer methods */

void SerializeFromBuffer(FNboSerializeFromBuffer& Packet, FSchemaVariant& Data)
{
	uint8 VariantType = 0;
	Packet >> VariantType;
	Data.VariantType = (ESchemaAttributeType)VariantType;

	switch (Data.VariantType)
	{
	case ESchemaAttributeType::Bool:
	{
		uint8 Read;
		Packet >> Read;
		Data.Set(!!Read);
		break;
	}
	case ESchemaAttributeType::Double:
	{
		double Read = 0.0;
		Packet >> Read;
		Data.Set(Read);
		break;
	}
	case ESchemaAttributeType::Int64:
	{
		int32 Read = 0;
		Packet >> Read;
		Data.Set((int64)Read);
		break;
	}
	case ESchemaAttributeType::String:
	{
		FString Read;
		Packet >> Read;
		Data.Set(Read);
		break;
	}
	}
}

void SerializeFromBuffer(FNboSerializeFromBuffer& Packet, FCustomSessionSetting& CustomSessionSetting)
{
	SerializeFromBuffer(Packet, CustomSessionSetting.Data);
	Packet >> CustomSessionSetting.ID;
	uint8 VisibilityNum = 0;
	Packet >> VisibilityNum;
	CustomSessionSetting.Visibility = (ESchemaAttributeVisibility)VisibilityNum;
}

void SerializeFromBuffer(FNboSerializeFromBuffer& Packet, FCustomSessionSettingsMap& CustomSessionSettingsMap)
{
	int32 NumEntries = 0;
	Packet >> NumEntries;

	for (int32 Index = 0; Index < NumEntries; ++Index)
	{
		FSchemaAttributeId Key;
		Packet >> Key;

		FCustomSessionSetting Value;
		SerializeFromBuffer(Packet, Value);

		CustomSessionSettingsMap.Emplace(Key, Value);
	}
}

void SerializeFromBuffer(FNboSerializeFromBuffer& Packet, FSessionMember& SessionMember)
{
	SerializeFromBuffer(Packet, SessionMember.MemberSettings);
}

void SerializeFromBuffer(FNboSerializeFromBuffer& Packet, FSessionSettings& SessionSettings)
{
	uint8 Read = 0;

	// Read all the booleans as bytes
	Packet >> Read;
	SessionSettings.bAllowNewMembers = !!Read;
	Packet >> Read;
	SessionSettings.bAllowSanctionedPlayers = !!Read;
	Packet >> Read;
	SessionSettings.bAntiCheatProtected = !!Read;
	Packet >> Read;
	SessionSettings.bIsDedicatedServerSession = !!Read;
	Packet >> Read;
	SessionSettings.bIsLANSession = !!Read;
	Packet >> Read;
	SessionSettings.bPresenceEnabled = !!Read;

	SerializeFromBuffer(Packet, SessionSettings.CustomSettings);

	Packet >> Read;
	SessionSettings.JoinPolicy = (ESessionJoinPolicy)Read;

	Packet >> SessionSettings.NumMaxPrivateConnections;;
	Packet >> SessionSettings.NumMaxPublicConnections;
	Packet >> SessionSettings.NumOpenPrivateConnections;
	Packet >> SessionSettings.NumOpenPublicConnections;
	// SessionSettings.RegisteredPlayers will be deserialized in implementations as user id types will vary
	Packet >> SessionSettings.SchemaName;
	Packet >> SessionSettings.SessionIdOverride;
	// SessionSettings.SessionMembers will be deserialized in implementations as user id types will vary
}

void SerializeFromBuffer(FNboSerializeFromBuffer& Packet, FSessionCommon& Session)
{
	// Session.OwnerUserId will be deserialized in implementations as user id types will vary
	// Session.SessionId will be deserialized in implementations as user id types will vary
	SerializeFromBuffer(Packet, Session.SessionSettings);
}

/* NboSerializerCommonSvc */ }

/* UE::Online */ }