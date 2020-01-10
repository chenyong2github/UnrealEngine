// Copyright Epic Games, Inc. All Rights Reserved.

//
// Ip endpoint based implementation of the net driver
//

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/NetDriver.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Containers/CircularQueue.h"
#include "SocketTypes.h"
#include "IpNetDriver.generated.h"


// Forward declarations
class Error;
class FInternetAddr;
class FNetworkNotify;
class FSocket;
struct FRecvMulti;


// CVars
#if !UE_BUILD_SHIPPING
extern TSharedPtr<FInternetAddr> GCurrentDuplicateIP;
#endif


UCLASS(transient, config=Engine)
class ONLINESUBSYSTEMUTILS_API UIpNetDriver : public UNetDriver
{
	friend class FPacketIterator;

    GENERATED_BODY()

public:
	/** Should port unreachable messages be logged */
    UPROPERTY(Config)
    uint32 LogPortUnreach:1;

	/** Does the game allow clients to remain after receiving ICMP port unreachable errors (handles flakey connections) */
    UPROPERTY(Config)
    uint32 AllowPlayerPortUnreach:1;

	/** Number of ports which will be tried if current one is not available for binding (i.e. if told to bind to port N, will try from N to N+MaxPortCountToTry inclusive) */
	UPROPERTY(Config)
	uint32 MaxPortCountToTry;

	/** Underlying socket communication */
	FSocket* Socket;

	/** If pausing socket receives, the time at which this should end */
	float PauseReceiveEnd;


	/** Base constructor */
	UIpNetDriver(const FObjectInitializer& ObjectInitializer);

	//~ Begin UNetDriver Interface.
	virtual bool IsAvailable() const override;
	virtual bool InitBase(bool bInitAsClient, FNetworkNotify* InNotify, const FURL& URL, bool bReuseAddressAndPort, FString& Error) override;
	virtual bool InitConnect( FNetworkNotify* InNotify, const FURL& ConnectURL, FString& Error ) override;
	virtual bool InitListen( FNetworkNotify* InNotify, FURL& LocalURL, bool bReuseAddressAndPort, FString& Error ) override;
	virtual void TickDispatch( float DeltaTime ) override;
	virtual void LowLevelSend(TSharedPtr<const FInternetAddr> Address, void* Data, int32 CountBits, FOutPacketTraits& Traits) override;
	virtual FString LowLevelGetNetworkNumber() override;
	virtual void LowLevelDestroy() override;
	virtual class ISocketSubsystem* GetSocketSubsystem() override;
	virtual bool IsNetResourceValid(void) override
	{
		return Socket != NULL;
	}
	//~ End UNetDriver Interface

	/**
	 * Processes packets not associated with a NetConnection, performing any handshaking and NetConnection creation or remapping, as necessary.
	 *
	 * @param Address			The address the packet came from
	 * @param Data				The packet data (may be modified)
	 * @param CountBytesRef		The packet size (may be modified)
	 * @return					If a new NetConnection is created, returns the net connection
	 */
	UE_DEPRECATED(4.24, "ProcessConnectionlessPacket has a new API, and will become private soon.")
	UNetConnection* ProcessConnectionlessPacket(const TSharedRef<FInternetAddr>& Address, uint8* Data, int32& CountBytesRef)
	{
		FReceivedPacketView PacketView;

		PacketView.Data = MakeArrayView(Data, CountBytesRef);
		PacketView.Address = Address;
		PacketView.Error = SE_NO_ERROR;

		UNetConnection* ReturnVal = ProcessConnectionlessPacket(PacketView, { Data, MAX_PACKET_SIZE } );

		CountBytesRef = PacketView.Data.Num();

		return ReturnVal;
	}

private:
	/**
	 * Process packets not associated with a NetConnection, performing handshaking and NetConnection creation or remapping as necessary.
	 *
	 * @param PacketRef			A view of the received packet (may output a new packet view, possibly using WorkingBuffer)
	 * @param WorkingBuffer		A buffer for storing processed packet data (may be the buffer the input ReceivedPacketRef points to)
	 * @return					If a new NetConnection is created, returns the net connection
	 */
	UNetConnection* ProcessConnectionlessPacket(FReceivedPacketView& PacketRef, const FPacketBufferView& WorkingBuffer);

public:
	//~ Begin UIpNetDriver Interface.
	virtual FSocket * CreateSocket();

	/**
	 * Returns the port number to use when a client is creating a socket.
	 * Platforms that can't use the default of 0 (system-selected port) may override
	 * this function.
	 *
	 * @return The port number to use for client sockets. Base implementation returns 0.
	 */
	virtual int GetClientPort();
	//~ End UIpNetDriver Interface.

