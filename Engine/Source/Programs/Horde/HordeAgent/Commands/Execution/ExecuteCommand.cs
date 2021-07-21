// Copyright Epic Games, Inc. All Rights Reserved.

using Build.Bazel.Remote.Execution.V2;
using Google.Protobuf;
using HordeAgent.Utility;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Threading.Tasks;

using Action = Build.Bazel.Remote.Execution.V2.Action;
using ActionResult = Build.Bazel.Remote.Execution.V2.ActionResult;
using RpcCommand = Build.Bazel.Remote.Execution.V2.Command;
using Directory = Build.Bazel.Remote.Execution.V2.Directory;
using RpcExecution = Build.Bazel.Remote.Execution.V2.Execution;
using EpicGames.Core;
using System.Text.Json;
using HordeAgent.Services;
using Grpc.Net.Client;
using System.Linq;
using System.Net.Http.Headers;
using Grpc.Core;
using Google.LongRunning;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Configuration;
using Serilog.Events;
using Digest = Build.Bazel.Remote.Execution.V2.Digest;
using EpicGames.Horde.Common.RemoteExecution;

namespace HordeAgent.Commands
{
	/// <summary>
	/// Installs the agent as a service
	/// </summary>
	[Command("Execute", "Executes a command through the remote execution API")]
	class ExecuteCommand : Command
	{
		/// <summary>
		/// Input file describing the work to execute
		/// </summary>
		[CommandLine(Required = true)]
		public FileReference Input = null!;

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
		/// gRPC URL for the content-addressable storage service.
		///
		/// If empty, Horde's built-in storage will be used.
		/// The URL must match what has been configured for the instance name in Horde server settings.
		/// </summary>
		[CommandLine("-CasUrl=")]
		public string? CasUrl = null;
		
		/// <summary>
		/// Auth token to use for accessing CAS service
		/// </summary>
		[CommandLine("-CasAuthToken=")]
		public string? CasAuthToken = null;
		
		/// <summary>
		/// Instance name to use
		///
		/// See RE API specification for description.
		/// </summary>
		[CommandLine("-InstanceName=")]
		public string? InstanceName = null;
				
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
		/// Number of concurrent executions to send (use with NumDuplicatedExecutions)
		/// </summary>
		[CommandLine("-NumConcurrentExecutions")]
		public int NumConcurrentExecutions = 1;
		
		/// <summary>
		/// Number of duplicated executions. Only useful for load testing purposes.
		/// </summary>
		[CommandLine("-NumDuplicatedExecutions")]
		public int NumDuplicatedExecutions = 1;
		
		/// <summary>
		/// Log verbosity level (use normal Serilog levels such as debug, warning or info)
		/// </summary>
		[CommandLine("-LogLevel")]
		public string LogLevelStr = "debug";

		/// <inheritdoc/>
		public override async Task<int> ExecuteAsync(ILogger Logger)
		{
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
				JsonAction Action = JsonSerializer.Deserialize<JsonAction>(Json, new JsonSerializerOptions { PropertyNameCaseInsensitive = true });

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

				UploadList UploadList = new UploadList();
				Digest ActionDigest = await Action.BuildAsync(UploadList, SaltBytes, SkipCacheLookup);

				if (NumDuplicatedExecutions == 1)
				{
					await ExecuteAction(Logger, GrpcService, UploadList, ActionDigest);	
				}
				else
				{
					await ExecuteActionsConcurrently(Logger, GrpcService, UploadList, ActionDigest);
				}
				
			}
			return 0;
		}

		private class ConcurrentExecuteOperation
		{
			internal DateTimeOffset CreatedTime;
			internal DateTimeOffset ExecutionSendTime;
			internal DateTimeOffset CompletedTime;
			internal ActionResult? Result;
			internal int NumRetries = 0;
		
			public ConcurrentExecuteOperation()
			{
				CreatedTime = DateTimeOffset.Now;
			}
		}

