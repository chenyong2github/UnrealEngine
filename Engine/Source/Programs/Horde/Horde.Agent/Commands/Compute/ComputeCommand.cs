// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Compute;
using EpicGames.Horde.Storage;
using EpicGames.Serialization;
using Google.Protobuf;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Serilog.Events;

namespace Horde.Agent.Commands
{
	/// <summary>
	/// Installs the agent as a service
	/// </summary>
	[Command("Compute", "Executes a command through the Horde Compute API")]
	class ComputeCommand : Command
	{
		class JsonRequirements
		{
			public string? Condition { get; set; }
			public Dictionary<string, int> Resources { get; set; } = new Dictionary<string, int>();
			public bool Exclusive { get; set; }
		}

		class JsonComputeTask
		{
			public ComputeOptions ComputeServer { get; set; } = new ComputeOptions();
			public StorageOptions StorageServer { get; set; } = new StorageOptions();
			public ClusterId ClusterId { get; set; } = new ClusterId("default");
			public string Executable { get; set; } = String.Empty;
			public List<string> Arguments { get; set; } = new List<string>();
			public Dictionary<string, string> EnvVars { get; set; } = new Dictionary<string, string>();
			public List<string> OutputPaths { get; set; } = new List<string>();
			public string WorkingDirectory { get; set; } = String.Empty;
			public JsonRequirements Requirements { get; set; } = new JsonRequirements();
		}

		/// <summary>
		/// Input file describing the work to execute
		/// </summary>
		[CommandLine(Required = true)]
		public FileReference Input { get; set; } = null!;

		/// <summary>
		/// The input directory. By default, the directory containing the input file will be used.
		/// </summary>
		[CommandLine]
		public DirectoryReference? InputDir { get; set; } = null;

		/// <summary>
		/// Apply a random salt to the cached value
		/// </summary>
		[CommandLine("-Salt")]
		public bool RandomSalt { get; set; }

		/// <summary>
		/// Add a known salt value to the cache
		/// </summary>
		[CommandLine("-Salt=")]
		public string? Salt { get; set; } = null;
						
		/// <summary>
		/// Skip checking if a result is already available in the action cache
		/// </summary>
		[CommandLine("-SkipCacheLookup")]
		public bool SkipCacheLookup { get; set; } = false;
		
		/// <summary>
		/// Directory to download the output files to. If not set, no results will be downloaded.
		/// </summary>
		[CommandLine("-OutputDir")]
		public string? OutputDir { get; set; } = null;
	
		/// <summary>
		/// Log verbosity level (use normal Serilog levels such as debug, warning or info)
		/// </summary>
		[CommandLine("-LogLevel")]
		public string LogLevelStr { get; set; } = "debug";

		static (DirectoryTree, IoHash) CreateSandbox(DirectoryInfo baseDirInfo, Dictionary<IoHash, byte[]> uploadList)
		{
			DirectoryTree tree = new DirectoryTree();

			foreach (DirectoryInfo subDirInfo in baseDirInfo.EnumerateDirectories())
			{
				(DirectoryTree subTree, IoHash subDirHash) = CreateSandbox(subDirInfo, uploadList);
				tree.Directories.Add(new DirectoryNode(subDirInfo.Name, subDirHash));
			}
			tree.Directories.SortBy(x => x.Name, Utf8StringComparer.Ordinal);

			foreach (FileInfo fileInfo in baseDirInfo.EnumerateFiles())
			{
				byte[] data = File.ReadAllBytes(fileInfo.FullName);
				IoHash hash = IoHash.Compute(data);
				uploadList[hash] = data;
				tree.Files.Add(new FileNode(fileInfo.Name, hash, fileInfo.Length, (int)fileInfo.Attributes));
			}
			tree.Files.SortBy(x => x.Name, Utf8StringComparer.Ordinal);

			return (tree, AddCbObject(uploadList, tree));
		}

		/// <inheritdoc/>
		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			InputDir ??= Input.Directory;

			if (Enum.TryParse(LogLevelStr, true, out LogEventLevel logEventLevel))
			{
				Logging.LogLevelSwitch.MinimumLevel = logEventLevel;
			}
			else
			{
				Console.WriteLine($"Unable to parse log level: {LogLevelStr}");
				return 0;
			}

			IConfiguration configuration = new ConfigurationBuilder()
				.AddEnvironmentVariables()
				.AddJsonFile(Input.FullName)
				.Build();

