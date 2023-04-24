// Copyright Epic Games, Inc. All Rights Reserved.

#include <iostream>
#include "ComputeChannel.h"
#include "SharedMemoryBuffer.h"

int main()
{
	FComputeSocket Socket;
	if (!Socket.Open())
	{
		std::cout << "Environment variable not set correctly" << std::endl;
		return 1;
	}

	FComputeChannel Channel;
	if (!Channel.Open(Socket, FComputeChannel::DefaultWorkerChannelId))
	{
		std::cout << "Unable to create channel to host" << std::endl;
		return 1;
	}

	std::cout << "Connected to client" << std::endl;

	size_t Length = 0;
	char Buffer[4];

	for (;;)
	{
		size_t Read = Channel.Receive(Buffer + Length, sizeof(Buffer) - Length);
		if (Read == 0)
		{
			return 0;
		}

		Length += Read;

		if (Length >= 4)
		{
			std::cout << "Read value " << *(int*)Buffer << std::endl;
			Length = 0;
		}
	}

	Channel.Close();
	return 0;
}

