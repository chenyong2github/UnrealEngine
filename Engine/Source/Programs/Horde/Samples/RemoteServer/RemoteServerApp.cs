// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Compute;
using Microsoft.Extensions.Logging.Abstractions;

namespace RemoteServer
{
	class ServerApp
	{
		static async Task Main()
		{
			await using IComputeSocket socket = ComputeSocket.ConnectAsWorker(NullLogger.Instance);
			using IComputeBufferReader reader = await socket.AttachRecvBufferAsync(1, 1024 * 1024);
			Console.WriteLine("Connected to client"); // Client will wait for output before sending data on channel 1

			while (!reader.IsComplete)
			{
				ReadOnlyMemory<byte> memory = reader.GetMemory();
				if (memory.Length > 0)
				{
					Console.WriteLine("Read value {0}", memory.Span[0]);
					reader.Advance(1);
				}
				await reader.WaitForDataAsync(0);
			}
		}
	}
}