	//~ Begin FExec Interface
	virtual bool Exec( UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar=*GLog ) override;
	//~ End FExec Interface


	/** Exec command handlers */

	bool HandleSocketsCommand(const TCHAR* Cmd, FOutputDevice& Ar, UWorld* InWorld);
	bool HandlePauseReceiveCommand(const TCHAR* Cmd, FOutputDevice& Ar, UWorld* InWorld);

#if !UE_BUILD_SHIPPING
	/**
	* Tests a scenario where a connected client suddenly starts sending traffic on a different port (can happen
	* due to a rare router bug.
	*
	* @param NumConnections - number of connections to test sending traffic on a different port.
	*/
	void TestSuddenPortChange(uint32 NumConnections);
#endif


	/** @return TCPIP connection to server */
	class UIpConnection* GetServerConnection();

private:
	/**
	 * Whether or not a socket receive failure Error indicates a blocking error, which should 'break;' the receive loop
	 */
	static FORCEINLINE bool IsRecvFailBlocking(ESocketErrors Error)
	{
		// SE_ECONNABORTED is for PS4 LAN cable pulls, SE_ENETDOWN is for a Switch hang
		return Error == SE_NO_ERROR || Error == SE_EWOULDBLOCK || Error == SE_ECONNABORTED || Error == SE_ENETDOWN;
	};

public:
	// Callback for platform handling when networking is taking a long time in a single frame (by default over 1 second).
	// It may get called multiple times in a single frame if additional processing after a previous alert exceeds the threshold again
	DECLARE_MULTICAST_DELEGATE(FOnNetworkProcessingCausingSlowFrame);
	static FOnNetworkProcessingCausingSlowFrame OnNetworkProcessingCausingSlowFrame;

private:
	/** Number of bytes that will be passed to FSocket::SetReceiveBufferSize when initializing a server. */
	UPROPERTY(Config)
	uint32 ServerDesiredSocketReceiveBufferBytes;

	/** Number of bytes that will be passed to FSocket::SetSendBufferSize when initializing a server. */
	UPROPERTY(Config)
	uint32 ServerDesiredSocketSendBufferBytes;

	/** Number of bytes that will be passed to FSocket::SetReceiveBufferSize when initializing a client. */
	UPROPERTY(Config)
	uint32 ClientDesiredSocketReceiveBufferBytes;

	/** Number of bytes that will be passed to FSocket::SetSendBufferSize when initializing a client. */
	UPROPERTY(Config)
	uint32 ClientDesiredSocketSendBufferBytes;

	/** Maximum time in seconds the TickDispatch can loop on received socket data*/
	UPROPERTY(Config)
	double MaxSecondsInReceive = 0.0;

	/** Nb of packets to wait before testing if the receive time went over MaxSecondsInReceive */
	UPROPERTY(Config)
	int32 NbPacketsBetweenReceiveTimeTest = 0;

	/** Represents a packet received and/or error encountered by the receive thread, if enabled, queued for the game thread to process. */
	struct FReceivedPacket
	{
		/** The content of the packet as received from the socket. */
		TArray<uint8> PacketBytes;

		/** Address from which the packet was received. */
		TSharedPtr<FInternetAddr> FromAddress;

		/** The error triggered by the socket RecvFrom call. */
		ESocketErrors Error;

		/** FPlatformTime::Seconds() at which this packet and/or error was received. Can be used for more accurate ping calculations. */
		double PlatformTimeSeconds;

		FReceivedPacket()
			: Error(SE_NO_ERROR)
			, PlatformTimeSeconds(0.0)
		{
		}
	};

	/** Runnable object representing the receive thread, if enabled. */
	class FReceiveThreadRunnable : public FRunnable
	{
	public:
		FReceiveThreadRunnable(UIpNetDriver* InOwningNetDriver);

		virtual uint32 Run() override;

		/** Thread-safe queue of received packets. The Run() function is the producer, UIpNetDriver::TickDispatch on the game thread is the consumer. */
		TCircularQueue<FReceivedPacket> ReceiveQueue;

		/** Running flag. The Run() function will return shortly after setting this to false. */
		TAtomic<bool> bIsRunning;

	private:
		UIpNetDriver* OwningNetDriver;
		ISocketSubsystem* SocketSubsystem;
	};

	/** Receive thread runnable object. */
	TUniquePtr<FReceiveThreadRunnable> SocketReceiveThreadRunnable;

	/** Receive thread object. */
	TUniquePtr<FRunnableThread> SocketReceiveThread;

	/** The preallocated state/buffers, for efficiently executing RecvMulti */
	TUniquePtr<FRecvMulti> RecvMultiState;
};
