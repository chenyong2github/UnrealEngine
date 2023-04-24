// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SharedMemoryBuffer.h"
#include "ComputeSocket.h"

class FComputeChannel
{
public:
	static const int DefaultWorkerChannelId = 100;

	FComputeChannel();
	~FComputeChannel();

	bool Open(FComputeSocket& Socket, int ChannelId);
	bool Open(FComputeSocket& Socket, int ChannelId, int NumChunks, int ChunkSize);
	bool Open(FComputeSocket& Socket, int ChannelId, int NumSendChunks, int SendChunkSize, int NumRecvChunks, int RecvChunkSize);
	void Close();

	void Send(const void* Data, size_t Length);
	size_t Receive(void* Data, size_t Length);

private:
	FSharedMemoryBuffer RecvBuffer;
	FSharedMemoryBuffer SendBuffer;
};
