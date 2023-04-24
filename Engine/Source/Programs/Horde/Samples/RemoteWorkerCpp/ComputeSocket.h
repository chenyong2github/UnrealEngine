// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SharedMemoryBuffer.h"

class FComputeSocket
{
	static const char* const EnvVarName;

public:
	FComputeSocket();
	~FComputeSocket();

	/** Opens a connection to the agent process */
	bool Open();

	/** Close the current connection */
	void Close();

	/** Attaches a new buffer for receiving data */
	void AttachRecvBuffer(int ChannelId, FSharedMemoryBuffer& Buffer);

	/** Attaches a new buffer for sending data */
	void AttachSendBuffer(int ChannelId, FSharedMemoryBuffer& Buffer);

private:
	FSharedMemoryBuffer CommandBuffer;

	void AttachBuffer(int ChannelId, int Type, FSharedMemoryBuffer& Buffer);

	static size_t WriteVarUInt(unsigned char* Pos, unsigned int Value);
	static size_t WriteString(unsigned char* Pos, const char* Text);
};
