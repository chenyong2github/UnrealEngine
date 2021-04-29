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

namespace HordeAgent
{
	/// <summary>
	/// Executes remote actions in a sandbox
	/// </summary>
	class ActionExecutor
	{
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
		ActionRpc.ActionRpcClient ActionRpc;

		/// <summary>
		/// Logger for internal executor output
		/// </summary>
		ILogger Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Channel"></param>
		/// <param name="Logger"></param>
		public ActionExecutor(GrpcChannel Channel, ILogger Logger)
		{
			this.Storage = new ContentAddressableStorage.ContentAddressableStorageClient(Channel);
			this.Cache = new ActionCache.ActionCacheClient(Channel);
			this.ActionRpc = new ActionRpc.ActionRpcClient(Channel);
			this.Logger = Logger;
		}

		/// <summary>
		/// Execute an action
		/// </summary>
		/// <param name="LeaseId">The lease id</param>
		/// <param name="ActionTask">Task to execute</param>
		/// <param name="SandboxDir">Directory to use as a sandbox for execution</param>
		/// <returns>The action result</returns>
		public async Task<ActionResult> ExecuteActionAsync(string LeaseId, ActionTask ActionTask, DirectoryReference SandboxDir)
		{
			Action? Action = await Storage.GetProtoMessageAsync<Action>(ActionTask.Digest);
			if (Action == null)
			{
				throw new RpcException(new Grpc.Core.Status(StatusCode.NotFound, "Unable to find action message"));
			}

			DirectoryReference.CreateDirectory(SandboxDir);
			FileUtils.ForceDeleteDirectoryContents(SandboxDir);

			Directory InputDirectory = await Storage.GetProtoMessageAsync<Directory>(Action.InputRootDigest);
			await SetupSandboxAsync(InputDirectory, SandboxDir);

			RpcCommand Command = await Storage.GetProtoMessageAsync<RpcCommand>(Action.CommandDigest);

			foreach (string OutputFile in Command.OutputFiles)
			{
				FileReference OutputFileLocation = FileReference.Combine(SandboxDir, OutputFile);
				DirectoryReference.CreateDirectory(OutputFileLocation.Directory);
			}

			if (Command.OutputPaths.Count > 0 || Command.OutputDirectories.Count > 0)
			{
				throw new NotImplementedException();
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
				using (ManagedProcess Process = new ManagedProcess(ProcessGroup, FileName, Arguments, WorkingDirectory, NewEnvironment, ProcessPriorityClass.Normal, ManagedProcessFlags.None))
				{
					byte[] StdOutData;
					byte[] StdErrData;
					using (MemoryStream StdOutStream = new MemoryStream())
					using (MemoryStream StdErrStream = new MemoryStream())
					{
						await Task.WhenAll(Process.StdOut.CopyToAsync(StdOutStream), Process.StdErr.CopyToAsync(StdErrStream));
						StdOutData = StdOutStream.ToArray();
						StdErrData = StdErrStream.ToArray();
					}

					Result = new ActionResult();
					Result.StdoutDigest = await Storage.PutBulkDataAsync(StdOutData);
					Result.StderrDigest = await Storage.PutBulkDataAsync(StdErrData);
					Result.ExitCode = Process.ExitCode;

					foreach (string Line in Encoding.UTF8.GetString(StdOutData).Split('\n'))
					{
						Logger.LogInformation("stdout: {Line}", Line);
					}
					foreach (string Line in Encoding.UTF8.GetString(StdErrData).Split('\n'))
					{
						Logger.LogInformation("stderr: {Line}", Line);
					}

					Logger.LogInformation("exit: {ExitCode}", Process.ExitCode);

					foreach (string OutputFile in Command.OutputFiles)
					{
						FileReference FileRef = FileReference.Combine(SandboxDir, OutputFile);
						if (FileReference.Exists(FileRef))
						{
							byte[] Bytes = await FileReference.ReadAllBytesAsync(FileRef);
							Digest Digest = await Storage.PutBulkDataAsync(Bytes);

							OutputFile OutputFileInfo = new OutputFile();
							OutputFileInfo.Path = OutputFile;
							OutputFileInfo.Digest = Digest;
							Result.OutputFiles.Add(OutputFileInfo);

							Logger.LogInformation("Uploaded {File} (digest: {Digest}, size: {Size})", FileRef, Digest.Hash, Digest.SizeBytes);
						}
					}

					PostActionResultRequest PostResultRequest = new PostActionResultRequest();
					PostResultRequest.LeaseId = LeaseId;
					PostResultRequest.ActionDigest = ActionTask.Digest;
					PostResultRequest.Result = Result;
					await ActionRpc.PostActionResultAsync(PostResultRequest);

					if (!Action.DoNotCache)
					{
						UpdateActionResultRequest Request = new UpdateActionResultRequest();
						Request.ActionDigest = ActionTask.Digest;
						Request.ActionResult = Result;
						await Cache.UpdateActionResultAsync(Request);
					}

					// Command.OutputPaths
					// Command.OutputDirectories
				}
			}
			return Result;
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

			foreach (FileNode FileNode in InputDirectory.Files)
			{
				ReadOnlyMemory<byte> Data = await Storage.GetBulkDataAsync(FileNode.Digest);
				FileReference File = FileReference.Combine(OutputDir, FileNode.Name);
				Logger.LogInformation("Writing {File} (digest: {Digest}, size: {Size})", File, FileNode.Digest.Hash, FileNode.Digest.SizeBytes);
				await FileReference.WriteAllBytesAsync(File, Data.ToArray());
			}

			foreach (DirectoryNode DirectoryNode in InputDirectory.Directories)
			{
				Directory InputSubDirectory = await Storage.GetProtoMessageAsync<Directory>(DirectoryNode.Digest);
				DirectoryReference OutputSubDirectory = DirectoryReference.Combine(OutputDir, DirectoryNode.Name);
				await SetupSandboxAsync(InputSubDirectory, OutputSubDirectory);
			}
		}
	}
}
