// Copyright Epic Games, Inc. All Rights Reserved.

#include <windows.h>
#include <iostream>
#include <assert.h>
#include "WorkerSocket.h"

const char* const FWorkerSocket::EnvVarName = "UE_HORDE_COMPUTE_IPC";

enum class FWorkerSocket::EIpcMessageType : unsigned char
{
	Finish,
	AttachSendBuffer,
	AttachRecvBuffer,
};

struct FWorkerSocket::FAttachMessage
{
	EIpcMessageType Type;
	unsigned char Padding[3];
	int ChannelId;
	void* MemoryMappedFile;
	void* ReaderEvent;
	void* WriterEvent;
};

FWorkerSocket::FWorkerSocket()
{
	ParentProcess = nullptr;
}

FWorkerSocket::~FWorkerSocket()
{
	Close();
}

bool FWorkerSocket::Open()
{
	Close();

	char Buffer[256];
	int Length = GetEnvironmentVariableA(EnvVarName, Buffer, sizeof(Buffer) / sizeof(Buffer[0]));

	if (Length <= 0 || Length >= sizeof(Buffer))
	{
		return false;
	}

	const size_t NumHandles = 4;
	size_t Handles[NumHandles] = { 0, };
	size_t HandleIdx = 0;

	char* Ptr = Buffer;
	for (; *Ptr != 0; Ptr++)
	{
		if (*Ptr == '.' && HandleIdx + 1 < NumHandles)
		{
			HandleIdx++;
		}
		else if (*Ptr >= '0' && *Ptr <= '9')
		{
			Handles[HandleIdx] = (Handles[HandleIdx] * 10) + (*Ptr - '0');
		}
		else
		{
			return false;
		}
	}

	ParentProcess = (void*)Handles[0];

	if (!IpcBuffer.OpenExisting((void*)Handles[1], (void*)Handles[2], (void*)Handles[3]))
	{
		Close();
		return false;
	}

	return true;
}

void FWorkerSocket::Close()
{
	ParentProcess = nullptr;
	IpcBuffer.Close();
}

bool FWorkerSocket::TryAttachRecvBuffer(int ChannelId, FSharedMemoryBuffer* Buffer)
{
	return TryAttachBuffer(EIpcMessageType::AttachRecvBuffer, ChannelId, Buffer);
}

bool FWorkerSocket::TryAttachSendBuffer(int ChannelId, FSharedMemoryBuffer* Buffer)
{
	return TryAttachBuffer(EIpcMessageType::AttachSendBuffer, ChannelId, Buffer);
}

bool FWorkerSocket::TryAttachBuffer(EIpcMessageType Type, int ChannelId, FSharedMemoryBuffer* Buffer)
{
	long long Size;

	FAttachMessage* Message = (FAttachMessage*)IpcBuffer.GetWriteMemory(Size);
	if (Size < sizeof(FAttachMessage))
	{
		return false;
	}

	memset(Message, 0, sizeof(*Message));

	Message->Type = Type;
	Message->ChannelId = ChannelId;
	if (!Buffer->DuplicateHandles(ParentProcess, Message->MemoryMappedFile, Message->ReaderEvent, Message->WriterEvent))
	{
		return false;
	}

	IpcBuffer.AdvanceWritePosition(sizeof(*Message));
	IpcBuffer.Flush();

	return true;
}
