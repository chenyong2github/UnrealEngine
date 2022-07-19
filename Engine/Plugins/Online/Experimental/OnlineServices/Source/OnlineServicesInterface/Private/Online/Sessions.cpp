// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/Sessions.h"

namespace UE::Online {

const TCHAR* LexToString(ESessionsComparisonOp Comparison)
{
	switch (Comparison)
	{
	case ESessionsComparisonOp::Equals:				return TEXT("Equals");
	case ESessionsComparisonOp::NotEquals:			return TEXT("NotEquals");
	case ESessionsComparisonOp::GreaterThan:		return TEXT("GreaterThan");
	case ESessionsComparisonOp::GreaterThanEquals:	return TEXT("GreaterThanEquals");
	case ESessionsComparisonOp::LessThan:			return TEXT("LessThan");
	case ESessionsComparisonOp::LessThanEquals:		return TEXT("LessThanEquals");
	default:										checkNoEntry(); // Intentional fallthrough
	case ESessionsComparisonOp::Near:				return TEXT("Near");
	}
}

void LexFromString(ESessionsComparisonOp& OutComparison, const TCHAR* InStr)
{
	if (FCString::Stricmp(InStr, TEXT("Equals")) == 0)
	{
		OutComparison = ESessionsComparisonOp::Equals;
	}
	else if (FCString::Stricmp(InStr, TEXT("NotEquals")) == 0)
	{
		OutComparison = ESessionsComparisonOp::NotEquals;
	}
	else if (FCString::Stricmp(InStr, TEXT("GreaterThan")) == 0)
	{
		OutComparison = ESessionsComparisonOp::GreaterThan;
	}
	else if (FCString::Stricmp(InStr, TEXT("GreaterThanEquals")) == 0)
	{
		OutComparison = ESessionsComparisonOp::GreaterThanEquals;
	}
	else if (FCString::Stricmp(InStr, TEXT("LessThan")) == 0)
	{
		OutComparison = ESessionsComparisonOp::LessThan;
	}
	else if (FCString::Stricmp(InStr, TEXT("LessThanEquals")) == 0)
	{
		OutComparison = ESessionsComparisonOp::LessThanEquals;
	}
	else if (FCString::Stricmp(InStr, TEXT("Near")) == 0)
	{
		OutComparison = ESessionsComparisonOp::Near;
	}
	else
	{
		checkNoEntry();
		OutComparison = ESessionsComparisonOp::Near;
	}
}

const TCHAR* LexToString(ECustomSessionSettingVisibility Value)
{
	switch (Value)
	{
	case ECustomSessionSettingVisibility::ViaPingOnly:				return TEXT("ViaPingOnly");
	case ECustomSessionSettingVisibility::ViaOnlineService:			return TEXT("ViaOnlineService");
	case ECustomSessionSettingVisibility::ViaOnlineServiceAndPing:	return TEXT("ViaOnlineServiceAndPing");
	default:														checkNoEntry(); // Intentional fallthrough
	case ECustomSessionSettingVisibility::DontAdvertise:			return TEXT("DontAdvertise");
	}
}

void LexFromString(ECustomSessionSettingVisibility& Value, const TCHAR* InStr)
{
	if (FCString::Stricmp(InStr, TEXT("DontAdvertise")) == 0)
	{
		Value = ECustomSessionSettingVisibility::DontAdvertise;
	}
	else if (FCString::Stricmp(InStr, TEXT("ViaPingOnly")) == 0)
	{
		Value = ECustomSessionSettingVisibility::ViaPingOnly;
	}
	else if (FCString::Stricmp(InStr, TEXT("ViaOnlineService")) == 0)
	{
		Value = ECustomSessionSettingVisibility::ViaOnlineService;
	}
	else if (FCString::Stricmp(InStr, TEXT("ViaOnlineServiceAndPing")) == 0)
	{
		Value = ECustomSessionSettingVisibility::ViaOnlineServiceAndPing;
	}
	else
	{
		checkNoEntry();
		Value = ECustomSessionSettingVisibility::DontAdvertise;
	}
}

const TCHAR* LexToString(ESessionState Value)
{
	switch (Value)
	{
	case ESessionState::Creating:	return TEXT("Creating");
	case ESessionState::Joining:	return TEXT("Joining");
	case ESessionState::Valid:		return TEXT("Valid");
	case ESessionState::Leaving:	return TEXT("Leaving");
	case ESessionState::Destroying:	return TEXT("Destroying");
	default:						checkNoEntry(); // Intentional fallthrough
	case ESessionState::Invalid:	return TEXT("Invalid");
	}
}

void LexFromString(ESessionState& Value, const TCHAR* InStr)
{
	if (FCString::Stricmp(InStr, TEXT("Invalid")) == 0)
	{
		Value = ESessionState::Invalid;
	}
	else if (FCString::Stricmp(InStr, TEXT("Creating")) == 0)
	{
		Value = ESessionState::Creating;
	}
	else if (FCString::Stricmp(InStr, TEXT("Joining")) == 0)
	{
		Value = ESessionState::Joining;
	}
	else if (FCString::Stricmp(InStr, TEXT("Valid")) == 0)
	{
		Value = ESessionState::Valid;
	}
	else if (FCString::Stricmp(InStr, TEXT("Leaving")) == 0)
	{
		Value = ESessionState::Leaving;
	}
	else if (FCString::Stricmp(InStr, TEXT("Destroying")) == 0)
	{
		Value = ESessionState::Destroying;
	}
	else
	{
		checkNoEntry();
		Value = ESessionState::Invalid;
	}
}

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
	COPY_TOPTIONAL_VALUE_IF_SET(bAllowUnregisteredPlayers)
	COPY_TOPTIONAL_VALUE_IF_SET(bAntiCheatProtected)
	COPY_TOPTIONAL_VALUE_IF_SET(bPresenceEnabled)
		
