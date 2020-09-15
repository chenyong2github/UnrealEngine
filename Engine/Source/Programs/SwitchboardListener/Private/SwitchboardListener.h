// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"

struct FRunningProcess;
struct FSwitchboardTask;
struct FSwitchboardDisconnectTask;
struct FSwitchboardSendFileToClientTask;
struct FSwitchboardStartTask;
struct FSwitchboardReceiveFileFromClientTask;
struct FSwitchboardVcsInitTask;
struct FSwitchboardVcsReportRevisionTask;
struct FSwitchboardVcsSyncTask;
class FInternetAddr;
class FSocket;
class FSwitchboardSourceControl;
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
	bool KillProcess(FRunningProcess* InProcess);
	bool KillAllProcesses();
	bool ReceiveFileFromClient(const FSwitchboardReceiveFileFromClientTask& InReceiveFileFromClientTask);
	bool SendFileToClient(const FSwitchboardSendFileToClientTask& InSendFileToClientTask);
	bool InitVersionControlSystem(const FSwitchboardVcsInitTask& InVcsInitTask);
	bool ReportVersionControlRevision(const FSwitchboardVcsReportRevisionTask& InVcsRevisionTask);
	bool SyncVersionControl(const FSwitchboardVcsSyncTask& InSyncTask);

	void CleanUpDisconnectedSockets();
	void DisconnectClient(const FIPv4Endpoint& InClientEndpoint);
	bool HandleRunningProcesses();

	bool SendMessage(const FString& InMessage, const FIPv4Endpoint& InEndpoint);

	void OnSourceControlConnectFinished(bool bInSuccess, FString InErrorMessage, const FIPv4Endpoint& InEndpoint);
	void OnSourceControlReportRevisionFinished(bool bInSuccess, FString InRevision, FString InErrorMessage, const FIPv4Endpoint& InEndpoint);
	void OnSourceControlSyncFinished(bool bInSuccess, FString InRevision, FString InErrorMessage, const FIPv4Endpoint& InEndpoint);

private:
	TUniquePtr<FIPv4Endpoint> Endpoint;
	TUniquePtr<FTcpListener> SocketListener;
	TQueue<TPair<FIPv4Endpoint, TSharedPtr<FSocket>>, EQueueMode::Spsc> PendingConnections;
	TMap<FIPv4Endpoint, TSharedPtr<FSocket>> Connections;
	TMap<FIPv4Endpoint, double> LastActivityTime;
	TMap<FIPv4Endpoint, TArray<uint8>> ReceiveBuffer;

	TQueue<TUniquePtr<FSwitchboardTask>, EQueueMode::Spsc> ScheduledTasks;
	TQueue<TUniquePtr<FSwitchboardTask>, EQueueMode::Spsc> DisconnectTasks;
	TArray<FRunningProcess> RunningProcesses;

	TUniquePtr<FSwitchboardSourceControl> SourceControl;
};
