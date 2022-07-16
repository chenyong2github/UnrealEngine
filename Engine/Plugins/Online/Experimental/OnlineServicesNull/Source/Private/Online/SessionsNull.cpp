// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/SessionsNull.h"

#include "Misc/Guid.h"
#include "Online/NboSerializerNullSvc.h"
#include "Online/OnlineServicesNull.h"
#include "SocketSubsystem.h"
#include "UObject/CoreNet.h"

namespace UE::Online {

/** FOnlineSessionIdRegistryNull */

FOnlineSessionIdRegistryNull& FOnlineSessionIdRegistryNull::Get()
{
	static FOnlineSessionIdRegistryNull Instance;
	return Instance;
}

FOnlineSessionIdHandle FOnlineSessionIdRegistryNull::GetNextSessionId()
{
	return BasicRegistry.FindOrAddHandle(FGuid().ToString());
}

/** FSessionsNull */

FSessionsNull::FSessionsNull(FOnlineServicesNull& InServices)
	: Super(InServices)
{

}

void FSessionsNull::Tick(float DeltaSeconds)
{
	if (LANSessionManager->GetBeaconState() > ELanBeaconState::NotUsingLanBeacon)
	{
		LANSessionManager->Tick(DeltaSeconds);
	}

	Super::Tick(DeltaSeconds);
}

TOnlineAsyncOpHandle<FCreateSession> FSessionsNull::CreateSession(FCreateSession::Params&& Params)
{
	TOnlineAsyncOpRef<FCreateSession> Op = GetOp<FCreateSession>(MoveTemp(Params));
	const FCreateSession::Params& OpParams = Op->GetParams();

	FOnlineError ParamsCheck = CheckCreateSessionParams(OpParams);
	if (ParamsCheck != Errors::Success())
	{
		Op->SetError(MoveTemp(ParamsCheck));
		return Op->GetHandle();
	}

	// We'll only host on the LAN beacon for public sessions
	if (OpParams.SessionSettings.JoinPolicy == ESessionJoinPolicy::Public)
	{
		if (!TryHostLANSession())
		{
			Op->SetError(Errors::RequestFailure()); // TODO: May need a new error type
			return Op->GetHandle();
		}
	}

	Op->Then([this](TOnlineAsyncOp<FCreateSession>& Op) mutable
	{
		const FCreateSession::Params& OpParams = Op.GetParams();

		FOnlineError StateCheck = CheckCreateSessionState(OpParams);
		if (StateCheck != Errors::Success())
		{
			Op.SetError(MoveTemp(StateCheck));
			return;
		}

		FSessionNull NewSession = FSessionNull();
		NewSession.CurrentState = ESessionState::Valid;
		NewSession.OwnerUserId = OpParams.LocalUserId;
		NewSession.SessionId = FOnlineSessionIdRegistryNull::Get().GetNextSessionId();
		NewSession.SessionSettings = OpParams.SessionSettings;

		// For Null sessions, we'll add all the Session members manually instead of calling JoinSession since there is no API calls involved
		NewSession.SessionSettings.SessionMembers.Append(OpParams.LocalUsers);
	
		TSharedRef<FSessionNull> NewSessionNullRef = MakeShared<FSessionNull>(NewSession);

		SessionsByName.Add(OpParams.SessionName, NewSessionNullRef);
		SessionsById.Add(NewSession.SessionId, NewSessionNullRef);

		Op.SetResult(FCreateSession::Result{ NewSessionNullRef });
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FUpdateSession> FSessionsNull::UpdateSession(FUpdateSession::Params&& Params)
{
	TOnlineAsyncOpRef<FUpdateSession> Op = GetOp<FUpdateSession>(MoveTemp(Params));

	Op->Then([this](TOnlineAsyncOp<FUpdateSession>& Op) mutable
	{
		const FUpdateSession::Params& OpParams = Op.GetParams();

		FOnlineError StateCheck = CheckUpdateSessionState(OpParams);
		if (StateCheck != Errors::Success())
		{
			Op.SetError(MoveTemp(StateCheck));
			return;
		}

		// We'll check and update all settings one by one, with additional logic wherever required

		TSharedRef<FSession>& FoundSession = SessionsByName.FindChecked(OpParams.SessionName);
		FSessionSettings& SessionSettings = FoundSession->SessionSettings;
		const FSessionSettingsUpdate& UpdatedSettings = OpParams.Mutations;

		if (UpdatedSettings.bAllowNewMembers.IsSet())
		{
			SessionSettings.bAllowNewMembers = UpdatedSettings.bAllowNewMembers.GetValue();
		}

		if (UpdatedSettings.bAllowSanctionedPlayers.IsSet())
		{
			SessionSettings.bAllowSanctionedPlayers = UpdatedSettings.bAllowSanctionedPlayers.GetValue();
		}

		if (UpdatedSettings.bAllowUnregisteredPlayers.IsSet())
		{
			SessionSettings.bAllowUnregisteredPlayers = UpdatedSettings.bAllowUnregisteredPlayers.GetValue();
		}

		if (UpdatedSettings.bAntiCheatProtected.IsSet())
		{
			SessionSettings.bAntiCheatProtected = UpdatedSettings.bAntiCheatProtected.GetValue();
		}

		if (UpdatedSettings.bPresenceEnabled.IsSet())
		{
			SessionSettings.bPresenceEnabled = UpdatedSettings.bPresenceEnabled.GetValue();
		}

		if (UpdatedSettings.IsDedicatedServerSession.IsSet())
		{
			SessionSettings.IsDedicatedServerSession = UpdatedSettings.IsDedicatedServerSession.GetValue();
		}

		if (UpdatedSettings.IsLANSession.IsSet())
		{
			SessionSettings.IsLANSession = UpdatedSettings.IsLANSession.GetValue();
		}

		if (UpdatedSettings.JoinPolicy.IsSet())
		{
			// Changes in the join policy setting will mean we start or stop the LAN Beacon broadcasting the session information
			// There is no FriendsNull interface at the time of this implementation, hence the binary behavior (Public/Non-Public)
			if (SessionSettings.JoinPolicy != ESessionJoinPolicy::Public && UpdatedSettings.JoinPolicy == ESessionJoinPolicy::Public)
			{
				TryHostLANSession();
			}
			else if (SessionSettings.JoinPolicy == ESessionJoinPolicy::Public && UpdatedSettings.JoinPolicy != ESessionJoinPolicy::Public)
			{
				StopLANSession();
			}

			SessionSettings.JoinPolicy = UpdatedSettings.JoinPolicy.GetValue();
		}

		if (UpdatedSettings.NumMaxPrivateConnections.IsSet())
		{
			SessionSettings.NumMaxPrivateConnections = UpdatedSettings.NumMaxPrivateConnections.GetValue();
		}

		if (UpdatedSettings.NumMaxPublicConnections.IsSet())
		{
			SessionSettings.NumMaxPublicConnections = UpdatedSettings.NumMaxPublicConnections.GetValue();
		}

		if (UpdatedSettings.NumOpenPrivateConnections.IsSet())
		{
			SessionSettings.NumOpenPrivateConnections = UpdatedSettings.NumOpenPrivateConnections.GetValue();
		}

		if (UpdatedSettings.NumOpenPublicConnections.IsSet())
		{
			SessionSettings.NumOpenPublicConnections = UpdatedSettings.NumOpenPublicConnections.GetValue();
		}

		// TODO: We may need some additional logic for schema changes
		if (UpdatedSettings.SchemaName.IsSet())
		{
			SessionSettings.SchemaName = UpdatedSettings.SchemaName.GetValue();
		}

		if (UpdatedSettings.SessionIdOverride.IsSet())
		{
			SessionSettings.SessionIdOverride = UpdatedSettings.SessionIdOverride.GetValue();
		}

		for (const FName& Key : UpdatedSettings.RemovedCustomSettings)
		{
			SessionSettings.CustomSettings.Remove(Key);
		}

		SessionSettings.CustomSettings.Append(UpdatedSettings.UpdatedCustomSettings);

		for (const FOnlineAccountIdHandle& Key : UpdatedSettings.RemovedRegisteredUsers)
		{
			SessionSettings.RegisteredUsers.Remove(Key);
		}

		SessionSettings.RegisteredUsers.Append(UpdatedSettings.UpdatedRegisteredUsers);

		for (const FOnlineAccountIdHandle& Key : UpdatedSettings.RemovedSessionMembers)
		{
			SessionSettings.SessionMembers.Remove(Key);
		}

		for (FSessionMemberUpdatesMap::TConstIterator It(UpdatedSettings.UpdatedSessionMembers); It; ++It)
		{
			if (FSessionMember* SessionMember = SessionSettings.SessionMembers.Find(It.Key()))
			{
				const FSessionMemberUpdate& SessionMemberUpdate = It.Value();

				SessionMember->MemberSettings.Append(SessionMemberUpdate.UpdatedMemberSettings);

				for (const FName& Key : SessionMemberUpdate.RemovedMemberSettings)
				{
					SessionMember->MemberSettings.Remove(Key);
				}
			}
		}

		// We set the result and fire the event
		Op.SetResult(FUpdateSession::Result{ FoundSession });

		FSessionUpdated SessionUpdatedEvent{ FoundSession, UpdatedSettings };
		SessionEvents.OnSessionUpdated.Broadcast(SessionUpdatedEvent);
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FFindSessions> FSessionsNull::FindSessions(FFindSessions::Params&& Params)
{
	TOnlineAsyncOpRef<FFindSessions> Op = GetOp<FFindSessions>(MoveTemp(Params));
	const FFindSessions::Params& OpParams = Op->GetParams();

	FOnlineError ParamsCheck = CheckFindSessionsParams(OpParams);
	if (ParamsCheck != Errors::Success())
	{
		Op->SetError(MoveTemp(ParamsCheck));
		return Op->GetHandle();
	}

	Op->Then([this](TOnlineAsyncOp<FFindSessions>& Op) mutable
	{
		const FFindSessions::Params& OpParams = Op.GetParams();

		FOnlineError StateCheck = CheckFindSessionsState(OpParams);
		if (StateCheck != Errors::Success())
		{
			Op.SetError(MoveTemp(StateCheck));
			return;
		}

		CurrentSessionSearch = MakeShared<FFindSessions::Result>();
		CurrentSessionSearchHandle = Op.AsShared();

		FindLANSessions();
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FJoinSession> FSessionsNull::JoinSession(FJoinSession::Params&& Params)
{
	TOnlineAsyncOpRef<FJoinSession> Op = GetOp<FJoinSession>(MoveTemp(Params));
	const FJoinSession::Params& OpParams = Op->GetParams();

	FOnlineError ParamsCheck = CheckJoinSessionParams(OpParams);
	if (ParamsCheck != Errors::Success())
	{
		Op->SetError(MoveTemp(ParamsCheck));
		return Op->GetHandle();
	}

	// If no restrictions apply, we'll save our copy of the session, add the new players, and register it
	Op->Then([this](TOnlineAsyncOp<FJoinSession>& Op) mutable
	{
		const FJoinSession::Params& OpParams = Op.GetParams();

		FOnlineError StateCheck = CheckJoinSessionState(OpParams);
		if (StateCheck != Errors::Success())
		{
			Op.SetError(MoveTemp(StateCheck));
			return;
		}

		TSharedRef<FSessionNull> NewSessionNullRef = MakeShared<FSessionNull>(FSessionNull::Cast(*OpParams.Session));
		FSessionSettings NewSessionNullSettings = NewSessionNullRef->SessionSettings;

		// TODO: This will move to AddSessionMember
		TArray<FOnlineAccountIdHandle> LocalUserIds;
		LocalUserIds.Reserve(OpParams.LocalUsers.Num());
		OpParams.LocalUsers.GenerateKeyArray(LocalUserIds);

		TArray<FOnlineAccountIdHandle> JoinedUsers;

		for (const FOnlineAccountIdHandle& LocalUserId : LocalUserIds)
		{
			bool bReserveSlot = true;

			if (const FRegisteredUser* RegisteredUser = NewSessionNullSettings.RegisteredUsers.Find(LocalUserId))
			{
				bReserveSlot = !RegisteredUser->bHasReservedSlot;
			}

			if (bReserveSlot)
			{
				if (NewSessionNullSettings.NumOpenPublicConnections > 0)
				{
					NewSessionNullSettings.NumOpenPublicConnections--;
				}
				else if (NewSessionNullSettings.NumOpenPrivateConnections > 0)
				{
					NewSessionNullSettings.NumOpenPrivateConnections--;
				}
			}

			JoinedUsers.Add(LocalUserId);
		}

		FSessionJoined SessionJoinedEvent = { MoveTemp(JoinedUsers), NewSessionNullRef };

		NewSessionNullSettings.SessionMembers.Append(OpParams.LocalUsers);

		SessionsByName.Add(OpParams.SessionName, NewSessionNullRef);
		SessionsById.Add(NewSessionNullRef->SessionId, NewSessionNullRef);

		Op.SetResult(FJoinSession::Result{ NewSessionNullRef });

		SessionEvents.OnSessionJoined.Broadcast(SessionJoinedEvent);
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FLeaveSession> FSessionsNull::LeaveSession(FLeaveSession::Params&& Params)
{
	TOnlineAsyncOpRef<FLeaveSession> Op = GetOp<FLeaveSession>(MoveTemp(Params));

	Op->Then([this](TOnlineAsyncOp<FLeaveSession>& Op) mutable
	{
		const FLeaveSession::Params& OpParams = Op.GetParams();

		FOnlineError StateCheck = CheckLeaveSessionState(OpParams);
		if (StateCheck != Errors::Success())
		{
			Op.SetError(MoveTemp(StateCheck));
			return;
		}

		TSharedRef<FSession>& FoundSession = SessionsByName.FindChecked(OpParams.SessionName);
		
		// Only the host should stop the session at this point
		if (FoundSession->OwnerUserId == OpParams.LocalUserId && FoundSession->SessionSettings.JoinPolicy == ESessionJoinPolicy::Public)
		{
			StopLANSession();
		}

		// TODO: Remove all local users from session

		SessionsById.Remove(FoundSession->SessionId);
		SessionsByName.Remove(OpParams.SessionName);

		Op.SetResult(FLeaveSession::Result{ });

		FSessionLeft SessionLeftEvent;
		SessionLeftEvent.LocalUserIds.Append(OpParams.LocalUsers);
		SessionEvents.OnSessionLeft.Broadcast(SessionLeftEvent);
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

/** FSessionsLAN */

void FSessionsNull::AppendSessionToPacket(FNboSerializeToBuffer& Packet, const FSessionNull& Session)
{
	using namespace NboSerializerNullSvc;

	// We won't save CurrentState in the packet as all advertised sessions will be Valid
	SerializeToBuffer(Packet, Session.OwnerUserId);
	SerializeToBuffer(Packet, Session.SessionId);
	Packet << *Session.OwnerInternetAddr;

	// TODO: Write session settings to packet, after SchemaVariant work
}

void FSessionsNull::ReadSessionFromPacket(FNboSerializeFromBuffer& Packet, FSessionNull& Session)
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