// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	IpNetDriver.cpp: Unreal IP network driver.
Notes:
	* See \msdev\vc98\include\winsock.h and \msdev\vc98\include\winsock2.h 
	  for Winsock WSAE* errors returned by Windows Sockets.
=============================================================================*/

#include "IpNetDriver.h"
#include "Misc/CommandLine.h"
#include "EngineGlobals.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "UObject/Package.h"
#include "PacketHandlers/StatelessConnectHandlerComponent.h"
#include "Engine/NetConnection.h"
#include "Engine/ChildConnection.h"
#include "SocketSubsystem.h"
#include "IpConnection.h"
#include "HAL/LowLevelMemTracker.h"

#include "Net/Core/Misc/PacketAudit.h"

#include "IPAddress.h"
#include "Sockets.h"
#include "Serialization/ArchiveCountMem.h"

/** For backwards compatibility with the engine stateless connect code */
#ifndef STATELESSCONNECT_HAS_RANDOM_SEQUENCE
	#define STATELESSCONNECT_HAS_RANDOM_SEQUENCE 0
#endif

/*-----------------------------------------------------------------------------
	Declarations.
-----------------------------------------------------------------------------*/

DECLARE_CYCLE_STAT(TEXT("IpNetDriver Add new connection"), Stat_IpNetDriverAddNewConnection, STATGROUP_Net);
DECLARE_CYCLE_STAT(TEXT("IpNetDriver Socket RecvFrom"), STAT_IpNetDriver_RecvFromSocket, STATGROUP_Net);
DECLARE_CYCLE_STAT(TEXT("IpNetDriver Destroy WaitForReceiveThread"), STAT_IpNetDriver_Destroy_WaitForReceiveThread, STATGROUP_Net);

UIpNetDriver::FOnNetworkProcessingCausingSlowFrame UIpNetDriver::OnNetworkProcessingCausingSlowFrame;

// Time before the alarm delegate is called (in seconds)
float GIpNetDriverMaxDesiredTimeSliceBeforeAlarmSecs = 1.0f;

FAutoConsoleVariableRef GIpNetDriverMaxDesiredTimeSliceBeforeAlarmSecsCVar(
	TEXT("n.IpNetDriverMaxFrameTimeBeforeAlert"),
	GIpNetDriverMaxDesiredTimeSliceBeforeAlarmSecs,
	TEXT("Time to spend processing networking data in a single frame before an alert is raised (in seconds)\n")
	TEXT("It may get called multiple times in a single frame if additional processing after a previous alert exceeds the threshold again\n")
	TEXT(" default: 1 s"));

// Time before the time taken in a single frame is printed out (in seconds)
float GIpNetDriverLongFramePrintoutThresholdSecs = 10.0f;

FAutoConsoleVariableRef GIpNetDriverLongFramePrintoutThresholdSecsCVar(
	TEXT("n.IpNetDriverMaxFrameTimeBeforeLogging"),
	GIpNetDriverLongFramePrintoutThresholdSecs,
	TEXT("Time to spend processing networking data in a single frame before an output log warning is printed (in seconds)\n")
	TEXT(" default: 10 s"));

TAutoConsoleVariable<int32> CVarNetIpNetDriverUseReceiveThread(
	TEXT("net.IpNetDriverUseReceiveThread"),
	0,
	TEXT("If true, the IpNetDriver will call the socket's RecvFrom function on a separate thread (not the game thread)"));

TAutoConsoleVariable<int32> CVarNetIpNetDriverReceiveThreadQueueMaxPackets(
	TEXT("net.IpNetDriverReceiveThreadQueueMaxPackets"),
	1024,
	TEXT("If net.IpNetDriverUseReceiveThread is true, the maximum number of packets that can be waiting in the queue. Additional packets received will be dropped."));

TAutoConsoleVariable<int32> CVarNetIpNetDriverReceiveThreadPollTimeMS(
	TEXT("net.IpNetDriverReceiveThreadPollTimeMS"),
	250,
	TEXT("If net.IpNetDriverUseReceiveThread is true, the number of milliseconds to use as the timeout value for FSocket::Wait on the receive thread. A negative value means to wait indefinitely (FSocket::Shutdown should cancel it though)."));

TAutoConsoleVariable<int32> CVarNetUseRecvMulti(
	TEXT("net.UseRecvMulti"),
	0,
	TEXT("If true, and if running on a Unix/Linux platform, multiple packets will be retrieved from the socket with one syscall, ")
		TEXT("improving performance and also allowing retrieval of timestamp information."));

TAutoConsoleVariable<int32> CVarRecvMultiCapacity(
	TEXT("net.RecvMultiCapacity"),
	2048,
	TEXT("When RecvMulti is enabled, this is the number of packets it is allocated to handle per call - ")
		TEXT("bigger is better (especially under a DDoS), but keep an eye on memory cost."));

TAutoConsoleVariable<int32> CVarNetUseRecvTimestamps(
	TEXT("net.UseRecvTimestamps"),
	0,
	TEXT("If true and if net.UseRecvMulti is also true, on a Unix/Linux platform, ")
		TEXT("the kernel timestamp will be retrieved for each packet received, providing more accurate ping calculations."));



/**
 * FPacketItrator
 *
 * Encapsulates the NetDriver TickDispatch code required for executing all variations of packet receives
 * (FSocket::RecvFrom, FSocket::RecvMulti, and the Receive Thread),
 * as well as implementing/abstracting-away some of the outermost (non-NetConnection-related) parts of the DDoS detection code.
 */
class FPacketIterator
{
	friend class UIpNetDriver;
	
private:
	struct FCachedPacket
	{
		/** Whether socket receive succeeded. Don't rely on the Error field for this, due to implementation/platform uncertainties. */
		bool bRecvSuccess;

		/** Pre-allocated Data field, for storing packets of any expected size */
		TArray<uint8, TFixedAllocator<MAX_PACKET_SIZE>> Data;

		/** Receive address for the packet */
		TSharedPtr<FInternetAddr> Address;

		/** OS-level timestamp for the packet receive, if applicable */
		double PacketTimestamp;

		/** Error if receiving a packet failed */
		ESocketErrors Error;
	};
	
private:
	FPacketIterator(UIpNetDriver* InDriver)
		: bBreak(false)
		, Driver(InDriver)
		, DDoS(Driver->DDoS)
		, SocketSubsystem(Driver->GetSocketSubsystem())
		, Socket(Driver->Socket)
		, SocketReceiveThreadRunnable(Driver->SocketReceiveThreadRunnable.Get())
		, CurrentPacket()
		, RecvMultiIdx(0)
		, RecvMultiPacketCount(0)
	{
		RMState = Driver->RecvMultiState.Get();
		bUseRecvMulti = CVarNetUseRecvMulti.GetValueOnAnyThread() != 0 && RMState != nullptr;

		if (!bUseRecvMulti && SocketSubsystem != nullptr)
		{
			CurrentPacket.Address = SocketSubsystem->CreateInternetAddr();
		}

		AdvanceCurrentPacket();
	}

