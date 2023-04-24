// Copyright Epic Games, Inc. All Rights Reserved.

#include <windows.h>
#include <iostream>
#include <assert.h>
#include "ComputeChannel.h"

FComputeChannel::FComputeChannel()
{
}

FComputeChannel::~FComputeChannel()
{
	Close();
}

bool FComputeChannel::Open(FComputeSocket& Socket, int ChannelId)
{
	return Open(Socket, ChannelId, ChannelId, 1024 * 1024);
}

bool FComputeChannel::Open(FComputeSocket& Socket, int ChannelId, int NumChunks, int ChunkSize)
{
	return Open(Socket, ChannelId, NumChunks, ChunkSize, NumChunks, ChunkSize);
}

bool FComputeChannel::Open(FComputeSocket& Socket, int ChannelId, int NumSendChunks, int SendChunkSize, int NumRecvChunks, int RecvChunkSize)
{
	if (!SendBuffer.CreateNew(nullptr, NumSendChunks, SendChunkSize))
	{
		Close();
		return false;
	}
	if (!RecvBuffer.CreateNew(nullptr, NumRecvChunks, RecvChunkSize))
	{
		Close();
		return false;
	}

	Socket.AttachSendBuffer(ChannelId, SendBuffer);
	Socket.AttachRecvBuffer(ChannelId, RecvBuffer);

	return true;
}

void FComputeChannel::Close()
{
	SendBuffer.MarkComplete();
	SendBuffer.Close();
	RecvBuffer.Close();
}

void FComputeChannel::Send(const void* Data, size_t Length)
{
	while (Length > 0)
	{
		size_t Size;
		unsigned char* WriteMemory = SendBuffer.GetWriteMemory(Size);

		if (Size == 0)
		{
			SendBuffer.WaitToWrite(0);
			continue;
		}

		size_t CopyLength = (Size < Length) ? Size : Length;
		memcpy(WriteMemory, Data, CopyLength);
		SendBuffer.AdvanceWritePosition(CopyLength);

		Data = (unsigned char*)Data + CopyLength;
		Length -= CopyLength;
	}
}

size_t FComputeChannel::Receive(void* Data, size_t Length)
{
	size_t Size;
	const unsigned char* ReadMemory = RecvBuffer.GetReadMemory(Size);

	while (Size == 0 && !RecvBuffer.IsComplete())
	{
		RecvBuffer.WaitToRead(0);
		ReadMemory = RecvBuffer.GetReadMemory(Size);
	}

	size_t CopyLength = (Size < Length) ? Size : Length;
	memcpy(Data, ReadMemory, CopyLength);
	RecvBuffer.AdvanceReadPosition(CopyLength);

	return CopyLength;
}

