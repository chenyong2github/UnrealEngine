// Copyright Epic Games, Inc. All Rights Reserved.

using System.Reflection;
using EpicGames.Core;
using EpicGames.Horde.Compute;
using EpicGames.Horde.Compute.Clients;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Backends;
using EpicGames.Horde.Storage.Nodes;
using Microsoft.Extensions.Logging;

namespace RemoteClient
{
	class ClientApp
	{
		static FileReference CurrentAssembly { get; } = new FileReference(Assembly.GetExecutingAssembly().Location);
		static DirectoryReference AssemblyDir { get; } = CurrentAssembly.Directory;
		static DirectoryReference ProjectDir { get; } = DirectoryReference.Combine(AssemblyDir, "../../..");
		static string ConfigPath { get; } = AssemblyDir.MakeRelativeTo(ProjectDir);

		static FileReference HordeAgentFile { get; } = FileReference.Combine(ProjectDir, "../../Horde.Agent", ConfigPath, "HordeAgent.dll");
		static FileReference RemoteServerFile { get; } = FileReference.Combine(ProjectDir, "../RemoteServer", ConfigPath, "RemoteServer.exe");

		static async Task Main()
		{
			ILogger logger = Log.Logger;
			CancellationToken cancellationToken = CancellationToken.None;

			// Create the client to handle our requests
			await using IComputeClient client = CreateClient(true, logger);

			// Allocate a worker
			await using IComputeLease? lease = await client.TryAssignWorkerAsync(new ClusterId("default"), null, cancellationToken);
			if (lease == null)
			{
				logger.LogInformation("Unable to connect to remote");
				return;
			}

			// Create a message channel on channel id 0. The Horde Agent always listens on this channel for requests.
			const int ControlChannelId = 0;
			await using IComputeMessageChannel channel = lease.Socket.CreateMessageChannel(ControlChannelId, 30 * 1024 * 1024, logger);

			// Upload the sandbox
			MemoryStorageClient storage = new MemoryStorageClient();
			using (TreeWriter treeWriter = new TreeWriter(storage))
			{
				DirectoryNode sandbox = new DirectoryNode();
				await sandbox.CopyFromDirectoryAsync(RemoteServerFile.Directory.ToDirectoryInfo(), new ChunkingOptions(), treeWriter, null, cancellationToken);
				NodeHandle handle = await treeWriter.FlushAsync(sandbox, cancellationToken);
				await channel.UploadFilesAsync("", handle.Locator, storage, cancellationToken);
			}

			// Run the task remotely in the background and echo the output to the console
			Task childProcessTask = channel.ExecuteAsync(RemoteServerFile.GetFileName(), new List<string>(), null, null, (string x) => logger.LogInformation("ChildProcess: {Message}", x), cancellationToken);

			// Generate data into a buffer attached to channel 1. The remote server will echo them back to us as it receives them, then exit when the channel is complete/closed.
			const int DataChannelId = 1;
			using (IComputeBufferWriter writer = lease.Socket.AttachSendBuffer(DataChannelId, 1024 * 1024))
			{
				for (int idx = 0; idx < 100; idx++)
				{
					logger.LogInformation("Writing value: {Value}", idx);
					writer.GetMemory().Span[0] = (byte)idx;
					writer.Advance(1);
					await Task.Delay(1000, cancellationToken);
				}
			}

			// Wait for the child process to finish
			await childProcessTask;
		}

		static IComputeClient CreateClient(bool loopback, ILogger logger)
		{
			if (loopback)
			{
				return new AgentComputeClient(HordeAgentFile.FullName, 2000, logger);
			}
			else
			{
				return new ServerComputeClient(new Uri("https://localhost:5001"), null, logger);
			}
		}
	}
}