	for (const FName& Key : UpdatedSettings.RemovedCustomSettings)
	{
		CustomSettings.Remove(Key);
	}

	CustomSettings.Append(UpdatedSettings.UpdatedCustomSettings);

	for (const FOnlineAccountIdHandle& Key : UpdatedSettings.RemovedRegisteredPlayers)
	{
		RegisteredPlayers.Remove(Key);
	}

	RegisteredPlayers.Append(UpdatedSettings.UpdatedRegisteredPlayers);

	for (const FOnlineAccountIdHandle& Key : UpdatedSettings.RemovedSessionMembers)
	{
		SessionMembers.Remove(Key);
	}

	for (const TPair<FOnlineAccountIdHandle, FSessionMemberUpdate>& Entry : UpdatedSettings.UpdatedSessionMembers)
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
	MOVE_TOPTIONAL_IF_SET(bAllowUnregisteredPlayers)
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

	for (TPair<FOnlineAccountIdHandle, FSessionMemberUpdate>& UpdatedSessionMember : UpdatedValue.UpdatedSessionMembers)
	{
		// If an update adds a modification to a member that had previously been marked for removal, we'll keep the latest change
		RemovedSessionMembers.Remove(UpdatedSessionMember.Key);

		FSessionMemberUpdate MemberUpdate = UpdatedSessionMembers.FindOrAdd(MoveTemp(UpdatedSessionMember.Key));
		MemberUpdate += MoveTemp(UpdatedSessionMember.Value);
	}

	for (FOnlineAccountIdHandle& Key : UpdatedValue.RemovedSessionMembers)
	{
		// If an update removes a member that had previously been modified, we'll keep the latest change
		UpdatedSessionMembers.Remove(Key);
		RemovedSessionMembers.AddUnique(MoveTemp(Key));
	}

	for (const TPair<FOnlineAccountIdHandle, FRegisteredPlayer>& UpdatedRegisteredPlayer : UpdatedValue.UpdatedRegisteredPlayers)
	{
		// If an update adds a modification to a registered player that had previously been marked for removal, we'll keep the latest change
		RemovedRegisteredPlayers.Remove(UpdatedRegisteredPlayer.Key);
	}
	UpdatedRegisteredPlayers.Append(MoveTemp(UpdatedValue.UpdatedRegisteredPlayers));

	for (FOnlineAccountIdHandle& Key : UpdatedValue.RemovedRegisteredPlayers)
	{
		// If an update removes a registered player that had previously been modified, we'll keep the latest change
		UpdatedRegisteredPlayers.Remove(Key);
		RemovedRegisteredPlayers.AddUnique(MoveTemp(Key));
	}

	return *this;
}

#undef MOVE_TOPTIONAL_IF_SET

FSession::FSession()
{

}

FSession::FSession(const FSession& InSession)
	: OwnerUserId(InSession.OwnerUserId)
	, SessionId(InSession.SessionId)
	, CurrentState(InSession.CurrentState)
	, SessionSettings(InSession.SessionSettings)
{

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