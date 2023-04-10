// Copyright Epic Games, Inc. All Rights Reserved.

#include <windows.h>
#include <iostream>
#include <assert.h>
#include "ComputeChannel.h"

const char* const FComputeChannel::EnvVarName = "UE_HORDE_COMPUTE_IPC";

FComputeChannel::FComputeChannel()
{
}

FComputeChannel::~FComputeChannel()
{
	Close();
}

bool FComputeChannel::Open()
{
	Close();

	char EnvVar[MAX_PATH];
	int Length = GetEnvironmentVariableA(EnvVarName, EnvVar, sizeof(EnvVar) / sizeof(EnvVar[0]));

	if (Length <= 0 || Length >= sizeof(EnvVar))
	{
		Close();
		return false;
	}

	char Buffer[MAX_PATH];

	sprintf_s(Buffer, "%s_SEND", EnvVar);
	if (!SendBuffer.OpenExisting(Buffer))
	{
		Close();
		return false;
	}

	sprintf_s(Buffer, "%s_RECV", EnvVar);
	if (!RecvBuffer.OpenExisting(Buffer))
	{
		Close();
		return false;
	}

	return true;
}

void FComputeChannel::Close()
{
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

