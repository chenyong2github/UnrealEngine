// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Compute;
using Microsoft.Extensions.Logging.Abstractions;

namespace RemoteServer
{
	class ServerApp
	{
		static async Task Main()
		{
			CancellationToken cancellationToken = CancellationToken.None;

			await using IComputeSocket socket = ComputeSocket.ConnectAsWorker(NullLogger.Instance);
			Console.WriteLine("Connected to client");

			using IComputeBufferReader reader = await socket.AttachRecvBufferAsync(1, 1024 * 1024, cancellationToken);
			while (!reader.IsComplete)
			{
				ReadOnlyMemory<byte> memory = reader.GetMemory();
				if (memory.Length == 0)
				{
					await reader.WaitForDataAsync(0, cancellationToken);
					continue;
				}

				Console.WriteLine("Received data: {0}", memory.Span[0]);
				reader.Advance(1);
			}
		}
	}
}