	FORCEINLINE FPacketIterator& operator++()
	{
		AdvanceCurrentPacket();

		return *this;
	}

	FORCEINLINE explicit operator bool() const
	{
		return !bBreak;
	}


	/**
	 * Retrieves the packet information from the current iteration. Avoid calling more than once, per iteration.
	 *
	 * @param OutPacket		Outputs a view to the received packet data
	 * @return				Returns whether or not receiving was successful for the current packet
	 */
	bool GetCurrentPacket(FReceivedPacketView& OutPacket)
	{
		bool bRecvSuccess = false;

		if (bUseRecvMulti)
		{
			RMState->GetPacket(RecvMultiIdx, OutPacket);
			bRecvSuccess = true;
		}
		else
		{
			OutPacket.Data = MakeArrayView(CurrentPacket.Data);
			OutPacket.Error = CurrentPacket.Error;
			OutPacket.Address = CurrentPacket.Address;
			bRecvSuccess = CurrentPacket.bRecvSuccess;
		}

		return bRecvSuccess;
	}

	/**
	 * Retrieves the packet timestamp information from the current iteration. As above, avoid calling more than once.
	 *
	 * @param ForConnection		The connection we are retrieving timestamp information for
	 */
	void GetCurrentPacketTimestamp(UNetConnection* ForConnection)
	{
		FPacketTimestamp CurrentTimestamp;
		bool bIsLocalTimestamp = false;
		bool bSuccess = false;

		if (bUseRecvMulti)
		{
			RMState->GetPacketTimestamp(RecvMultiIdx, CurrentTimestamp);
			bIsLocalTimestamp = false;
			bSuccess = true;
		}
		else if (CurrentPacket.PacketTimestamp != 0.0)
		{
			CurrentTimestamp.Timestamp = FTimespan::FromSeconds(CurrentPacket.PacketTimestamp);
			bIsLocalTimestamp = true;
			bSuccess = true;
		}

		if (bSuccess)
		{
			ForConnection->SetPacketOSReceiveTime(CurrentTimestamp, bIsLocalTimestamp);
		}
	}

	/**
	 * Returns a view of the iterator's packet buffer, for updating packet data as it's processed, and generating new packet views
	 */
	FPacketBufferView GetWorkingBuffer()
	{
		return { CurrentPacket.Data.GetData(), MAX_PACKET_SIZE };
	}

	/**
	 * Advances the current packet to the next iteration
	 */
	void AdvanceCurrentPacket()
	{
		if (bUseRecvMulti)
		{
			if (RecvMultiPacketCount == 0 || ((RecvMultiIdx + 1) >= RecvMultiPacketCount))
			{
				AdvanceRecvMultiState();
			}
			else
			{
				RecvMultiIdx++;
			}

			// At this point, bBreak will be set, or RecvMultiPacketCount will be > 0
		}
		else
		{
			bBreak = !ReceiveSinglePacket();
		}
	}

	/**
	 * Receives a single packet from the network socket, outputting to the CurrentPacket buffer.
	 *
	 * @return				Whether or not a packet or an error was successfully received
	 */
	bool ReceiveSinglePacket()
	{
		bool bReceivedPacketOrError = false;

		CurrentPacket.bRecvSuccess = false;
		CurrentPacket.Data.SetNumUninitialized(0, false);

		if (CurrentPacket.Address.IsValid())
		{
			CurrentPacket.Address->SetAnyAddress();
		}

		CurrentPacket.PacketTimestamp = 0.0;
		CurrentPacket.Error = SE_NO_ERROR;

		while (true)
		{
			bReceivedPacketOrError = false;

			if (SocketReceiveThreadRunnable != nullptr)
			{
				// Very-early-out - the NetConnection per frame time limit, limits all packet processing
				// @todo #JohnB: This DDoS detection code will be redundant, as it's performed in the Receive Thread in a coming refactor
				if (DDoS.ShouldBlockNetConnPackets())
				{
					// Approximate due to threading
					uint32 DropCountApprox = SocketReceiveThreadRunnable->ReceiveQueue.Count();

					SocketReceiveThreadRunnable->ReceiveQueue.Empty();

					if (DropCountApprox > 0)
					{
						DDoS.IncDroppedPacketCounter(DropCountApprox);
					}
				}
				else
				{
					UIpNetDriver::FReceivedPacket IncomingPacket;
					const bool bHasPacket = SocketReceiveThreadRunnable->ReceiveQueue.Dequeue(IncomingPacket);

					if (bHasPacket)
					{
						if (IncomingPacket.FromAddress.IsValid())
						{
							CurrentPacket.Address = IncomingPacket.FromAddress.ToSharedRef();
						}

						ESocketErrors CurError = IncomingPacket.Error;
						bool bReceivedPacket = CurError == SE_NO_ERROR;

						CurrentPacket.bRecvSuccess = bReceivedPacket;
						CurrentPacket.PacketTimestamp = IncomingPacket.PlatformTimeSeconds;
						CurrentPacket.Error = CurError;
						bReceivedPacketOrError = bReceivedPacket;

						if (bReceivedPacket)
						{
							int32 BytesRead = IncomingPacket.PacketBytes.Num();

							if (IncomingPacket.PacketBytes.Num() <= MAX_PACKET_SIZE)
							{
								CurrentPacket.Data.SetNumUninitialized(BytesRead, false);

								FMemory::Memcpy(CurrentPacket.Data.GetData(), IncomingPacket.PacketBytes.GetData(), BytesRead);
							}
							else
							{
								UE_LOG(LogNet, Warning, TEXT("IpNetDriver receive thread received a packet of %d bytes, which is larger than the data buffer size of %d bytes."),
										BytesRead, MAX_PACKET_SIZE);

								continue;
							}
						}
						// Received an error
						else if (!UIpNetDriver::IsRecvFailBlocking(CurError))
						{
							bReceivedPacketOrError = true;
						}
					}
				}
			}
			else if (Socket != nullptr && SocketSubsystem != nullptr)
			{
				SCOPE_CYCLE_COUNTER(STAT_IpNetDriver_RecvFromSocket);

				int32 BytesRead = 0;
				bool bReceivedPacket = Socket->RecvFrom(CurrentPacket.Data.GetData(), MAX_PACKET_SIZE, BytesRead, *CurrentPacket.Address);

				CurrentPacket.bRecvSuccess = bReceivedPacket;
				bReceivedPacketOrError = bReceivedPacket;

				if (bReceivedPacket)
				{
					// Fixed allocator, so no risk of realloc from copy-then-resize
					CurrentPacket.Data.SetNumUninitialized(BytesRead, false);
				}
				else
				{
					ESocketErrors CurError = SocketSubsystem->GetLastErrorCode();

					CurrentPacket.Error = CurError;
					CurrentPacket.Data.SetNumUninitialized(0, false);

					// Received an error
					if (!UIpNetDriver::IsRecvFailBlocking(CurError))
					{
						bReceivedPacketOrError = true;
					}
				}

				// Very-early-out - the NetConnection per frame time limit, limits all packet processing
				if (bReceivedPacketOrError && DDoS.ShouldBlockNetConnPackets())
				{
					if (bReceivedPacket)
					{
						DDoS.IncDroppedPacketCounter();
					}

					continue;
				}
			}

			// While loop only exists to allow 'continue' for DDoS and invalid packet code, above
			break;
		}

		return bReceivedPacketOrError;
	}

