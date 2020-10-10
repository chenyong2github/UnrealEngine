// Copyright Epic Games, Inc. All Rights Reserved.

#include "Network/Transport/DisplayClusterSocketOperations.h"

#include "Misc/ScopeLock.h"
#include "SocketSubsystem.h"

#include "Misc/DisplayClusterConstants.h"
#include "Misc/DisplayClusterLog.h"


FDisplayClusterSocketOperations::FDisplayClusterSocketOperations(FSocket* InSocket, int32 PersistentBufferSize, const FString& InConnectionName)
	: Socket(InSocket)
	, ConnectionName(InConnectionName)
{
	check(InSocket);
	DataBuffer.AddUninitialized(PersistentBufferSize);
}


FDisplayClusterSocketOperations::~FDisplayClusterSocketOperations()
{
	ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
}

bool FDisplayClusterSocketOperations::RecvChunk(TArray<uint8>& ChunkBuffer, const uint32 ChunkSize, const FString& ChunkName)
{
	FScopeLock Loc(&CritSecInternals);

	uint32 BytesRecvTotal = 0;
	int32  BytesRecvPass = 0;
	uint32 BytesRecvLeft = 0;

	// Make sure there is enough space for incoming data
	ChunkBuffer.Reset();
	ChunkBuffer.AddUninitialized(ChunkSize);

	// Read all requested bytes
	while (BytesRecvTotal < ChunkSize)
	{
		// Read data
		BytesRecvLeft = ChunkSize - BytesRecvTotal;
		if (!Socket->Recv(ChunkBuffer.GetData() + BytesRecvTotal, BytesRecvLeft, BytesRecvPass))
		{
			UE_LOG(LogDisplayClusterNetwork, Log, TEXT("%s - %s recv failed. It seems the client has disconnected."), *Socket->GetDescription(), *ChunkName);
			return false;
		}

		// Check amount of read data
		if (BytesRecvPass <= 0 || static_cast<uint32>(BytesRecvPass) > BytesRecvLeft)
		{
			UE_LOG(LogDisplayClusterNetwork, Error, TEXT("%s - %s recv failed - read wrong amount of bytes: %d"), *Socket->GetDescription(), *ChunkName, BytesRecvPass);
			return false;
		}

		BytesRecvTotal += BytesRecvPass;
		UE_LOG(LogDisplayClusterNetwork, VeryVerbose, TEXT("%s - %s received %d bytes, left %u bytes"), *Socket->GetDescription(), *ChunkName, BytesRecvPass, ChunkSize - BytesRecvTotal);
	}

	// Operation succeeded
	return true;
}

bool FDisplayClusterSocketOperations::SendChunk(const TArray<uint8>& ChunkBuffer, const uint32 ChunkSize, const FString& ChunkName)
{
	uint32 BytesSentTotal = 0;
	int32  BytesSentNow = 0;
	uint32 BytesSendLeft = 0;

	// Write all bytes
	while (BytesSentTotal < ChunkSize)
	{
		BytesSendLeft = ChunkSize - BytesSentTotal;

		// Send data
		if (!Socket->Send(ChunkBuffer.GetData() + BytesSentTotal, BytesSendLeft, BytesSentNow))
		{
			UE_LOG(LogDisplayClusterNetwork, Error, TEXT("%s - %s send failed (length=%d)"), *Socket->GetDescription(), *ChunkName, ChunkSize);
			return false;
		}

		// Check amount of bytes sent
		if (BytesSentNow <= 0 || static_cast<uint32>(BytesSentNow) > BytesSendLeft)
		{
			UE_LOG(LogDisplayClusterNetwork, Error, TEXT("%s - %s send failed: %d of %u left"), *Socket->GetDescription(), *ChunkName, BytesSentNow, BytesSendLeft);
			return false;
		}

		BytesSentTotal += BytesSentNow;
		UE_LOG(LogDisplayClusterNetwork, VeryVerbose, TEXT("%s - %s sent %d bytes, %u bytes left"), *Socket->GetDescription(), *ChunkName, BytesSentNow, ChunkSize - BytesSentTotal);
	}

	UE_LOG(LogDisplayClusterNetwork, Verbose, TEXT("%s - %s was sent"), *Socket->GetDescription(), *ChunkName);

	return true;
}
