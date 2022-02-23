// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkFileServerPlatformProtocol.h"
#include "HAL/RunnableThread.h"
#include "Misc/OutputDeviceRedirector.h"
#include "NetworkMessage.h"
#include "NetworkFileSystemLog.h"
#include "NetworkFileServerConnection.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetDevice.h"
#include "Interfaces/ITargetDeviceSocket.h"
#include "GenericPlatform/GenericPlatformHostCommunication.h"



namespace
{
	class FSimpleAbstractSocket_PlatformProtocol : public FSimpleAbstractSocket
	{
	public:

		FSimpleAbstractSocket_PlatformProtocol(ITargetDeviceSocketPtr InSocket)
			: Socket(InSocket)
		{
			check(Socket != nullptr);
		}

		virtual bool Receive(uint8* Results, int32 Size) const override
		{
			return Socket->Receive(Results, Size);
		}

		virtual bool Send(const uint8* Buffer, int32 Size) const override
		{
			return Socket->Send(Buffer, Size);
		}

		virtual uint32 GetMagic() const override
		{
			return 0x9E2B83C7;
		}

	private:

		ITargetDeviceSocketPtr Socket;
	};
}


class FNetworkFileServerPlatformProtocol::FConnectionThreaded
	: public FNetworkFileServerClientConnection
	, protected FRunnable 
{
public:

	FConnectionThreaded(ITargetDevicePtr InDevice, ITargetDeviceSocketPtr InSocket, const FNetworkFileServerOptions& InOptions)
		: FNetworkFileServerClientConnection(InOptions)
		, Device(InDevice)
		, Socket(InSocket)
	{
		Running       = true;
		StopRequested = false;
	
#if UE_BUILD_DEBUG
		// This thread needs more space in debug builds as it tries to log messages and such.
		const static uint32 NetworkFileServerThreadSize = 2 * 1024 * 1024; 
#else
		const static uint32 NetworkFileServerThreadSize = 1 * 1024 * 1024; 
#endif
		WorkerThread = FRunnableThread::Create(this, TEXT("FNetworkFileServerCustomClientConnection"), NetworkFileServerThreadSize, TPri_AboveNormal);
	}


	virtual bool Init() override
	{
#if PLATFORM_WINDOWS
		FWindowsPlatformMisc::CoInitialize(ECOMModel::Multithreaded);
#endif

		return true; 
	}

	virtual uint32 Run() override
	{
		while (!StopRequested)
		{
			// Read a header and payload pair.
			FArrayReader Payload; 
			if (!FNFSMessageHeader::ReceivePayload(Payload, FSimpleAbstractSocket_PlatformProtocol(Socket)))
				break; 

			// Now process the contents of the payload.
			if ( !FNetworkFileServerClientConnection::ProcessPayload(Payload) )
			{
				// Give the processing of the payload a chance to terminate the connection
				// failed to process message.
				UE_LOG(LogFileServer, Warning, TEXT("Unable to process payload terminating connection"));
				break;
			}
		}

		return true;
	}

	virtual void Stop() override
	{
		StopRequested = true;
	}

	virtual void Exit() override
	{
		Device->CloseConnection(Socket);
		Socket = nullptr;

#if PLATFORM_WINDOWS
		FWindowsPlatformMisc::CoUninitialize();
#endif

		Running = false; 
	}

	virtual bool SendPayload( TArray<uint8> &Out ) override
	{
		return FNFSMessageHeader::WrapAndSendPayload(Out, FSimpleAbstractSocket_PlatformProtocol(Socket));
	}

	bool IsRunning()
	{
		return Running; 
	}

	FString GetName() const
	{
		return FString::Printf(TEXT("%s (%s)"), *Device->GetName(), *Device->GetTargetPlatform().PlatformName());
	}

	ITargetDevicePtr GetDevice() const
	{
		return Device;
	}

	~FConnectionThreaded()
	{
		WorkerThread->Kill(true);
	}

private:

	ITargetDevicePtr	   Device;
	ITargetDeviceSocketPtr Socket;

	std::atomic<bool>      StopRequested;
	std::atomic<bool>      Running;
	FRunnableThread*       WorkerThread; 
};


FNetworkFileServerPlatformProtocol::FNetworkFileServerPlatformProtocol(FNetworkFileServerOptions InFileServerOptions)
	: FileServerOptions(MoveTemp(InFileServerOptions))
{
	Running       = false;
	StopRequested = false;

	UE_LOG(LogFileServer, Display , TEXT("Unreal Network File Server (custom protocol) starting up..."));

	// Check the list of platforms once on start (any missing platforms will be ignored later on to avoid spamming the log).
	for (ITargetPlatform* TargetPlatform : FileServerOptions.TargetPlatforms)
	{
		if (!TargetPlatform->SupportsFeature(ETargetPlatformFeatures::DirectDataExchange))
		{
			UE_LOG(LogFileServer, Error, TEXT("Platform '%s' does not support direct communication with targets (it will be ignored)."), *TargetPlatform->PlatformName());
		}
	}

	// Create a thread that will be updating the list of connected target devices.
	Thread = FRunnableThread::Create(this, TEXT("FNetworkFileServerCustomProtocol"), 8 * 1024, TPri_AboveNormal);

	UE_LOG(LogFileServer, Display, TEXT("Unreal Network File Server is ready for client connections!"));
}


