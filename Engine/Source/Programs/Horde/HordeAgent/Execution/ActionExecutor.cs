// Copyright Epic Games, Inc. All Rights Reserved.

using Build.Bazel.Remote.Execution.V2;
using EpicGames.Core;
using Google.LongRunning;
using Google.Protobuf;
using Grpc.Core;
using Microsoft.Extensions.Options;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Diagnostics;
using Action = Build.Bazel.Remote.Execution.V2.Action;
using Digest = Build.Bazel.Remote.Execution.V2.Digest;
using Directory = Build.Bazel.Remote.Execution.V2.Directory;
using RpcCommand = Build.Bazel.Remote.Execution.V2.Command;
using Status = Google.Rpc.Status;
using System.Text;
using Google.Protobuf.WellKnownTypes;
using Microsoft.Extensions.Hosting;
using System.Collections.Concurrent;
using Grpc.Net.Client;
using HordeAgent.Utility;
using HordeCommon.Rpc.Tasks;
using Microsoft.Extensions.Logging;
using HordeCommon.Rpc;
using EpicGames.Horde.Common.RemoteExecution;

namespace HordeAgent
{
	/// <summary>
	/// Executes remote actions in a sandbox
	/// </summary>
	class ActionExecutor
	{
		/// <summary>
		/// The instance name
		/// </summary>
		string InstanceName;
		
		/// <summary>
		/// The agent name name
		/// </summary>
		string AgentName;
		
		/// <summary>
		/// The storage wrapper
		/// </summary>
		ContentAddressableStorage.ContentAddressableStorageClient Storage;

		/// <summary>
		/// Action class instance
		/// </summary>
		ActionCache.ActionCacheClient Cache;

		/// <summary>
		/// Horde RPC wrapper
		/// </summary>
		internal ActionRpc.ActionRpcClient ActionRpc { get; }

		/// <summary>
		/// Logger for internal executor output
		/// </summary>
		ILogger Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="AgentName"></param>
		/// <param name="InstanceName"></param>
		/// <param name="CasClient"></param>
		/// <param name="CacheClient"></param>
		/// <param name="ActionRpcClient"></param>
		/// <param name="Logger"></param>
		public ActionExecutor(string AgentName, string InstanceName, ContentAddressableStorage.ContentAddressableStorageClient CasClient, ActionCache.ActionCacheClient CacheClient, ActionRpc.ActionRpcClient ActionRpcClient, ILogger Logger)
		{
			this.AgentName = AgentName;
			this.InstanceName = InstanceName;
			this.Storage = CasClient;
			this.Cache = CacheClient;
			this.ActionRpc = ActionRpcClient;
			this.Logger = Logger;
		}

