// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/DisplayClusterServer.h"
#include "Network/Session/IDisplayClusterSession.h"
#include "Network/DisplayClusterTcpListener.h"

#include "Engine/GameEngine.h"
#include "TimerManager.h"
#include "Misc/DateTime.h"

#include "Misc/ScopeLock.h"
#include "Misc/DisplayClusterConstants.h"
#include "Misc/DisplayClusterLog.h"


// Give 10 seconds minimum for every session that finished its job to finalize
// the working thread and other internals before freeing the session object
const double FDisplayClusterServer::CleanSessionResourcesSafePeriod = 10.f;


FDisplayClusterServer::FDisplayClusterServer(const FString& InName)
	: Name(InName)
	, Listener(new FDisplayClusterTcpListener(InName + FString("_listener")))
{
	// Bind connection handler method
	Listener->OnConnectionAccepted().BindRaw(this, &FDisplayClusterServer::ConnectionHandler);
}

FDisplayClusterServer::~FDisplayClusterServer()
{
	// Call from child .dtor
	Shutdown();
}


bool FDisplayClusterServer::Start(const FString& Address, int32 Port)
{
	check(Port > 0 && Port < 0xffff);
	check(Listener);

	FScopeLock Lock(&ServerStateCritSec);

	// Nothing to do if alrady running
	if (bIsRunning == true)
	{
		return true;
	}

	// Start listening for incoming connections
	if (!Listener->StartListening(Address, Port))
	{
		UE_LOG(LogDisplayClusterNetwork, Error, TEXT("%s couldn't start the listener [%s:%d]"), *Name, *Address, Port);
		return false;
	}

	// Update server state
	bIsRunning = true;

	// Update socket data
	ServerAddress = Address;
	ServerPort = Port;

	return bIsRunning;
}

void FDisplayClusterServer::Shutdown()
{
	FScopeLock Lock(&ServerStateCritSec);

	// Nothing to do if not running
	if (!bIsRunning)
	{
		return;
	}

	UE_LOG(LogDisplayClusterNetwork, Log, TEXT("%s stopping the service..."), *Name);

	// Stop connections listening
	Listener->StopListening();
	// Destroy sessions
	ActiveSessions.Reset();
	PendingSessions.Reset();
	PendingKillSessionsInfo.Empty();
	// Update server state
	bIsRunning = false;
	// Update socket data
	ServerAddress = FString();
	ServerPort = 0;
}

bool FDisplayClusterServer::IsRunning() const
{
	FScopeLock Lock(&ServerStateCritSec);
	return bIsRunning;
}

bool FDisplayClusterServer::ConnectionHandler(FSocket* Socket, const FIPv4Endpoint& Endpoint)
{
	check(Socket);

	FScopeLock Lock(&SessionsCritSec);

	if (IsRunning() && IsConnectionAllowed(Socket, Endpoint))
	{
		Socket->SetLinger(false, 0);
		Socket->SetNonBlocking(false);
		Socket->SetNoDelay(true);

		// Create new session for this incoming connection
		TUniquePtr<IDisplayClusterSession> Session = CreateSession(Socket, Endpoint, IncrementalSessionId);
		check(Session.IsValid());
		if (Session && Session->StartSession()) 
		{
			// Store session object
			PendingSessions.Emplace(IncrementalSessionId, MoveTemp(Session));
			// Increment session ID counter
			++IncrementalSessionId;
		}

		return true;
	}

	return false;
}


void FDisplayClusterServer::CleanPendingKillSessions()
{
	FScopeLock Lock(&SessionsCritSec);

	if (!PendingKillSessionsInfo.IsEmpty())
	{
		const FPendingKillSessionInfo* Info = PendingKillSessionsInfo.Peek();
		const double CurTime = FPlatformTime::Seconds();

		// Free only those sessions that have cleaning safe period expired
		while (Info && (CurTime - Info->Time > FDisplayClusterServer::CleanSessionResourcesSafePeriod))
		{
			UE_LOG(LogDisplayClusterNetwork, Verbose, TEXT("%s - cleaning session resources id=%llu..."), *Name, Info->SessionId);
			ActiveSessions.Remove(Info->SessionId);
			PendingKillSessionsInfo.Pop();
			Info = PendingKillSessionsInfo.Peek();
		}
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterSessionListener
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterServer::NotifySessionOpen(uint64 SessionId)
{
	FScopeLock Lock(&SessionsCritSec);

	// Change session status from 'pending' to 'active'
	TUniquePtr<IDisplayClusterSession> Session;
	if (PendingSessions.RemoveAndCopyValue(SessionId, Session))
	{
		ActiveSessions.Emplace(SessionId, MoveTemp(Session));
	}
}

void FDisplayClusterServer::NotifySessionClose(uint64 SessionId)
{
	FScopeLock Lock(&SessionsCritSec);

	// Clean the 'pending kill' session that have been queued previously
	CleanPendingKillSessions();

	// We come here from a Session object so we can't delete it right now. The delete operation should
	// be performed later when the session is completely finished and its working thread is closed.
	// Here we store ID of the session and its death time to clean the resources safely later.
	FPendingKillSessionInfo PendingKillSession;
	PendingKillSession.SessionId = SessionId;
	PendingKillSession.Time = FPlatformTime::Seconds();
	PendingKillSessionsInfo.Enqueue(PendingKillSession);
}
