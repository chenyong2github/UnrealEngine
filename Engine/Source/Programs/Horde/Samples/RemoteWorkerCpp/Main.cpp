// Copyright Epic Games, Inc. All Rights Reserved.

#include <iostream>
#include "WorkerSocket.h"
#include "SharedMemoryBuffer.h"

int main()
{
	FWorkerSocket Socket;
	if (!Socket.Open())
	{
		std::cout << "Environment variable not set correctly" << std::endl;
		return 1;
	}

	FSharedMemoryBuffer Buffer;
	if (!Buffer.CreateNew(1024 * 1024))
	{
		std::cout << "Couldn't create buffer" << std::endl;
		return 1;
	}
	if (!Socket.TryAttachRecvBuffer(1, &Buffer))
	{
		std::cout << "Failed to attach recieve buffer" << std::endl;
		return 1;
	}

	std::cout << "Connected to client" << std::endl; // // Client will wait for output before sending data on channel 1

	while (!Buffer.HasFinishedReading())
	{
		long long Size;
		const unsigned char* Memory = Buffer.GetReadMemory(Size);

		if (Size > 0)
		{
			std::cout << "Read value " << (int)*Memory << std::endl;
			Buffer.AdvanceReadPosition(1);
		}

		Buffer.WaitForData(0);
	}

	return 0;
}

