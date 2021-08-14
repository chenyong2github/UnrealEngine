// Copyright Epic Games, Inc. All Rights Reserved.

using Google.Protobuf;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;
using System.Text.Json;
using HordeAgent.Services;
using Grpc.Net.Client;
using System.Net.Http.Headers;
using Grpc.Core;
using Google.LongRunning;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Configuration;
using Serilog.Events;
using EpicGames.Serialization;
using EpicGames.Horde.Compute;
using EpicGames.Horde.Storage;

namespace HordeAgent.Commands
{
	/// <summary>
	/// Installs the agent as a service
	/// </summary>
	[Command("ExecuteV2", "Executes a command through the remote execution API")]
	class ExecuteCommandV2 : Command
	{
		class JsonRequirements
		{
			public string? Condition { get; set; }
			public Dictionary<string, int> Resources { get; set; } = new Dictionary<string, int>();
			public bool Exclusive { get; set; }
		}

		class JsonComputeTask
		{
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
		public FileReference Input = null!;

		/// <summary>
		/// The input directory. By default, the directory containing the input file will be used.
		/// </summary>
		[CommandLine]
		public DirectoryReference? InputDir = null;

		/// <summary>
		/// Apply a random salt to the cached value
		/// </summary>
		[CommandLine("-Salt")]
		public bool RandomSalt;

		/// <summary>
		/// Add a known salt value to the cache
		/// </summary>
		[CommandLine("-Salt=")]
		public string? Salt = null;
						
		/// <summary>
		/// Skip checking if a result is already available in the action cache
		/// </summary>
		[CommandLine("-SkipCacheLookup")]
		public bool SkipCacheLookup = false;
		
		/// <summary>
		/// Directory to download the output files to. If not set, no results will be downloaded.
		/// </summary>
		[CommandLine("-OutputDir")]
		public string? OutputDir = null;
	
		/// <summary>
		/// Log verbosity level (use normal Serilog levels such as debug, warning or info)
		/// </summary>
		[CommandLine("-LogLevel")]
		public string LogLevelStr = "debug";

		static IoHash CreateSandbox(DirectoryInfo BaseDirInfo, Dictionary<IoHash, byte[]> UploadList)
		{
			DirectoryTree Tree = new DirectoryTree();

			foreach (DirectoryInfo SubDirInfo in BaseDirInfo.EnumerateDirectories())
			{
				IoHash SubDirHash = CreateSandbox(SubDirInfo, UploadList);
				Tree.Directories.Add(new DirectoryNode(SubDirInfo.Name, SubDirHash));
			}
			Tree.Directories.SortBy(x => x.Name, Utf8StringComparer.Ordinal);

			foreach (FileInfo FileInfo in BaseDirInfo.EnumerateFiles())
			{
				byte[] Data = File.ReadAllBytes(FileInfo.FullName);
				IoHash Hash = IoHash.Compute(Data);
				UploadList[Hash] = Data;
				Tree.Files.Add(new FileNode(FileInfo.Name, Hash));
			}
			Tree.Files.SortBy(x => x.Name, Utf8StringComparer.Ordinal);

			return AddCbObject(UploadList, Tree);
		}