			IHostBuilder hostBuilder = Host.CreateDefaultBuilder()
				.ConfigureLogging(builder =>
				{
					builder.SetMinimumLevel(LogLevel.Warning);
				})
				.ConfigureServices(services =>
				{
					services.AddLogging();

					IConfigurationSection computeSettings = configuration.GetSection(nameof(JsonComputeTask.ComputeServer));
					services.AddHordeCompute(settings => computeSettings.Bind(settings));

					IConfigurationSection storageSettings = configuration.GetSection(nameof(JsonComputeTask.StorageServer));
					services.AddHordeStorage(settings => storageSettings.Bind(settings));
				});

			using (IHost host = hostBuilder.Build())
			{
				IStorageClient storageClient = host.Services.GetRequiredService<IStorageClient>();
				IComputeClient computeClient = host.Services.GetRequiredService<IComputeClient>();

				ByteString saltBytes = ByteString.Empty;
				if (Salt != null)
				{
					saltBytes = ByteString.CopyFromUtf8(Salt);
				}
				else if(RandomSalt)
				{
					saltBytes = ByteString.CopyFrom(Guid.NewGuid().ToByteArray());
				}

				if (saltBytes.Length > 0)
				{
					logger.LogInformation("Using salt: {SaltBytes}", StringUtils.FormatHexString(saltBytes.ToByteArray()));
				}

				Dictionary<IoHash, byte[]> blobs = new Dictionary<IoHash, byte[]>();
				(_, IoHash sandboxHash) = CreateSandbox(InputDir.ToDirectoryInfo(), blobs);

				JsonComputeTask jsonComputeTask = new JsonComputeTask();
				configuration.Bind(jsonComputeTask);

				logger.LogInformation("compute server: {ServerUrl}", jsonComputeTask.ComputeServer.Url);
				logger.LogInformation("storage server: {ServerUrl}", jsonComputeTask.StorageServer.Url);

				JsonRequirements jsonRequirements = jsonComputeTask.Requirements;
				Requirements requirements = new Requirements(jsonRequirements.Condition ?? String.Empty);
				requirements.Resources = jsonRequirements.Resources.ToDictionary(x => x.Key, x => x.Value);
				requirements.Exclusive = jsonRequirements.Exclusive;
				IoHash requirementsHash = AddCbObject(blobs, requirements);

				ComputeTask task = new ComputeTask(jsonComputeTask.Executable, jsonComputeTask.Arguments.ConvertAll<Utf8String>(x => x), jsonComputeTask.WorkingDirectory, sandboxHash);
				task.EnvVars = jsonComputeTask.EnvVars.ToDictionary(x => (Utf8String)x.Key, x => (Utf8String)x.Value);
				task.OutputPaths.AddRange(jsonComputeTask.OutputPaths.Select(x => (Utf8String)x));
				task.RequirementsHash = AddCbObject(blobs, requirements);

				await ExecuteAction(logger, computeClient, storageClient, jsonComputeTask.ClusterId, task, blobs);	
			}
			return 0;
		}

		static IoHash AddCbObject<T>(Dictionary<IoHash, byte[]> hashToData, T source)
		{
			CbObject obj = CbSerializer.Serialize<T>(source);
			IoHash hash = obj.GetHash();
			hashToData[hash] = obj.GetView().ToArray();
			return hash;
		}

