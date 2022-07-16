// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/SessionsLAN.h"

#include "Misc/Guid.h"
#include "Online/OnlineServicesCommon.h"
#include "Online/OnlineServicesCommonEngineUtils.h"
#include "SocketSubsystem.h"
#include "UObject/CoreNet.h"

namespace UE::Online {

/** FSessionLAN */

FSessionLAN::FSessionLAN()
{
	Initialize();
}

FSessionLAN::FSessionLAN(const FSessionLAN& InSession)
	: FSession(InSession)
	, OwnerInternetAddr(InSession.OwnerInternetAddr)
{

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
	check(InSession.SessionId.GetOnlineServicesType() == EOnlineServices::Null);

	return *static_cast<FSessionLAN*>(&InSession);
}

const FSessionLAN& FSessionLAN::Cast(const FSession& InSession)
{
	check(InSession.SessionId.GetOnlineServicesType() == EOnlineServices::Null);

	return *static_cast<const FSessionLAN*>(&InSession);
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

bool FSessionsLAN::TryHostLANSession()
{
	// The LAN Beacon can only broadcast one session at a time
	if (LANSessionManager->GetBeaconState() == ELanBeaconState::NotUsingLanBeacon)
	{
		FOnValidQueryPacketDelegate QueryPacketDelegate = FOnValidQueryPacketDelegate::CreateThreadSafeSP(this, &FSessionsLAN::OnValidQueryPacketReceived);

		if (LANSessionManager->Host(QueryPacketDelegate))
		{
			UE_LOG(LogTemp, VeryVerbose, TEXT("[FSessionsLAN::TryHostLANSession] LAN Beacon hosting..."));

			PublicSessionsHosted++;

			return true;
		}
		else
		{
			UE_LOG(LogTemp, VeryVerbose, TEXT("[FSessionsLAN::TryHostLANSession] LAN Beacon failed to host!"));

			LANSessionManager->StopLANSession();
		}
	}
	else
	{
		UE_LOG(LogTemp, VeryVerbose, TEXT("[FSessionsLAN::TryHostLANSession] LAN Beacon already in use!"));
	}

	return false;
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
	for (TMap<FName, TSharedRef<FSession>>::TConstIterator It(SessionsByName); It; ++It)
	{
		const TSharedRef<FSession>& Session = It.Value();

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
		ReadSessionFromPacket(Packet, FSessionLAN::Cast(*Session));

		CurrentSessionSearch->FoundSessions.Add(Session);
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