		/// <inheritdoc/>
		public override async Task<int> ExecuteAsync(ILogger Logger)
		{
			InputDir ??= Input.Directory;

			if (Enum.TryParse(LogLevelStr, true, out LogEventLevel LogEventLevel))
			{
				Logging.LogLevelSwitch.MinimumLevel = LogEventLevel;
			}
			else
			{
				Console.WriteLine($"Unable to parse log level: {this.LogLevelStr}");
				return 0;
			}

			IConfiguration Configuration = new ConfigurationBuilder()
				.AddEnvironmentVariables()
				.AddJsonFile("appsettings.json")
				.Build();

			IConfigurationSection ConfigSection = Configuration.GetSection(AgentSettings.SectionName);
			IHostBuilder HostBuilder = Host.CreateDefaultBuilder()
				.ConfigureLogging(Builder =>
				{
					Builder.SetMinimumLevel(LogLevel.Warning);
				})
				.ConfigureServices(Services =>
				{
					Services.AddOptions<AgentSettings>().Configure(Options => ConfigSection.Bind(Options)).ValidateDataAnnotations();
					Services.AddLogging();
					Services.AddSingleton<GrpcService>();
				});

			using (IHost Host = HostBuilder.Build())
			{
				GrpcService GrpcService = Host.Services.GetRequiredService<GrpcService>();
				
				byte[] Json = await FileReference.ReadAllBytesAsync(Input);
				JsonComputeTask JsonComputeTask = JsonSerializer.Deserialize<JsonComputeTask>(Json, new JsonSerializerOptions { PropertyNameCaseInsensitive = true });

				ByteString SaltBytes = ByteString.Empty;
				if (Salt != null)
				{
					SaltBytes = ByteString.CopyFromUtf8(Salt);
				}
				else if(RandomSalt)
				{
					SaltBytes = ByteString.CopyFrom(Guid.NewGuid().ToByteArray());
				}

				if (SaltBytes.Length > 0)
				{
					Logger.LogInformation("Using salt: {SaltBytes}", StringUtils.FormatHexString(SaltBytes.ToByteArray()));
				}

				Dictionary<IoHash, byte[]> Blobs = new Dictionary<IoHash, byte[]>();
				IoHash SandboxHash = CreateSandbox(InputDir.ToDirectoryInfo(), Blobs);

				ComputeTask Task = new ComputeTask(JsonComputeTask.Executable, JsonComputeTask.Arguments.ConvertAll<Utf8String>(x => x), JsonComputeTask.WorkingDirectory, SandboxHash);
				Task.EnvVars = JsonComputeTask.EnvVars.ToDictionary(x => (Utf8String)x.Key, x => (Utf8String)x.Value);
				Task.OutputPaths.AddRange(JsonComputeTask.OutputPaths.Select(x => (Utf8String)x));
				IoHash TaskHash = AddCbObject(Blobs, Task);

				JsonRequirements JsonRequirements = JsonComputeTask.Requirements;
				Requirements Requirements = new Requirements(JsonRequirements.Condition ?? String.Empty);
				Requirements.Resources = JsonRequirements.Resources.ToDictionary(x => (Utf8String)x.Key, x => x.Value);
				Requirements.Exclusive = JsonRequirements.Exclusive;
				IoHash RequirementsHash = AddCbObject(Blobs, Requirements);

				await ExecuteAction(Logger, GrpcService, TaskHash, RequirementsHash, Blobs);	
			}
			return 0;
		}

		static IoHash AddCbObject<T>(Dictionary<IoHash, byte[]> HashToData, T Source)
		{
			CbObject Obj = CbSerializer.Serialize<T>(Source);
			IoHash Hash = Obj.GetHash();
			HashToData[Hash] = Obj.GetView().ToArray();
			return Hash;
		}

