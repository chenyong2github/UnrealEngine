// Copyright Epic Games, Inc. All Rights Reserved.

using System.Buffers.Binary;
using EpicGames.Horde.Compute;
using Microsoft.Extensions.Logging.Abstractions;

namespace RemoteServer
{
	class ServerApp
	{
		static async Task Main()
		{
			using IComputeChannel channel = ComputeChannel.ConnectAsWorker();
			Console.WriteLine("Connected to client"); // Client will wait for output before sending data on channel 1

			byte[] data = new byte[4];
			while(await channel.TryReceiveMessageAsync(data))
			{
				int value = BinaryPrimitives.ReadInt32LittleEndian(data);
				Console.WriteLine("Read value {0}", value);
			}
		}
	}
}