	/**
	 * Load a fresh batch of RecvMulti packets
	 */
	void AdvanceRecvMultiState()
	{
		RecvMultiIdx = 0;
		RecvMultiPacketCount = 0;

		while (true)
		{
			SCOPE_CYCLE_COUNTER(STAT_IpNetDriver_RecvFromSocket);

			bool bRecvMultiOk = Socket != nullptr ? Socket->RecvMulti(*RMState) : false;

			if (!bRecvMultiOk)
			{
				ESocketErrors RecvMultiError = (SocketSubsystem != nullptr ? SocketSubsystem->GetLastErrorCode() : SE_NO_ERROR);

				if (UIpNetDriver::IsRecvFailBlocking(RecvMultiError))
				{
					bBreak = true;
					break;
				}
				else
				{
					// When the Linux recvmmsg syscall encounters an error after successfully receiving at least one packet,
					// it won't return an error until called again, but this error can be overwritten before recvmmsg is called again.
					// This makes the error handling for recvmmsg unreliable. Continue until the socket blocks.

					// Continue is safe, as 0 packets have been received
					continue;
				}
			}

			// Extreme-early-out. NetConnection per frame time limit, limits all packet processing - RecvMulti drops all packets at once
			if (DDoS.ShouldBlockNetConnPackets())
			{
				int32 NumDropped = RMState->GetNumPackets();

				DDoS.IncDroppedPacketCounter(NumDropped);

				// Have a threshold, to stop the RecvMulti syscall spinning with low packet counts - let the socket buffer build up
				if (NumDropped > 10)
				{
					continue;
				}
				else
				{
					bBreak = true;
					break;
				}
			}

			RecvMultiPacketCount = RMState->GetNumPackets();

			break;
		}
	}


private:
	/** Specified internally, when the packet iterator should break/stop (no packets, DDoS limits triggered, etc.) */
	bool bBreak;


	/** Cached reference to the NetDriver, and NetDriver variables/values */

	UIpNetDriver* Driver;

	FDDoSDetection& DDoS;

	ISocketSubsystem* SocketSubsystem;

	FSocket*& Socket;

	UIpNetDriver::FReceiveThreadRunnable* SocketReceiveThreadRunnable;

	/** Stores information for the current packet being received (when using single-receive mode) */
	FCachedPacket CurrentPacket;

	/** Stores information for receiving packets using RecvMulti */
	FRecvMulti* RMState;

	/** Whether or not RecvMulti is enabled/supported */
	bool bUseRecvMulti;

	/** The RecvMulti index of the next packet to be received (if RecvMultiPacketCount > 0) */
	int32 RecvMultiIdx;

	/** The number of packets waiting to be read from the FRecvMulti state */
	int32 RecvMultiPacketCount;
};

/**
 * UIpNetDriver
 */

