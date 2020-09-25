// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Network/IDisplayClusterServer.h"
#include "Network/Session/IDisplayClusterSessionStatusListener.h"

#include "Containers/Queue.h"

struct FIPv4Endpoint;
class FSocket;
class IDisplayClusterSession;
class FDisplayClusterTcpListener;


/**
 * Base DisplayCluster TCP server
 */
class FDisplayClusterServer
	: public IDisplayClusterServer
	, public IDisplayClusterSessionStatusListener
{
public:
	// Minimal time (seconds) before cleaning resources of the 'pending kill' sessions
	static const double CleanSessionResourcesSafePeriod;

public:
	FDisplayClusterServer(const FString& Name);
	virtual ~FDisplayClusterServer();

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterServer
	//////////////////////////////////////////////////////////////////////////////////////////////
	
	// Start server
	virtual bool Start(const FString& Address, int32 Port) override;

	// Stop server
	virtual void Shutdown() override;

	// Returns current server state
	virtual bool IsRunning() const override;

	// Server name
	virtual FString GetName() const override
	{
		return Name;
	}

	// Server address
	virtual FString GetAddress() const override
	{
		return ServerAddress;
	}

	// Server port
	virtual int32 GetPort() const override
	{
		return ServerPort;
	}

protected:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IDisplayClusterSessionStatusListener
	//////////////////////////////////////////////////////////////////////////////////////////////

	// Callback on session opened
	virtual void NotifySessionOpen(uint64 SessionId) override;
	// Callback on session closed
	virtual void NotifySessionClose(uint64 SessionId) override;

protected:
	// Ask concrete server implementation if connection is allowed
	virtual bool IsConnectionAllowed(FSocket* Socket, const FIPv4Endpoint& Endpoint)
	{
		return true;
	}

	// Allow to specify custom session class
	virtual TUniquePtr<IDisplayClusterSession> CreateSession(FSocket* Socket, const FIPv4Endpoint& Endpoint, uint64 SessionId) = 0;

private:
	// Handles incoming connections
	bool ConnectionHandler(FSocket* Socket, const FIPv4Endpoint& Endpoint);
	// Free resources of the sessions that already finished their job
	void CleanPendingKillSessions();

private:
	// Server data
	const FString Name;
	
	// Socket data
	FString ServerAddress;
	int32   ServerPort = 0;

	// Server running state
	bool bIsRunning = false;

	// Socket listener
	TUniquePtr<FDisplayClusterTcpListener> Listener;

	// Session counter used for session ID generation
	uint64 IncrementalSessionId = 0;
	// Active sessions
	TMap<uint64, TUniquePtr<IDisplayClusterSession>> ActiveSessions;
	// Pending sessions
	TMap<uint64, TUniquePtr<IDisplayClusterSession>> PendingSessions;

	// Session end time
	struct FPendingKillSessionInfo
	{
		uint64 SessionId;
		double Time;
	};
	// Dead sessions info
	TQueue<FPendingKillSessionInfo, EQueueMode::Mpsc> PendingKillSessionsInfo;

	// Sync access
	mutable FCriticalSection ServerStateCritSec;
	mutable FCriticalSection SessionsCritSec;
};
