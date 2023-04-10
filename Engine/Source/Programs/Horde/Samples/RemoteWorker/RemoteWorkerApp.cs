// Copyright Epic Games, Inc. All Rights Reserved.

using System.Buffers.Binary;
using EpicGames.Horde.Compute;

namespace RemoteWorker
{
	class WorkerApp
	{
		static async Task Main()
		{
			using IComputeChannel channel = ComputeChannel.ConnectAsWorker();
			Console.WriteLine("Connected to client");

			byte[] data = new byte[4];
			while(await channel.TryReceiveMessageAsync(data))
			{
				int value = BinaryPrimitives.ReadInt32LittleEndian(data);
				Console.WriteLine("Read value {0}", value);
			}

			Console.WriteLine("Exiting worker");
		}
	}
}