// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	IpConnection.cpp: Unreal IP network connection.
Notes:
	* See \msdev\vc98\include\winsock.h and \msdev\vc98\include\winsock2.h 
	  for Winsock WSAE* errors returned by Windows Sockets.
=============================================================================*/

#include "IpConnection.h"
#include "IpNetDriver.h"
#include "SocketSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/PendingNetGame.h"

#include "IPAddress.h"
#include "Sockets.h"
#include "Net/NetworkProfiler.h"
#include "Net/DataChannel.h"

#include "Net/Core/Misc/PacketAudit.h"
#include "Misc/ScopeExit.h"

/*-----------------------------------------------------------------------------
	Declarations.
-----------------------------------------------------------------------------*/

// Size of a UDP header.
#define IP_HEADER_SIZE     (20)
#define UDP_HEADER_SIZE    (IP_HEADER_SIZE+8)

DECLARE_CYCLE_STAT(TEXT("IpConnection InitRemoteConnection"), Stat_IpConnectionInitRemoteConnection, STATGROUP_Net);
DECLARE_CYCLE_STAT(TEXT("IpConnection Socket SendTo"), STAT_IpConnection_SendToSocket, STATGROUP_Net);
DECLARE_CYCLE_STAT(TEXT("IpConnection WaitForSendTasks"), STAT_IpConnection_WaitForSendTasks, STATGROUP_Net);
DECLARE_CYCLE_STAT(TEXT("IpConnection Address Synthesis"), STAT_IpConnection_AddressSynthesis, STATGROUP_Net);

TAutoConsoleVariable<int32> CVarNetIpConnectionUseSendTasks(
	TEXT("net.IpConnectionUseSendTasks"),
	0,
	TEXT("If true, the IpConnection will call the socket's SendTo function in a task graph task so that it can run off the game thread."));

TAutoConsoleVariable<int32> CVarNetIpConnectionDisableResolution(
	TEXT("net.IpConnectionDisableResolution"),
	0,
	TEXT("If enabled, any future ip connections will not use resolution methods."),
	ECVF_Default | ECVF_Cheat);

