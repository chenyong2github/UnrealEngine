// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterTcpListener.h"

#include "HAL/RunnableThread.h"

#include "Common/TcpSocketBuilder.h"

#include "Misc/DisplayClusterAppExit.h"
#include "Misc/DisplayClusterHelpers.h"

#include "DisplayClusterLog.h"


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
	FScopeLock lock(&InternalsCritSec);

	if (bIsListening == true)
	{
		return true;
	}

	FIPv4Endpoint EP;
	if (!DisplayClusterHelpers::net::GenIPv4Endpoint(InAddr, InPort, EP))
	{
		return false;
	}

	return StartListening(EP);
}

bool FDisplayClusterTcpListener::StartListening(const FIPv4Endpoint& InEP)
{
	FScopeLock lock(&InternalsCritSec);

	if (bIsListening == true)
	{
		return true;
	}

	// Save new endpoint
	Endpoint = InEP;

	// Create listening thread
	ThreadObj.Reset(FRunnableThread::Create(this, *(Name + FString("_thread")), 128 * 1024, TPri_Normal));
	ensure(ThreadObj);

	// Update state
	bIsListening = ThreadObj.IsValid();
	
	return bIsListening;
}


void FDisplayClusterTcpListener::StopListening()
{
	FScopeLock lock(&InternalsCritSec);

	if (bIsListening == false)
	{
		return;
	}

	// Ask runnable to stop
	Stop();

	// Wait for thread finish and release it then
	if (ThreadObj)
	{
		ThreadObj->WaitForCompletion();
		ThreadObj.Reset();
	}
}

bool FDisplayClusterTcpListener::IsActive() const
{
	return bIsListening;
}

bool FDisplayClusterTcpListener::Init()
{
	// Create socket
	SocketObj = FTcpSocketBuilder(*Name).AsBlocking().BoundToEndpoint(Endpoint).Listening(128);

	return SocketObj != nullptr;
}

uint32 FDisplayClusterTcpListener::Run()
{
	TSharedRef<FInternetAddr> RemoteAddress = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();

	if (SocketObj)
	{
		while (FSocket* NewSock = SocketObj->Accept(*RemoteAddress, TEXT("FDisplayClusterTcpListener client")))
		{
			UE_LOG(LogDisplayClusterNetwork, Log, TEXT("New incoming connection: %s"), *RemoteAddress->ToString(true));

			if (NewSock)
			{
				// Ask a server implementation if it confirms this new incoming connection
				if (OnConnectionAcceptedDelegate.IsBound())
				{
					// If no, close the socket
					if (!OnConnectionAcceptedDelegate.Execute(NewSock, FIPv4Endpoint(RemoteAddress)))
					{
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
