// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SharedMemoryBuffer.h"

class FWorkerSocket
{
	static const char* const EnvVarName;
	enum class EIpcMessageType : unsigned char;
	struct FAttachMessage;

public:
	FWorkerSocket();
	~FWorkerSocket();

	bool Open();
	void Close();

	bool TryAttachRecvBuffer(int ChannelId, FSharedMemoryBuffer* Buffer);
	bool TryAttachSendBuffer(int ChannelId, FSharedMemoryBuffer* Buffer);

private:
	void* ParentProcess;
	FSharedMemoryBuffer IpcBuffer;

	bool TryAttachBuffer(EIpcMessageType Type, int ChannelId, FSharedMemoryBuffer* Buffer);
};