		/// <summary>
		/// Execute an action
		/// </summary>
		/// <param name="LeaseId">The lease id</param>
		/// <param name="ActionTask">Task to execute</param>
		/// <param name="SandboxDir">Directory to use as a sandbox for execution</param>
		/// <param name="ActionTaskStartTime">When the agent received the action task. Used for reporting execution stats in REAPI.</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>The action result</returns>
		public async Task<ActionResult> ExecuteActionAsync(string LeaseId, ActionTask ActionTask, DirectoryReference SandboxDir, DateTimeOffset ActionTaskStartTime, CancellationToken CancellationToken)
		{
			Action? Action = await Storage.GetProtoMessageAsync<Action>(InstanceName, ActionTask.Digest);
			if (Action == null)
			{
				throw new RpcException(new Grpc.Core.Status(StatusCode.NotFound, "Unable to find action message"));
			}
			Logger.LogInformation("Using action {Hash} / {Size} bytes for lease ID {LeaseId}", ActionTask.Digest.Hash, ActionTask.Digest.SizeBytes, LeaseId);

			DirectoryReference.CreateDirectory(SandboxDir);
			FileUtils.ForceDeleteDirectoryContents(SandboxDir);


			DateTimeOffset InputFetchStart = DateTimeOffset.UtcNow;
			Directory InputDirectory = await Storage.GetProtoMessageAsync<Directory>(InstanceName, Action.InputRootDigest);
			await SetupSandboxAsync(InputDirectory, SandboxDir);
			DateTimeOffset InputFetchCompleted = DateTimeOffset.UtcNow;

			RpcCommand Command = await Storage.GetProtoMessageAsync<RpcCommand>(InstanceName, Action.CommandDigest);

			foreach (string OutputFile in Command.OutputFiles)
			{
				FileReference OutputFileLocation = FileReference.Combine(SandboxDir, OutputFile);
				DirectoryReference.CreateDirectory(OutputFileLocation.Directory);
			}

			if (Command.OutputFiles.Count > 0 || Command.OutputDirectories.Count > 0)
			{
				throw new NotImplementedException("OutputFiles/OutputDirectories are deprecated. Use OutputPaths instead.");
			}

			ActionResult Result;
			using (ManagedProcessGroup ProcessGroup = new ManagedProcessGroup())
			{
				string FileName = FileReference.Combine(SandboxDir, Command.WorkingDirectory, Command.Arguments[0]).FullName;
				string Arguments = CommandLineArguments.Join(Command.Arguments.Skip(1));
				string WorkingDirectory = DirectoryReference.Combine(SandboxDir, Command.WorkingDirectory).FullName;

				Dictionary<string, string> NewEnvironment = new Dictionary<string, string>();
				foreach (System.Collections.DictionaryEntry? Entry in Environment.GetEnvironmentVariables())
				{
					NewEnvironment[Entry!.Value.Key.ToString()!] = Entry!.Value.Value!.ToString()!;
				}
				foreach (RpcCommand.Types.EnvironmentVariable EnvironmentVariable in Command.EnvironmentVariables)
				{
					NewEnvironment[EnvironmentVariable.Name] = EnvironmentVariable.Value;
				}

				Logger.LogInformation("Executing {FileName} with arguments {Arguments}", FileName, Arguments);
				DateTimeOffset ExecutionStartTime = DateTimeOffset.UtcNow;
				using (ManagedProcess Process = new ManagedProcess(ProcessGroup, FileName, Arguments, WorkingDirectory, NewEnvironment, ProcessPriorityClass.Normal, ManagedProcessFlags.None))
				{
					byte[] StdOutData;
					byte[] StdErrData;
					using (MemoryStream StdOutStream = new MemoryStream())
					using (MemoryStream StdErrStream = new MemoryStream())
					{
						await Task.WhenAll(Process.StdOut.CopyToAsync(StdOutStream, CancellationToken), Process.StdErr.CopyToAsync(StdErrStream, CancellationToken));
						if (CancellationToken.IsCancellationRequested)
						{
							throw new RpcException(new Grpc.Core.Status(StatusCode.Cancelled, "Action Cancelled"));
						}
						StdOutData = StdOutStream.ToArray();
						StdErrData = StdErrStream.ToArray();
					}
					DateTimeOffset ExecutionCompletedTime = DateTimeOffset.UtcNow;
						

					Result = new ActionResult();
					Result.StdoutDigest = await Storage.PutBulkDataAsync(InstanceName, StdOutData);
					Result.StderrDigest = await Storage.PutBulkDataAsync(InstanceName, StdErrData);
					Result.ExitCode = Process.ExitCode;
					Result.ExecutionMetadata = new ExecutedActionMetadata
					{
						Worker = AgentName,
						QueuedTimestamp = ActionTask.QueueTime,
						ExecutionStartTimestamp = Timestamp.FromDateTimeOffset(ExecutionStartTime),
						ExecutionCompletedTimestamp = Timestamp.FromDateTimeOffset(ExecutionCompletedTime),
						WorkerStartTimestamp = Timestamp.FromDateTimeOffset(ActionTaskStartTime),
						InputFetchStartTimestamp = Timestamp.FromDateTimeOffset(InputFetchStart),
						InputFetchCompletedTimestamp = Timestamp.FromDateTimeOffset(InputFetchCompleted)
					};

					foreach (string Line in Encoding.UTF8.GetString(StdOutData).Split('\n'))
					{
						Logger.LogInformation("stdout: {Line}", Line);
					}
					foreach (string Line in Encoding.UTF8.GetString(StdErrData).Split('\n'))
					{
						Logger.LogInformation("stderr: {Line}", Line);
					}

					Logger.LogInformation("exit: {ExitCode}", Process.ExitCode);

					Result.ExecutionMetadata.OutputUploadStartTimestamp = Timestamp.FromDateTimeOffset(DateTimeOffset.UtcNow);
					foreach (var (FileRef, OutputPath) in ResolveOutputPaths(SandboxDir, Command.OutputPaths))
					{
						byte[] Bytes = await FileReference.ReadAllBytesAsync(FileRef);
						Digest Digest = await Storage.PutBulkDataAsync(InstanceName, Bytes);
				
						OutputFile OutputFileInfo = new OutputFile();
						OutputFileInfo.Path = OutputPath;
						OutputFileInfo.Digest = Digest;
						Result.OutputFiles.Add(OutputFileInfo);
				
						Logger.LogInformation("Uploaded {File} (digest: {Digest}, size: {Size})", FileRef, Digest.Hash, Digest.SizeBytes);
					}
					Result.ExecutionMetadata.OutputUploadCompletedTimestamp = Timestamp.FromDateTimeOffset(DateTimeOffset.UtcNow);
					Result.ExecutionMetadata.WorkerCompletedTimestamp = Timestamp.FromDateTimeOffset(DateTimeOffset.UtcNow);

					PostActionResultRequest PostResultRequest = new PostActionResultRequest();
					PostResultRequest.LeaseId = LeaseId;
					PostResultRequest.ActionDigest = ActionTask.Digest;
					PostResultRequest.Result = Result;
					await ActionRpc.PostActionResultAsync(PostResultRequest);

					await CacheActionResultAsync(Action, ActionTask, Result);

					// Command.OutputPaths
					// Command.OutputDirectories
				}
			}
			return Result;
		}