UIpNetDriver::UIpNetDriver(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, PauseReceiveEnd(0.f)
	, ServerDesiredSocketReceiveBufferBytes(0x20000)
	, ServerDesiredSocketSendBufferBytes(0x20000)
	, ClientDesiredSocketReceiveBufferBytes(0x8000)
	, ClientDesiredSocketSendBufferBytes(0x8000)
	, RecvMultiState(nullptr)
{
}

bool UIpNetDriver::IsAvailable() const
{
	// IP driver always valid for now
	return true;
}

ISocketSubsystem* UIpNetDriver::GetSocketSubsystem()
{
	return ISocketSubsystem::Get();
}

FSocket * UIpNetDriver::CreateSocket()
{
	// Create UDP socket and enable broadcasting.
	ISocketSubsystem* SocketSubsystem = GetSocketSubsystem();

	if (SocketSubsystem == NULL)
	{
		UE_LOG(LogNet, Warning, TEXT("UIpNetDriver::CreateSocket: Unable to find socket subsystem"));
		return NULL;
	}

	return SocketSubsystem->CreateSocket(NAME_DGram, TEXT("Unreal"), ( LocalAddr.IsValid() ? LocalAddr->GetProtocolType() : NAME_None) );
}

int UIpNetDriver::GetClientPort()
{
	return 0;
}

bool UIpNetDriver::InitBase( bool bInitAsClient, FNetworkNotify* InNotify, const FURL& URL, bool bReuseAddressAndPort, FString& Error )
{
	if (!Super::InitBase(bInitAsClient, InNotify, URL, bReuseAddressAndPort, Error))
	{
		return false;
	}

	ISocketSubsystem* SocketSubsystem = GetSocketSubsystem();
	if (SocketSubsystem == nullptr)
	{
		UE_LOG(LogNet, Warning, TEXT("Unable to find socket subsystem"));
		return false;
	}

	// Derived types may have already allocated a socket
	const TCHAR* MultiHomeBindAddr = URL.GetOption(TEXT("multihome="), nullptr);
	if (MultiHomeBindAddr != nullptr)
	{
		LocalAddr = SocketSubsystem->GetAddressFromString(MultiHomeBindAddr);
		if (!LocalAddr.IsValid())
		{
			UE_LOG(LogNet, Warning, TEXT("Failed to created valid address from multihome address: %s"), MultiHomeBindAddr);
		}
	}

	if (!LocalAddr.IsValid())
	{
		LocalAddr = SocketSubsystem->GetLocalBindAddr(*GLog);
	}

	// Create the socket that we will use to communicate with
	Socket = CreateSocket();

	if(Socket == nullptr)
	{
		Socket = 0;
		Error = FString::Printf( TEXT("%s: socket failed (%i)"), SocketSubsystem->GetSocketAPIName(), (int32)SocketSubsystem->GetLastErrorCode() );
		return false;
	}
	if (SocketSubsystem->RequiresChatDataBeSeparate() == false &&
		Socket->SetBroadcast() == false)
	{
		Error = FString::Printf( TEXT("%s: setsockopt SO_BROADCAST failed (%i)"), SocketSubsystem->GetSocketAPIName(), (int32)SocketSubsystem->GetLastErrorCode() );
		return false;
	}

	if (Socket->SetReuseAddr(bReuseAddressAndPort) == false)
	{
		UE_LOG(LogNet, Log, TEXT("setsockopt with SO_REUSEADDR failed"));
	}

	if (Socket->SetRecvErr() == false)
	{
		UE_LOG(LogNet, Log, TEXT("setsockopt with IP_RECVERR failed"));
	}

	// Increase socket queue size, because we are polling rather than threading
	// and thus we rely on the OS socket to buffer a lot of data.
	const int32 DesiredRecvSize = bInitAsClient ? ClientDesiredSocketReceiveBufferBytes	: ServerDesiredSocketReceiveBufferBytes;
	const int32 DesiredSendSize = bInitAsClient ? ClientDesiredSocketSendBufferBytes	: ServerDesiredSocketSendBufferBytes;
	int32 ActualRecvSize(0);
	int32 ActualSendSize(0);
	Socket->SetReceiveBufferSize(DesiredRecvSize, ActualRecvSize);
	Socket->SetSendBufferSize(DesiredSendSize, ActualSendSize);
	UE_LOG(LogInit, Log, TEXT("%s: Socket queue. Rx: %i (config %i) Tx: %i (config %i)"), SocketSubsystem->GetSocketAPIName(),
		ActualRecvSize, DesiredRecvSize, ActualSendSize, DesiredSendSize);

	// Bind socket to our port.
	LocalAddr->SetPort(bInitAsClient ? GetClientPort() : URL.Port);
	
	int32 AttemptPort = LocalAddr->GetPort();
	int32 BoundPort = SocketSubsystem->BindNextPort( Socket, *LocalAddr, MaxPortCountToTry + 1, 1 );
	if (BoundPort == 0)
	{
		Error = FString::Printf( TEXT("%s: binding to port %i failed (%i)"), SocketSubsystem->GetSocketAPIName(), AttemptPort,
			(int32)SocketSubsystem->GetLastErrorCode() );
		return false;
	}
	if( Socket->SetNonBlocking() == false )
	{
		Error = FString::Printf( TEXT("%s: SetNonBlocking failed (%i)"), SocketSubsystem->GetSocketAPIName(),
			(int32)SocketSubsystem->GetLastErrorCode());
		return false;
	}

	// If the cvar is set and the socket subsystem supports it, create the receive thread.
	if (CVarNetIpNetDriverUseReceiveThread.GetValueOnAnyThread() != 0 && SocketSubsystem->IsSocketWaitSupported())
	{
		SocketReceiveThreadRunnable = MakeUnique<FReceiveThreadRunnable>(this);
		SocketReceiveThread.Reset(FRunnableThread::Create(SocketReceiveThreadRunnable.Get(), *FString::Printf(TEXT("IpNetDriver Receive Thread"), *NetDriverName.ToString())));
	}


	bool bRecvMultiEnabled = CVarNetUseRecvMulti.GetValueOnAnyThread() != 0;
	bool bRecvThreadEnabled = CVarNetIpNetDriverUseReceiveThread.GetValueOnAnyThread() != 0;

	if (bRecvMultiEnabled && !bRecvThreadEnabled)
	{
		bool bSupportsRecvMulti = SocketSubsystem->IsSocketRecvMultiSupported();

		if (bSupportsRecvMulti)
		{
			bool bRetrieveTimestamps = CVarNetUseRecvTimestamps.GetValueOnAnyThread() != 0;

			if (bRetrieveTimestamps)
			{
				Socket->SetRetrieveTimestamp(true);
			}


			ERecvMultiFlags RecvMultiFlags = bRetrieveTimestamps ? ERecvMultiFlags::RetrieveTimestamps : ERecvMultiFlags::None;
			int32 MaxRecvMultiPackets = FMath::Max(32, CVarRecvMultiCapacity.GetValueOnAnyThread());

			RecvMultiState = SocketSubsystem->CreateRecvMulti(MaxRecvMultiPackets, MAX_PACKET_SIZE, RecvMultiFlags);


			FArchiveCountMem MemArc(nullptr);

			RecvMultiState->CountBytes(MemArc);

			UE_LOG(LogNet, Log, TEXT("NetDriver RecvMulti state size: %i, Retrieve Timestamps: %i"), MemArc.GetMax(),
					(uint32)bRetrieveTimestamps);
		}
		else
		{
			UE_LOG(LogNet, Warning, TEXT("NetDriver could not enable RecvMulti, as current socket subsystem does not support it."));
		}
	}
	else if (bRecvThreadEnabled)
	{
		UE_LOG(LogNet, Warning, TEXT("NetDriver RecvMulti is not yet supported with the Receive Thread enabled."));
	}

	// Success.
	return true;
}

bool UIpNetDriver::InitConnect( FNetworkNotify* InNotify, const FURL& ConnectURL, FString& Error )
{
	if( !InitBase( true, InNotify, ConnectURL, false, Error ) )
	{
		UE_LOG(LogNet, Warning, TEXT("Failed to init net driver ConnectURL: %s: %s"), *ConnectURL.ToString(), *Error);
		return false;
	}

	// Create new connection.
	ServerConnection = NewObject<UNetConnection>(GetTransientPackage(), NetConnectionClass);
	ServerConnection->InitLocalConnection( this, Socket, ConnectURL, USOCK_Pending);
	UE_LOG(LogNet, Log, TEXT("Game client on port %i, rate %i"), ConnectURL.Port, ServerConnection->CurrentNetSpeed );

	CreateInitialClientChannels();

	return true;
}

bool UIpNetDriver::InitListen( FNetworkNotify* InNotify, FURL& LocalURL, bool bReuseAddressAndPort, FString& Error )
{
	if( !InitBase( false, InNotify, LocalURL, bReuseAddressAndPort, Error ) )
	{
		UE_LOG(LogNet, Warning, TEXT("Failed to init net driver ListenURL: %s: %s"), *LocalURL.ToString(), *Error);
		return false;
	}


	InitConnectionlessHandler();

	// Update result URL.
	//LocalURL.Host = LocalAddr->ToString(false);
	LocalURL.Port = LocalAddr->GetPort();
	UE_LOG(LogNet, Log, TEXT("%s IpNetDriver listening on port %i"), *GetDescription(), LocalURL.Port );

	return true;
}

class FIpConnectionHelper
{
private:
	friend class UIpNetDriver;
	static void HandleSocketRecvError(UIpNetDriver* Driver, UIpConnection* Connection, const FString& ErrorString)
	{
		Connection->HandleSocketRecvError(Driver, ErrorString);
	}
};

void UIpNetDriver::TickDispatch(float DeltaTime)
{
	LLM_SCOPE(ELLMTag::Networking);

	Super::TickDispatch( DeltaTime );

#if !UE_BUILD_SHIPPING
	PauseReceiveEnd = (PauseReceiveEnd != 0.f && PauseReceiveEnd - (float)FPlatformTime::Seconds() > 0.f) ? PauseReceiveEnd : 0.f;

	if (PauseReceiveEnd != 0.f)
	{
		return;
	}
#endif

	// Set the context on the world for this driver's level collection.
	const int32 FoundCollectionIndex = World ? World->GetLevelCollections().IndexOfByPredicate([this](const FLevelCollection& Collection)
	{
		return Collection.GetNetDriver() == this;
	}) : INDEX_NONE;

	FScopedLevelCollectionContextSwitch LCSwitch(FoundCollectionIndex, World);


	DDoS.PreFrameReceive(DeltaTime);

	ISocketSubsystem* SocketSubsystem = GetSocketSubsystem();
	const double StartReceiveTime = FPlatformTime::Seconds();
	double AlarmTime = StartReceiveTime + GIpNetDriverMaxDesiredTimeSliceBeforeAlarmSecs;
	const bool bSlowFrameChecks = OnNetworkProcessingCausingSlowFrame.IsBound();
	bool bRetrieveTimestamps = CVarNetUseRecvTimestamps.GetValueOnAnyThread() != 0;

	const bool bCheckReceiveTime = (MaxSecondsInReceive > 0.0) && (NbPacketsBetweenReceiveTimeTest > 0);
	const double BailOutTime = StartReceiveTime + MaxSecondsInReceive;
	int32 PacketsLeftUntilTimeTest = NbPacketsBetweenReceiveTimeTest;

	bool bContinueProcessing(true);

	// Process all incoming packets
	for (FPacketIterator It(this); It && bContinueProcessing; ++It)
	{
		// @todo: Remove the slow frame checks, eventually - potential DDoS and Switch platform constraint
		if (bSlowFrameChecks)
		{
			const double CurrentTime = FPlatformTime::Seconds();
			if (CurrentTime > AlarmTime)
			{
				OnNetworkProcessingCausingSlowFrame.Broadcast();

				AlarmTime = CurrentTime + GIpNetDriverMaxDesiredTimeSliceBeforeAlarmSecs;
			}
		}

		if (bCheckReceiveTime)
		{
			--PacketsLeftUntilTimeTest;
			if (PacketsLeftUntilTimeTest <= 0)
			{
				PacketsLeftUntilTimeTest = NbPacketsBetweenReceiveTimeTest;

				const double CurrentTime = FPlatformTime::Seconds();
				if (CurrentTime > BailOutTime)
				{
					bContinueProcessing = false;
					UE_LOG(LogNet, Warning, TEXT("UIpNetDriver::TickDispatch: Stopping packet reception after processing for more than %f seconds. %s"), MaxSecondsInReceive, *GetName());
				}
			}
		}

		FReceivedPacketView ReceivedPacket;
		bool bOk = It.GetCurrentPacket(ReceivedPacket);
		const TSharedRef<const FInternetAddr> FromAddr = ReceivedPacket.Address.ToSharedRef();
		UNetConnection* Connection = nullptr;
		UIpConnection* const MyServerConnection = GetServerConnection();

		if (bOk)
		{
			// Immediately stop processing (continuing to next receive), for empty packets (usually a DDoS)
			if (ReceivedPacket.Data.Num() == 0)
			{
				DDoS.IncBadPacketCounter();
				continue;
			}

			FPacketAudit::NotifyLowLevelReceive((uint8*)ReceivedPacket.Data.GetData(), ReceivedPacket.Data.Num());
		}
		else
		{
			if (IsRecvFailBlocking(ReceivedPacket.Error))
			{
				break;
			}
			else if (ReceivedPacket.Error != SE_ECONNRESET && ReceivedPacket.Error != SE_UDP_ERR_PORT_UNREACH)
			{
				// MalformedPacket: Client tried receiving a packet that exceeded the maximum packet limit
				// enforced by the server
				if (ReceivedPacket.Error == SE_EMSGSIZE)
				{
					DDoS.IncBadPacketCounter();

					if (MyServerConnection)
					{
						if (MyServerConnection->RemoteAddr->CompareEndpoints(*FromAddr))
						{
							Connection = MyServerConnection;
						}
						else
						{
							UE_LOG(LogNet, Log, TEXT("Received packet with bytes > max MTU from an incoming IP address that doesn't match expected server address: Actual: %s Expected: %s"),
								*FromAddr->ToString(true),
								MyServerConnection->RemoteAddr.IsValid() ? *MyServerConnection->RemoteAddr->ToString(true) : TEXT("Invalid"));
							continue;
						}
					}

					if (Connection != nullptr)
					{
						UE_SECURITY_LOG(Connection, ESecurityEvent::Malformed_Packet, TEXT("Received Packet with bytes > max MTU"));
					}
				}
				else
				{
					DDoS.IncErrorPacketCounter();
				}

				FString ErrorString = FString::Printf(TEXT("UIpNetDriver::TickDispatch: Socket->RecvFrom: %i (%s) from %s"),
					static_cast<int32>(ReceivedPacket.Error),
					SocketSubsystem->GetSocketError(ReceivedPacket.Error),
					*FromAddr->ToString(true));


				// This should only occur on clients - on servers it leaves the NetDriver in an invalid/vulnerable state
				if (MyServerConnection != nullptr)
				{
					// TODO: Maybe we should check to see whether or not the From address matches the server?
					// If not, we could forward errors incorrectly, causing the connection to shut down.

					FIpConnectionHelper::HandleSocketRecvError(this, MyServerConnection, ErrorString);
					break;
				}
				else
				{
					// TODO: Should we also forward errors to connections here?
					// If we did, instead of just shutting down the NetDriver completely we could instead
					// boot the given connection.
					// May be DDoS concerns with the cost of looking up the connections for malicious packets
					// from sources that won't have connections.
					UE_CLOG(!DDoS.CheckLogRestrictions(), LogNet, Warning, TEXT("%s"), *ErrorString);
				}

				// Unexpected packet errors should continue to the next iteration, rather than block all further receives this tick
				continue;
			}
		}


		// Figure out which socket the received data came from.
		if (MyServerConnection)
		{
			if (MyServerConnection->RemoteAddr->CompareEndpoints(*FromAddr))
			{
				Connection = MyServerConnection;
			}
			else
			{
				UE_LOG(LogNet, Warning, TEXT("Incoming ip address doesn't match expected server address: Actual: %s Expected: %s"),
					*FromAddr->ToString(true),
					MyServerConnection->RemoteAddr.IsValid() ? *MyServerConnection->RemoteAddr->ToString(true) : TEXT("Invalid"));
			}
		}

		bool bRecentlyDisconnectedClient = false;

		if (Connection == nullptr)
		{
			UNetConnection** Result = MappedClientConnections.Find(FromAddr);

			if (Result != nullptr)
			{
				UNetConnection* ConnVal = *Result;

				if (ConnVal != nullptr)
				{
					Connection = ConnVal;
				}
				else
				{
					bRecentlyDisconnectedClient = true;
				}
			}
			check(Connection == nullptr || CastChecked<UIpConnection>(Connection)->RemoteAddr->CompareEndpoints(*FromAddr));
		}


		if( bOk == false )
		{
			if( Connection )
			{
				if( Connection != GetServerConnection() )
				{
					// We received an ICMP port unreachable from the client, meaning the client is no longer running the game
					// (or someone is trying to perform a DoS attack on the client)

					// rcg08182002 Some buggy firewalls get occasional ICMP port
					// unreachable messages from legitimate players. Still, this code
					// will drop them unceremoniously, so there's an option in the .INI
					// file for servers with such flakey connections to let these
					// players slide...which means if the client's game crashes, they
					// might get flooded to some degree with packets until they timeout.
					// Either way, this should close up the usual DoS attacks.
					if ((Connection->State != USOCK_Open) || (!AllowPlayerPortUnreach))
					{
						if (LogPortUnreach)
						{
							UE_LOG(LogNet, Log, TEXT("Received ICMP port unreachable from client %s.  Disconnecting."),
								*FromAddr->ToString(true));
						}
						Connection->CleanUp();
					}
				}
			}
			else
			{
				bRecentlyDisconnectedClient ? DDoS.IncDisconnPacketCounter() : DDoS.IncNonConnPacketCounter();

				if (LogPortUnreach && !DDoS.CheckLogRestrictions())
				{
					UE_LOG(LogNet, Log, TEXT("Received ICMP port unreachable from %s.  No matching connection found."),
						*FromAddr->ToString(true));
				}
			}
		}
		else
		{
			bool bIgnorePacket = false;

			// If we didn't find a client connection, maybe create a new one.
			if (Connection == nullptr)
			{
				if (DDoS.IsDDoSDetectionEnabled())
				{
					// If packet limits were reached, stop processing
					if (DDoS.ShouldBlockNonConnPackets())
					{
						DDoS.IncDroppedPacketCounter();
						continue;
					}


					bRecentlyDisconnectedClient ? DDoS.IncDisconnPacketCounter() : DDoS.IncNonConnPacketCounter();

					DDoS.CondCheckNonConnQuotasAndLimits();
				}

				// Determine if allowing for client/server connections
				const bool bAcceptingConnection = Notify != nullptr && Notify->NotifyAcceptingConnection() == EAcceptConnection::Accept;

				if (bAcceptingConnection)
				{
					UE_CLOG(!DDoS.CheckLogRestrictions(), LogNet, Log, TEXT("NotifyAcceptingConnection accepted from: %s"),
								*FromAddr->ToString(true));

					FPacketBufferView WorkingBuffer = It.GetWorkingBuffer();

					Connection = ProcessConnectionlessPacket(ReceivedPacket, WorkingBuffer);
					bIgnorePacket = ReceivedPacket.Data.Num() == 0;
				}
				else
				{
					UE_LOG(LogNet, VeryVerbose, TEXT("NotifyAcceptingConnection denied from: %s"), *FromAddr->ToString(true));
				}
			}

			// Send the packet to the connection for processing.
			if (Connection != nullptr && !bIgnorePacket)
			{
				if (DDoS.IsDDoSDetectionEnabled())
				{
					DDoS.IncNetConnPacketCounter();
					DDoS.CondCheckNetConnLimits();
				}

				if (bRetrieveTimestamps)
				{
					It.GetCurrentPacketTimestamp(Connection);
				}

				Connection->ReceivedRawPacket((uint8*)ReceivedPacket.Data.GetData(), ReceivedPacket.Data.Num());
			}
		}
	}

	DDoS.PostFrameReceive();

	const float DeltaReceiveTime = FPlatformTime::Seconds() - StartReceiveTime;

	if (DeltaReceiveTime > GIpNetDriverLongFramePrintoutThresholdSecs)
	{
		UE_LOG( LogNet, Warning, TEXT( "UIpNetDriver::TickDispatch: Took too long to receive packets. Time: %2.2f %s" ), DeltaReceiveTime, *GetName() );
	}
}

UNetConnection* UIpNetDriver::ProcessConnectionlessPacket(FReceivedPacketView& PacketRef, const FPacketBufferView& WorkingBuffer)
{
	UNetConnection* ReturnVal = nullptr;
	TSharedPtr<StatelessConnectHandlerComponent> StatelessConnect;
	const TSharedPtr<FInternetAddr>& Address = PacketRef.Address;
	FString IncomingAddress = Address->ToString(true);
	bool bPassedChallenge = false;
	bool bRestartedHandshake = false;
	bool bIgnorePacket = true;

	if (ConnectionlessHandler.IsValid() && StatelessConnectComponent.IsValid())
	{
		StatelessConnect = StatelessConnectComponent.Pin();

		const ProcessedPacket HandlerResult = ConnectionlessHandler->IncomingConnectionless(Address,
																		(uint8*)PacketRef.Data.GetData(), PacketRef.Data.Num());

		if (!HandlerResult.bError)
		{
			bPassedChallenge = StatelessConnect->HasPassedChallenge(Address, bRestartedHandshake);

			if (bPassedChallenge)
			{
				if (bRestartedHandshake)
				{
					UE_LOG(LogNet, Log, TEXT("Finding connection to update to new address: %s"), *IncomingAddress);

					TSharedPtr<StatelessConnectHandlerComponent> CurComp;
					UIpConnection* FoundConn = nullptr;

					for (UNetConnection* const CurConn : ClientConnections)
					{
						CurComp = CurConn != nullptr ? CurConn->StatelessConnectComponent.Pin() : nullptr;

						if (CurComp.IsValid() && StatelessConnect->DoesRestartedHandshakeMatch(*CurComp))
						{
							FoundConn = Cast<UIpConnection>(CurConn);
							break;
						}
					}

					if (FoundConn != nullptr)
					{
						UNetConnection* RemovedConn = nullptr;
						TSharedRef<FInternetAddr> RemoteAddrRef = FoundConn->RemoteAddr.ToSharedRef();

						verify(MappedClientConnections.RemoveAndCopyValue(RemoteAddrRef, RemovedConn) && RemovedConn == FoundConn);


						// @todo: There needs to be a proper/standardized copy API for this. Also in IpConnection.cpp
						bool bIsValid = false;

						const FString OldAddress = RemoteAddrRef->ToString(true);

						RemoteAddrRef->SetIp(*Address->ToString(false), bIsValid);
						RemoteAddrRef->SetPort(Address->GetPort());


						MappedClientConnections.Add(RemoteAddrRef, FoundConn);


						// Make sure we didn't just invalidate a RecentlyDisconnectedClients entry, with the same address
						int32 RecentDisconnectIdx = RecentlyDisconnectedClients.IndexOfByPredicate(
							[&RemoteAddrRef](const FDisconnectedClient& CurElement)
							{
								return *RemoteAddrRef == *CurElement.Address;
							});

						if (RecentDisconnectIdx != INDEX_NONE)
						{
							RecentlyDisconnectedClients.RemoveAt(RecentDisconnectIdx);
						}


						ReturnVal = FoundConn;

						// We shouldn't need to log IncomingAddress, as the UNetConnection should dump it with it's description.
						UE_LOG(LogNet, Log, TEXT("Updated IP address for connection. Connection = %s, Old Address = %s"), *FoundConn->Describe(), *OldAddress);
					}
					else
					{
						UE_LOG(LogNet, Log, TEXT("Failed to find an existing connection with a matching cookie. Restarted Handshake failed."));
					}
				}


				int32 NewCountBytes = FMath::DivideAndRoundUp(HandlerResult.CountBits, 8);

				if (NewCountBytes > 0)
				{
					FMemory::Memcpy(WorkingBuffer.Buffer.GetData(), HandlerResult.Data, NewCountBytes);

					bIgnorePacket = false;
				}

				PacketRef.Data = MakeArrayView(WorkingBuffer.Buffer.GetData(), NewCountBytes);
			}
		}
	}
#if !UE_BUILD_SHIPPING
	else if (FParse::Param(FCommandLine::Get(), TEXT("NoPacketHandler")))
	{
		UE_CLOG(!DDoS.CheckLogRestrictions(), LogNet, Log, TEXT("Accepting connection without handshake, due to '-NoPacketHandler'."))

		bIgnorePacket = false;
		bPassedChallenge = true;
	}
#endif
	else
	{
		UE_LOG(LogNet, Log, TEXT("Invalid ConnectionlessHandler (%i) or StatelessConnectComponent (%i); can't accept connections."),
				(int32)(ConnectionlessHandler.IsValid()), (int32)(StatelessConnectComponent.IsValid()));
	}

	if (bPassedChallenge)
	{
		if (!bRestartedHandshake)
		{
			SCOPE_CYCLE_COUNTER(Stat_IpNetDriverAddNewConnection);

			UE_LOG(LogNet, Log, TEXT("Server accepting post-challenge connection from: %s"), *IncomingAddress);

			ReturnVal = NewObject<UIpConnection>(GetTransientPackage(), NetConnectionClass);
			check(ReturnVal != nullptr);

#if STATELESSCONNECT_HAS_RANDOM_SEQUENCE
			// Set the initial packet sequence from the handshake data
			if (StatelessConnect.IsValid())
			{
				int32 ServerSequence = 0;
				int32 ClientSequence = 0;

				StatelessConnect->GetChallengeSequence(ServerSequence, ClientSequence);

				ReturnVal->InitSequence(ClientSequence, ServerSequence);
			}
#endif

			ReturnVal->InitRemoteConnection(this, Socket, World ? World->URL : FURL(), *Address, USOCK_Open);

			if (ReturnVal->Handler.IsValid())
			{
				ReturnVal->Handler->BeginHandshaking();
			}

			Notify->NotifyAcceptedConnection(ReturnVal);
			AddClientConnection(ReturnVal);
		}

		if (StatelessConnect.IsValid())
		{
			StatelessConnect->ResetChallengeData();
		}
	}
	else
	{
		UE_LOG(LogNet, VeryVerbose, TEXT("Server failed post-challenge connection from: %s"), *IncomingAddress);
	}

	if (bIgnorePacket)
	{
		PacketRef.Data = MakeArrayView(PacketRef.Data.GetData(), 0);
	}

	return ReturnVal;
}

void UIpNetDriver::LowLevelSend(TSharedPtr<const FInternetAddr> Address, void* Data, int32 CountBits, FOutPacketTraits& Traits)
{
	if (Address.IsValid() && Address->IsValid())
	{
		const uint8* DataToSend = reinterpret_cast<uint8*>(Data);

		if (ConnectionlessHandler.IsValid())
		{
			const ProcessedPacket ProcessedData =
					ConnectionlessHandler->OutgoingConnectionless(Address, (uint8*)DataToSend, CountBits, Traits);

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


		int32 BytesSent = 0;

		if (CountBits > 0)
		{
			CLOCK_CYCLES(SendCycles);
			Socket->SendTo(DataToSend, FMath::DivideAndRoundUp(CountBits, 8), BytesSent, *Address);
			UNCLOCK_CYCLES(SendCycles);
		}


		// @todo: Can't implement these profiling events (require UNetConnections)
		//NETWORK_PROFILER(GNetworkProfiler.FlushOutgoingBunches(/* UNetConnection */));
		//NETWORK_PROFILER(GNetworkProfiler.TrackSocketSendTo(Socket->GetDescription(),Data,BytesSent,NumPacketIdBits,NumBunchBits,
							//NumAckBits,NumPaddingBits, /* UNetConnection */));
	}
	else
	{
		UE_LOG(LogNet, Warning, TEXT("UIpNetDriver::LowLevelSend: Invalid send address '%s'"), *Address->ToString(true));
	}
}



FString UIpNetDriver::LowLevelGetNetworkNumber()
{
	return LocalAddr.IsValid() ? LocalAddr->ToString(true) : FString(TEXT(""));
}

void UIpNetDriver::LowLevelDestroy()
{
	Super::LowLevelDestroy();

	// Close the socket.
	if( Socket && !HasAnyFlags(RF_ClassDefaultObject) )
	{
		// Wait for send tasks if needed before closing the socket,
		// since at this point CleanUp() may not have been called on the server connection.
		UIpConnection* const IpServerConnection = GetServerConnection();
		if (IpServerConnection)
		{
			IpServerConnection->WaitForSendTasks();
		}

		ISocketSubsystem* SocketSubsystem = GetSocketSubsystem();

		// If using a recieve thread, shut down the socket, which will signal the thread to exit gracefully, then wait on the thread.
		if (SocketReceiveThread.IsValid() && SocketReceiveThreadRunnable.IsValid())
		{
			UE_LOG(LogNet, Log, TEXT("Shutting down and waiting for socket receive thread for %s"), *GetDescription());

			SocketReceiveThreadRunnable->bIsRunning = false;
			
			if (!Socket->Shutdown(ESocketShutdownMode::Read))
			{
				const ESocketErrors ShutdownError = SocketSubsystem->GetLastErrorCode();
				UE_LOG(LogNet, Log, TEXT("UIpNetDriver::LowLevelDestroy Socket->Shutdown returned error %s (%d) for %s"), SocketSubsystem->GetSocketError(ShutdownError), static_cast<int>(ShutdownError), *GetDescription());
			}

			SCOPE_CYCLE_COUNTER(STAT_IpNetDriver_Destroy_WaitForReceiveThread);
			SocketReceiveThread->WaitForCompletion();
		}

		if( !Socket->Close() )
		{
			UE_LOG(LogExit, Log, TEXT("closesocket error (%i)"), (int32)SocketSubsystem->GetLastErrorCode() );
		}
		// Free the memory the OS allocated for this socket
		SocketSubsystem->DestroySocket(Socket);
		Socket = nullptr;

		UE_LOG(LogExit, Log, TEXT("%s shut down"),*GetDescription() );
	}

}


bool UIpNetDriver::HandleSocketsCommand( const TCHAR* Cmd, FOutputDevice& Ar, UWorld* InWorld )
{
	Ar.Logf(TEXT(""));
	if (Socket != NULL)
	{
		TSharedRef<FInternetAddr> LocalInternetAddr = GetSocketSubsystem()->CreateInternetAddr();
		Socket->GetAddress(*LocalInternetAddr);
		Ar.Logf(TEXT("%s Socket: %s"), *GetDescription(), *LocalInternetAddr->ToString(true));
	}		
	else
	{
		Ar.Logf(TEXT("%s Socket: null"), *GetDescription());
	}
	return UNetDriver::Exec( InWorld, TEXT("SOCKETS"),Ar);
}

bool UIpNetDriver::HandlePauseReceiveCommand(const TCHAR* Cmd, FOutputDevice& Ar, UWorld* InWorld)
{
	FString PauseTimeStr;
	uint32 PauseTime;

	if (FParse::Token(Cmd, PauseTimeStr, false) && (PauseTime = FCString::Atoi(*PauseTimeStr)) > 0)
	{
		Ar.Logf(TEXT("Pausing Socket Receives for '%i' seconds."), PauseTime);

		PauseReceiveEnd = FPlatformTime::Seconds() + (double)PauseTime;
	}
	else
	{
		Ar.Logf(TEXT("Must specify a pause time, in seconds."));
	}

	return true;
}

#if !UE_BUILD_SHIPPING
void UIpNetDriver::TestSuddenPortChange(uint32 NumConnections)
{
	if (ConnectionlessHandler.IsValid() && StatelessConnectComponent.IsValid())
	{
		TSharedPtr<StatelessConnectHandlerComponent> StatelessConnect = StatelessConnectComponent.Pin();

		for (int32 i = 0; i < ClientConnections.Num() && NumConnections-- > 0; i++)
		{
			// Reset the connection's port to pretend that we used to be sending traffic on an old connection. This is
			// done because once the test is complete, we need to be back onto the port we started with. This
			// fakes what happens in live with clients randomly sending traffic on a new port.
			UIpConnection* const TestConnection = (UIpConnection*)ClientConnections[i];
			TSharedRef<FInternetAddr> RemoteAddrRef = TestConnection->RemoteAddr.ToSharedRef();

			MappedClientConnections.Remove(RemoteAddrRef);

			TestConnection->RemoteAddr->SetPort(i + 9876);

			MappedClientConnections.Add(RemoteAddrRef, TestConnection);

			// We need to set AllowPlayerPortUnreach to true because the net driver will try sending traffic
			// to the IP/Port we just set which is invalid. On Windows, this causes an error to be returned in
			// RecvFrom (WSAECONNRESET). When AllowPlayerPortUnreach is true, these errors are ignored.
			AllowPlayerPortUnreach = true;
			UE_LOG(LogNet, Log, TEXT("TestSuddenPortChange - Changed this connection: %s."), *TestConnection->Describe());
		}
	}
}
#endif

bool UIpNetDriver::Exec( UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar )
{
	if (FParse::Command(&Cmd,TEXT("SOCKETS")))
	{
		return HandleSocketsCommand( Cmd, Ar, InWorld );
	}
	else if (FParse::Command(&Cmd, TEXT("PauseReceive")))
	{
		return HandlePauseReceiveCommand(Cmd, Ar, InWorld);
	}

	return UNetDriver::Exec( InWorld, Cmd,Ar);
}

UIpConnection* UIpNetDriver::GetServerConnection() 
{
	return (UIpConnection*)ServerConnection;
}

UIpNetDriver::FReceiveThreadRunnable::FReceiveThreadRunnable(UIpNetDriver* InOwningNetDriver)
	: ReceiveQueue(CVarNetIpNetDriverReceiveThreadQueueMaxPackets.GetValueOnAnyThread())
	, bIsRunning(true)
	, OwningNetDriver(InOwningNetDriver)
{
	SocketSubsystem = OwningNetDriver->GetSocketSubsystem();
}

uint32 UIpNetDriver::FReceiveThreadRunnable::Run()
{
	const FTimespan Timeout = FTimespan::FromMilliseconds(CVarNetIpNetDriverReceiveThreadPollTimeMS.GetValueOnAnyThread());

	UE_LOG(LogNet, Log, TEXT("UIpNetDriver::FReceiveThreadRunnable::Run starting up."));

	while (bIsRunning && OwningNetDriver->Socket)
	{
		FReceivedPacket IncomingPacket;

		if (OwningNetDriver->Socket->Wait(ESocketWaitConditions::WaitForRead, Timeout))
		{
			bool bOk = false;
			int32 BytesRead = 0;

			IncomingPacket.FromAddress = SocketSubsystem->CreateInternetAddr();

			IncomingPacket.PacketBytes.AddUninitialized(MAX_PACKET_SIZE);

			{
				SCOPE_CYCLE_COUNTER(STAT_IpNetDriver_RecvFromSocket);
				bOk = OwningNetDriver->Socket->RecvFrom(IncomingPacket.PacketBytes.GetData(), IncomingPacket.PacketBytes.Num(), BytesRead, *IncomingPacket.FromAddress);
			}

			if (bOk)
			{
				if (BytesRead == 0)
				{
					// Don't even queue empty packets, they can be ignored.
					continue;
				}
			}
			else
			{
				// This relies on the platform's implementation using thread-local storage for the last socket error code.
				IncomingPacket.Error = SocketSubsystem->GetLastErrorCode();

				// Pass all other errors back to the Game Thread
				if (IsRecvFailBlocking(IncomingPacket.Error))
				{
					continue;
				}
			}


			IncomingPacket.PacketBytes.SetNum(FMath::Max(BytesRead, 0), false);
			IncomingPacket.PlatformTimeSeconds = FPlatformTime::Seconds();

			// Add packet to queue. Since ReceiveQueue is a TCircularQueue, if the queue is full, this will simply return false without adding anything.
			ReceiveQueue.Enqueue(MoveTemp(IncomingPacket));
		}
		else
		{
			const ESocketErrors WaitError = SocketSubsystem->GetLastErrorCode();
			if(WaitError != ESocketErrors::SE_NO_ERROR)
			{
				IncomingPacket.Error = WaitError;
				IncomingPacket.PlatformTimeSeconds = FPlatformTime::Seconds();

				UE_LOG(LogNet, Log, TEXT("UIpNetDriver::FReceiveThreadRunnable::Run: Socket->Wait returned error %s (%d)"), SocketSubsystem->GetSocketError(IncomingPacket.Error), static_cast<int>(IncomingPacket.Error));

				ReceiveQueue.Enqueue(MoveTemp(IncomingPacket));
			}
		}
	}

	UE_LOG(LogNet, Log, TEXT("UIpNetDriver::FReceiveThreadRunnable::Run returning."));

	return 0;
}
