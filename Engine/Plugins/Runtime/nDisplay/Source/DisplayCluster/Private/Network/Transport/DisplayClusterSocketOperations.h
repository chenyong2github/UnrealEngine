// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Sockets.h"


/**
 * Low-level socket operations
 */
class FDisplayClusterSocketOperations
{
public:
	FDisplayClusterSocketOperations(FSocket* Socket, int32 PersistentBufferSize, const FString& ConnectionName);
	virtual ~FDisplayClusterSocketOperations();

public:
	// Returns true if the socket is valid and connected
	inline bool IsOpen() const
	{
		return (Socket && (Socket->GetConnectionState() == ESocketConnectionState::SCS_Connected));
	}

	// Access to the socket synchronization object
	inline FCriticalSection& GetSyncObj()
	{
		return CritSecInternals;
	}

	// Access to the socket
	inline FSocket* GetSocket()
	{
		return Socket;
	}

	// Access to the connection name
	inline const FString& GetConnectionName() const
	{
		return ConnectionName;
	}

	// Access to the internal read/write buffer
	inline TArray<uint8>& GetPersistentBuffer()
	{
		return DataBuffer;
	}

public:
	// Receive specified amount of bytes to custom buffer
	bool RecvChunk(TArray<uint8>& ChunkBuffer, const uint32 ChunkSize, const FString& ChunkName = FString("ReadDataChunk"));
	// Send specified amount of bytes from specified buffer
	bool SendChunk(const TArray<uint8>& ChankBuffer, const uint32 ChunkSize, const FString& ChunkName = FString("WriteDataChunk"));

private:
	// Socket
	FSocket* const Socket = nullptr;
	// Data buffer
	TArray<uint8> DataBuffer;
	// Connection name (basically for nice logging)
	FString ConnectionName;
	// Sync access to internals
	mutable FCriticalSection CritSecInternals;
};
