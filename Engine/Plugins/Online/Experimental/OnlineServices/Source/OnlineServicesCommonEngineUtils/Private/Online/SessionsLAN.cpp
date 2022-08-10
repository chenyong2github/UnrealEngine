// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/SessionsLAN.h"

#include "Misc/Guid.h"
#include "Online/OnlineServicesCommon.h"
#include "Online/OnlineServicesCommonEngineUtils.h"
#include "SocketSubsystem.h"
#include "UObject/CoreNet.h"

namespace UE::Online {

/** FOnlineSessionIdRegistryLAN */

FOnlineSessionIdRegistryLAN& FOnlineSessionIdRegistryLAN::Get()
{
	static FOnlineSessionIdRegistryLAN Instance;
	return Instance;
}

FOnlineSessionIdHandle FOnlineSessionIdRegistryLAN::GetNextSessionId()
{
	return BasicRegistry.FindOrAddHandle(FGuid::NewGuid().ToString());
}

bool FOnlineSessionIdRegistryLAN::IsSessionIdExpired(const FOnlineSessionIdHandle& InHandle) const
{
	return BasicRegistry.FindIdValue(InHandle).IsEmpty();
}

/** FSessionLAN */

FSessionLAN::FSessionLAN()
{
	Initialize();
}

void FSessionLAN::Initialize()
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

	// Now set the port that was configured
	OwnerInternetAddr->SetPort(GetPortFromNetDriver(NAME_None));

	if (OwnerInternetAddr->GetPort() == 0)
	{
		OwnerInternetAddr->SetPort(7777); // Default port
	}
}

FSessionLAN& FSessionLAN::Cast(FSession& InSession)
{
	return static_cast<FSessionLAN&>(InSession);
}

const FSessionLAN& FSessionLAN::Cast(const FSession& InSession)
{
	return static_cast<const FSessionLAN&>(InSession);
}

/** FSessionsLAN */

FSessionsLAN::FSessionsLAN(FOnlineServicesCommon& InServices)
	: Super(InServices)
	, LANSessionManager(MakeShared<FLANSession>())
{

}

void FSessionsLAN::Tick(float DeltaSeconds)
{
	if (LANSessionManager->GetBeaconState() > ELanBeaconState::NotUsingLanBeacon)
	{
		LANSessionManager->Tick(DeltaSeconds);
	}

	Super::Tick(DeltaSeconds);
}

