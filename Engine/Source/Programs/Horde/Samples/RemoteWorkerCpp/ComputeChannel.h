// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SharedMemoryBuffer.h"

class FComputeChannel
{
	static const char* const EnvVarName;

public:
	FComputeChannel();
	~FComputeChannel();

	bool Open();
	void Close();

	void Send(const void* Data, size_t Length);
	size_t Receive(void* Data, size_t Length);

private:
	FSharedMemoryBuffer RecvBuffer;
	FSharedMemoryBuffer SendBuffer;
};
