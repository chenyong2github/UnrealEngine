// Copyright Epic Games, Inc. All Rights Reserved.

//
// Ip based implementation of a network connection used by the net driver class
//

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/NetConnection.h"
#include "Async/TaskGraphInterfaces.h"
#include "SocketTypes.h"
#include "IpConnection.generated.h"

class FInternetAddr;
class ISocketSubsystem;

/** A state system of the address resolution functionality. */
enum class EAddressResolutionState : uint8
{
	None = 0,
	Disabled,
	WaitingForResolves,
	Connecting,
	TryNextAddress,
	Connected,
	Done,	
	Error
};

UCLASS(transient, config=Engine)
class ONLINESUBSYSTEMUTILS_API UIpConnection : public UNetConnection
{
    GENERATED_UCLASS_BODY()
	// Variables.

	/** This is a non-owning pointer to a socket owned elsewhere, IpConnection will not destroy the socket through this pointer. */
	class FSocket*				Socket;
	UE_DEPRECATED(4.25, "Address resolution is now handled in the IpNetDriver and no longer done entirely in the IpConnection")
	class FResolveInfo*			ResolveInfo;

	//~ Begin NetConnection Interface
	virtual void InitBase(UNetDriver* InDriver, class FSocket* InSocket, const FURL& InURL, EConnectionState InState, int32 InMaxPacket = 0, int32 InPacketOverhead = 0) override;
	virtual void InitRemoteConnection(UNetDriver* InDriver, class FSocket* InSocket, const FURL& InURL, const class FInternetAddr& InRemoteAddr, EConnectionState InState, int32 InMaxPacket = 0, int32 InPacketOverhead = 0) override;
	virtual void InitLocalConnection(UNetDriver* InDriver, class FSocket* InSocket, const FURL& InURL, EConnectionState InState, int32 InMaxPacket = 0, int32 InPacketOverhead = 0) override;
	virtual void LowLevelSend(void* Data, int32 CountBits, FOutPacketTraits& Traits) override;
	FString LowLevelGetRemoteAddress(bool bAppendPort=false) override;
	FString LowLevelDescribe() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void CleanUp() override;
	virtual void ReceivedRawPacket(void* Data, int32 Count) override;
	virtual float GetTimeoutValue() override;
	//~ End NetConnection Interface

	/**
	 * If CVarNetIpConnectionUseSendTasks is true, blocks until there are no outstanding send tasks.
	 * Since these tasks need to access the socket, this is called before the net driver closes the socket.
	 */
	void WaitForSendTasks();

private:
	/**
	 * Struct to hold the result of a socket SendTo call. If net.IpConnectionUseSendTasks is true,
	 * these are communicated back to the game thread via SocketSendResults.
	 */
	struct FSocketSendResult
	{
		FSocketSendResult()
			: BytesSent(0)
			, Error(SE_NO_ERROR)
		{
		}

		int32 BytesSent;
		ESocketErrors Error;
	};

	/** Critical section to protect SocketSendResults */
	FCriticalSection SocketSendResultsCriticalSection;

	/** Socket SendTo results from send tasks if net.IpConnectionUseSendTasks is true */
	TArray<FSocketSendResult> SocketSendResults;

	/**
	 * If net.IpConnectionUseSendTasks is true, reference to the last send task used as a prerequisite
	 * for the next send task. Also, CleanUp() blocks until this task is complete.
	 */
	FGraphEventRef LastSendTask;

	/** Instead of disconnecting immediately on a socket error, wait for some time to see if we can recover. Specified in Seconds. */
	UPROPERTY(Config)
	float SocketErrorDisconnectDelay;

	/** Cached time of the first send socket error that will be used to compute disconnect delay. */
	double SocketError_SendDelayStartTime;

	/** Cached time of the first recv socket error that will be used to compute disconnect delay. */
	double SocketError_RecvDelayStartTime;

private:

	/** Handles any SendTo errors on the game thread. */
	void HandleSocketSendResult(const FSocketSendResult& Result, ISocketSubsystem* SocketSubsystem);

	/** Notifies us that we've encountered an error while receiving a packet. */
	void HandleSocketRecvError(class UNetDriver* NetDriver, const FString& ErrorString);

	/** An array of sockets tied to every binding address. */
	TArray<TSharedPtr<FSocket>> BindSockets;

	/** Holds a refcount to the actual socket to be used from BindSockets. */
	TSharedPtr<FSocket> ResolutionSocket;

	/** An array containing the address results GAI returns for the current host value. Given to us from the netdriver. */
	TArray<TSharedRef<FInternetAddr>> ResolverResults;

	/** The index into the ResolverResults that we're currently attempting */
	int32 CurrentAddressIndex;

	/** 
	 *  The connection's current status of where it is in the resolution state machine.
	 *  If a platform should not use resolution, call DisableAddressResolution() in your constructor
	 */
	EAddressResolutionState ResolutionState;

	/**
	 * Cleans up the socket information in use with resolution. This can get called numerous times.
	 */
	void CleanupResolutionSockets();

	/**
	 * Determines if we can continue processing resolution results or not based on flags and
	 * current flow.
	 *
	 * @return if resolution is allowed to continue processing.
	 */
	bool CanContinueResolution() const {
		return CurrentAddressIndex < ResolverResults.Num() && IsAddressResolutionEnabled() &&
			ResolutionState != EAddressResolutionState::Error && ResolutionState != EAddressResolutionState::Done;
	}

	/**
	 * Checks to see if this netconnection class can use address resolution
	 *
	 * @return if address resolution is allowed to continue processing.
	 */
	bool IsAddressResolutionEnabled() const {
		return ResolutionState != EAddressResolutionState::Disabled;
	}

	/**
	 * Checks to see if this netconnection class has encountered an error during process resolution
	 *
	 * @return If an error has occurred
	 */
	bool HasAddressResolutionFailed() const {
		return ResolutionState == EAddressResolutionState::Error;
	}

protected:
	/**
	 * Disables address resolution by pushing the disabled flag into the status field.
	 */
	void DisableAddressResolution() { ResolutionState = EAddressResolutionState::Disabled; }

	/**
	 * Handles a NetConnection timeout. Overridden in order to handle parsing multiple GAI results during resolution.
	 * 
	 * @param ErrorStr A string containing the current error message for either usage or writing into.
	 */
	virtual void HandleConnectionTimeout(const FString& ErrorStr) override;

	friend class FIpConnectionHelper;
};
