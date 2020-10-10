// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterTcpListener.h"

#include "HAL/RunnableThread.h"

#include "Common/TcpSocketBuilder.h"

#include "Misc/DisplayClusterHelpers.h"
#include "Misc/DisplayClusterLog.h"


FDisplayClusterTcpListener::FDisplayClusterTcpListener(const FString& InName) :
	Name(InName)
{
}


FDisplayClusterTcpListener::~FDisplayClusterTcpListener()
{
	// Just free resources by stopping the listening
	StopListening();
}


bool FDisplayClusterTcpListener::StartListening(const FString& InAddr, const int32 InPort)
{
	if (bIsListening == true)
	{
		return true;
	}

	FIPv4Endpoint EP;
	if (!GenIPv4Endpoint(InAddr, InPort, EP))
	{
		return false;
	}

	return StartListening(EP);
}

bool FDisplayClusterTcpListener::StartListening(const FIPv4Endpoint& InEndpoint)
{
	if (bIsListening == true)
	{
		return true;
	}

	// Save new endpoint
	Endpoint = InEndpoint;

	UE_LOG(LogDisplayClusterNetwork, Log, TEXT("TCP listener %s: started listening to %s:%d..."), *Name, *InEndpoint.Address.ToString(), Endpoint.Port);

	// Create listening thread
	ThreadObj.Reset(FRunnableThread::Create(this, *(Name + FString("_thread")), 128 * 1024, TPri_Normal));
	ensure(ThreadObj);

	// Update state
	bIsListening = ThreadObj.IsValid();
	
	return bIsListening;
}


void FDisplayClusterTcpListener::StopListening()
{
	if (bIsListening == false)
	{
		return;
	}

	UE_LOG(LogDisplayClusterNetwork, Log, TEXT("TCP listener %s: stopped listening to %s:%d..."), *Name, *Endpoint.Address.ToString(), Endpoint.Port);

	// Ask runnable to stop
	Stop();

	// Wait for thread finish and release it then
	if (ThreadObj)
	{
		ThreadObj->WaitForCompletion();
		ThreadObj.Reset();
	}
}

bool FDisplayClusterTcpListener::Init()
{
	// Create socket
	SocketObj = FTcpSocketBuilder(*Name).AsBlocking().BoundToEndpoint(Endpoint).Listening(128);

	if (SocketObj)
	{
		// Set TCP_NODELAY=1
		SocketObj->SetNoDelay(true);
	}

	return SocketObj != nullptr;
}

uint32 FDisplayClusterTcpListener::Run()
{
	TSharedRef<FInternetAddr> RemoteAddress = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();

	if (SocketObj)
	{
		while (FSocket* NewSock = SocketObj->Accept(*RemoteAddress, TEXT("DisplayCluster session")))
		{
			UE_LOG(LogDisplayClusterNetwork, Verbose, TEXT("TCP listener %s: New incoming connection: %s"), *Name, *RemoteAddress->ToString(true));

			if (NewSock)
			{
				// Ask a server implementation if it confirms this new incoming connection
				if (OnConnectionAcceptedDelegate.IsBound())
				{
					// If no, close the socket
					if (OnConnectionAcceptedDelegate.Execute(NewSock, FIPv4Endpoint(RemoteAddress)))
					{
						UE_LOG(LogDisplayClusterNetwork, Verbose, TEXT("TCP listener %s: New incoming connection accepted: %s"), *Name, *RemoteAddress->ToString(true));
					}
					else
					{
						UE_LOG(LogDisplayClusterNetwork, Verbose, TEXT("TCP listener %s: New incoming connection declined: %s"), *Name, *RemoteAddress->ToString(true));
						NewSock->Close();
						ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(NewSock);
					}
				}
			}
		}
	}
	else
	{
		UE_LOG(LogDisplayClusterNetwork, Error, TEXT("Socket %s is not initialized"), *Name);
		return 0;
	}

	return 0;
}

void FDisplayClusterTcpListener::Stop()
{
	// Close the socket to unblock thread
	if (SocketObj)
	{
		SocketObj->Close();
	}
}

void FDisplayClusterTcpListener::Exit()
{
	// Release the socket
	if (SocketObj)
	{
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(SocketObj);
		SocketObj = nullptr;
	}
}

bool FDisplayClusterTcpListener::GenIPv4Endpoint(const FString& Addr, const int32 Port, FIPv4Endpoint& EP) const
{
	FIPv4Address ipAddr;
	if (!FIPv4Address::Parse(Addr, ipAddr))
	{
		return false;
	}

	EP = FIPv4Endpoint(ipAddr, Port);
	return true;
}
