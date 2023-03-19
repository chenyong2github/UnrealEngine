// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Compute;
using EpicGames.Horde.Compute.Cpp;
using EpicGames.Horde.Logs;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Backends;
using EpicGames.Horde.Storage.Nodes;
using Microsoft.Extensions.Logging;

namespace Horde.Agent.Commands.Compute
{
	/// <summary>
	/// Installs the agent as a service
	/// </summary>
	[Command("cppcompute", "Executes a command through the C++ Compute API")]
	class CppComputeCommand : ComputeCommand
	{
		class JsonComputeTask
		{
			public string Executable { get; set; } = null!;
			public List<string> Arguments { get; set; } = new List<string>();
			public string WorkingDir { get; set; } = String.Empty;
			public Dictionary<string, string> EnvVars { get; set; } = new Dictionary<string, string>();
			public List<string> OutputPaths { get; set; } = new List<string>();
		}

		[CommandLine("-Task=", Required = true)]
		FileReference TaskFile { get; set; } = null!;

		readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public CppComputeCommand(IServiceProvider serviceProvider, ILogger<CppComputeCommand> logger)
			: base(serviceProvider, logger)
		{
			_logger = logger;
		}

		/// <inheritdoc/>
		protected override async Task<bool> HandleRequestAsync(IComputeLease lease, CancellationToken cancellationToken)
		{
			await using IComputeChannel defaultChannel = lease.CreateChannel(0);

			const int ReplyChannelId = 1;
			await using IComputeChannel channel = lease.CreateChannel(ReplyChannelId);

			MemoryStorageClient storage = new MemoryStorageClient();

			NodeLocator node = await CreateComputeNodeAsync(TaskFile, storage, cancellationToken);
			defaultChannel.CppStart(node, ReplyChannelId);

			for (; ; )
			{
				using IComputeMessage message = await channel.ReceiveAsync(cancellationToken);
				switch (message.Type)
				{
					case ComputeMessageType.None:
						return false;
					case ComputeMessageType.CppBlobRead:
						{
							CppBlobReadMessage cppBlobRead = message.AsCppBlobRead();
							await channel.CppBlobDataAsync(cppBlobRead, storage, cancellationToken);
						}
						break;
					case ComputeMessageType.CppSuccess:
						{
							NodeLocator cppOutputLocator = message.AsCppSuccess();

							using ComputeStorageClient remoteStorage = new ComputeStorageClient(channel);
							TreeReader reader = new TreeReader(remoteStorage, null, _logger);

							CppComputeOutputNode computeOutput = await reader.ReadNodeAsync<CppComputeOutputNode>(cppOutputLocator, cancellationToken);
							_logger.LogInformation("Exit code: {ExitCode}", computeOutput.ExitCode);

							LogNode logNode = await computeOutput.Log.ExpandAsync(reader, cancellationToken);
							foreach (LogChunkRef logChunkRef in logNode.TextChunkRefs)
							{
								LogChunkNode logChunk = await logChunkRef.ExpandAsync(reader, cancellationToken);
								foreach (Utf8String line in logChunk.Lines)
								{
									_logger.LogInformation("Output: {Line}", line);
								}
							}

							channel.CppFinish();
						}
						return false;
					case ComputeMessageType.CppFailure:
						{
							string error = message.AsCppFailure();
							_logger.LogError("Failed: {Error}", error);
						}
						return false;
					default:
						throw new NotImplementedException();
				}
			}
		}

		static async Task<NodeLocator> CreateComputeNodeAsync(FileReference taskFile, IStorageClient storage, CancellationToken cancellationToken)
		{
			using TreeWriter writer = new TreeWriter(storage);

			byte[] data = await FileReference.ReadAllBytesAsync(taskFile, cancellationToken);
			JsonComputeTask jsonComputeTask = JsonSerializer.Deserialize<JsonComputeTask>(data, new JsonSerializerOptions { AllowTrailingCommas = true, PropertyNameCaseInsensitive = true, PropertyNamingPolicy = JsonNamingPolicy.CamelCase })!;

			DirectoryNode sandbox = new DirectoryNode();
			await sandbox.CopyFromDirectoryAsync(taskFile.Directory.ToDirectoryInfo(), new ChunkingOptions(), writer, null, cancellationToken);

			CppComputeNode computeNode = new CppComputeNode(jsonComputeTask.Executable, jsonComputeTask.Arguments, jsonComputeTask.WorkingDir, new TreeNodeRef<DirectoryNode>(sandbox));
			foreach ((string key, string value) in jsonComputeTask.EnvVars)
			{
				computeNode.EnvVars.Add(key, value);
			}
			foreach (string outputPath in jsonComputeTask.OutputPaths)
			{
				computeNode.OutputPaths.Add(outputPath);
			}

			NodeHandle handle = await writer.FlushAsync(computeNode, cancellationToken);
			return handle.Locator;
		}
	}
}