		private async Task ExecuteActionsConcurrently(ILogger Logger, GrpcService GrpcService, UploadList UploadList, Digest ActionDigest)
		{
			Logger.LogInformation($"Running {NumDuplicatedExecutions} concurrently using {NumConcurrentExecutions} tasks...");
			ConcurrentQueue<ConcurrentExecuteOperation> PendingOps = new ConcurrentQueue<ConcurrentExecuteOperation>();
			ConcurrentQueue<ConcurrentExecuteOperation> CompletedOps = new ConcurrentQueue<ConcurrentExecuteOperation>();

			for (int i = 0; i < NumDuplicatedExecutions; i++)
			{
				PendingOps.Enqueue(new ConcurrentExecuteOperation());
			}
			
			List<Task> RunnerTasks = new List<Task>();
			DateTime StartTime = DateTime.UtcNow;
			
			for (int i = 0; i < NumConcurrentExecutions; i++)
			{
				RunnerTasks.Add(Task.Run(async () =>
				{
					while (PendingOps.TryDequeue(out var Op))
					{
						Op.ExecutionSendTime = DateTimeOffset.Now;
						Op.Result = await ExecuteAction(Logger, GrpcService, UploadList, ActionDigest);
						if (Op.Result == null)
						{
							Logger.LogInformation("Retrying...");
							Op.NumRetries++;
							PendingOps.Enqueue(Op);
							continue;
						}
						
						Op.CompletedTime = DateTimeOffset.Now;
						CompletedOps.Enqueue(Op);
						Logger.LogInformation($"{CompletedOps.Count}/{NumDuplicatedExecutions} done");
					}
				}));
			}

			await Task.WhenAll(RunnerTasks.ToArray());
			DateTime EndTime = DateTime.UtcNow;
			TimeSpan TimeTaken = EndTime - StartTime;
			Logger.LogInformation("Finished executing all tasks");

			Logger.LogInformation("{0,35} {1,16} {2,15} {3,7}", "Worker", "ServerQueueTime", "WorkerTime", "Retries");
			Logger.LogInformation(new string('-', 100));
			foreach (ConcurrentExecuteOperation Op in CompletedOps.ToArray())
			{
				if (Op.Result == null)
				{
					Logger.LogError("Received a null result");
					continue;
				}
				ExecutedActionMetadata Md = Op.Result.ExecutionMetadata;
				TimeSpan QueueTime = Md.WorkerStartTimestamp.ToDateTimeOffset() - Md.QueuedTimestamp.ToDateTimeOffset();
				TimeSpan TotalWorkerTime = Md.WorkerCompletedTimestamp.ToDateTimeOffset() - Md.WorkerStartTimestamp.ToDateTimeOffset();
				Logger.LogInformation("{Worker,35} {QueueTime,16:N2} {TotalWorkerTime,15:N2} {Retries,7}", Md.Worker, QueueTime.TotalSeconds, TotalWorkerTime.TotalSeconds, Op.NumRetries);
			}
			
			Logger.LogInformation(" Time taken: {TimeTaken:N1} secs", TimeTaken.TotalSeconds);
			Logger.LogInformation("  Num execs: {NumExecutions}", CompletedOps.Count);
			Logger.LogInformation("Num retries: {NumRetries}", CompletedOps.Sum(o => o.NumRetries));
		}