PRAGMA_DISABLE_DEPRECATION_WARNINGS
UIpConnection::UIpConnection(const FObjectInitializer& ObjectInitializer) :
	Super(ObjectInitializer),
	Socket(nullptr),
	ResolveInfo(nullptr),
	SocketErrorDisconnectDelay(5.f),
	SocketError_SendDelayStartTime(0.f),
	SocketError_RecvDelayStartTime(0.f),
	CurrentAddressIndex(0),
	ResolutionState(EAddressResolutionState::None)
{
	// Auto add address resolution disable flags if the cvar is set.
	if (!!CVarNetIpConnectionDisableResolution.GetValueOnAnyThread())
	{
		DisableAddressResolution();
	}
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

void UIpConnection::InitBase(UNetDriver* InDriver, class FSocket* InSocket, const FURL& InURL, EConnectionState InState, int32 InMaxPacket, int32 InPacketOverhead)
{
	// Pass the call up the chain
	Super::InitBase(InDriver, InSocket, InURL, InState, 
		// Use the default packet size/overhead unless overridden by a child class
		(InMaxPacket == 0 || InMaxPacket > MAX_PACKET_SIZE) ? MAX_PACKET_SIZE : InMaxPacket,
		InPacketOverhead == 0 ? UDP_HEADER_SIZE : InPacketOverhead);

	if (!IsAddressResolutionEnabled())
	{
		Socket = InSocket;
	}

	if (CVarNetEnableCongestionControl.GetValueOnAnyThread() > 0)
	{
		NetworkCongestionControl.Emplace(CurrentNetSpeed, FNetPacketNotify::SequenceHistoryT::Size);
	}
}

void UIpConnection::InitLocalConnection(UNetDriver* InDriver, class FSocket* InSocket, const FURL& InURL, EConnectionState InState, int32 InMaxPacket, int32 InPacketOverhead)
{
	InitBase(InDriver, InSocket, InURL, InState, 
		// Use the default packet size/overhead unless overridden by a child class
		(InMaxPacket == 0 || InMaxPacket > MAX_PACKET_SIZE) ? MAX_PACKET_SIZE : InMaxPacket,
		InPacketOverhead == 0 ? UDP_HEADER_SIZE : InPacketOverhead);

	// If resolution is disabled, fall back to address synthesis
	if (!IsAddressResolutionEnabled())
	{
		// Figure out IP address from the host URL
		bool bIsValid = false;
		// Get numerical address directly.
		RemoteAddr = InDriver->GetSocketSubsystem()->CreateInternetAddr();
		RemoteAddr->SetIp(*InURL.Host, bIsValid);

		// If the protocols do not match, attempt to synthesize the address so they do.
		if ((bIsValid && InSocket->GetProtocol() != RemoteAddr->GetProtocolType()) || !bIsValid)
		{
			SCOPE_CYCLE_COUNTER(STAT_IpConnection_AddressSynthesis);

			// We want to use GAI to create the address with the correct protocol.
			const FAddressInfoResult MapRequest = InDriver->GetSocketSubsystem()->GetAddressInfo(*InURL.Host, nullptr,
				EAddressInfoFlags::AllResultsWithMapping | EAddressInfoFlags::OnlyUsableAddresses, InSocket->GetProtocol());

			// Set the remote addr provided we have information.
			if (MapRequest.ReturnCode == SE_NO_ERROR && MapRequest.Results.Num() > 0)
			{
				RemoteAddr = MapRequest.Results[0].Address->Clone();
				bIsValid = true;
			}
			else
			{
				UE_LOG(LogNet, Warning, TEXT("IpConnection::InitConnection: Address protocols do not match and cannot be synthesized to a similar address, this will likely lead to issues!"));
			}
		}

		RemoteAddr->SetPort(InURL.Port);

		// If synthesis failed then we shouldn't continue.
		if (bIsValid == false)
		{
			Close();
			UE_LOG(LogNet, Verbose, TEXT("IpConnection::InitConnection: Unable to resolve %s"), *InURL.Host);
			return;
		}

		// Initialize our send bunch
		InitSendBuffer();
	}
	else
	{
		ResolutionState = EAddressResolutionState::WaitingForResolves;
	}
}

void UIpConnection::InitRemoteConnection(UNetDriver* InDriver, class FSocket* InSocket, const FURL& InURL, const class FInternetAddr& InRemoteAddr, EConnectionState InState, int32 InMaxPacket, int32 InPacketOverhead)
{
	SCOPE_CYCLE_COUNTER(Stat_IpConnectionInitRemoteConnection);

	// Listeners don't need to perform address resolution, so flag it as disabled.
	ResolutionState = EAddressResolutionState::Disabled;

	InitBase(InDriver, InSocket, InURL, InState, 
		// Use the default packet size/overhead unless overridden by a child class
		(InMaxPacket == 0 || InMaxPacket > MAX_PACKET_SIZE) ? MAX_PACKET_SIZE : InMaxPacket,
		InPacketOverhead == 0 ? UDP_HEADER_SIZE : InPacketOverhead);

	RemoteAddr = InRemoteAddr.Clone();
	URL.Host = RemoteAddr->ToString(false);

	// Initialize our send bunch
	InitSendBuffer();

	// This is for a client that needs to log in, setup ClientLoginState and ExpectedClientLoginMsgType to reflect that
	SetClientLoginState( EClientLoginState::LoggingIn );
	SetExpectedClientLoginMsgType( NMT_Hello );
}

void UIpConnection::Tick(float DeltaSeconds)
{
	if (CVarNetIpConnectionUseSendTasks.GetValueOnGameThread() != 0)
	{
		ISocketSubsystem* const SocketSubsystem = Driver->GetSocketSubsystem();
		TArray<FSocketSendResult> ResultsCopy;

		{
			FScopeLock ScopeLock(&SocketSendResultsCriticalSection);

			if (SocketSendResults.Num())
			{
				ResultsCopy = MoveTemp(SocketSendResults);
			}
		}
		
		for (const FSocketSendResult& Result : ResultsCopy)
		{
			HandleSocketSendResult(Result, SocketSubsystem);
		}
	}

	if (ResolutionState == EAddressResolutionState::TryNextAddress)
	{
		if (CurrentAddressIndex >= ResolverResults.Num())
		{
			UE_LOG(LogNet, Warning, TEXT("Exhausted the number of resolver results, closing the connection now."));
			ResolutionState = EAddressResolutionState::Error;
			return;
		}

		RemoteAddr = ResolverResults[CurrentAddressIndex];
		Socket = nullptr;

		for (const TSharedPtr<FSocket>& BindSocket : BindSockets)
		{
			if (BindSocket->GetProtocol() == RemoteAddr->GetProtocolType())
			{
				ResolutionSocket = BindSocket;
				Socket = ResolutionSocket.Get();
				break;
			}
		}

		if (Socket == nullptr)
		{
			UE_LOG(LogNet, Error, TEXT("Unable to find a binding socket for the resolve address result %s"), *RemoteAddr->ToString(true));
			ResolutionState = EAddressResolutionState::Error;
			return;
		}
		
		ResolutionState = EAddressResolutionState::Connecting;

		// Reset any timers
		LastReceiveTime = Driver->GetElapsedTime();
		LastReceiveRealtime = FPlatformTime::Seconds();
		LastGoodPacketRealtime = FPlatformTime::Seconds();

		// Reinit the buffer
		InitSendBuffer();

		// Resend initial packets again (only need to do this if this connection does not have packethandlers)
		// Otherwise most packethandlers will do retry (the stateless handshake being one example)
		if (CurrentAddressIndex != 0 && !Handler.IsValid())
		{
			FWorldContext* WorldContext = GEngine->GetWorldContextFromPendingNetGameNetDriver(Driver);
			if (WorldContext != nullptr && WorldContext->PendingNetGame != nullptr)
			{
				WorldContext->PendingNetGame->SendInitialJoin();
			}
		}

		++CurrentAddressIndex;
	}
	else if (ResolutionState == EAddressResolutionState::Connected)
	{
		CleanupResolutionSockets();
		UIpNetDriver* IpDriver = Cast<UIpNetDriver>(Driver);

		// Set the right object now that we have a connection
		IpDriver->SetSocketAndLocalAddress(ResolutionSocket);

		ResolutionState = EAddressResolutionState::Done;
	}
	else if (ResolutionState == EAddressResolutionState::Error)
	{
		UE_LOG(LogNet, Warning, TEXT("Encountered an error, cleaning up this connection now"));
		ResolutionState = EAddressResolutionState::Done;

		// Host name resolution just now failed.
		State = USOCK_Closed;
		Close();
	}

	Super::Tick(DeltaSeconds);
}

void UIpConnection::CleanUp()
{
	// Force ourselves into a finishing state such that we clean up any excess network objects.
	if (IsAddressResolutionEnabled())
	{
		ResolutionState = EAddressResolutionState::Done;
	}

	// Clean up these sockets now, as we'll lose the NetDriver pointer later on
	CleanupResolutionSockets();

	Super::CleanUp();

	WaitForSendTasks();
}

void UIpConnection::HandleConnectionTimeout(const FString& ErrorStr)
{
	if (CanContinueResolution())
	{
		ResolutionState = EAddressResolutionState::TryNextAddress;
		bPendingDestroy = false;
	}
	else
	{
		Super::HandleConnectionTimeout(ErrorStr);
	}
}

void UIpConnection::WaitForSendTasks()
{
	if (CVarNetIpConnectionUseSendTasks.GetValueOnGameThread() != 0 && LastSendTask.IsValid())
	{
		check(IsInGameThread());

		SCOPE_CYCLE_COUNTER(STAT_IpConnection_WaitForSendTasks);
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(LastSendTask, ENamedThreads::GameThread);
	}
}

void UIpConnection::LowLevelSend(void* Data, int32 CountBits, FOutPacketTraits& Traits)
{
	const uint8* DataToSend = reinterpret_cast<uint8*>(Data);

	// If the remote addr hasn't been set, we need to wait for it.
	if (!RemoteAddr.IsValid() || !RemoteAddr->IsValid())
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	TSharedPtr<FInternetAddr> OrigAddr;

	if (GCurrentDuplicateIP.IsValid() && RemoteAddr->CompareEndpoints(*GCurrentDuplicateIP))
	{
		OrigAddr = RemoteAddr;

		TSharedRef<FInternetAddr> NewAddr = OrigAddr->Clone();
		int32 NewPort = NewAddr->GetPort() - 9876;

		NewAddr->SetPort(NewPort >= 0 ? NewPort : (65536 + NewPort));

		RemoteAddr = NewAddr;
	}

	ON_SCOPE_EXIT
	{
		if (OrigAddr.IsValid())
		{
			RemoteAddr = OrigAddr;
		}
	};
#endif

	if (IsAddressResolutionEnabled() && (ResolutionState == EAddressResolutionState::WaitingForResolves || ResolutionState == EAddressResolutionState::TryNextAddress))
	{
		UE_LOG(LogNet, Verbose, TEXT("Skipping send task as we are waiting on the next resolution step"));
		return;
	}

	// Process any packet modifiers
	if (Handler.IsValid() && !Handler->GetRawSend())
	{
		const ProcessedPacket ProcessedData = Handler->Outgoing(reinterpret_cast<uint8*>(Data), CountBits, Traits);

		if (!ProcessedData.bError)
		{
			DataToSend = ProcessedData.Data;
			CountBits = ProcessedData.CountBits;
		}
		else
		{
			CountBits = 0;
		}
	}

	bool bBlockSend = false;
	int32 CountBytes = FMath::DivideAndRoundUp(CountBits, 8);

#if !UE_BUILD_SHIPPING
	LowLevelSendDel.ExecuteIfBound((void*)DataToSend, CountBytes, bBlockSend);
#endif

	if (!bBlockSend)
	{
		// Send to remote.
		FSocketSendResult SendResult;
		CLOCK_CYCLES(Driver->SendCycles);

		if ( CountBytes > MaxPacket )
		{
			UE_LOG( LogNet, Warning, TEXT( "UIpConnection::LowLevelSend: CountBytes > MaxPacketSize! Count: %i, MaxPacket: %i %s" ), CountBytes, MaxPacket, *Describe() );
		}

		FPacketAudit::NotifyLowLevelSend((uint8*)DataToSend, CountBytes, CountBits);

		if (CountBytes > 0)
		{
			const bool bNotifyOnSuccess = (SocketErrorDisconnectDelay > 0.f) && (SocketError_SendDelayStartTime != 0.f);

			if (CVarNetIpConnectionUseSendTasks.GetValueOnAnyThread() != 0)
			{
				DECLARE_CYCLE_STAT(TEXT("IpConnection SendTo task"), STAT_IpConnection_SendToTask, STATGROUP_TaskGraphTasks);

				FGraphEventArray Prerequisites;
				if (LastSendTask.IsValid())
				{
					Prerequisites.Add(LastSendTask);
				}

				ISocketSubsystem* const SocketSubsystem = Driver->GetSocketSubsystem();
				
				LastSendTask = FFunctionGraphTask::CreateAndDispatchWhenReady([this, Packet = TArray<uint8>(DataToSend, CountBytes), SocketSubsystem, bNotifyOnSuccess]
				{
					if (Socket != nullptr)
					{
						bool bWasSendSuccessful = false;
						UIpConnection::FSocketSendResult Result;

						{
							SCOPE_CYCLE_COUNTER(STAT_IpConnection_SendToSocket);
							bWasSendSuccessful = Socket->SendTo(Packet.GetData(), Packet.Num(), Result.BytesSent, *RemoteAddr);
						}

						if (!bWasSendSuccessful && SocketSubsystem)
						{
							Result.Error = SocketSubsystem->GetLastErrorCode();
						}

						if (!bWasSendSuccessful || (bNotifyOnSuccess && Result.Error == SE_NO_ERROR))
						{
							FScopeLock ScopeLock(&SocketSendResultsCriticalSection);
							SocketSendResults.Add(MoveTemp(Result));
						}
					}
				},
				GET_STATID(STAT_IpConnection_SendToTask), &Prerequisites);

				// Always flush this profiler data now. Technically this could be incorrect if the send in the task fails,
				// but this keeps the bookkeeping simpler for now.
				NETWORK_PROFILER(GNetworkProfiler.FlushOutgoingBunches(this));
				NETWORK_PROFILER(GNetworkProfiler.TrackSocketSendTo(Socket->GetDescription(), DataToSend, CountBytes, NumPacketIdBits, NumBunchBits, NumAckBits, NumPaddingBits, this));
			}
			else
			{
				bool bWasSendSuccessful = false;
				{
					SCOPE_CYCLE_COUNTER(STAT_IpConnection_SendToSocket);
					bWasSendSuccessful = Socket->SendTo(DataToSend, CountBytes, SendResult.BytesSent, *RemoteAddr);
				}

				if (bWasSendSuccessful)
				{
					UNCLOCK_CYCLES(Driver->SendCycles);
					NETWORK_PROFILER(GNetworkProfiler.FlushOutgoingBunches(this));
					NETWORK_PROFILER(GNetworkProfiler.TrackSocketSendTo(Socket->GetDescription(), DataToSend, SendResult.BytesSent, NumPacketIdBits, NumBunchBits, NumAckBits, NumPaddingBits, this));

					if (bNotifyOnSuccess)
					{
						HandleSocketSendResult(SendResult, nullptr);
					}
				}
				else
				{
					ISocketSubsystem* const SocketSubsystem = Driver->GetSocketSubsystem();
					SendResult.Error = SocketSubsystem->GetLastErrorCode();

					HandleSocketSendResult(SendResult, SocketSubsystem);
				}
			}
		}
	}
}

void UIpConnection::ReceivedRawPacket(void* Data, int32 Count)
{
	UE_CLOG(SocketError_RecvDelayStartTime > 0.0, LogNet, Log, TEXT("UIpConnection::ReceivedRawPacket: Recoverd from socket errors. %s Connection"), *Describe());
	
	// We received data successfully, reset our error counters.
	SocketError_RecvDelayStartTime = 0.0;

	// Set that we've gotten packet from the server, this begins destruction of the other elements.
	if (IsAddressResolutionEnabled() && ResolutionState != EAddressResolutionState::Done)
	{
		// We only want to write this once, because we don't want to waste cycles trying to clean up nothing.
		ResolutionState = EAddressResolutionState::Connected;
	}

	Super::ReceivedRawPacket(Data, Count);
}

float UIpConnection::GetTimeoutValue()
{
	// Include support for no-timeouts
#if !UE_BUILD_SHIPPING
	check(Driver);
	if (Driver->bNoTimeouts)
	{
		return UNetConnection::GetTimeoutValue();
	}
#endif

	if (ResolutionState == EAddressResolutionState::Connecting)
	{
		UIpNetDriver* IpDriver = Cast<UIpNetDriver>(Driver);
		if (IpDriver == nullptr || IpDriver->GetResolutionTimeoutValue() == 0.0f)
		{
			return Driver->InitialConnectTimeout;
		}
		return IpDriver->GetResolutionTimeoutValue();
	}

	return UNetConnection::GetTimeoutValue();
}

void UIpConnection::HandleSocketSendResult(const FSocketSendResult& Result, ISocketSubsystem* SocketSubsystem)
{
	if (Result.Error == SE_NO_ERROR)
	{
		UE_CLOG(SocketError_SendDelayStartTime > 0.f, LogNet, Log, TEXT("UIpConnection::HandleSocketSendResult: Recovered from socket errors. %s Connection"), *Describe());

		// We sent data successfully, reset our error counters.
		SocketError_SendDelayStartTime = 0.0;
	}
	else if (Result.Error != SE_EWOULDBLOCK)
	{
		check(SocketSubsystem);

		if (SocketErrorDisconnectDelay > 0.f)
		{
			const double Time = Driver->GetElapsedTime();
			if (SocketError_SendDelayStartTime == 0.0)
			{
				UE_LOG(LogNet, Log, TEXT("UIpConnection::HandleSocketSendResult: Socket->SendTo failed with error %i (%s). %s Connection beginning close timeout (Timeout = %d)."),
					static_cast<int32>(Result.Error),
					SocketSubsystem->GetSocketError(Result.Error),
					*Describe(),
					SocketErrorDisconnectDelay);

				SocketError_SendDelayStartTime = Time;
				return;
			}
			else if ((Time - SocketError_SendDelayStartTime) < SocketErrorDisconnectDelay)
			{
				// Our delay hasn't elapsed yet. Just suppress further warnings until we either recover or are disconnected.
				return;
			}
		}

		// Broadcast the error only on the first occurrence
		if( GetPendingCloseDueToSocketSendFailure() == false )
		{
			if (CanContinueResolution())
			{
				SetPendingCloseDueToSocketSendFailure();
				SocketError_SendDelayStartTime = 0.f;
				ResolutionState = EAddressResolutionState::TryNextAddress;
			}
			else
			{
				ResolutionState = EAddressResolutionState::Error;

				// Request the connection to be disconnected during next tick() since we got a critical socket failure, the actual disconnect is postponed 		
				// to avoid issues with the call Close() causing issues with reentrant code paths in DataChannel::SendBunch() and FlushNet()
				SetPendingCloseDueToSocketSendFailure();

				FString ErrorString = FString::Printf(TEXT("UIpNetConnection::HandleSocketSendResult: Socket->SendTo failed with error %i (%s). %s Connection will be closed during next Tick()!"),
					static_cast<int32>(Result.Error),
					SocketSubsystem->GetSocketError(Result.Error),
					*Describe());

				GEngine->BroadcastNetworkFailure(Driver->GetWorld(), Driver, ENetworkFailure::ConnectionLost, ErrorString);
			}

		}
	}
}

void UIpConnection::HandleSocketRecvError(class UNetDriver* NetDriver, const FString& ErrorString)
{
	check(NetDriver);

	if (SocketErrorDisconnectDelay > 0.f)
	{
		const double Time = NetDriver->GetElapsedTime();
		if (SocketError_RecvDelayStartTime == 0.0)
		{
			UE_LOG(LogNet, Log, TEXT("%s. %s Connection beginning close timeout (Timeout = %d)."),
				*ErrorString,
				*Describe(),
				SocketErrorDisconnectDelay);

			SocketError_RecvDelayStartTime = Time;
			return;
		}
		else if ((Time - SocketError_RecvDelayStartTime) < SocketErrorDisconnectDelay)
		{
			return;
		}
	}

	if (CanContinueResolution())
	{
		SocketErrorDisconnectDelay = 0.0f;
		ResolutionState = EAddressResolutionState::TryNextAddress;
	}
	else
	{
		ResolutionState = EAddressResolutionState::Error;

		// For now, this is only called on clients when the ServerConnection fails.
		// Because of that, on failure we'll shut down the NetDriver.
		GEngine->BroadcastNetworkFailure(NetDriver->GetWorld(), NetDriver, ENetworkFailure::ConnectionLost, ErrorString);
		NetDriver->Shutdown();
	}

}

void UIpConnection::CleanupResolutionSockets()
{
	if (Driver == nullptr || Driver->GetSocketSubsystem() == nullptr || !IsAddressResolutionEnabled())
	{
		return;
	}

	const bool bCleanAll = (ResolutionState != EAddressResolutionState::Connected);
	if (Socket == nullptr && !bCleanAll)
	{
		UE_LOG(LogNet, Warning, TEXT("Cannot clean up resolution sockets as our current socket pointer is null!"));
		return;
	}

	if (bCleanAll)
	{
		if(ResolutionSocket.Get() == Socket)
		{
			Socket = nullptr;
		}
		
		ResolutionSocket.Reset();
	}
	
	BindSockets.Reset();
	ResolverResults.Empty();
}

FString UIpConnection::LowLevelGetRemoteAddress(bool bAppendPort)
{
	return (RemoteAddr.IsValid()) ? RemoteAddr->ToString(bAppendPort) : TEXT("");
}

FString UIpConnection::LowLevelDescribe()
{
	TSharedRef<FInternetAddr> LocalAddr = Driver->GetSocketSubsystem()->CreateInternetAddr();

	if (Socket != nullptr)
	{
		Socket->GetAddress(*LocalAddr);
	}

	return FString::Printf
	(
		TEXT("url=%s remote=%s local=%s uniqueid=%s state: %s"),
		*URL.Host,
		(RemoteAddr.IsValid() ? *RemoteAddr->ToString(true) : TEXT("nullptr")),
		*LocalAddr->ToString(true),
		(PlayerId.IsValid() ? *PlayerId->ToDebugString() : TEXT("nullptr")),
			State==USOCK_Pending	?	TEXT("Pending")
		:	State==USOCK_Open		?	TEXT("Open")
		:	State==USOCK_Closed		?	TEXT("Closed")
		:								TEXT("Invalid")
	);
}
