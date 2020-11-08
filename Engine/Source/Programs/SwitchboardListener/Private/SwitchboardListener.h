// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Containers/Queue.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"

struct FRunningProcess;
struct FSwitchboardTask;
struct FSwitchboardDisconnectTask;
struct FSwitchboardSendFileToClientTask;
struct FSwitchboardStartTask;
struct FSwitchboardKillTask;
struct FSwitchboardKillAllTask;
struct FSwitchboardReceiveFileFromClientTask;
struct FSwitchboardGetSyncStatusTask;
struct FSwitchboardMessageFuture;
struct FSwitchboardForceFocusTask;
struct FSwitchboardFixExeFlagsTask;

class FInternetAddr;
class FSocket;
class FTcpListener;


class FSwitchboardListener
{
public:
	explicit FSwitchboardListener(const FIPv4Endpoint& InEndpoint);
	~FSwitchboardListener();

	bool Init();
	bool Tick();

private:
	bool OnIncomingConnection(FSocket* InSocket, const FIPv4Endpoint& InEndpoint);
	bool ParseIncomingMessage(const FString& InMessage, const FIPv4Endpoint& InEndpoint);

	bool RunScheduledTask(const FSwitchboardTask& InTask);
	bool StartProcess(const FSwitchboardStartTask& InRunTask);
	bool KillProcessNow(FRunningProcess* InProcess, float SoftKillTimeout = 0.0f);
	bool KillProcess(const FSwitchboardKillTask& KillTask);
	bool KillAllProcesses(const FSwitchboardKillAllTask& KillAllTask);
	void KillAllProcessesNow();
	bool ReceiveFileFromClient(const FSwitchboardReceiveFileFromClientTask& InReceiveFileFromClientTask);
	bool SendFileToClient(const FSwitchboardSendFileToClientTask& InSendFileToClientTask);
	bool GetSyncStatus(const FSwitchboardGetSyncStatusTask& InGetSyncStatusTask);
	bool ForceFocus(const FSwitchboardForceFocusTask& ForceFocusTask);
	bool FixExeFlags(const FSwitchboardFixExeFlagsTask& ForceFocusTask);
	FRunningProcess* FindOrStartFlipModeMonitorForUUID(const FGuid& UUID);

	void CleanUpDisconnectedSockets();
	void DisconnectClient(const FIPv4Endpoint& InClientEndpoint);
	void HandleRunningProcesses(TArray<TSharedPtr<FRunningProcess, ESPMode::ThreadSafe>>& Processes, bool bNotifyThatProgramEnded);

	bool SendMessage(const FString& InMessage, const FIPv4Endpoint& InEndpoint);
	void SendMessageFutures();

	bool EquivalentTaskFutureExists(uint32 TaskEquivalenceHash) const;

private:
	TUniquePtr<FIPv4Endpoint> Endpoint;
	TUniquePtr<FTcpListener> SocketListener;
	TQueue<TPair<FIPv4Endpoint, TSharedPtr<FSocket>>, EQueueMode::Spsc> PendingConnections;
	TMap<FIPv4Endpoint, TSharedPtr<FSocket>> Connections;
	TMap<FIPv4Endpoint, double> LastActivityTime;
	TMap<FIPv4Endpoint, TArray<uint8>> ReceiveBuffer;

	TQueue<TUniquePtr<FSwitchboardTask>, EQueueMode::Spsc> ScheduledTasks;
	TQueue<TUniquePtr<FSwitchboardTask>, EQueueMode::Spsc> DisconnectTasks;
	TArray<TSharedPtr<FRunningProcess, ESPMode::ThreadSafe>> RunningProcesses;
	TArray<TSharedPtr<FRunningProcess, ESPMode::ThreadSafe>> FlipModeMonitors;
	TArray<FSwitchboardMessageFuture> MessagesFutures;
};
