// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CompactBinaryTCP.h"
#include "CookTypes.h"
#include "IPAddress.h"

namespace UE::Cook { struct FDirectorConnectionInfo; }

namespace UE::Cook
{

/** Class in a CookWorker process that communicates over a Socket with FCookWorkerServer in a Director process. */
class FCookWorkerClient
{
public:
	~FCookWorkerClient();
	/** Blocking operation: open the socket to the Director, send the Connected message, receive the setup message. */
	bool TryConnect(FDirectorConnectionInfo&& ConnectInfo);

private:
	enum class EConnectStatus
	{
		Uninitialized,
		PollWriteConnectMessage,
		PollReceiveConfigMessage,
		Failed,
		Connected,
	};

private:
	/** Reentrant helper for TryConnect which early exits if currently blocked. */
	EPollStatus PollTryConnect(const FDirectorConnectionInfo& ConnectInfo);
	/** Helper for PollTryConnect: create the ServerSocket */
	void CreateServerSocket(const FDirectorConnectionInfo& ConnectInfo);
	/** Try to send the Connect Message, switch state when it succeeds or fails. */
	void PollWriteConnectMessage();
	/** Wait for the Config Message, switch state when it succeeds or fails. */
	void PollReceiveConfigMessage();
	void LogConnected();

private:
	TSharedPtr<FInternetAddr> DirectorAddr;
	UE::CompactBinaryTCP::FSendBuffer SendBuffer;
	FString DirectorURI;
	FSocket* ServerSocket = nullptr;
	double ConnectStartTimeSeconds = 0.;
	EConnectStatus ConnectStatus = EConnectStatus::Uninitialized;
};

}