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
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Configuration;
using Serilog.Events;
using EpicGames.Serialization;
using EpicGames.Horde.Compute;
using EpicGames.Horde.Storage;
using System.Net.Http;

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

		/// <summary>
		/// Use the HTTP server endpoints
		/// </summary>
		[CommandLine("-Http")]
		public bool UseHttp;

		/// <summary>
		/// The server url
		/// </summary>
		[CommandLine("-Server=")]
		public string Server = Environment.GetEnvironmentVariable("HORDE_SERVER") ?? "https://localhost:5001";

		/// <summary>
		/// The token to use to connect with
		/// </summary>
		[CommandLine("-Token=")]
		public string? Token = Environment.GetEnvironmentVariable("HORDE_TOKEN");

		interface IComputeClient : IDisposable
		{
			Task AddBlobAsync(string NamespaceId, IoHash Hash, byte[] Data);
			Task<byte[]> GetBlobAsync(string NamespaceId, IoHash Hash);

			Task AddObjectAsync(string NamespaceId, IoHash Hash, byte[] Data);
			Task<byte[]> GetObjectAsync(string NamespaceId, IoHash Hash);

			Task AddTaskAsync(string ChannelId, string NamespaceId, IoHash RequirementsHash, IoHash TaskHash, bool SkipTaskLookup);
			Task GetTaskUpdatesAsync(string ChannelId, string NamespaceId, Func<GetTaskUpdateResponse, Task<bool>> OnUpdate);
		}

		class HttpComputeClient : IComputeClient
		{
			HttpClient HttpClient;

			public HttpComputeClient(Uri Server, string? BearerToken)
			{
				HttpClient = new HttpClient();
				HttpClient.BaseAddress = Server;
				if (BearerToken != null)
				{
					HttpClient.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", BearerToken);
				}
			}

			public void Dispose()
			{
				HttpClient.Dispose();
			}

			public async Task AddBlobAsync(string NamespaceId, IoHash Hash, byte[] Data)
			{
				HttpRequestMessage Request = new HttpRequestMessage(HttpMethod.Put, $"api/v1/blobs/{NamespaceId}/{Hash}");
				Request.Content = new ByteArrayContent(Data);

				HttpResponseMessage Response = await HttpClient.SendAsync(Request);
				Response.EnsureSuccessStatusCode();
			}

			public async Task<byte[]> GetBlobAsync(string NamespaceId, IoHash Hash)
			{
				HttpRequestMessage Request = new HttpRequestMessage(HttpMethod.Get, $"api/v1/blobs/{NamespaceId}/{Hash}");
				HttpResponseMessage Response = await HttpClient.SendAsync(Request);
				Response.EnsureSuccessStatusCode();
				return await Response.Content.ReadAsByteArrayAsync();
			}

			public Task AddObjectAsync(string NamespaceId, IoHash Hash, byte[] Data)
			{
				return AddBlobAsync(NamespaceId, Hash, Data);
			}

			public Task<byte[]> GetObjectAsync(string NamespaceId, IoHash Hash)
			{
				return GetBlobAsync(NamespaceId, Hash);
			}

			public async Task AddTaskAsync(string ChannelId, string NamespaceId, IoHash RequirementsHash, IoHash TaskHash, bool SkipCacheLookup)
			{
				AddTasksRequest AddTasks = new AddTasksRequest();
				AddTasks.RequirementsHash = RequirementsHash;
				AddTasks.TaskHashes.Add(TaskHash);
				AddTasks.DoNotCache = SkipCacheLookup;

				ReadOnlyMemoryContent Content = new ReadOnlyMemoryContent(CbSerializer.Serialize(AddTasks).GetView());
				Content.Headers.Add("Content-Type", "application/x-ue-cb");

				HttpRequestMessage Request = new HttpRequestMessage(HttpMethod.Post, $"api/v1/compute/{ChannelId}");
				Request.Content = Content;

				HttpResponseMessage Response = await HttpClient.SendAsync(Request);
				Response.EnsureSuccessStatusCode();
			}

			public async Task GetTaskUpdatesAsync(string ChannelId, string NamespaceId, Func<GetTaskUpdateResponse, Task<bool>> OnUpdateAsync)
			{
				for (; ; )
				{
					HttpRequestMessage Request = new HttpRequestMessage(HttpMethod.Post, $"api/v1/compute/{ChannelId}/updates?wait=10");
					Request.Headers.Add("Accept", "application/x-ue-cb");

					HttpResponseMessage Response = await HttpClient.SendAsync(Request);
					Response.EnsureSuccessStatusCode();

					byte[] Data = await Response.Content.ReadAsByteArrayAsync();
					GetTaskUpdatesResponse ParsedResponse = CbSerializer.Deserialize<GetTaskUpdatesResponse>(new CbField(Data));
					
					foreach (GetTaskUpdateResponse Update in ParsedResponse.Updates)
					{
						if (!await OnUpdateAsync(Update))
						{
							return;
						}
					}
				}
			}
		}

		class GrpcComputeClient : IComputeClient
		{
			GrpcChannel Channel;
			BlobStore.BlobStoreClient BlobStore;
			ComputeRpc.ComputeRpcClient ComputeClient;

			public GrpcComputeClient(GrpcService GrpcService)
			{
				Channel = GrpcService.CreateGrpcChannel();
				BlobStore = new BlobStore.BlobStoreClient(Channel);
				ComputeClient = new ComputeRpc.ComputeRpcClient(Channel);
			}

			public void Dispose()
			{
				Channel.Dispose();
			}

			public async Task AddBlobAsync(string NamespaceId, IoHash Hash, byte[] Data)
			{
				AddBlobsRequest Request = new AddBlobsRequest();
				Request.NamespaceId = NamespaceId;
				Request.Blobs.Add(new AddBlobRequest(Hash, Data));
				await BlobStore.AddAsync(Request);
			}

			public async Task<byte[]> GetBlobAsync(string NamespaceId, IoHash Hash)
			{
				return await BlobStore.GetBlobAsync(NamespaceId, Hash);
			}

			public Task AddObjectAsync(string NamespaceId, IoHash Hash, byte[] Data)
			{
				return AddBlobAsync(NamespaceId, Hash, Data);
			}

			public Task<byte[]> GetObjectAsync(string NamespaceId, IoHash Hash)
			{
				return BlobStore.GetBlobAsync(NamespaceId, Hash);
			}

			public async Task AddTaskAsync(string ChannelId, string NamespaceId, IoHash RequirementsHash, IoHash TaskHash, bool SkipCacheLookup)
			{
				AddTasksRpcRequest AddTasksRequest = new AddTasksRpcRequest(ChannelId, NamespaceId, RequirementsHash, new List<CbObjectAttachment> { TaskHash }, SkipCacheLookup);
				await ComputeClient.AddTasksAsync(AddTasksRequest);
			}

			public async Task GetTaskUpdatesAsync(string ChannelId, string NamespaceId, Func<GetTaskUpdateResponse, Task<bool>> OnUpdateAsync)
			{
				using (AsyncDuplexStreamingCall<GetTaskUpdatesRpcRequest, GetTaskUpdatesRpcResponse> Call = ComputeClient.GetTaskUpdates())
				{
					await Call.RequestStream.WriteAsync(new GetTaskUpdatesRpcRequest(ChannelId));

					//					TaskCompletionSource<bool> CompleteNowTask = new TaskCompletionSource<bool>();
					//					Task CompleteTask = Task.Run(async () => { await CompleteNowTask.Task; await Call.RequestStream.CompleteAsync(); });

					while (await Call.ResponseStream.MoveNext())
					{
						GetTaskUpdatesRpcResponse RpcResponse = Call.ResponseStream.Current;

						GetTaskUpdateResponse Response = new GetTaskUpdateResponse();
						Response.AgentId = RpcResponse.AgentId;
						Response.LeaseId = RpcResponse.LeaseId;
						Response.Result = RpcResponse.Result?.Hash;
						Response.State = RpcResponse.State;
						Response.TaskHash = RpcResponse.Task;
						Response.Time = RpcResponse.Time.ToDateTime();

						if (!await OnUpdateAsync(Response))
						{
							await Call.RequestStream.CompleteAsync();
						}
					}
				}
			}
		}

		static (DirectoryTree, IoHash) CreateSandbox(DirectoryInfo BaseDirInfo, Dictionary<IoHash, byte[]> UploadList)
		{
			DirectoryTree Tree = new DirectoryTree();

			foreach (DirectoryInfo SubDirInfo in BaseDirInfo.EnumerateDirectories())
			{
				(DirectoryTree SubTree, IoHash SubDirHash) = CreateSandbox(SubDirInfo, UploadList);
				Tree.Directories.Add(new DirectoryNode(SubDirInfo.Name, SubDirHash));
			}
			Tree.Directories.SortBy(x => x.Name, Utf8StringComparer.Ordinal);

			foreach (FileInfo FileInfo in BaseDirInfo.EnumerateFiles())
			{
				byte[] Data = File.ReadAllBytes(FileInfo.FullName);
				IoHash Hash = IoHash.Compute(Data);
				UploadList[Hash] = Data;
				Tree.Files.Add(new FileNode(FileInfo.Name, Hash, FileInfo.Length, (int)FileInfo.Attributes));
			}
			Tree.Files.SortBy(x => x.Name, Utf8StringComparer.Ordinal);

			return (Tree, AddCbObject(UploadList, Tree));
		}

		IComputeClient CreateComputeClient(IServiceProvider Services)
		{
			if (UseHttp)
			{
				return new HttpComputeClient(new Uri(Server), Token);
			}
			else
			{
				return new GrpcComputeClient(Services.GetRequiredService<GrpcService>());
			}
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
				using IComputeClient ComputeClient = CreateComputeClient(Host.Services);
				
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
				(_, IoHash SandboxHash) = CreateSandbox(InputDir.ToDirectoryInfo(), Blobs);

				ComputeTask Task = new ComputeTask(JsonComputeTask.Executable, JsonComputeTask.Arguments.ConvertAll<Utf8String>(x => x), JsonComputeTask.WorkingDirectory, SandboxHash);
				Task.EnvVars = JsonComputeTask.EnvVars.ToDictionary(x => (Utf8String)x.Key, x => (Utf8String)x.Value);
				Task.OutputPaths.AddRange(JsonComputeTask.OutputPaths.Select(x => (Utf8String)x));
				IoHash TaskHash = AddCbObject(Blobs, Task);

				JsonRequirements JsonRequirements = JsonComputeTask.Requirements;
				Requirements Requirements = new Requirements(JsonRequirements.Condition ?? String.Empty);
				Requirements.Resources = JsonRequirements.Resources.ToDictionary(x => x.Key, x => x.Value);
				Requirements.Exclusive = JsonRequirements.Exclusive;
				IoHash RequirementsHash = AddCbObject(Blobs, Requirements);

				await ExecuteAction(Logger, ComputeClient, TaskHash, RequirementsHash, Blobs);	
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

		private async Task ExecuteAction(ILogger Logger, IComputeClient ComputeClient, CbObjectAttachment TaskHash, CbObjectAttachment RequirementsHash, Dictionary<IoHash, byte[]> UploadList)
		{
			Logger.LogInformation("task: {TaskHash}", TaskHash);
			Logger.LogInformation("requirements: {RequirementsHash}", RequirementsHash);

			const string NamespaceId = "default";

			foreach (KeyValuePair<IoHash, byte[]> Pair in UploadList)
			{
				await ComputeClient.AddBlobAsync(NamespaceId, Pair.Key, Pair.Value);
			}

			// Execute the action
			string ChannelId = Guid.NewGuid().ToString();
			await ComputeClient.AddTaskAsync(ChannelId, NamespaceId, RequirementsHash, TaskHash, SkipCacheLookup);

			await ComputeClient.GetTaskUpdatesAsync(ChannelId, NamespaceId, async Response =>
			{
				Logger.LogInformation("{OperationName}: Execution state: {State}", (IoHash)(CbObjectAttachment)Response.TaskHash, Response.State.ToString());
				if (!String.IsNullOrEmpty(Response.AgentId) || !String.IsNullOrEmpty(Response.LeaseId))
				{
					Logger.LogInformation("{OperationName}: Running on agent {AgentId} under lease {LeaseId}", (IoHash)(CbObjectAttachment)Response.TaskHash, Response.AgentId, Response.LeaseId);
				}
				if (Response.Result != null)
				{
					await HandleCompleteTask(ComputeClient, NamespaceId, Response.Result.Value, Logger);
				}
				return Response.State != ComputeTaskState.Complete;
			});
		}

		async Task HandleCompleteTask(IComputeClient ComputeClient, string NamespaceId, CbObjectAttachment Hash, ILogger Logger)
		{
			Logger.LogInformation("result: {ResultHash}", Hash);

			ComputeTaskResult Result = await GetObjectAsync<ComputeTaskResult>(ComputeClient, NamespaceId, Hash);
			Logger.LogInformation("exit: {ExitCode}", Result.ExitCode);

			await LogTaskOutputAsync(ComputeClient, "stdout", NamespaceId, Result.StdOutHash, Logger);
			await LogTaskOutputAsync(ComputeClient, "stderr", NamespaceId, Result.StdErrHash, Logger);

			if (Result.OutputHash != null && OutputDir != null)
			{
				await WriteOutputAsync(ComputeClient, NamespaceId, Result.OutputHash.Value, new DirectoryReference(OutputDir));
			}
		}

		async Task LogTaskOutputAsync(IComputeClient ComputeClient, string Channel, string NamespaceId, IoHash? LogHash, ILogger Logger)
		{
			if (LogHash != null)
			{
				byte[] StdOutData = await ComputeClient.GetBlobAsync(NamespaceId, LogHash.Value);
				if (StdOutData.Length > 0)
				{
					foreach (string Line in Encoding.UTF8.GetString(StdOutData).Split('\n'))
					{
						Logger.LogDebug("{Channel}: {Line}", Channel, Line);
					}
				}
			}
		}

		async Task WriteOutputAsync(IComputeClient ComputeClient, string NamespaceId, IoHash TreeHash, DirectoryReference OutputDir)
		{
			DirectoryTree Tree = await GetObjectAsync<DirectoryTree>(ComputeClient, NamespaceId, TreeHash);

			List<Task> Tasks = new List<Task>();
			foreach (FileNode File in Tree.Files)
			{
				FileReference OutputFile = FileReference.Combine(OutputDir, File.Name.ToString());
				Tasks.Add(WriteObjectToFileAsync(ComputeClient, NamespaceId, File.Hash, OutputFile));
			}
			foreach (DirectoryNode Directory in Tree.Directories)
			{
				DirectoryReference NextOutputDir = DirectoryReference.Combine(OutputDir, Directory.Name.ToString());
				Tasks.Add(WriteOutputAsync(ComputeClient, NamespaceId, Directory.Hash, NextOutputDir));
			}

			await Task.WhenAll(Tasks);
		}

		static async Task<T> GetObjectAsync<T>(IComputeClient ComputeClient, string NamespaceId, IoHash Hash)
		{
			byte[] Data = await ComputeClient.GetObjectAsync(NamespaceId, Hash);
			return CbSerializer.Deserialize<T>(new CbField(Data));
		}

		static async Task WriteObjectToFileAsync(IComputeClient ComputeClient, string NamespaceId, IoHash Hash, FileReference OutputFile)
		{
			DirectoryReference.CreateDirectory(OutputFile.Directory);

			byte[] Data = await ComputeClient.GetBlobAsync(NamespaceId, Hash);
			await FileReference.WriteAllBytesAsync(OutputFile, Data);
		}
	}
}