		private async Task<ActionResult?> ExecuteAction(ILogger Logger, GrpcService GrpcService, UploadList UploadList, Digest ActionDigest)
		{
			using GrpcChannel Channel = GrpcService.CreateGrpcChannel();
			ActionResult? ActionResult = null;

			GrpcChannel GetChannel()
			{
				if (CasUrl == null) return Channel;
				if (CasAuthToken != null) return GrpcService.CreateGrpcChannel(CasUrl, new AuthenticationHeaderValue("ServiceAccount", CasAuthToken));
				return GrpcService.CreateGrpcChannel(CasUrl, null);
			}

			using GrpcChannel CasChannel = GetChannel();
			ContentAddressableStorage.ContentAddressableStorageClient StorageClient = new ContentAddressableStorage.ContentAddressableStorageClient(CasChannel);

			// Find the missing blobs and upload them
			FindMissingBlobsRequest MissingBlobsRequest = new FindMissingBlobsRequest();
			MissingBlobsRequest.InstanceName = InstanceName;
			MissingBlobsRequest.BlobDigests.Add(UploadList.Blobs.Values.Select(x => x.Digest));

			FindMissingBlobsResponse MissingBlobs = await StorageClient.FindMissingBlobsAsync(MissingBlobsRequest);
			if (MissingBlobs.MissingBlobDigests.Count > 0)
			{
				BatchUpdateBlobsRequest UpdateRequest = new BatchUpdateBlobsRequest();
				UpdateRequest.InstanceName = InstanceName;
				UpdateRequest.Requests.AddRange(MissingBlobs.MissingBlobDigests.Select(x => UploadList.Blobs[x.Hash]));
				Logger.LogInformation($"Updating {UpdateRequest.Requests.Count} blobs");

				BatchUpdateBlobsResponse UpdateResponse = await StorageClient.BatchUpdateBlobsAsync(UpdateRequest);

				bool UpdateFailed = false;
				foreach (var Response in UpdateResponse.Responses)
				{
					if (Response.Status.Code != (int) Google.Rpc.Code.Ok)
					{
						Logger.LogError($"Upload failed for {Response.Digest}. Code: {Response.Status.Code} Message: {Response.Status.Message}");
						UpdateFailed = true;
					}
				}

				if (UpdateFailed)
				{
					throw new Exception("Failed updating blobs in CAS service");
				}
			}

			// Execute the action
			RpcExecution.ExecutionClient ExecutionClient = new RpcExecution.ExecutionClient(Channel);

			ExecuteRequest ExecuteRequest = new ExecuteRequest();
			ExecuteRequest.ActionDigest = ActionDigest;
			ExecuteRequest.SkipCacheLookup = SkipCacheLookup;
			if (InstanceName != null)
			{
				ExecuteRequest.InstanceName = InstanceName;
			}

			ExecutionStage.Types.Value CurrentStage = ExecutionStage.Types.Value.Unknown;
			using AsyncServerStreamingCall<Operation> Call = ExecutionClient.Execute(ExecuteRequest);
			while (await Call.ResponseStream.MoveNext())
			{
				Operation Operation = Call.ResponseStream.Current;

				ExecuteOperationMetadata Metadata = Operation.Metadata.Unpack<ExecuteOperationMetadata>();
				Logger.LogDebug("{OperationName}: Execution state: {State}", Operation.Name, Metadata.Stage);
				if (Metadata.Stage < CurrentStage)
				{
					Logger.LogError("Trying to set stage {NewStage} when current stage is {CurrentStage}", Metadata.Stage, CurrentStage);
				}

				if (Operation.Done)
				{
					// Unpack the response
					ExecuteResponse ExecuteResponse;
					if(!Operation.Response.TryUnpack(out ExecuteResponse))
					{
						Logger.LogError("Unable to decode response");
						break;
					}
					if((StatusCode)ExecuteResponse.Status.Code != StatusCode.OK)
					{
						Logger.LogError("Error {0}: {1}", ExecuteResponse.Status.Code, ExecuteResponse.Status.Message);
						break;
					}

					ActionResult = ExecuteResponse.Result;
					if (ExecuteResponse.Result == null)
					{
						Logger.LogError("Empty result");
						break;
					}

					Logger.LogDebug("Worker: {Worker}", ActionResult.ExecutionMetadata.Worker);
					Logger.LogDebug("Cached: {CachedResult}", ExecuteResponse.CachedResult);

					Logger.LogDebug("Output directories:");
					foreach (OutputDirectory Dir in ActionResult.OutputDirectories)
					{
						Logger.LogDebug("Path: {Path} Digest: {Digest}", Dir.Path, Dir.TreeDigest);
					}
							
					Logger.LogDebug("Output files:");
					foreach (OutputFile File in ActionResult.OutputFiles)
					{
						Logger.LogDebug("Path: {Path} Digest: {Digest}", File.Path, File.Digest);
					}

					// Print the result
					BatchReadBlobsRequest BatchReadRequest = new BatchReadBlobsRequest();
					BatchReadRequest.InstanceName = InstanceName;
					if (ActionResult.StdoutDigest != null && ActionResult.StdoutDigest.SizeBytes > 0)
					{
						BatchReadRequest.Digests.Add(ActionResult.StdoutDigest);
					}
					if (ActionResult.StderrDigest != null && ActionResult.StderrDigest.SizeBytes > 0)
					{
						BatchReadRequest.Digests.Add(ActionResult.StderrDigest);
					}
					if (OutputDir != null && ActionResult.OutputFiles != null && ActionResult.OutputFiles.Count > 0)
					{
						BatchReadRequest.Digests.Add(ActionResult.OutputFiles.Select(x => x.Digest));
					}

					BatchReadBlobsResponse BatchReadResponse = await StorageClient.BatchReadBlobsAsync(BatchReadRequest);
					if (ActionResult.StdoutDigest != null)
					{
						BatchReadBlobsResponse.Types.Response? StdoutResponse = BatchReadResponse.Responses.FirstOrDefault(x => x.Digest.Hash == ActionResult.StdoutDigest.Hash);
						if (StdoutResponse != null && StdoutResponse.Data.Length > 0)
						{
							foreach (string Line in Encoding.UTF8.GetString(StdoutResponse.Data.ToByteArray()).Split('\n'))
							{
								Logger.LogDebug("stdout: {Line}", Line);
							}
						}
					}
					if (ActionResult.StderrDigest != null)
					{
						BatchReadBlobsResponse.Types.Response? StderrResponse = BatchReadResponse.Responses.FirstOrDefault(x => x.Digest.Hash == ActionResult.StderrDigest.Hash);
						if (StderrResponse != null)
						{
							foreach (string Line in Encoding.UTF8.GetString(StderrResponse.Data.ToByteArray()).Split('\n'))
							{
								if (!string.IsNullOrWhiteSpace(Line))
									Logger.LogError("stderr: {Line}", Line);
							}
						}
					}
					if (OutputDir != null && ActionResult.OutputFiles != null)
					{
						foreach (OutputFile OutputFile in ActionResult.OutputFiles)
						{
							BatchReadBlobsResponse.Types.Response? FileResponse = BatchReadResponse.Responses.FirstOrDefault(x => x.Digest.Hash == OutputFile.Digest.Hash);
							if (FileResponse == null)
							{
								continue;
							}
							string OutputPath = Path.Join(OutputDir, OutputFile.Path);
							System.IO.Directory.CreateDirectory(Path.GetDirectoryName(OutputPath));
							await File.WriteAllBytesAsync(OutputPath, FileResponse.Data.ToByteArray());
							Logger.LogDebug("Wrote {OutputFilePath}", OutputFile.Path);
						}
					}

					Logger.LogDebug("exit: {ExitCode}", ActionResult.ExitCode);
					break;
				}
			}

			return ActionResult;
		}
	}
}
