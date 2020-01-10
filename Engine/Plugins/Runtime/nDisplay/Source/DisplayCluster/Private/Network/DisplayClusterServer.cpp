// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/DisplayClusterServer.h"
#include "Misc/ScopeLock.h"

#include "DisplayClusterLog.h"


FDisplayClusterServer::FDisplayClusterServer(const FString& InName, const FString& InAddr, const int32 InPort) :
	Name(InName),
	Address(InAddr),
	Port(InPort),
	Listener(InName + FString("_listener"))
{
	check(InPort > 0 && InPort < 0xffff);
	
	// Bind connection handler method
	Listener.OnConnectionAccepted().BindRaw(this, &FDisplayClusterServer::ConnectionHandler);
}

FDisplayClusterServer::~FDisplayClusterServer()
{
	// Call from child .dtor
	Shutdown();
}

bool FDisplayClusterServer::Start()
{
	FScopeLock lock(&InternalsCritSec);

	if (bIsRunning == true)
	{
		return true;
	}

	if (!Listener.StartListening(Address, Port))
	{
		UE_LOG(LogDisplayClusterNetwork, Error, TEXT("%s couldn't start the listener [%s:%d]"), *Name, *Address, Port);
		return false;
	}

	// Update server state
	bIsRunning = true;

	return bIsRunning;
}

void FDisplayClusterServer::Shutdown()
{
	FScopeLock lock(&InternalsCritSec);

	if (!bIsRunning)
	{
		return;
	}

	UE_LOG(LogDisplayClusterNetwork, Log, TEXT("%s stopping the service..."), *Name);

	// Stop connections listening
	Listener.StopListening();
	// Destroy active sessions
	Sessions.Reset();
	// Update server state
	bIsRunning = false;
}

bool FDisplayClusterServer::IsRunning() const
{
	FScopeLock lock(&InternalsCritSec);
	return bIsRunning;
}

bool FDisplayClusterServer::ConnectionHandler(FSocket* InSock, const FIPv4Endpoint& InEP)
{
	FScopeLock lock(&InternalsCritSec);
	check(InSock);

	if (IsRunning() && IsConnectionAllowed(InSock, InEP))
	{
		InSock->SetLinger(false, 0);
		InSock->SetNonBlocking(false);

		const int32 RequestedBufferSize = static_cast<int32>(DisplayClusterConstants::net::SocketBufferSize);
		int32 FinalBufferSize;
		InSock->SetReceiveBufferSize(RequestedBufferSize, FinalBufferSize);
		InSock->SetSendBufferSize(RequestedBufferSize, FinalBufferSize);

		TSharedPtr<FDisplayClusterSessionBase> Session = CreateSession(InSock, InEP);
		Session->StartSession();
		Sessions.Add(Session);

		return true;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterSessionListener
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterServer::NotifySessionOpen(FDisplayClusterSessionBase* InSession)
{
}

void FDisplayClusterServer::NotifySessionClose(FDisplayClusterSessionBase* InSession)
{
	// We come here from a Session object so we can't delete it right now. The delete operation should
	// be performed later when the Session completely finished. This should be refactored in future.
	// For now we just hold all 'dead' session objects in the Sessions array and free memory when
	// this server is shutting down (look at the destructor).
}
