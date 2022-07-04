// Copyright Epic Games, Inc. All Rights Reserved.

#include "CookWorkerClient.h"

#include "CompactBinaryTCP.h"
#include "CookDirector.h"
#include "CookTypes.h"
#include "CoreGlobals.h"
#include "HAL/PlatformTime.h"
#include "Serialization/CompactBinaryWriter.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "WorkerRequestsRemote.h"

namespace UE::Cook
{

FCookWorkerClient::~FCookWorkerClient()
{
	Sockets::CloseSocket(ServerSocket);
}

bool FCookWorkerClient::TryConnect(FDirectorConnectionInfo&& ConnectInfo)
{
	EPollStatus Status;
	for (;;)
	{
		Status = PollTryConnect(ConnectInfo);
		if (Status != EPollStatus::Incomplete)
		{
			break;
		}
		constexpr float SleepTime = 0.01f; // 10 ms
		FPlatformProcess::Sleep(SleepTime);
	}
	return Status == EPollStatus::Success;
}

EPollStatus FCookWorkerClient::PollTryConnect(const FDirectorConnectionInfo& ConnectInfo)
{
	for (;;)
	{
		switch (ConnectStatus)
		{
		case EConnectStatus::Connected:
			return EPollStatus::Success;
		case EConnectStatus::Uninitialized:
			CreateServerSocket(ConnectInfo);
			break;
		case EConnectStatus::PollWriteConnectMessage:
			PollWriteConnectMessage();
			if (ConnectStatus == EConnectStatus::PollWriteConnectMessage)
			{
				return EPollStatus::Incomplete;
			}
			break;
		case EConnectStatus::PollReceiveConfigMessage:
			PollReceiveConfigMessage();
			if (ConnectStatus == EConnectStatus::PollReceiveConfigMessage)
			{
				return EPollStatus::Incomplete;
			}
			break;
		case EConnectStatus::Failed:
			return EPollStatus::Error;
		default:
			checkNoEntry();
			return EPollStatus::Error;
		}
	}
}

void FCookWorkerClient::CreateServerSocket(const FDirectorConnectionInfo& ConnectInfo)
{
	using namespace CompactBinaryTCP;

	ConnectStartTimeSeconds = FPlatformTime::Seconds();
	DirectorURI = ConnectInfo.HostURI;

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get();
	if (!SocketSubsystem)
	{
		UE_LOG(LogCook, Error, TEXT("CookWorker initialization failure: platform does not support network sockets, cannot connect to CookDirector."));
		ConnectStatus = EConnectStatus::Failed;
		return;
	}

	DirectorAddr = Sockets::GetAddressFromStringWithPort(DirectorURI);
	if (!DirectorAddr)
	{
		UE_LOG(LogCook, Error, TEXT("CookWorker initialization failure: could not convert -CookDirectorHost=%s into an address, cannot connect to CookDirector."),
			*DirectorURI);
		ConnectStatus = EConnectStatus::Failed;
		return;
	}

	UE_LOG(LogCook, Display, TEXT("Connecting to CookDirector at %s..."), *DirectorURI);

	ServerSocket = Sockets::ConnectToHost(*DirectorAddr, TEXT("FCookWorkerClient-WorkerConnect"));
	if (!ServerSocket)
	{
		UE_LOG(LogCook, Error, TEXT("CookWorker initialization failure: Could not connect to CookDirector."));
		ConnectStatus = EConnectStatus::Failed;
		return;
	}

	constexpr float WaitForConnectTimeout = 60.f * 10;
	float ConditionalTimeoutSeconds = IsCookIgnoreTimeouts() ? MAX_flt : WaitForConnectTimeout;
	bool bServerSocketReady = ServerSocket->Wait(ESocketWaitConditions::WaitForWrite, FTimespan::FromSeconds(ConditionalTimeoutSeconds));
	if (!bServerSocketReady)
	{
		UE_LOG(LogCook, Error, TEXT("CookWorker initialization failure: Timed out after %.0f seconds trying to connect to CookDirector."),
			ConditionalTimeoutSeconds);
		ConnectStatus = EConnectStatus::Failed;
		return;
	}

	FWorkerConnectMessage ConnectMessage;
	ConnectMessage.RemoteIndex = ConnectInfo.RemoteIndex;
	EConnectionStatus Status = TryWritePacket(ServerSocket, SendBuffer, ConnectMessage);
	if (Status == EConnectionStatus::Incomplete)
	{
		ConnectStatus = EConnectStatus::PollWriteConnectMessage;
		return;
	}
	else if (Status != EConnectionStatus::Okay)
	{
		UE_LOG(LogCook, Error, TEXT("CookWorker initialization failure: could not send ConnectMessage."));
		ConnectStatus = EConnectStatus::Failed;
		return;
	}
	LogConnected();

	ConnectStatus = EConnectStatus::PollReceiveConfigMessage;
}

void FCookWorkerClient::PollWriteConnectMessage()
{
	using namespace CompactBinaryTCP;
	constexpr float WaitForConnectTimeout = 60.f;

	EConnectionStatus Status = TryFlushBuffer(ServerSocket, SendBuffer);
	if (Status == EConnectionStatus::Incomplete)
	{
		if (FPlatformTime::Seconds() - ConnectStartTimeSeconds > WaitForConnectTimeout && !IsCookIgnoreTimeouts())
		{
			UE_LOG(LogCook, Error, TEXT("CookWorker initialization failure: timed out waiting for %fs to send ConnectMessage."), WaitForConnectTimeout);
			ConnectStatus = EConnectStatus::Failed;
		}
		return;
	}
	else if (Status != EConnectionStatus::Okay)
	{
		UE_LOG(LogCook, Error, TEXT("CookWorker initialization failure: could not send ConnectMessage."));
		ConnectStatus = EConnectStatus::Failed;
		return;
	}
	LogConnected();
	ConnectStatus = EConnectStatus::PollReceiveConfigMessage;
}

void FCookWorkerClient::PollReceiveConfigMessage()
{
	UE_LOG(LogCook, Error, TEXT("CookWorker initialization failure: reading Settings information is not yet implemented."));
	ConnectStatus = EConnectStatus::Failed;
}

void FCookWorkerClient::LogConnected()
{
	UE_LOG(LogCook, Display, TEXT("Connection to CookDirector successful."));
}

}