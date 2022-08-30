// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/Sessions.h"

namespace UE::Online {

const TCHAR* LexToString(ESessionJoinPolicy Value)
{
	switch (Value)
	{
	case ESessionJoinPolicy::Public:		return TEXT("Public");
	case ESessionJoinPolicy::FriendsOnly:	return TEXT("FriendsOnly");
	default:								checkNoEntry(); // Intentional fallthrough
	case ESessionJoinPolicy::InviteOnly:	return TEXT("InviteOnly");
	}
}

void LexFromString(ESessionJoinPolicy& Value, const TCHAR* InStr)
{
	if (FCString::Stricmp(InStr, TEXT("Public")) == 0)
	{
		Value = ESessionJoinPolicy::Public;
	}
	else if (FCString::Stricmp(InStr, TEXT("FriendsOnly")) == 0)
	{
		Value = ESessionJoinPolicy::FriendsOnly;
	}
	else if (FCString::Stricmp(InStr, TEXT("InviteOnly")) == 0)
	{
		Value = ESessionJoinPolicy::InviteOnly;
	}
	else
	{
		checkNoEntry();
		Value = ESessionJoinPolicy::InviteOnly;
	}
}

#define COPY_TOPTIONAL_VALUE_IF_SET(Value) \
	if (UpdatedSettings.Value.IsSet()) \
	{ \
		Value = UpdatedSettings.Value.GetValue(); \
	} \

FSessionSettings& FSessionSettings::operator+=(const FSessionSettingsUpdate& UpdatedSettings)
{
	COPY_TOPTIONAL_VALUE_IF_SET(SchemaName) // TODO: We may need some additional logic for schema changes
	COPY_TOPTIONAL_VALUE_IF_SET(NumMaxPublicConnections)
	COPY_TOPTIONAL_VALUE_IF_SET(NumOpenPublicConnections)
	COPY_TOPTIONAL_VALUE_IF_SET(NumMaxPrivateConnections)
	COPY_TOPTIONAL_VALUE_IF_SET(NumOpenPrivateConnections)
	COPY_TOPTIONAL_VALUE_IF_SET(JoinPolicy)
	COPY_TOPTIONAL_VALUE_IF_SET(SessionIdOverride)
	COPY_TOPTIONAL_VALUE_IF_SET(bIsDedicatedServerSession)
	COPY_TOPTIONAL_VALUE_IF_SET(bAllowNewMembers)
	COPY_TOPTIONAL_VALUE_IF_SET(bAllowSanctionedPlayers)
	COPY_TOPTIONAL_VALUE_IF_SET(bAntiCheatProtected)
	COPY_TOPTIONAL_VALUE_IF_SET(bPresenceEnabled)
		
	for (const FName& Key : UpdatedSettings.RemovedCustomSettings)
	{
		CustomSettings.Remove(Key);
	}

	CustomSettings.Append(UpdatedSettings.UpdatedCustomSettings);

	for (const FAccountId& Key : UpdatedSettings.RemovedSessionMembers)
	{
		SessionMembers.Remove(Key);
	}

	for (const TPair<FAccountId, FSessionMemberUpdate>& Entry : UpdatedSettings.UpdatedSessionMembers)
	{
		if (FSessionMember* SessionMember = SessionMembers.Find(Entry.Key))
		{
			const FSessionMemberUpdate& SessionMemberUpdate = Entry.Value;

			for (const FName& Key : SessionMemberUpdate.RemovedMemberSettings)
			{
				SessionMember->MemberSettings.Remove(Key);
			}

			SessionMember->MemberSettings.Append(SessionMemberUpdate.UpdatedMemberSettings);
		}
	}

	return *this;
}

#undef COPY_TOPTIONAL_VALUE_IF_SET

FSessionMemberUpdate& FSessionMemberUpdate::operator+=(FSessionMemberUpdate&& UpdatedValue)
{
	for (const TPair<FName, FCustomSessionSetting>& UpdatedMemberSetting : UpdatedValue.UpdatedMemberSettings)
	{
		// If an update adds a modification to a setting that had previously been marked for removal, we'll keep the latest change
		RemovedMemberSettings.Remove(UpdatedMemberSetting.Key);
	}
	UpdatedMemberSettings.Append(MoveTemp(UpdatedValue.UpdatedMemberSettings));

	for (FName& Key : UpdatedValue.RemovedMemberSettings)
	{
		// If an update removes a setting that had previously been modified, we'll keep the latest change
		UpdatedMemberSettings.Remove(Key);
		RemovedMemberSettings.AddUnique(MoveTemp(Key));
	}

	return *this;
}

#define MOVE_TOPTIONAL_IF_SET(Value) \
	if (UpdatedValue.Value.IsSet()) \
	{ \
		Value = MoveTemp(UpdatedValue.Value); \
	} \

FSessionSettingsUpdate& FSessionSettingsUpdate::operator+=(FSessionSettingsUpdate&& UpdatedValue)
{
	MOVE_TOPTIONAL_IF_SET(SchemaName)
	MOVE_TOPTIONAL_IF_SET(NumMaxPublicConnections)
	MOVE_TOPTIONAL_IF_SET(NumOpenPublicConnections)
	MOVE_TOPTIONAL_IF_SET(NumMaxPrivateConnections)
	MOVE_TOPTIONAL_IF_SET(NumOpenPrivateConnections)
	MOVE_TOPTIONAL_IF_SET(JoinPolicy)
	MOVE_TOPTIONAL_IF_SET(SessionIdOverride)
	MOVE_TOPTIONAL_IF_SET(bIsDedicatedServerSession)
	MOVE_TOPTIONAL_IF_SET(bAllowNewMembers)
	MOVE_TOPTIONAL_IF_SET(bAllowSanctionedPlayers)
	MOVE_TOPTIONAL_IF_SET(bAntiCheatProtected)
	MOVE_TOPTIONAL_IF_SET(bPresenceEnabled)

	for (const TPair<FName, FCustomSessionSetting>& UpdatedCustomSetting : UpdatedValue.UpdatedCustomSettings)
	{
		// If an update adds a modification to a setting that had previously been marked for removal, we'll keep the latest change
		RemovedCustomSettings.Remove(UpdatedCustomSetting.Key);
	}
	UpdatedCustomSettings.Append(MoveTemp(UpdatedValue.UpdatedCustomSettings));

	for (FName& Key : UpdatedValue.RemovedCustomSettings)
	{
		// If an update removes a setting that had previously been modified, we'll keep the latest change
		UpdatedCustomSettings.Remove(Key);
		RemovedCustomSettings.AddUnique(MoveTemp(Key));
	}

	for (TPair<FAccountId, FSessionMemberUpdate>& UpdatedSessionMember : UpdatedValue.UpdatedSessionMembers)
	{
		// If an update adds a modification to a member that had previously been marked for removal, we'll keep the latest change
		RemovedSessionMembers.Remove(UpdatedSessionMember.Key);

		FSessionMemberUpdate MemberUpdate = UpdatedSessionMembers.FindOrAdd(MoveTemp(UpdatedSessionMember.Key));
		MemberUpdate += MoveTemp(UpdatedSessionMember.Value);
	}

	for (FAccountId& Key : UpdatedValue.RemovedSessionMembers)
	{
		// If an update removes a member that had previously been modified, we'll keep the latest change
		UpdatedSessionMembers.Remove(Key);
		RemovedSessionMembers.AddUnique(MoveTemp(Key));
	}

	return *this;
}

#undef MOVE_TOPTIONAL_IF_SET

const FString ToLogString(const ISession& Session)
{
	return Session.ToLogString();
}

const TCHAR* LexToString(EUISessionJoinRequestedSource UISessionJoinRequestedSource)
{
	switch (UISessionJoinRequestedSource)
	{
	case EUISessionJoinRequestedSource::FromInvitation:	return TEXT("FromInvitation");
	default:											checkNoEntry(); // Intentional fallthrough
	case EUISessionJoinRequestedSource::Unspecified:	return TEXT("Unspecified");
	}
}

void LexFromString(EUISessionJoinRequestedSource& OutUISessionJoinRequestedSource, const TCHAR* InStr)
{
	if (FCString::Stricmp(InStr, TEXT("FromInvitation")) == 0)
	{
		OutUISessionJoinRequestedSource = EUISessionJoinRequestedSource::FromInvitation;
	}
	else if (FCString::Stricmp(InStr, TEXT("Unspecified")) == 0)
	{
		OutUISessionJoinRequestedSource = EUISessionJoinRequestedSource::Unspecified;
	}
	else
	{
		checkNoEntry();
		OutUISessionJoinRequestedSource = EUISessionJoinRequestedSource::Unspecified;
	}
}

/* UE::Online */ }