TOnlineAsyncOpHandle<FCreateSession> FSessionsLAN::CreateSession(FCreateSession::Params&& Params)
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
		if (TOptional<FOnlineError> TryHostLANSessionResult = TryHostLANSession())
		{
			Op->SetError(MoveTemp(TryHostLANSessionResult.GetValue()));
			return Op->GetHandle();
		}
	}

	Op->Then([this](TOnlineAsyncOp<FCreateSession>& Op)
	{
		const FCreateSession::Params& OpParams = Op.GetParams();

		FOnlineError StateCheck = CheckCreateSessionState(OpParams);
		if (StateCheck != Errors::Success())
		{
			Op.SetError(MoveTemp(StateCheck));
			return;
		}

		TSharedRef<FSessionLAN> NewSessionLANRef = MakeShared<FSessionLAN>();
		NewSessionLANRef->CurrentState = ESessionState::Valid;
		NewSessionLANRef->OwnerUserId = OpParams.LocalUserId;
		NewSessionLANRef->SessionId = FOnlineSessionIdRegistryLAN::Get().GetNextSessionId();
		NewSessionLANRef->SessionSettings = OpParams.SessionSettings;

		// For LAN sessions, we'll add all the Session members manually instead of calling JoinSession since there is no API calls involved
		NewSessionLANRef->SessionSettings.SessionMembers.Append(OpParams.LocalUsers);

		LocalSessionsByName.Add(OpParams.SessionName, NewSessionLANRef);

		Op.SetResult(FCreateSession::Result{ NewSessionLANRef });
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FUpdateSession> FSessionsLAN::UpdateSession(FUpdateSession::Params&& Params)
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

		TSharedRef<FSession>& FoundSession = LocalSessionsByName.FindChecked(OpParams.SessionName);
		FSessionSettings& SessionSettings = FoundSession->SessionSettings;
		const FSessionSettingsUpdate& UpdatedSettings = OpParams.Mutations;

		if (UpdatedSettings.JoinPolicy.IsSet())
		{
			// Changes in the join policy setting will mean we start or stop the LAN Beacon broadcasting the session information
			// There is no FriendsLAN interface at the time of this implementation, hence the binary behavior (Public/Non-Public)
			if (SessionSettings.JoinPolicy != ESessionJoinPolicy::Public && UpdatedSettings.JoinPolicy.GetValue() == ESessionJoinPolicy::Public)
			{
				if (TOptional<FOnlineError> TryHostLANSessionResult = TryHostLANSession())
				{
					Op.SetError(MoveTemp(TryHostLANSessionResult.GetValue()));
					return;
				}
			}
			else if (SessionSettings.JoinPolicy == ESessionJoinPolicy::Public && UpdatedSettings.JoinPolicy.GetValue() != ESessionJoinPolicy::Public)
			{
				StopLANSession();
			}
		}

		SessionSettings += UpdatedSettings;

		// We set the result and fire the event
		Op.SetResult(FUpdateSession::Result{ FoundSession });

		FSessionUpdated SessionUpdatedEvent{ FoundSession, UpdatedSettings };
		SessionEvents.OnSessionUpdated.Broadcast(SessionUpdatedEvent);
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FFindSessions> FSessionsLAN::FindSessions(FFindSessions::Params&& Params)
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

TOnlineAsyncOpHandle<FJoinSession> FSessionsLAN::JoinSession(FJoinSession::Params&& Params)
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

		TOnlineResult<FGetSessionById> GetSessionByIdResult = GetSessionById({ OpParams.LocalUserId, OpParams.SessionId });
		if (GetSessionByIdResult.IsError())
		{
			// If no result is found, the id might be expired, which we should notify
			if (FOnlineSessionIdRegistryLAN::Get().IsSessionIdExpired(OpParams.SessionId))
			{
				UE_LOG(LogTemp, Warning, TEXT("[FSessionsLAN::JoinSession] SessionId parameter [%s] is expired. Please call FindSessions to get an updated list of available sessions "), *ToLogString(OpParams.SessionId));
			}

			Op.SetError(MoveTemp(GetSessionByIdResult.GetErrorValue()));
			return ;
		}

		const TSharedRef<const FSession>& FoundSession = GetSessionByIdResult.GetOkValue().Session;
		TSharedRef<FSession> NewSessionRef = ConstCastSharedRef<FSession>(FoundSession);

		LocalSessionsByName.Add(OpParams.SessionName, NewSessionRef);

		Op.SetResult(FJoinSession::Result{ NewSessionRef });

		TArray<FOnlineAccountIdHandle> LocalUserIds;
		LocalUserIds.Reserve(OpParams.LocalUsers.Num());
		OpParams.LocalUsers.GenerateKeyArray(LocalUserIds);

		FSessionJoined SessionJoinedEvent = { MoveTemp(LocalUserIds), NewSessionRef };
		
		SessionEvents.OnSessionJoined.Broadcast(SessionJoinedEvent);
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

TOnlineAsyncOpHandle<FLeaveSession> FSessionsLAN::LeaveSession(FLeaveSession::Params&& Params)
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

		// This scope guarantees that FoundSession can't be used after its removal
		{
			TSharedRef<FSession>& FoundSession = LocalSessionsByName.FindChecked(OpParams.SessionName);

			// Only the host should stop the session at this point
			if (FoundSession->OwnerUserId == OpParams.LocalUserId && FoundSession->SessionSettings.JoinPolicy == ESessionJoinPolicy::Public)
			{
				StopLANSession();
			}

			LocalSessionsByName.Remove(OpParams.SessionName);
		}

		Op.SetResult(FLeaveSession::Result{ });

		FSessionLeft SessionLeftEvent;
		SessionLeftEvent.LocalUserIds.Append(OpParams.LocalUsers);
		SessionEvents.OnSessionLeft.Broadcast(SessionLeftEvent);
	})
	.Enqueue(GetSerialQueue());

	return Op->GetHandle();
}

/** LANSessionManager methods */

TOptional<FOnlineError> FSessionsLAN::TryHostLANSession()
{
	TOptional<FOnlineError> Result;

	// The LAN Beacon can only broadcast one session at a time
	if (LANSessionManager->GetBeaconState() == ELanBeaconState::NotUsingLanBeacon)
	{
		FOnValidQueryPacketDelegate QueryPacketDelegate = FOnValidQueryPacketDelegate::CreateThreadSafeSP(this, &FSessionsLAN::OnValidQueryPacketReceived);

		if (LANSessionManager->Host(QueryPacketDelegate))
		{
			UE_LOG(LogTemp, VeryVerbose, TEXT("[FSessionsLAN::TryHostLANSession] LAN Beacon hosting..."));

			PublicSessionsHosted++;
		}
		else
		{
			UE_LOG(LogTemp, VeryVerbose, TEXT("[FSessionsLAN::TryHostLANSession] LAN Beacon failed to host!"));

			LANSessionManager->StopLANSession();

			Result.Emplace(Errors::RequestFailure()); // TODO: May need a new error type
		}
	}
	else
	{
		UE_LOG(LogTemp, VeryVerbose, TEXT("[FSessionsLAN::TryHostLANSession] LAN Beacon already in use!"));

		Result.Emplace(Errors::RequestFailure()); // TODO: May need a new error type
	}

	return Result;
}

