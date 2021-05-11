// Copyright Epic Games, Inc. All Rights Reserved.

using Build.Bazel.Remote.Execution.V2;
using Google.Protobuf;
using HordeAgent.Utility;
using Microsoft.Extensions.Logging;
using System;
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

namespace HordeAgent.Commands
{
	/// <summary>
	/// Installs the agent as a service
	/// </summary>
	[Command("Execute", "Executes a command through the remote execution API")]
	class ExecuteCommand : Command
	{
		/// <summary>
		/// List of files that need to be uploaded
		/// </summary>
		class UploadList
		{
			public Dictionary<string, BatchUpdateBlobsRequest.Types.Request> Blobs = new Dictionary<string, BatchUpdateBlobsRequest.Types.Request>();

			public Digest Add<T>(IMessage<T> Message) where T : IMessage<T>
			{
				ByteString ByteString = Message.ToByteString();

				Digest Digest = StorageExtensions.GetDigest(ByteString);
				if (!Blobs.ContainsKey(Digest.Hash))
				{
					BatchUpdateBlobsRequest.Types.Request Request = new BatchUpdateBlobsRequest.Types.Request();
					Request.Data = ByteString;
					Request.Digest = Digest;
					Blobs.Add(Digest.Hash, Request);
				}
				return Digest;
			}

			public async Task<Digest> AddFileAsync(string LocalFile)
			{
				BatchUpdateBlobsRequest.Types.Request Content = new BatchUpdateBlobsRequest.Types.Request();
				using (FileStream Stream = File.Open(LocalFile, FileMode.Open, FileAccess.Read))
				{
					Content.Data = await ByteString.FromStreamAsync(Stream);
					Content.Digest = StorageExtensions.GetDigest(Content.Data);
				}
				Blobs[Content.Digest.Hash] = Content;
				return Content.Digest;
			}
		}

		class JsonFileNode
		{
			public string Name { get; set; } = String.Empty;
			public string LocalFile { get; set; } = String.Empty;

			public async Task<FileNode> BuildAsync(UploadList UploadList)
			{
				FileNode Node = new FileNode();
				Node.Name = String.IsNullOrEmpty(Name) ? Path.GetFileName(LocalFile) : Name;
				Node.Digest = await UploadList.AddFileAsync(LocalFile);
				return Node;
			}
		}

		class JsonDirectory
		{
			public List<JsonFileNode> Files { get; set; } = new List<JsonFileNode>();
			public List<JsonDirectoryNode> Directories { get; set; } = new List<JsonDirectoryNode>();

			public async Task<Digest> BuildAsync(UploadList UploadList)
			{
				Directory Directory = new Directory();
				foreach (JsonFileNode File in Files)
				{
					Directory.Files.Add(await File.BuildAsync(UploadList));
				}
				foreach (JsonDirectoryNode DirectoryNode in Directories)
				{
					Directory.Directories.Add(await DirectoryNode.BuildAsync(UploadList));
				}
				return UploadList.Add(Directory);
			}
		}

		class JsonDirectoryNode : JsonDirectory
		{
			public string Name { get; set; } = String.Empty;

			public new async Task<DirectoryNode> BuildAsync(UploadList UploadList)
			{
				DirectoryNode Node = new DirectoryNode();
				Node.Name = Name;
				Node.Digest = await base.BuildAsync(UploadList);
				return Node;
			}
		}

		class JsonCommand
		{
			public List<string> Arguments { get; set; } = new List<string>();
			public string WorkingDirectory { get; set; } = String.Empty;

			public Digest Build(UploadList UploadList)
			{
				RpcCommand Command = new RpcCommand();
				Command.Arguments.Add(Arguments);
				Command.WorkingDirectory = WorkingDirectory;
				return UploadList.Add(Command);
			}
		}

		class JsonAction
		{
			public JsonCommand Command { get; set; } = new JsonCommand();
			public JsonDirectory Workspace { get; set; } = new JsonDirectory();

