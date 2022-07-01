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

FSessionMemberUpdate& FSessionMemberUpdate::operator+=(FSessionMemberUpdate&& UpdatedValue)
{
	for (TPair<FName, FCustomSessionSetting>& UpdatedMemberSetting : UpdatedValue.UpdatedMemberSettings)
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

FSessionSettingsUpdate& FSessionSettingsUpdate::operator+=(FSessionSettingsUpdate&& UpdatedValue)
{
	if (UpdatedValue.SchemaName.IsSet())
	{
		SchemaName = MoveTemp(UpdatedValue.SchemaName);
	}

	if (UpdatedValue.NumMaxPublicConnections.IsSet())
	{
		NumMaxPublicConnections = MoveTemp(UpdatedValue.NumMaxPublicConnections);
	}

	if (UpdatedValue.NumOpenPublicConnections.IsSet())
	{
		NumOpenPublicConnections = MoveTemp(UpdatedValue.NumOpenPublicConnections);
	}

	if (UpdatedValue.NumMaxPrivateConnections.IsSet())
	{
		NumMaxPrivateConnections = MoveTemp(UpdatedValue.NumMaxPrivateConnections);
	}

	if (UpdatedValue.NumOpenPrivateConnections.IsSet())
	{
		NumOpenPrivateConnections = MoveTemp(UpdatedValue.NumOpenPrivateConnections);
	}

	if (UpdatedValue.JoinPolicy.IsSet())
	{
		JoinPolicy = MoveTemp(UpdatedValue.JoinPolicy);
	}

	if (UpdatedValue.SessionIdOverride.IsSet())
	{
		SessionIdOverride = MoveTemp(UpdatedValue.SessionIdOverride);
	}

	if (UpdatedValue.IsLANSession.IsSet())
	{
		IsLANSession = MoveTemp(UpdatedValue.IsLANSession);
	}

	if (UpdatedValue.IsDedicatedServerSession.IsSet())
	{
		IsDedicatedServerSession = MoveTemp(UpdatedValue.IsDedicatedServerSession);
	}

	if (UpdatedValue.bAllowNewMembers.IsSet())
	{
		bAllowNewMembers = MoveTemp(UpdatedValue.bAllowNewMembers);
	}

	if (UpdatedValue.bAllowSanctionedPlayers.IsSet())
	{
		bAllowSanctionedPlayers = MoveTemp(UpdatedValue.bAllowSanctionedPlayers);
	}

	if (UpdatedValue.bAllowUnregisteredPlayers.IsSet())
	{
		bAllowUnregisteredPlayers = MoveTemp(UpdatedValue.bAllowUnregisteredPlayers);
	}

	if (UpdatedValue.bAntiCheatProtected.IsSet())
	{
		bAntiCheatProtected = MoveTemp(UpdatedValue.bAntiCheatProtected);
	}

	if (UpdatedValue.bPresenceEnabled.IsSet())
	{
		UpdatedValue.bPresenceEnabled = MoveTemp(UpdatedValue.bPresenceEnabled);
	}

	for (TPair<FName, FCustomSessionSetting>& UpdatedCustomSetting : UpdatedValue.UpdatedCustomSettings)
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

	for (TPair<FOnlineAccountIdHandle, FRegisteredUser>& UpdatedRegisteredUser : UpdatedValue.UpdatedRegisteredUsers)
	{
		// If an update adds a modification to a registered user that had previously been marked for removal, we'll keep the latest change
		RemovedRegisteredUsers.Remove(UpdatedRegisteredUser.Key);
	}
	UpdatedRegisteredUsers.Append(MoveTemp(UpdatedValue.UpdatedRegisteredUsers));

	for (FOnlineAccountIdHandle& Key : UpdatedValue.RemovedRegisteredUsers)
	{
		// If an update removes a registered user that had previously been modified, we'll keep the latest change
		UpdatedRegisteredUsers.Remove(Key);
		RemovedRegisteredUsers.AddUnique(MoveTemp(Key));
	}

	return *this;
}

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

/* UE::Online */ }