void FSessionsLAN::FindLANSessions()
{
	// Recreate the unique identifier for this client
	GenerateNonce((uint8*)&LANSessionManager->LanNonce, 8);

	// Bind delegates
	FOnValidResponsePacketDelegate ResponseDelegate = FOnValidResponsePacketDelegate::CreateThreadSafeSP(this, &FSessionsLAN::OnValidResponsePacketReceived);
	FOnSearchingTimeoutDelegate TimeoutDelegate = FOnSearchingTimeoutDelegate::CreateThreadSafeSP(this, &FSessionsLAN::OnLANSearchTimeout);

	FNboSerializeToBuffer Packet(LAN_BEACON_MAX_PACKET_SIZE);
	LANSessionManager->CreateClientQueryPacket(Packet, LANSessionManager->LanNonce);
	if (LANSessionManager->Search(Packet, ResponseDelegate, TimeoutDelegate) == false)
	{
		UE_LOG(LogTemp, Warning, TEXT("[FSessionsLAN::FindLANSessions] Search failed!"));

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
		UE_LOG(LogTemp, Warning, TEXT("[]FSessionsLAN::FindLANSessions] Searching...."));
	}
}

void FSessionsLAN::StopLANSession()
{
	check(PublicSessionsHosted > 0);

	PublicSessionsHosted--;

	if (PublicSessionsHosted == 0)
	{
		LANSessionManager->StopLANSession();
	}
}

void FSessionsLAN::OnValidQueryPacketReceived(uint8* PacketData, int32 PacketLength, uint64 ClientNonce)
{
	// Iterate through all registered sessions and respond for each one that can be joinable
	for (const TPair<FName, TSharedRef<FSession>>& Entry : LocalSessionsByName)
	{
		const TSharedRef<FSession>& Session = Entry.Value;

		if (Session->SessionSettings.JoinPolicy == ESessionJoinPolicy::Public)
		{
			FNboSerializeToBuffer Packet(LAN_BEACON_MAX_PACKET_SIZE);
			// Create the basic header before appending additional information
			LANSessionManager->CreateHostResponsePacket(Packet, ClientNonce);

			// Add all the session details
			AppendSessionToPacket(Packet, FSessionLAN::Cast(*Session));

			// Broadcast this response so the client can see us
			if (!Packet.HasOverflow())
			{
				LANSessionManager->BroadcastPacket(Packet, Packet.GetByteCount());
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[FSessionsLAN::OnValidQueryPacketReceived] LAN broadcast packet overflow, cannot broadcast on LAN"));
			}
		}
	}
}

void FSessionsLAN::OnValidResponsePacketReceived(uint8* PacketData, int32 PacketLength)
{
	if (CurrentSessionSearch.IsValid())
	{
		TSharedRef<FSessionLAN> Session = MakeShared<FSessionLAN>();

		FNboSerializeFromBuffer Packet(PacketData, PacketLength);
		ReadSessionFromPacket(Packet, *Session);

		CurrentSessionSearch->FoundSessions.Add(Session);

		TMap<FOnlineSessionIdHandle, TSharedRef<FSession>>& UserMap = SessionSearchResultsUserMap.FindOrAdd(CurrentSessionSearchHandle->GetParams().LocalUserId);
		UserMap.Emplace(Session->SessionId, Session);
	}
}

void FSessionsLAN::OnLANSearchTimeout()
{
	if (LANSessionManager->GetBeaconState() == ELanBeaconState::Searching)
	{
		LANSessionManager->StopLANSession();
	}

	if (!CurrentSessionSearch.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[FSessionsLAN::OnLANSearchTimeout] Session search invalid"));

		CurrentSessionSearchHandle->SetError(Errors::InvalidState());
	}

	if (CurrentSessionSearch->FoundSessions.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[FSessionsLAN::OnLANSearchTimeout] Session search found no results"));

		CurrentSessionSearchHandle->SetError(Errors::NotFound());
	}

	UE_LOG(LogTemp, Warning, TEXT("[FSessionsLAN::OnLANSearchTimeout] %d sessions found!"), CurrentSessionSearch->FoundSessions.Num());

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

/* UE::Online */ }