FNetworkFileServerPlatformProtocol::~FNetworkFileServerPlatformProtocol()
{
	// Kill the running thread.
	if (Thread != nullptr)
	{
		Thread->Kill(true);

		delete Thread;
		Thread = nullptr;
	}
}


uint32 FNetworkFileServerPlatformProtocol::Run()
{
#if PLATFORM_WINDOWS
	FWindowsPlatformMisc::CoInitialize(ECOMModel::Multithreaded);
#endif

	Running = true; 

	// Go until requested to be done.
	while (!StopRequested)
	{
		UpdateConnections();

		FPlatformProcess::Sleep(1.0f);
	}

#if PLATFORM_WINDOWS
	FWindowsPlatformMisc::CoUninitialize();
#endif

	return 0;
}


void FNetworkFileServerPlatformProtocol::Stop()
{
	StopRequested = true;
}


void FNetworkFileServerPlatformProtocol::Exit()
{
	// Close all connections.
	for (auto* Connection : Connections)
	{
		delete Connection;
	}

	Connections.Empty();
}


FString FNetworkFileServerPlatformProtocol::GetSupportedProtocol() const
{
	return FString("custom");
}


bool FNetworkFileServerPlatformProtocol::GetAddressList( TArray<TSharedPtr<FInternetAddr> >& OutAddresses ) const
{
	return 0;
}


bool FNetworkFileServerPlatformProtocol::IsItReadyToAcceptConnections() const
{
	return Running; 
}


int32 FNetworkFileServerPlatformProtocol::NumConnections() const
{
	return Connections.Num(); 
}


void FNetworkFileServerPlatformProtocol::Shutdown()
{
	Stop();
}


void FNetworkFileServerPlatformProtocol::UpdateConnections()
{
	RemoveClosedConnections();

	AddConnectionsForNewDevices();
}


void FNetworkFileServerPlatformProtocol::RemoveClosedConnections()
{
	for (int32 ConnectionIndex = 0; ConnectionIndex < Connections.Num(); ++ConnectionIndex)
	{
		FConnectionThreaded* Connection = Connections[ConnectionIndex];

		if (!Connection->IsRunning())
		{
			UE_LOG(LogFileServer, Display, TEXT("Client %s disconnected."), *Connection->GetName());
			Connections.RemoveAtSwap(ConnectionIndex);
			delete Connection;
		}
	}
}


void FNetworkFileServerPlatformProtocol::AddConnectionsForNewDevices()
{
	for (ITargetPlatform* TargetPlatform : FileServerOptions.TargetPlatforms)
	{
		if (TargetPlatform->SupportsFeature(ETargetPlatformFeatures::DirectDataExchange))
		{
			AddConnectionsForNewDevices(TargetPlatform);
		}
	}
}


void FNetworkFileServerPlatformProtocol::AddConnectionsForNewDevices(ITargetPlatform* TargetPlatform)
{
	TArray<ITargetDevicePtr> TargetDevices;

	TargetPlatform->GetAllDevices(TargetDevices);

	for (ITargetDevicePtr Device : TargetDevices)
	{
		if (Device->IsConnected())
		{
			FConnectionThreaded** ExistingConnection =
				Connections.FindByPredicate(
					[&Device](const FConnectionThreaded* Connection)
					{
						return Connection->GetDevice() == Device;
					}
			);

			// Checking IsProtocolAvailable first would make more sense, but internally it queries
			// COM interfaces, which throws exceptions if the protocol is already in use. While we catch
			// and process these exceptions, Visual Studio intercepts then as well and outputs messages
			// spamming the log, which hinders the debugging experience.
			if (!ExistingConnection)
			{
				if (Device->IsProtocolAvailable(EHostProtocol::CookOnTheFly))
				{
					ITargetDeviceSocketPtr Socket = Device->OpenConnection(EHostProtocol::CookOnTheFly);

					if (Socket)
					{
						FConnectionThreaded* Connection =
							new FConnectionThreaded(Device, Socket, FileServerOptions);

						Connections.Add(Connection);
						UE_LOG(LogFileServer, Display, TEXT("Client %s connected."), *Connection->GetName());
					}
				}
			}
		}
	}
}
