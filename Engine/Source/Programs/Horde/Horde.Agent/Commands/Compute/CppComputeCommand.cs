// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Net.Http;
using System.Net.Http.Headers;
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
using Horde.Agent.Leases.Handlers;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace Horde.Agent.Commands
{
	/// <summary>
	/// Installs the agent as a service
	/// </summary>
	[Command("cppcompute", "Executes a command through the C++ Compute API")]
	class CppComputeCommand : Command
	{
		class JsonComputeTask
		{
			public string Executable { get; set; } = null!;
			public List<string> Arguments { get; set; } = new List<string>();
			public string WorkingDir { get; set; } = String.Empty;
			public Dictionary<string, string> EnvVars { get; set; } = new Dictionary<string, string>();
			public List<string> OutputPaths { get; set; } = new List<string>();
		}

		[CommandLine("-Cluster")]
		public string ClusterId { get; set; } = "default";

		[CommandLine("-Loopback")]
		public bool Loopback { get; set; }

		[CommandLine("-Task=", Required = true)]
		FileReference TaskFile { get; set; } = null!;

		readonly IServiceProvider _serviceProvider;
		readonly IHttpClientFactory _httpClientFactory;
		readonly AgentSettings _settings;
		readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public CppComputeCommand(IServiceProvider serviceProvider, IHttpClientFactory httpClientFactory, IOptions<AgentSettings> settings, ILogger<CppComputeCommand> logger)
		{
			_serviceProvider = serviceProvider;
			_httpClientFactory = httpClientFactory;
			_settings = settings.Value;
			_logger = logger;
		}

		/// <inheritdoc/>
		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			await using IComputeClient client = CreateClient();

			bool result = await client.ExecuteAsync(new ClusterId(ClusterId), null, HandleRequestAsync, CancellationToken.None);

			return result? 0 : 1;
		}

		async Task<bool> HandleRequestAsync(IComputeChannel channel, CancellationToken cancellationToken)
		{
			MemoryStorageClient storage = new MemoryStorageClient();
			NodeLocator node = await CreateComputeNodeAsync(TaskFile, storage, cancellationToken);

			CppComputeMessage message = new CppComputeMessage { Locator = node };
			await channel.WriteCbMessageAsync(message, cancellationToken);

			for (; ; )
			{
				object? obj = await channel.ReadCbMessageAsync(cancellationToken);
				switch (obj)
				{
					case null:
						return false;
					case BlobReadMessage readBlob:
						await channel.WriteBlobDataAsync(readBlob, storage, cancellationToken);
						break;
					case CppComputeOutputMessage cppOutput:
						{
							using ComputeStorageClient remoteStorage = new ComputeStorageClient(channel);
							TreeReader reader = new TreeReader(remoteStorage, null, _logger);

							CppComputeOutputNode computeOutput = await reader.ReadNodeAsync<CppComputeOutputNode>(cppOutput.Locator, cancellationToken);
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

							await channel.WriteCbMessageAsync(new CppComputeFinishMessage(), cancellationToken);
							await channel.WriteCbMessageAsync(new CloseMessage(), cancellationToken);
						}
						return true;
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

		IComputeClient CreateClient()
		{
			if (Loopback)
			{
				return new LoopbackComputeClient(RunListenerAsync);
			}
			else
			{
				return new ServerComputeClient(CreateHttpClient, _logger);
			}
		}

		HttpClient CreateHttpClient()
		{
			ServerProfile profile = _settings.GetCurrentServerProfile();

			HttpClient client = _httpClientFactory.CreateClient();
			client.BaseAddress = profile.Url;
			client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", profile.Token);
			return client;
		}

		async Task RunListenerAsync(IComputeChannel channel, CancellationToken cancellationToken)
		{
			ComputeHandler handler = ActivatorUtilities.CreateInstance<ComputeHandler>(_serviceProvider);
			await handler.RunAsync(channel, cancellationToken);
		}
	}
}