		async Task CacheActionResultAsync(Action Action, ActionTask ActionTask, ActionResult Result)
		{
			if (!Action.DoNotCache)
			{
				try
				{
					UpdateActionResultRequest Request = new UpdateActionResultRequest();
					Request.InstanceName = InstanceName;
					Request.ActionDigest = ActionTask.Digest;
					Request.ActionResult = Result;
					await Cache.UpdateActionResultAsync(Request);
				}
				catch (Exception e)
				{
					Logger.LogError(e, "Unable to update action result for digest {DigestHash}", ActionTask.Digest.Hash);
				}
			}
		}

		/// <summary>
		/// Downloads files to the sandbox
		/// </summary>
		/// <param name="InputDirectory">The directory spec</param>
		/// <param name="OutputDir">Output directory on disk</param>
		/// <returns>Async task</returns>
		async Task SetupSandboxAsync(Directory InputDirectory, DirectoryReference OutputDir)
		{
			DirectoryReference.CreateDirectory(OutputDir);

			async Task DownloadFile(FileNode FileNode)
			{
				FileReference File = FileReference.Combine(OutputDir, FileNode.Name);
				Logger.LogInformation("Downloading {File} (digest: {Digest}, size: {Size})", File, FileNode.Digest.Hash, FileNode.Digest.SizeBytes);
				ReadOnlyMemory<byte> Data = await Storage.GetBulkDataAsync(InstanceName, FileNode.Digest);
				Logger.LogInformation("Writing {File} (digest: {Digest}, size: {Size})", File, FileNode.Digest.Hash, FileNode.Digest.SizeBytes);
				await FileReference.WriteAllBytesAsync(File, Data.ToArray());
			}
			await Task.WhenAll(InputDirectory.Files.Select(x => Task.Run(() => DownloadFile(x))));
			
			async Task DownloadDir(DirectoryNode DirectoryNode)
			{
				Directory InputSubDirectory = await Storage.GetProtoMessageAsync<Directory>(InstanceName, DirectoryNode.Digest);
				DirectoryReference OutputSubDirectory = DirectoryReference.Combine(OutputDir, DirectoryNode.Name);
				await SetupSandboxAsync(InputSubDirectory, OutputSubDirectory);
			}
			await Task.WhenAll(InputDirectory.Directories.Select(x => Task.Run(() => DownloadDir(x))));
		}

		/// <summary>
		/// Resolves a list of output paths into file references
		///
		/// The REAPI spec allows directories to be specified as an output path which require all sub dirs and files
		/// to be resolved.
		/// </summary>
		/// <param name="SandboxDir">Base directory where execution is taking place</param>
		/// <param name="OutputPaths">List of output paths relative to SandboxDir</param>
		/// <returns>List of resolved paths (incl expanded dirs)</returns>
		internal static List<(FileReference FileRef, string RelativePath)> ResolveOutputPaths(DirectoryReference SandboxDir, IEnumerable<string> OutputPaths)
		{
			var Files = new List<(FileReference FileRef, string RelativePath)>();

			foreach (string OutputPath in OutputPaths)
			{
				DirectoryReference DirRef = DirectoryReference.Combine(SandboxDir, OutputPath);
				if (DirectoryReference.Exists(DirRef))
				{
					IEnumerable<FileReference> ListedFiles = DirectoryReference.EnumerateFiles(DirRef, "*", SearchOption.AllDirectories);
					foreach (FileReference ListedFileRef in ListedFiles)
					{
						Files.Add((ListedFileRef, ListedFileRef.MakeRelativeTo(SandboxDir).Replace("\\", "/")));
					}
				}
				else
				{
					FileReference FileRef = FileReference.Combine(SandboxDir, OutputPath);
					if (FileReference.Exists(FileRef))
					{
						Files.Add((FileRef, OutputPath));
					}
				}
			}

			return Files;
		}
	}
}
