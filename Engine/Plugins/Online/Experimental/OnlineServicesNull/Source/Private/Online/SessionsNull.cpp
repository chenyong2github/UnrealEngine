// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/SessionsNull.h"

#include "Misc/Guid.h"
#include "Online/NboSerializerNullSvc.h"
#include "Online/OnlineServicesNull.h"
#include "SocketSubsystem.h"
#include "UObject/CoreNet.h"
#if WITH_ENGINE
	#include "OnlineSubsystemUtils.h" // Needed for GetPortFromNetDriver
#endif

namespace UE::Online {

/** FOnlineSessionIdRegistryNull */

FOnlineSessionIdRegistryNull& FOnlineSessionIdRegistryNull::Get()
{
	static FOnlineSessionIdRegistryNull Instance;
	return Instance;
}

FOnlineSessionIdHandle FOnlineSessionIdRegistryNull::Find(FString SessionId) const
{
	if(const FOnlineSessionIdString* Entry = StringToId.Find(SessionId))
	{
		return Entry->Handle;
	}
	return FOnlineSessionIdHandle();
}

FOnlineSessionIdHandle FOnlineSessionIdRegistryNull::FindOrAdd(FString SessionId)
{
	const FOnlineSessionIdString* Entry = StringToId.Find(SessionId);
	if (Entry)
	{
		return Entry->Handle;
	}

	FOnlineSessionIdString& NewId = Ids.Emplace_GetRef();
	NewId.Data = SessionId;
	NewId.Handle = FOnlineSessionIdHandle(EOnlineServices::Null, Ids.Num());

	StringToId.Add(SessionId, NewId);

	return NewId.Handle;
}

FOnlineSessionIdHandle FOnlineSessionIdRegistryNull::GetNext()
{
	return FindOrAdd(FGuid().ToString());
}

const FOnlineSessionIdString* FOnlineSessionIdRegistryNull::GetInternal(const FOnlineSessionIdHandle& Handle) const
{
	if (Handle.IsValid() && Handle.GetOnlineServicesType() == EOnlineServices::Null && Handle.GetHandle() <= (uint32)Ids.Num())
	{
		return &Ids[Handle.GetHandle() - 1];
	}
	return nullptr;
}

FString FOnlineSessionIdRegistryNull::ToLogString(const FOnlineSessionIdHandle& Handle) const
{
	if (const FOnlineSessionIdString* Id = GetInternal(Handle))
	{
		return Id->Data;
	}

	return FString(TEXT("[InvalidSessionID]"));
}

TArray<uint8> FOnlineSessionIdRegistryNull::ToReplicationData(const FOnlineSessionIdHandle& Handle) const
{
	if (const FOnlineSessionIdString* Id = GetInternal(Handle))
	{
		TArray<uint8> ReplicationData;
		ReplicationData.SetNumUninitialized(Id->Data.Len());
		StringToBytes(Id->Data, ReplicationData.GetData(), Id->Data.Len());
		return ReplicationData;
	}

	return TArray<uint8>();;
}

FOnlineSessionIdHandle FOnlineSessionIdRegistryNull::FromReplicationData(const TArray<uint8>& ReplicationData)
{
	FString Result = BytesToString(ReplicationData.GetData(), ReplicationData.Num());
	if (Result.Len() > 0)
	{
		return FindOrAdd(Result);
	}
	return FOnlineSessionIdHandle();
}

/** FSessionNull */

FSessionNull::FSessionNull()
	: FSessionNull(FSession())
{
}

FSessionNull::FSessionNull(const FSession& InSession)
	: FSession(InSession)
{
	bool bCanBindAll;
	OwnerInternetAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLocalHostAddr(*GLog, bCanBindAll);

	// The below is a workaround for systems that set hostname to a distinct address from 127.0.0.1 on a loop back interface.
	// See e.g. https://www.debian.org/doc/manuals/debian-reference/ch05.en.html#_the_hostname_resolution
	// and http://serverfault.com/questions/363095/why-does-my-hostname-appear-with-the-address-127-0-1-1-rather-than-127-0-0-1-in
	// Since we bind to 0.0.0.0, we won't answer on 127.0.1.1, so we need to advertise ourselves as 127.0.0.1 for any other loop back address we may have.

	uint32 HostIp = 0;
	OwnerInternetAddr->GetIp(HostIp); // Will return in host order

	// If this address is on loop back interface, advertise it as 127.0.0.1
	if ((HostIp & 0xff000000) == 0x7f000000)
	{
		OwnerInternetAddr->SetIp(0x7f000001); // 127.0.0.1
	}

#if WITH_ENGINE
	// Now set the port that was configured
	OwnerInternetAddr->SetPort(GetPortFromNetDriver(NAME_None));
#endif

	if (OwnerInternetAddr->GetPort() == 0)
	{
		OwnerInternetAddr->SetPort(7777); // Default port
	}
}

/** FSessionsNull */

FSessionsNull::FSessionsNull(FOnlineServicesNull& InServices)
	: Super(InServices)
	, Services(InServices)
{
	if (!LANSessionManager.IsValid())
	{
		LANSessionManager = MakeShareable(new FLANSession());
	}
}

void FSessionsNull::Tick(float DeltaSeconds)
{
	if (LANSessionManager.IsValid() && LANSessionManager->GetBeaconState() > ELanBeaconState::NotUsingLanBeacon)
	{
		LANSessionManager->Tick(DeltaSeconds);
	}

	Super::Tick(DeltaSeconds);
}

TOnlineResult<FGetAllSessions> FSessionsNull::GetAllSessions(FGetAllSessions::Params&& Params)
{
	FGetAllSessions::Result Result;

	for (TMap<FName, TSharedRef<FSessionNull>>::TConstIterator It(SessionsByName); It; ++It)
	{
		Result.Sessions.Add(It.Value());
	}

	return TOnlineResult<FGetAllSessions>(MoveTemp(Result));
}

TOnlineResult<FGetSessionByName> FSessionsNull::GetSessionByName(FGetSessionByName::Params&& Params)
{
	if (TSharedRef<FSessionNull>* FoundSession = SessionsByName.Find(Params.LocalName))
	{
		return TOnlineResult<FGetSessionByName>({ *FoundSession });
	}
	else
	{
		return TOnlineResult<FGetSessionByName>(Errors::NotFound());
	}
}

TOnlineResult<FGetSessionById> FSessionsNull::GetSessionById(FGetSessionById::Params&& Params)
{
	if (TSharedRef<FSessionNull>* FoundSession = SessionsById.Find(Params.IdHandle))
	{
		return TOnlineResult<FGetSessionById>({ *FoundSession });
	}
	else
	{
		return TOnlineResult<FGetSessionById>(Errors::NotFound());
	}
}

TOnlineAsyncOpHandle<FCreateSession> FSessionsNull::CreateSession(FCreateSession::Params&& Params)
{
	TOnlineAsyncOpRef<FCreateSession> Op = GetOp<FCreateSession>(MoveTemp(Params));
	const FCreateSession::Params& OpParams = Op->GetParams();

	if (TSharedRef<FSessionNull>* FoundSession = SessionsByName.Find(OpParams.SessionName))
	{
		UE_LOG(LogTemp, Warning, TEXT("[FSessionsNull::CreateSession] Could not create session with name [%s]. A session with that name already exists"), *OpParams.SessionName.ToString());

		Op->SetError(Errors::InvalidParams()); // TODO: New error: Session with name %s already exists
		return Op->GetHandle();
	}

	if (OpParams.SessionSettings.NumMaxPrivateConnections <= 0 && OpParams.SessionSettings.NumMaxPublicConnections <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FSessionsNull::CreateSession] Could not create session with no valid NumMaxPrivateConnections [%d] or NumMaxPublicConnections [%d]"), OpParams.SessionSettings.NumMaxPrivateConnections, OpParams.SessionSettings.NumMaxPublicConnections);

		Op->SetError(Errors::InvalidParams());
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

		FSessionNull NewSession = FSessionNull();
		NewSession.CurrentState = ESessionState::Valid;
		NewSession.OwnerUserId = OpParams.LocalUserId;
		NewSession.SessionId = FOnlineSessionIdRegistryNull::Get().GetNext();
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

		if (TSharedRef<FSessionNull>* FoundSession = SessionsByName.Find(OpParams.SessionName))
		{
			// If the session exists, we'll check and update all settings one by one, with additional logic wherever required

			FSessionSettings& SessionSettings = FoundSession->Get().SessionSettings;
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
			Op.SetResult(FUpdateSession::Result{ *FoundSession });

			FSessionUpdated SessionUpdatedEvent{ *FoundSession, UpdatedSettings };
			SessionEvents.OnSessionUpdated.Broadcast(SessionUpdatedEvent);
		}
		else
		{
			Op.SetError(Errors::NotFound());
		}
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FFindSessions> FSessionsNull::FindSessions(FFindSessions::Params&& Params)
{
	if (!CurrentSessionSearch.IsValid())
	{
		CurrentSessionSearch = MakeShared<FFindSessions::Result>();
		CurrentSessionSearchHandle = GetOp<FFindSessions>(MoveTemp(Params));

		FindLANSessions();

		return CurrentSessionSearchHandle->GetHandle();
	}
	else
	{
		TOnlineAsyncOpRef<FFindSessions> Operation = GetOp<FFindSessions>(MoveTemp(Params));
		Operation->SetError(Errors::AlreadyPending());
		return Operation->GetHandle();
	}
}

TOnlineAsyncOpHandle<FJoinSession> FSessionsNull::JoinSession(FJoinSession::Params&& Params)
{
	TOnlineAsyncOpRef<FJoinSession> Op = GetOp<FJoinSession>(MoveTemp(Params));
	const FJoinSession::Params& OpParams = Op->GetParams();

	// First, check if a session with that name already exists
	if (TSharedRef<FSessionNull>* FoundSession = SessionsByName.Find(OpParams.SessionName))
	{
		Op->SetError(Errors::InvalidParams()); // TODO: New error: Session with name %s already exists
		return Op->GetHandle();
	}

	const FSessionSettings& SessionSettings = OpParams.Session->SessionSettings;

	TArray<FOnlineAccountIdHandle> LocalUserIds;
	LocalUserIds.Reserve(OpParams.LocalUsers.Num());
	OpParams.LocalUsers.GenerateKeyArray(LocalUserIds);

	for (const FOnlineAccountIdHandle& LocalUserId : LocalUserIds)
	{
		if (SessionSettings.SessionMembers.Contains(LocalUserId))
		{
			Op->SetError(Errors::AccessDenied()); // TODO: Bespoke error: User %s already in session
			return Op->GetHandle();
		}
	}

	if (!SessionSettings.bAllowNewMembers)
	{
		Op->SetError(Errors::AccessDenied());
		return Op->GetHandle();
	}

	if (!SessionSettings.bAllowUnregisteredPlayers)
	{
		for (const FOnlineAccountIdHandle& LocalUserId : LocalUserIds)
		{
			if (!SessionSettings.RegisteredUsers.Contains(LocalUserId))
			{
				Op->SetError(Errors::AccessDenied());
				return Op->GetHandle();
			}
		}
	}

	if (SessionSettings.NumMaxPublicConnections > 0 && SessionSettings.NumOpenPublicConnections < LocalUserIds.Num())
	{
		Op->SetError(Errors::AccessDenied());
		return Op->GetHandle();
	}

	if (SessionSettings.NumMaxPrivateConnections > 0 && SessionSettings.NumOpenPrivateConnections < LocalUserIds.Num())
	{
		Op->SetError(Errors::AccessDenied());
		return Op->GetHandle();
	}

	// If no restrictions apply, we'll save our copy of the session, add the new players, and register it
	Op->Then([this](TOnlineAsyncOp<FJoinSession>& Op) mutable
	{
		const FJoinSession::Params& OpParams = Op.GetParams();
		const FSessionSettings& SessionSettings = OpParams.Session->SessionSettings;

		TSharedRef<FSessionNull> NewSessionNullRef = MakeShared<FSessionNull>(FSessionNull(*OpParams.Session));
		FSessionSettings NewSessionNullSettings = NewSessionNullRef->SessionSettings;

		FSessionJoined SessionJoinedEvent;
		SessionJoinedEvent.Session = NewSessionNullRef;

		TArray<FOnlineAccountIdHandle> LocalUserIds;
		LocalUserIds.Reserve(OpParams.LocalUsers.Num());
		OpParams.LocalUsers.GenerateKeyArray(LocalUserIds);

		for (const FOnlineAccountIdHandle& LocalUserId : LocalUserIds)
		{
			bool bReserveSlot = true;

			if (const FRegisteredUser* RegisteredUser = SessionSettings.RegisteredUsers.Find(LocalUserId))
			{
				bReserveSlot = !RegisteredUser->bHasReservedSlot;
			}

			if (bReserveSlot)
			{
				if (SessionSettings.NumOpenPublicConnections > 0)
				{
					NewSessionNullSettings.NumOpenPublicConnections--;
				}
				else if (SessionSettings.NumOpenPrivateConnections > 0)
				{
					NewSessionNullSettings.NumOpenPrivateConnections--;
				}
			}

			SessionJoinedEvent.LocalUserIds.Add(LocalUserId);
		}

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

		if (TSharedRef<FSessionNull>* FoundSession = SessionsByName.Find(OpParams.SessionName))
		{
			// Only the host should stop the session at this point
			if (FoundSession->Get().OwnerUserId == OpParams.LocalUserId && FoundSession->Get().SessionSettings.JoinPolicy == ESessionJoinPolicy::Public)
			{
				StopLANSession();
			}

			SessionsById.Remove((*FoundSession)->SessionId);
			SessionsByName.Remove(OpParams.SessionName);

			Op.SetResult(FLeaveSession::Result{ });

			FSessionLeft SessionLeftEvent;
			SessionLeftEvent.LocalUserIds.Add(OpParams.LocalUserId);
			SessionLeftEvent.LocalUserIds.Append(OpParams.AdditionalLocalUsers);
			SessionEvents.OnSessionLeft.Broadcast(SessionLeftEvent);
		}
		else
		{
			Op.SetError(Errors::NotFound());
		}
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

// TODO: The logic for player registration vs adding a new session member is likely to be updated
TOnlineAsyncOpHandle<FRegisterPlayers> FSessionsNull::RegisterPlayers(FRegisterPlayers::Params&& Params)
{
	TOnlineAsyncOpRef<FRegisterPlayers> Op = GetOp<FRegisterPlayers>(MoveTemp(Params));

	Op->Then([this](TOnlineAsyncOp<FRegisterPlayers>& Op) mutable
	{
		const FRegisterPlayers::Params& OpParams = Op.GetParams();

		if (TSharedRef<FSessionNull>* FoundSession = SessionsByName.Find(OpParams.SessionName))
		{
			FSessionSettings& SessionSettings = FoundSession->Get().SessionSettings;

			for (const FOnlineAccountIdHandle& User : OpParams.TargetUsers)
			{
				FRegisteredUser& RegisteredUser = SessionSettings.RegisteredUsers.FindOrAdd(User);

				// RegisterPlayers will not revoke a slot reserved for a registered user
				if (!RegisteredUser.bHasReservedSlot && OpParams.bReserveSlot)
				{
					bool bSlotReserved = false;
					if (SessionSettings.NumOpenPublicConnections > 0)
					{
						SessionSettings.NumOpenPublicConnections--;
						bSlotReserved = true;
					}
					else if (SessionSettings.NumOpenPrivateConnections > 0)
					{
						SessionSettings.NumOpenPrivateConnections--;
						bSlotReserved = true;
					}

					RegisteredUser.bHasReservedSlot = bSlotReserved;
				}

				if (const FSessionMember* SessionMember = SessionSettings.SessionMembers.Find(User))
				{
					RegisteredUser.bIsInSession = true;
				}
			}

			Op.SetResult(FRegisterPlayers::Result{});
		}
		else
		{
			Op.SetError(Errors::NotFound());
		}
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

// TODO: The logic for player registration removal vs removing a session member is likely to be updated
TOnlineAsyncOpHandle<FUnregisterPlayers> FSessionsNull::UnregisterPlayers(FUnregisterPlayers::Params&& Params)
{
	TOnlineAsyncOpRef<FUnregisterPlayers> Op = GetOp<FUnregisterPlayers>(MoveTemp(Params));

	Op->Then([this](TOnlineAsyncOp<FUnregisterPlayers>& Op) mutable
	{
		const FUnregisterPlayers::Params& OpParams = Op.GetParams();

		if (TSharedRef<FSessionNull>* FoundSession = SessionsByName.Find(OpParams.SessionName))
		{
			FSessionSettings& SessionSettings = FoundSession->Get().SessionSettings;

			for (const FOnlineAccountIdHandle& User : OpParams.TargetUsers)
			{
				if (const FRegisteredUser* RegisteredUser = SessionSettings.RegisteredUsers.Find(User))
				{
					SessionSettings.RegisteredUsers.Remove(User);

					bool bRemoveUserFromSession = RegisteredUser->bIsInSession && OpParams.bRemoveUnregisteredPlayers;

					if (bRemoveUserFromSession)
					{
						SessionSettings.SessionMembers.Remove(User);
					}

					if (RegisteredUser->bHasReservedSlot || bRemoveUserFromSession)
					{
						if (SessionSettings.NumOpenPublicConnections < SessionSettings.NumMaxPublicConnections)
						{
							SessionSettings.NumOpenPublicConnections++;
						}
						else if (SessionSettings.NumOpenPrivateConnections < SessionSettings.NumMaxPrivateConnections)
						{
							SessionSettings.NumOpenPrivateConnections++;
						}
					}
				}
			}

			Op.SetResult(FUnregisterPlayers::Result{});
		}
		else
		{
			Op.SetError(Errors::NotFound());
		}
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

/** LAN Methods */

bool FSessionsNull::TryHostLANSession()
{
	// The LAN Beacon can only broadcast one session at a time
	if (LANSessionManager->GetBeaconState() == ELanBeaconState::NotUsingLanBeacon)
	{
		FOnValidQueryPacketDelegate QueryPacketDelegate = FOnValidQueryPacketDelegate::CreateRaw(this, &FSessionsNull::OnValidQueryPacketReceived);

		if (LANSessionManager->Host(QueryPacketDelegate))
		{
			UE_LOG(LogTemp, VeryVerbose, TEXT("[FSessionsNull::TryHostLANSession] LAN Beacon hosting..."));

			PublicSessionsHosted++;

			return true;
		}
		else
		{
			UE_LOG(LogTemp, VeryVerbose, TEXT("[FSessionsNull::TryHostLANSession] LAN Beacon failed to host!"));

			LANSessionManager->StopLANSession();
		}
	}
	else
	{
		UE_LOG(LogTemp, VeryVerbose, TEXT("[FSessionsNull::TryHostLANSession] LAN Beacon already in use!"));
	}

	return false;
}

void FSessionsNull::FindLANSessions()
{
	// Recreate the unique identifier for this client
	GenerateNonce((uint8*)&LANSessionManager->LanNonce, 8);

	// Bind delegates
	FOnValidResponsePacketDelegate ResponseDelegate = FOnValidResponsePacketDelegate::CreateRaw(this, &FSessionsNull::OnValidResponsePacketReceived);
	FOnSearchingTimeoutDelegate TimeoutDelegate = FOnSearchingTimeoutDelegate::CreateRaw(this, &FSessionsNull::OnLANSearchTimeout);

	FNboSerializeToBufferNullSvc Packet(LAN_BEACON_MAX_PACKET_SIZE);
	LANSessionManager->CreateClientQueryPacket(Packet, LANSessionManager->LanNonce);
	if (LANSessionManager->Search(Packet, ResponseDelegate, TimeoutDelegate) == false)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FSessionsNull::FindLANSessions] Search failed!"));

		if (LANSessionManager->GetBeaconState() == ELanBeaconState::Searching)
		{
			LANSessionManager->StopLANSession();
		}

		// Trigger the delegate as having failed
		CurrentSessionSearchHandle->SetError(Errors::RequestFailure()); // TODO: May need a new error type

		CurrentSessionSearch.Reset();
		CurrentSessionSearchHandle.Reset();

		// If we were hosting public sessions before the search, we'll return the beacon to that state
		if (PublicSessionsHosted > 0)
		{
			TryHostLANSession();
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[]FSessionsNull::FindLANSessions] Searching...."));
	}
}

void FSessionsNull::StopLANSession()
{
	PublicSessionsHosted--;

	if (PublicSessionsHosted <= 0)
	{
		LANSessionManager->StopLANSession();
	}
}

void FSessionsNull::OnValidQueryPacketReceived(uint8* PacketData, int32 PacketLength, uint64 ClientNonce)
{
	// Iterate through all registered sessions and respond for each one that can be joinable
	for (TMap<FName, TSharedRef<FSessionNull>>::TConstIterator It(SessionsByName); It; ++It)
	{
		const TSharedRef<FSessionNull>& Session = It.Value();

		if (Session->SessionSettings.JoinPolicy == ESessionJoinPolicy::Public)
		{
			FNboSerializeToBufferNullSvc Packet(LAN_BEACON_MAX_PACKET_SIZE);
			// Create the basic header before appending additional information
			LANSessionManager->CreateHostResponsePacket(Packet, ClientNonce);

			// Add all the session details
			AppendSessionToPacket(Packet, Session);

			// Broadcast this response so the client can see us
			if (!Packet.HasOverflow())
			{
				LANSessionManager->BroadcastPacket(Packet, Packet.GetByteCount());
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[FSessionsNull::OnValidQueryPacketReceived] LAN broadcast packet overflow, cannot broadcast on LAN"));
			}
		}
	}
}

void FSessionsNull::OnValidResponsePacketReceived(uint8* PacketData, int32 PacketLength)
{
	if (CurrentSessionSearch.IsValid())
	{
		TSharedRef<FSessionNull> Session = MakeShared<FSessionNull>();

		FNboSerializeFromBufferNullSvc Packet(PacketData, PacketLength);
		ReadSessionFromPacket(Packet, Session);

		CurrentSessionSearch->FoundSessions.Add(Session);
	}
}

void FSessionsNull::OnLANSearchTimeout()
{
	if (LANSessionManager->GetBeaconState() == ELanBeaconState::Searching)
	{
		LANSessionManager->StopLANSession();
	}

	if (!CurrentSessionSearch.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[FSessionsNull::OnLANSearchTimeout] Session search invalid"));

		CurrentSessionSearchHandle->SetError(Errors::InvalidState());
	}

	if (CurrentSessionSearch->FoundSessions.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[FSessionsNull::OnLANSearchTimeout] Session search found no results"));

		CurrentSessionSearchHandle->SetError(Errors::NotFound());
	}

	UE_LOG(LogTemp, Warning, TEXT("[FSessionsNull::OnLANSearchTimeout] %d sessions found!"), CurrentSessionSearch->FoundSessions.Num());

	FFindSessions::Result Result = *CurrentSessionSearch;
	CurrentSessionSearchHandle->SetResult(MoveTemp(Result));

	CurrentSessionSearch.Reset();
	CurrentSessionSearchHandle.Reset();

	// If we were hosting public sessions before the search, we'll return the beacon to that state
	if (PublicSessionsHosted > 0)
	{
		TryHostLANSession();
	}
}

void FSessionsNull::AppendSessionToPacket(FNboSerializeToBufferNullSvc& Packet, const TSharedRef<FSessionNull>& Session)
{
	// We won't save CurrentState in the packet as all advertised sessions will be Valid
	Packet << Session->OwnerUserId
		<< Session->SessionId
		<< *Session->OwnerInternetAddr;

	// TODO: Write session settings to packet, after SchemaVariant work
}

void FSessionsNull::ReadSessionFromPacket(FNboSerializeFromBufferNullSvc& Packet, const TSharedRef<FSessionNull>& Session)
{
	Packet >> Session->OwnerUserId
		>> Session->SessionId
		>> *Session->OwnerInternetAddr;

	// We'll set the connect address for the remote session as a custom parameter, so it can be read in OnlineServices' GetResolvedConnectString
	FCustomSessionSetting ConnectString;
	ConnectString.Data.Set<FString>(Session->OwnerInternetAddr->ToString(true));
	ConnectString.Visibility = ECustomSessionSettingVisibility::ViaOnlineService;
	Session->SessionSettings.CustomSettings.Add(CONNECT_STRING_TAG, ConnectString);

	// TODO:: Read session settings from packet, after SchemaVariant work
}

/* UE::Online */ }