// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CompactBinaryTCP.h"
#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/Set.h"
#include "CookSockets.h"
#include "CookTypes.h"
#include "Misc/Guid.h"

class FSocket;
class UCookOnTheFlyServer;
struct FProcHandle;
namespace UE::CompactBinaryTCP { struct FMarshalledMessage; }
namespace UE::Cook { class FCookDirector; }
namespace UE::Cook { struct FPackageData; }
namespace UE::Cook { struct FWorkerConnectMessage; }

namespace UE::Cook
{

/** Class in a Director process that communicates over a Socket with FCookWorkerClient in a CookWorker process. */
class FCookWorkerServer
{
public:
	FCookWorkerServer(FCookDirector& InDirector, FWorkerId InWorkerId);
	~FCookWorkerServer();

	FWorkerId GetWorkerId() const { return WorkerId; }

	/** Add the given assignments for the CookWorker. They will be sent during Tick */
	void AppendAssignments(TArrayView<FPackageData*> Assignments);
	/** Remove assignment of the package from local state and from the connected Client. */
	void AbortAssignment(FPackageData& PackageData);
	/**
	 * Remove assignment of the all assigned packages from local state and from the connected Client.
	 * Report all packages that were unassigned.
	 */
	void AbortAssignments(TSet<FPackageData*>& OutPendingPackages);
	/** AbortAssignments and tell the connected Client to gracefully terminate. Report all packages that were unassigned. */
	void AbortWorker(TSet<FPackageData*>& OutPendingPackages);
	/** Take over the Socket for a CookWorker that has just connected. */
	bool TryHandleConnectMessage(FWorkerConnectMessage& Message, FSocket* InSocket, TArray<UE::CompactBinaryTCP::FMarshalledMessage>&& OtherPacketMessages);
	/** Periodic Tick function to send and receive messages to the Client. */
	void TickFromSchedulerThread();

	/** Is this either shutting down or completed shutdown of its remote Client? */
	bool IsShuttingDown() const;
	/** Is this not yet or no longer connected to a remote Client? */
	bool IsShutdownComplete() const;

private:
	enum class EConnectStatus
	{
		Uninitialized,
		WaitForConnect,
		Connected,
		WaitForDisconnect,
		LostConnection,
	};

private:
	/** Helper for Tick, Pump messages when connecting or disconnecting. */
	void PumpConnect();
	/** Helper for PumpConnect, launch the remote Client process. */
	void LaunchProcess();
	/** Helper for PumpConnect, wait for connect message from Client, set state to LostConnection if we timeout. */
	void TickWaitForConnect();
	/** Helper for PumpConnect, wait for disconnect message from Client, set state to LostConnection if we timeout. */
	void TickWaitForDisconnect();
	/** Helper for Tick, pump send messages to a connected Client. */
	void PumpSendMessages();
	/** Helper for Tick, pump receive messages to a connected Client. */
	void PumpReceiveMessages();
	/** Send the message immediately to the Socket. If cannot complete immediately, it will be finished during Tick. */
	void SendMessage(const UE::CompactBinaryTCP::IMessage& Message);
	/** Send this into the given state. Update any state-dependent variables. */
	void SendToState(EConnectStatus TargetStatus);
	/** Close the connection and connection resources to the remote process. Does not kill the process. */
	void DetachFromRemoteProcess();
	/** Kill the Client process (non-graceful termination), and close the connection resources. */
	void ShutdownRemoteProcess();
	/** Helper for PumpReceiveMessages: dispatch the messages received from the socket. */
	void HandleReceiveMessages(TArray<UE::CompactBinaryTCP::FMarshalledMessage>&& Messages);

	TArray<FPackageData*> PackagesToAssign;
	TSet<FPackageData*> PendingPackages;
	UE::CompactBinaryTCP::FSendBuffer SendBuffer;
	UE::CompactBinaryTCP::FReceiveBuffer ReceiveBuffer;
	FCookDirector& Director;
	UCookOnTheFlyServer& COTFS;
	FSocket* Socket = nullptr;
	FProcHandle CookWorkerHandle;
	uint32 CookWorkerProcessId = 0;
	double ConnectStartTimeSeconds = 0.;
	double ConnectTestStartTimeSeconds = 0.;
	FWorkerId WorkerId = FWorkerId::Invalid();
	EConnectStatus ConnectStatus = EConnectStatus::Uninitialized;
	bool bTerminateImmediately = false;
};

/** Message from Server to Client to cook the given packages. */
struct FAssignPackagesMessage : public UE::CompactBinaryTCP::IMessage
{
public:
	FAssignPackagesMessage() = default;
	FAssignPackagesMessage(TArray<FName>&& InPackageNames);

	virtual void Write(FCbWriter& Writer) const override;
	virtual bool TryRead(FCbObjectView Object) override;
	virtual FGuid GetMessageType() const override { return MessageType; }

public:
	TArray<FName> PackageNames;
	static FGuid MessageType;
};

/** Message from Server to Client to cancel the cook of the given packages. */
struct FAbortPackagesMessage : public UE::CompactBinaryTCP::IMessage
{
public:
	FAbortPackagesMessage() = default;
	FAbortPackagesMessage(TArray<FName>&& InPackageNames);

	virtual void Write(FCbWriter& Writer) const override;
	virtual bool TryRead(FCbObjectView Object) override;
	virtual FGuid GetMessageType() const override { return MessageType; }

public:
	TArray<FName> PackageNames;
	static FGuid MessageType;
};

/**
 * Message from either Server to Client.
 * If from Server, request that Client shutdown.
 * If from Client, notify Server it is shutting down.
 */
struct FAbortWorkerMessage : public UE::CompactBinaryTCP::IMessage
{
public:
	virtual void Write(FCbWriter& Writer) const override { }
	virtual bool TryRead(FCbObjectView Object) override { return true; }
	virtual FGuid GetMessageType() const override { return MessageType; }

public:
	static FGuid MessageType;
};

/** IMessage helper: write as strings to compact binary. */
void WriteArrayOfNames(FCbWriter& Writer, const char* ArrayName, TConstArrayView<FName> Names);
/** IMessage helper: read FNames as strings from compact binary. */
bool TryReadArrayOfNames(FCbObjectView Object, const char* ArrayName, TArray<FName>& OutNames);

}