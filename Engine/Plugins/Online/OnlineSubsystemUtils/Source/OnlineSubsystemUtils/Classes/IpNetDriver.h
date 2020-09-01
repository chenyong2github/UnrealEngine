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
#include "SocketSubsystem.h"
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
	UE_DEPRECATED(4.25, "Socket access is now controlled through the getter/setter combo: GetSocket and SetSocketAndLocalAddress")
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
		return GetSocket() != nullptr;
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

		PacketView.DataView = {Data, CountBytesRef, ECountUnits::Bytes};
		PacketView.Address = Address;
		PacketView.Error = SE_NO_ERROR;

		UNetConnection* ReturnVal = ProcessConnectionlessPacket(PacketView, { Data, MAX_PACKET_SIZE } );

		CountBytesRef = PacketView.DataView.NumBytes();

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

	/** Clears all references to sockets held by this net driver. */
	void ClearSockets();

public:
	//~ Begin UIpNetDriver Interface.

	/**
	 * Creates a socket to be used for network communications. Uses the LocalAddr (if set) to determine protocol flags
	 *
	 * @return an FSocket if creation succeeded, nullptr if creation failed.
	 */
	UE_DEPRECATED(4.25, "Socket creation is now restricted to subclasses and done through CreateSocketForProtocol or CreateAndBindSocket")
	virtual FSocket* CreateSocket();
	
	/**
	 * Returns the current FSocket to be used with this NetDriver. This is useful in the cases of resolution as it will always point to the Socket that's currently being
	 * used with the current resolution attempt.
	 *
	 * @return a pointer to the socket to use.
	 */
	virtual FSocket* GetSocket();

	/**
	 * Set the current NetDriver's socket to the given socket and takes ownership of it (the driver will handle destroying it).
	 * This is typically done after resolution completes successfully. 
	 * This will also set the LocalAddr for the netdriver automatically.
	 *
	 * @param NewSocket the socket pointer to set this netdriver's socket to
	 */
	void SetSocketAndLocalAddress(FSocket* NewSocket);

	/**
	* Set the current NetDriver's socket to the given socket.
	* This is typically done after resolution completes successfully. 
	* This will also set the LocalAddr for the netdriver automatically.
	*
	* @param NewSocket the socket pointer to set this netdriver's socket to
	*/
	void SetSocketAndLocalAddress(const TSharedPtr<FSocket>& SharedSocket);

	/**
	 * Returns the port number to use when a client is creating a socket.
	 * Platforms that can't use the default of 0 (system-selected port) may override
	 * this function.
	 *
	 * @return The port number to use for client sockets. Base implementation returns 0.
	 */
	virtual int GetClientPort();

protected:
	/**
	 * Creates a socket set up for communication using the given protocol. This allows for explicit creation instead of inferring type for you.
	 *
	 * @param ProtocolType	an FName that represents the protocol to allocate the new socket under. Typically set to None or a value in FNetworkProtocolTypes
	 * @return				an FSocket if creation succeeded, nullptr if creation failed.
	 */
	virtual FUniqueSocket CreateSocketForProtocol(const FName& ProtocolType);

	/**
	 * Creates, initializes and binds a socket using the given bind address information.
	 *
	 * @param BindAddr				the address to bind the new socket to, will also create the socket using the address protocol using CreateSocketForProtocol
	 * @param Port					the port number to use with the given bind address.
	 * @param bReuseAddressAndPort	if true, will set the socket to be bound even if the address is in use
	 * @param DesiredRecvSize		the max size of the recv buffer for the socket
	 * @param DesiredSendSize		the max size of the sending buffer for the socket
	 * @param Error					a string reference that will be populated with any error messages should an error occur
	 *
	 * @return if the socket could be created and bound with all the appropriate options, a pointer to the new socket is given, otherwise null
	 */
	virtual FUniqueSocket CreateAndBindSocket(TSharedRef<FInternetAddr> BindAddr, int32 Port, bool bReuseAddressAndPort, int32 DesiredRecvSize, int32 DesiredSendSize, FString& Error);
	//~ End UIpNetDriver Interface.

public:
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


	/** @return IP connection to server */
	class UIpConnection* GetServerConnection();

	/** @return The address resolution timeout value */
	float GetResolutionTimeoutValue() const { return ResolutionConnectionTimeout; }

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

	/** The amount of time to wait in seconds until we consider a connection to a resolution result as timed out */
	UPROPERTY(Config)
	float ResolutionConnectionTimeout;

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
		bool DispatchPacket(FReceivedPacket&& IncomingPacket, int32 NbBytesRead);

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

	/** 
	 * An array sockets created for every binding address a machine has in use for performing address resolution. 
	 * This array empties after connections have been spun up.
	 */
	TArray<TSharedPtr<FSocket>> BoundSockets;

	/** Underlying socket communication */
	TSharedPtr<FSocket> SocketPrivate;
};
