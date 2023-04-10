// Copyright Epic Games, Inc. All Rights Reserved.

#include <iostream>
#include "ComputeChannel.h"
#include "SharedMemoryBuffer.h"

int main()
{
	FComputeChannel Channel;
	if (!Channel.Open())
	{
		std::cout << "Environment variable not set correctly" << std::endl;
		return 1;
	}

	std::cout << "Connected to client" << std::endl; // // Client will wait for output before sending data on channel 1

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

	return 0;
}