			public async Task<Digest> BuildAsync(UploadList UploadList, ByteString? Salt)
			{
				Action NewAction = new Action();
				NewAction.CommandDigest = Command.Build(UploadList);
				NewAction.InputRootDigest = await Workspace.BuildAsync(UploadList);
				NewAction.Salt = Salt;
				return UploadList.Add(NewAction);
			}
		}

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

		/// <inheritdoc/>
		public override async Task<int> ExecuteAsync(ILogger Logger)
		{
			IConfiguration Configuration = new ConfigurationBuilder()
				.AddEnvironmentVariables()
				.AddJsonFile("appsettings.json")
				.Build();

			IConfigurationSection ConfigSection = Configuration.GetSection(AgentSettings.SectionName);
			IHostBuilder HostBuilder = Host.CreateDefaultBuilder()
				.ConfigureLogging(Builder => { })
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
				Digest ActionDigest = await Action.BuildAsync(UploadList, SaltBytes);

				using (GrpcChannel Channel = GrpcService.CreateGrpcChannel())
				{
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
						foreach (Digest MissingBlobDigest in MissingBlobs.MissingBlobDigests)
						{
							BatchUpdateBlobsRequest UpdateRequest = new BatchUpdateBlobsRequest();
							UpdateRequest.InstanceName = InstanceName;
							UpdateRequest.Requests.Add(UploadList.Blobs[MissingBlobDigest.Hash]);
							BatchUpdateBlobsResponse UpdateResponse = await StorageClient.BatchUpdateBlobsAsync(UpdateRequest);
							
							bool UpdateFailed = false;
							foreach (var Response in UpdateResponse.Responses)
							{
								if (Response.Status.Code != (int) Google.Rpc.Code.Ok)
								{
									Console.WriteLine($"Upload failed for {Response.Digest}. Code: {Response.Status.Code} Message: {Response.Status.Message}");
									UpdateFailed = true;
								}
							}

							if (UpdateFailed)
							{
								throw new Exception("Failed updating blobs in CAS service");
							}
						}
					}

					// Execute the action
					RpcExecution.ExecutionClient ExecutionClient = new RpcExecution.ExecutionClient(Channel);

					ExecuteRequest ExecuteRequest = new ExecuteRequest();
					ExecuteRequest.ActionDigest = ActionDigest;
					if (InstanceName != null)
					{
						ExecuteRequest.InstanceName = InstanceName;
					}

					using (AsyncServerStreamingCall<Operation> Call = ExecutionClient.Execute(ExecuteRequest))
					{
						while (await Call.ResponseStream.MoveNext())
						{
							Operation Operation = Call.ResponseStream.Current;

							ExecuteOperationMetadata Metadata = Operation.Metadata.Unpack<ExecuteOperationMetadata>();
							Logger.LogInformation("Execution state: {State}", Metadata.Stage);

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

								ActionResult? ActionResult = ExecuteResponse.Result;
								if (ExecuteResponse.Result == null)
								{
									Logger.LogError("Empty result");
									break;
								}

								Logger.LogInformation("Cached: {CachedResult}", ExecuteResponse.CachedResult);

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

								BatchReadBlobsResponse BatchReadResponse = await StorageClient.BatchReadBlobsAsync(BatchReadRequest);
								if (ActionResult.StdoutDigest != null)
								{
									BatchReadBlobsResponse.Types.Response? StdoutResponse = BatchReadResponse.Responses.FirstOrDefault(x => x.Digest.Hash == ActionResult.StdoutDigest.Hash);
									if (StdoutResponse != null && StdoutResponse.Data.Length > 0)
									{
										foreach (string Line in Encoding.UTF8.GetString(StdoutResponse.Data.ToByteArray()).Split('\n'))
										{
											Logger.LogInformation("stdout: {Line}", Line);
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
											Logger.LogError("stderr: {Line}", Line);
										}
									}
								}

								Logger.LogInformation("exit: {ExitCode}", ActionResult.ExitCode);
								break;
							}
						}
					}
				}
			}
			return 0;
		}
	}
}