		private async Task ExecuteAction(ILogger Logger, GrpcService GrpcService, IoHash TaskHash, IoHash RequirementsHash, Dictionary<IoHash, byte[]> UploadList)
		{
			using GrpcChannel Channel = GrpcService.CreateGrpcChannel();

			const string NamespaceId = "default";

			using GrpcChannel CasChannel = Channel;

			BlobStore.BlobStoreClient BlobStore = new BlobStore.BlobStoreClient(CasChannel);
			foreach (KeyValuePair<IoHash, byte[]> Pair in UploadList)
			{
				AddBlobsRequest Request = new AddBlobsRequest();
				Request.NamespaceId = NamespaceId;
				Request.Blobs.Add(new AddBlobRequest(Pair.Key, Pair.Value));
				await BlobStore.AddAsync(Request);
			}

			// Execute the action
			ComputeRpc.ComputeRpcClient ComputeClient = new ComputeRpc.ComputeRpcClient(Channel);

			AddTasksRpcRequest AddTasksRequest = new AddTasksRpcRequest(Guid.NewGuid().ToString(), NamespaceId, RequirementsHash, new List<IoHash> { TaskHash }, SkipCacheLookup);
			await ComputeClient.AddTasksAsync(AddTasksRequest);

			using (AsyncDuplexStreamingCall<GetTaskUpdatesRpcRequest, GetTaskUpdatesRpcResponse> Call = ComputeClient.GetTaskUpdates())
			{
				await Call.RequestStream.WriteAsync(new GetTaskUpdatesRpcRequest(AddTasksRequest.ChannelId));

				TaskCompletionSource<bool> CompleteNowTask = new TaskCompletionSource<bool>();
				Task CompleteTask = Task.Run(async () => { await CompleteNowTask.Task; await Call.RequestStream.CompleteAsync(); } );

				while (await Call.ResponseStream.MoveNext())
				{
					GetTaskUpdatesRpcResponse Response = Call.ResponseStream.Current;
					Logger.LogInformation("{OperationName}: Execution state: {State}", (IoHash)Response.TaskHash, Response.State.ToString());
					if (!String.IsNullOrEmpty(Response.AgentId) || !String.IsNullOrEmpty(Response.LeaseId))
					{
						Logger.LogInformation("{OperationName}: Running on agent {AgentId} under lease {LeaseId}", (IoHash)Response.TaskHash, Response.AgentId, Response.LeaseId);
					}
					if (Response.ResultHash != null)
					{
						await HandleCompleteTask(BlobStore, NamespaceId, Response.ResultHash, Logger);
					}
					if (Response.State == ComputeTaskState.Complete)
					{
						CompleteNowTask.TrySetResult(true);
					}
				}

				await CompleteTask;
			}
		}

		async Task HandleCompleteTask(BlobStore.BlobStoreClient Client, string NamespaceId, IoHash Hash, ILogger Logger)
		{
			ComputeTaskResult Result = await Client.GetObjectAsync<ComputeTaskResult>(NamespaceId, Hash);
			Logger.LogInformation("exit: {ExitCode}", Result.ExitCode);

			await LogTaskOutputAsync(Client, "stdout", NamespaceId, Result.StdOutHash, Logger);
			await LogTaskOutputAsync(Client, "stderr", NamespaceId, Result.StdErrHash, Logger);

			if (Result.OutputHash != null && OutputDir != null)
			{
				await WriteOutputAsync(Client, NamespaceId, Result.OutputHash.Value, new DirectoryReference(OutputDir));
			}
		}

		async Task LogTaskOutputAsync(BlobStore.BlobStoreClient Client, string Channel, string NamespaceId, IoHash? LogHash, ILogger Logger)
		{
			if (LogHash != null)
			{
				byte[] StdOutData = await Client.GetBlobAsync(NamespaceId, LogHash.Value);
				if (StdOutData.Length > 0)
				{
					foreach (string Line in Encoding.UTF8.GetString(StdOutData).Split('\n'))
					{
						Logger.LogDebug("{Channel}: {Line}", Channel, Line);
					}
				}
			}
		}

		async Task WriteOutputAsync(BlobStore.BlobStoreClient Client, string NamespaceId, IoHash TreeHash, DirectoryReference OutputDir)
		{
			DirectoryTree Tree = await Client.GetObjectAsync<DirectoryTree>(NamespaceId, TreeHash);

			List<Task> Tasks = new List<Task>();
			foreach (FileNode File in Tree.Files)
			{
				FileReference OutputFile = FileReference.Combine(OutputDir, File.Name.ToString());
				Tasks.Add(Client.GetFileAsync(NamespaceId, File.Hash, OutputFile));
			}
			foreach (DirectoryNode Directory in Tree.Directories)
			{
				DirectoryReference NextOutputDir = DirectoryReference.Combine(OutputDir, Directory.Name.ToString());
				Tasks.Add(WriteOutputAsync(Client, NamespaceId, Directory.Hash, NextOutputDir));
			}

			await Task.WhenAll(Tasks);
		}
	}
}
