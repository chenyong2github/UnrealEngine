// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CompactBinaryTCP.h"
#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "CookSockets.h"
#include "CookTypes.h"
#include "Memory/SharedBuffer.h"
#include "Misc/Guid.h"
#include "Templates/UniquePtr.h"

class FCbObject;
class FCbWriter;
class UCookOnTheFlyServer;
namespace UE::Cook { class FCookWorkerServer; }
namespace UE::Cook { struct FPackageData; }
namespace UE::Cook { struct FWorkerId; }

namespace UE::Cook
{

/**
 * Helper for CookOnTheFlyServer that sends requests to CookWorker processes for load/save and merges
 * their replies into the local process's cook results.
 */
class FCookDirector
{
public:
	FCookDirector(UCookOnTheFlyServer& InCOTFS);
	~FCookDirector();

	/** Assign the given requests out to CookWorkers (or keep on local COTFS), return the list of assignments. */
	void AssignRequests(TArrayView<UE::Cook::FPackageData*> Requests, TArray<FWorkerId>& OutAssignments);
	/** Notify the CookWorker that owns the cook of the package that the Director wants to take it back. */
	void RemoveFromWorker(FPackageData& PackageData);
	/** Periodic tick function. Sends/Receives messages to CookWorkers. */
	void TickFromSchedulerThread();
	/** Called when the COTFS Server has detected all packages are complete. Tells the CookWorkers to flush messages and exit. */
	void PumpCookComplete(bool& bOutCompleted);
	/** Called when a session ends. The Director blocks on shutdown of all CookWorkers and returns state to before session started. */
	void ShutdownCookSession();

	/** Enum specifying how CookWorker log output should be shown. */
	enum EShowWorker
	{
		CombinedLogs,
		SeparateLogs,
		SeparateWindows, // Implies SeparateLogs as well
	};
	EShowWorker GetShowWorkerOption() const { return ShowWorkerOption; }

private:
	/** CookWorker connections that have not yet identified which CookWorker they are. */
	struct FPendingConnection
	{
		explicit FPendingConnection(FSocket* InSocket = nullptr)
		:Socket(InSocket)
		{
		}
		FPendingConnection(FPendingConnection&& Other);
		FPendingConnection(const FPendingConnection& Other) = delete;
		~FPendingConnection();

		FSocket* DetachSocket();

		FSocket* Socket = nullptr;
		UE::CompactBinaryTCP::FReceiveBuffer Buffer;
	};
	/** Helper for constructor parsing. */
	void ParseDesiredNumRemoteWorkers();
	/** Helper for constructor parsing. */
	void ParseShowWorkerOption();
	/** Initialization helper: create the listen socket. */
	bool TryCreateWorkerConnectSocket();
	/** Initialization helper: add the local Server for a remote worker, worker process not yet created. */
	void InitializeWorkers();
	/** Tick helper: tick any workers that have not yet finished initialization. */
	void TickWorkerConnects();
	/** Tick helper: tick any workers that are shutting down. */
	void TickWorkerShutdowns();
	/** Get the commandline to launch a worker process with. */
	FString GetWorkerCommandLine(FWorkerId WorkerId);
	/** Simple assignment that divides requests evenly without considering dependencies or load burden. */
	void LoadBalanceStriped(TArrayView<FCookWorkerServer*> SortedWorkers, TArrayView<FPackageData*> Requests,
		TArray<FWorkerId>& OutAssignments);
	/** Move the given worker from active workers to the list of workers shutting down. */
	void AbortWorker(FWorkerId WorkerId);
	/**
	 * Periodically update whether (1) local server is done and (2) no results from cookworkers have come in.
	 * Send warning when it goes on too long.
	 */
	void SetWorkersStalled(bool bInWorkersStalled);

private:
	TMap<int32, TUniquePtr<FCookWorkerServer>> RemoteWorkers;
	TMap<FCookWorkerServer*, TUniquePtr<FCookWorkerServer>> ShuttingDownWorkers;
	TArray<FPendingConnection> PendingConnections;
	FString WorkerConnectAuthority;
	UCookOnTheFlyServer& COTFS;
	FSocket* WorkerConnectSocket = nullptr;
	double WorkersStalledStartTimeSeconds = 0.;
	double WorkersStalledWarnTimeSeconds = 0.;
	int32 DesiredNumRemoteWorkers = 0;
	int32 WorkerConnectPort = 0;
	bool bWorkersStalled = false;
	EShowWorker ShowWorkerOption = EShowWorker::CombinedLogs;

	friend class UE::Cook::FCookWorkerServer;
};

/** Parameters parsed from commandline for how a CookWorker connects to the CooKDirector. */
struct FDirectorConnectionInfo
{
	bool TryParseCommandLine();

	FString HostURI;
	int32 RemoteIndex = 0;
};

/** Message sent from a CookWorker to the Director to report that it is ready for setup messages and cooking. */
struct FWorkerConnectMessage : public UE::CompactBinaryTCP::IMessage
{
public:
	virtual void Write(FCbWriter& Writer) const override;
	virtual bool TryRead(FCbObject&& Object) override;
	virtual FGuid GetMessageType() const override { return MessageType; }

public:
	int32 RemoteIndex = 0;
	static FGuid MessageType;
};

}