		private async Task ExecuteAction(ILogger logger, IComputeClient computeClient, IStorageClient storageClient, ClusterId clusterId, ComputeTask task, Dictionary<IoHash, byte[]> uploadList)
		{
			IComputeClusterInfo cluster = await computeClient.GetClusterInfoAsync(clusterId);

			{
				int index = 0, totalUploaded = 0, totalSkipped = 0, uploadedBytes = 0, skippedBytes = 0;
				IEnumerable<Task> tasks = uploadList.Select(async (pair) =>
				{
					if (await storageClient.HasBlobAsync(cluster.NamespaceId, pair.Key))
					{
						logger.LogInformation("Skipped blob {Idx}/{Count}: {Hash} ({Length} bytes)", Interlocked.Increment(ref index), uploadList.Count, pair.Key, pair.Value.Length);
						Interlocked.Increment(ref totalSkipped);
						Interlocked.Add(ref skippedBytes, pair.Value.Length);
						return;
					}

					await storageClient.WriteBlobFromMemoryAsync(cluster.NamespaceId, pair.Key, pair.Value);
					logger.LogInformation("Uploaded blob {Idx}/{Count}: {Hash} ({Bytes} bytes)", Interlocked.Increment(ref index), uploadList.Count, pair.Key, pair.Value.Length);
					Interlocked.Increment(ref totalUploaded);
					Interlocked.Add(ref uploadedBytes, pair.Value.Length);
				});
				await Task.WhenAll(tasks);

				logger.LogInformation("Uploaded {UploadedCount} blobs ({UploadedBytes} bytes), Skipped {SkippedCount} blobs ({SkippedBytes} bytes)", totalUploaded, uploadedBytes, totalSkipped, skippedBytes);
			}

			CbObject taskObject = CbSerializer.Serialize(task);
			IoHash taskHash = IoHash.Compute(taskObject.GetView().Span);
			RefId taskRefId = new RefId(taskHash);
			await storageClient.SetRefAsync(cluster.NamespaceId, cluster.RequestBucketId, taskRefId, taskObject);

			ChannelId channelId = new ChannelId(Guid.NewGuid().ToString());
			logger.LogInformation("cluster: {ClusterId}", clusterId);
			logger.LogInformation("channel: {ChannelId}", channelId);
			logger.LogInformation("task: {TaskHash}", taskHash);
			logger.LogInformation("requirements: {RequirementsHash}", task.RequirementsHash);

			// Execute the action
			await computeClient.AddTaskAsync(clusterId, channelId, taskRefId, task.RequirementsHash, SkipCacheLookup);

			await foreach(IComputeTaskInfo response in computeClient.GetTaskUpdatesAsync(clusterId, channelId))
			{
				logger.LogInformation("{OperationName}: Execution state: {State}", response.TaskRefId, response.State.ToString());
				if (!String.IsNullOrEmpty(response.AgentId) || !String.IsNullOrEmpty(response.LeaseId))
				{
					logger.LogInformation("{OperationName}: Running on agent {AgentId} under lease {LeaseId}", response.TaskRefId, response.AgentId, response.LeaseId);
				}
				if (response.ResultRefId != null)
				{
					await HandleCompleteTask(storageClient, cluster.NamespaceId, cluster.ResponseBucketId, response.ResultRefId.Value, logger);
				}
				if (response.State == ComputeTaskState.Complete)
				{
					if (response.Outcome != ComputeTaskOutcome.Success)
					{
						logger.LogError("{OperationName}: Outcome: {Outcome}, Detail: {Detail}", response.TaskRefId, response.Outcome.ToString(), response.Detail ?? "(none)");
					}
					break;
				}
			}
		}

		async Task HandleCompleteTask(IStorageClient storageClient, NamespaceId namespaceId, BucketId outputBucketId, RefId outputRefId, ILogger logger)
		{
			ComputeTaskResult result = await storageClient.GetRefAsync<ComputeTaskResult>(namespaceId, outputBucketId, outputRefId);
			logger.LogInformation("exit: {ExitCode}", result.ExitCode);

			await LogTaskOutputAsync(storageClient, "stdout", namespaceId, result.StdOutHash, logger);
			await LogTaskOutputAsync(storageClient, "stderr", namespaceId, result.StdErrHash, logger);

			if (result.OutputHash != null && OutputDir != null)
			{
				await WriteOutputAsync(storageClient, namespaceId, result.OutputHash.Value, new DirectoryReference(OutputDir));
			}
		}

		async Task LogTaskOutputAsync(IStorageClient storageClient, string channel, NamespaceId namespaceId, IoHash? logHash, ILogger logger)
		{
			if (logHash != null)
			{
				byte[] stdOutData = await storageClient.ReadBlobToMemoryAsync(namespaceId, logHash.Value);
				if (stdOutData.Length > 0)
				{
					foreach (string line in Encoding.UTF8.GetString(stdOutData).Split('\n'))
					{
						logger.LogDebug("{Channel}: {Line}", channel, line);
					}
				}
			}
		}

		async Task WriteOutputAsync(IStorageClient storageClient, NamespaceId namespaceId, IoHash treeHash, DirectoryReference outputDir)
		{
			DirectoryTree tree = await storageClient.ReadBlobAsync<DirectoryTree>(namespaceId, treeHash);

			List<Task> tasks = new List<Task>();
			foreach (FileNode file in tree.Files)
			{
				FileReference outputFile = FileReference.Combine(outputDir, file.Name.ToString());
				tasks.Add(WriteObjectToFileAsync(storageClient, namespaceId, file.Hash, outputFile));
			}
			foreach (DirectoryNode directory in tree.Directories)
			{
				DirectoryReference nextOutputDir = DirectoryReference.Combine(outputDir, directory.Name.ToString());
				tasks.Add(WriteOutputAsync(storageClient, namespaceId, directory.Hash, nextOutputDir));
			}

			await Task.WhenAll(tasks);
		}

		static async Task WriteObjectToFileAsync(IStorageClient storageClient, NamespaceId namespaceId, IoHash hash, FileReference outputFile)
		{
			DirectoryReference.CreateDirectory(outputFile.Directory);

			byte[] data = await storageClient.ReadBlobToMemoryAsync(namespaceId, hash);
			await FileReference.WriteAllBytesAsync(outputFile, data);
		}
	}
}
