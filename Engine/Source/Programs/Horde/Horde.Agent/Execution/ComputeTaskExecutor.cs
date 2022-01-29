// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde;
using EpicGames.Horde.Storage;
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
using System.Text;
using Google.Protobuf.WellKnownTypes;
using Microsoft.Extensions.Hosting;
using System.Collections.Concurrent;
using Grpc.Net.Client;
using HordeAgent.Utility;
using HordeCommon.Rpc.Tasks;
using Microsoft.Extensions.Logging;
using HordeCommon.Rpc;
using EpicGames.Serialization;
using EpicGames.Horde.Compute;
using EpicGames.Horde.Common;

namespace HordeAgent
{
	/// <summary>
	/// Executes remote actions in a sandbox
	/// </summary>
	class ComputeTaskExecutor
	{
		class OutputTree
		{
			public Dictionary<string, Task<IoHash>> Files = new Dictionary<string, Task<IoHash>>();
			public Dictionary<string, OutputTree> SubDirs = new Dictionary<string, OutputTree>();
		}

		/// <summary>
		/// The blob store
		/// </summary>
		IStorageClient StorageClient;

		/// <summary>
		/// Logger for internal executor output
		/// </summary>
		ILogger Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="StorageClient"></param>
		/// <param name="Logger"></param>
		public ComputeTaskExecutor(IStorageClient StorageClient, ILogger Logger)
		{
			this.StorageClient = StorageClient;
			this.Logger = Logger;
		}

		/// <summary>
		/// Read stdout/stderr and wait until process completes
		/// </summary>
		/// <param name="Process">The process reading from</param>
		/// <param name="Timeout">Max execution timeout for process</param>
		/// <param name="CancelToken">Cancellation token</param>
		/// <returns>stdout and stderr data</returns>
		/// <exception cref="RpcException">Raised for either timeout or cancel</exception>
		private async Task<(byte[] StdOutData, byte[] StdOutErr)> ReadProcessStreams(ManagedProcess Process, TimeSpan Timeout, CancellationToken CancelToken)
		{
			// Read stdout/stderr without cancellation token.
			using MemoryStream StdOutStream = new MemoryStream();
			Task StdOutReadTask = Process.StdOut.CopyToAsync(StdOutStream);

			using MemoryStream StdErrStream = new MemoryStream();
			Task StdErrReadTask = Process.StdErr.CopyToAsync(StdErrStream);

			Task OutputTask = Task.WhenAll(StdOutReadTask, StdErrReadTask);

			// Instead, create a separate task that will wait for either timeout or cancellation
			// as cancel interruptions are not reliable with Stream.CopyToAsync()
			Task TimeoutTask = Task.Delay(Timeout, CancelToken);

			Task WaitTask = await Task.WhenAny(OutputTask, TimeoutTask);
			if (WaitTask == TimeoutTask)
			{
				throw new RpcException(new Grpc.Core.Status(StatusCode.DeadlineExceeded, $"Action timed out after {Timeout.TotalMilliseconds} ms"));
			}
			
			return (StdOutStream.ToArray(), StdErrStream.ToArray());
		}

		/// <summary>
		/// Execute an action
		/// </summary>
		/// <param name="LeaseId">The lease id</param>
		/// <param name="ComputeTaskMessage">Task to execute</param>
		/// <param name="SandboxDir">Directory to use as a sandbox for execution</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>The action result</returns>
		public async Task<ComputeTaskResultMessage> ExecuteAsync(string LeaseId, ComputeTaskMessage ComputeTaskMessage, DirectoryReference SandboxDir, CancellationToken CancellationToken)
		{
			NamespaceId NamespaceId = new NamespaceId(ComputeTaskMessage.NamespaceId);
			BucketId InputBucketId = new BucketId(ComputeTaskMessage.InputBucketId);
			BucketId OutputBucketId = new BucketId(ComputeTaskMessage.OutputBucketId);

			ComputeTask Task = await StorageClient.GetRefAsync<ComputeTask>(NamespaceId, InputBucketId, ComputeTaskMessage.TaskRefId.AsRefId());
			Logger.LogInformation("Executing task {Hash} for lease ID {LeaseId}", ComputeTaskMessage.TaskRefId, LeaseId);

			DirectoryReference.CreateDirectory(SandboxDir);
			FileUtils.ForceDeleteDirectoryContents(SandboxDir);

			DateTimeOffset InputFetchStart = DateTimeOffset.UtcNow;
			DirectoryTree InputDirectory = await StorageClient.ReadObjectAsync<DirectoryTree>(NamespaceId, Task.SandboxHash);
			await SetupSandboxAsync(NamespaceId, InputDirectory, SandboxDir);
			DateTimeOffset InputFetchCompleted = DateTimeOffset.UtcNow;

			using (ManagedProcessGroup ProcessGroup = new ManagedProcessGroup())
			{
				string FileName = FileReference.Combine(SandboxDir, Task.WorkingDirectory.ToString(), Task.Executable.ToString()).FullName;
				string WorkingDirectory = DirectoryReference.Combine(SandboxDir, Task.WorkingDirectory.ToString()).FullName;

				Dictionary<string, string> NewEnvironment = new Dictionary<string, string>();
				foreach (System.Collections.DictionaryEntry? Entry in Environment.GetEnvironmentVariables())
				{
					NewEnvironment[Entry!.Value.Key.ToString()!] = Entry!.Value.Value!.ToString()!;
				}
				foreach(KeyValuePair<Utf8String, Utf8String> Pair in Task.EnvVars)
				{
					NewEnvironment[Pair.Key.ToString()] = Pair.Value.ToString();
				}

				string Arguments = CommandLineArguments.Join(Task.Arguments.Select(x => x.ToString()));
				Logger.LogInformation("Executing {FileName} with arguments {Arguments}", FileName, Arguments);

				TimeSpan Timeout = TimeSpan.FromMinutes(5.0);// Action.Timeout == null ? TimeSpan.FromMinutes(5) : Action.Timeout.ToTimeSpan();
				DateTimeOffset ExecutionStartTime = DateTimeOffset.UtcNow;
				using (ManagedProcess Process = new ManagedProcess(ProcessGroup, FileName, Arguments, WorkingDirectory, NewEnvironment, ProcessPriorityClass.Normal, ManagedProcessFlags.None))
				{
					(byte[] StdOutData, byte[] StdErrData) = await ReadProcessStreams(Process, Timeout, CancellationToken);

					foreach (string Line in Encoding.UTF8.GetString(StdOutData).Split('\n'))
					{
						Logger.LogInformation("stdout: {Line}", Line);
					}
					foreach (string Line in Encoding.UTF8.GetString(StdErrData).Split('\n'))
					{
						Logger.LogInformation("stderr: {Line}", Line);
					}

					Logger.LogInformation("exit: {ExitCode}", Process.ExitCode);

					ComputeTaskResult Result = new ComputeTaskResult(Process.ExitCode);
					Result.StdOutHash = await StorageClient.WriteBlobFromMemoryAsync(NamespaceId, StdOutData);
					Result.StdErrHash = await StorageClient.WriteBlobFromMemoryAsync(NamespaceId, StdErrData);

					FileReference[] OutputFiles = ResolveOutputPaths(SandboxDir, Task.OutputPaths.Select(x => x.ToString())).OrderBy(x => x.FullName, StringComparer.Ordinal).ToArray();
					if (OutputFiles.Length > 0)
					{
						foreach (FileReference OutputFile in OutputFiles)
						{
							Logger.LogInformation("output: {File}", OutputFile.MakeRelativeTo(SandboxDir));
						}
						Result.OutputHash = await PutOutput(NamespaceId, SandboxDir, OutputFiles);
					}

					CbObject ResultObject = CbSerializer.Serialize(Result);

					await StorageClient.SetRefAsync(NamespaceId, OutputBucketId, ComputeTaskMessage.TaskRefId, ResultObject);
					return new ComputeTaskResultMessage(ComputeTaskMessage.TaskRefId);
				}
			}
		}

		/// <summary>
		/// Downloads files to the sandbox
		/// </summary>
		/// <param name="NamespaceId">Namespace for fetching data</param>
		/// <param name="InputDirectory">The directory spec</param>
		/// <param name="OutputDir">Output directory on disk</param>
		/// <returns>Async task</returns>
		async Task SetupSandboxAsync(NamespaceId NamespaceId, DirectoryTree InputDirectory, DirectoryReference OutputDir)
		{
			DirectoryReference.CreateDirectory(OutputDir);

			async Task DownloadFile(FileNode FileNode)
			{
				FileReference File = FileReference.Combine(OutputDir, FileNode.Name.ToString());
				Logger.LogInformation("Downloading {File} (digest: {Digest})", File, FileNode.Hash);
				byte[] Data = await StorageClient.ReadBlobToMemoryAsync(NamespaceId, FileNode.Hash);
				Logger.LogInformation("Writing {File} (digest: {Digest})", File, FileNode.Hash);
				await FileReference.WriteAllBytesAsync(File, Data);
			}

			async Task DownloadDir(DirectoryNode DirectoryNode)
			{
				DirectoryTree InputSubDirectory = await StorageClient.ReadObjectAsync<DirectoryTree>(NamespaceId, DirectoryNode.Hash);
				DirectoryReference OutputSubDirectory = DirectoryReference.Combine(OutputDir, DirectoryNode.Name.ToString());
				await SetupSandboxAsync(NamespaceId, InputSubDirectory, OutputSubDirectory);
			}

			List<Task> Tasks = new List<Task>();
			Tasks.AddRange(InputDirectory.Files.Select(x => Task.Run(() => DownloadFile(x))));
			Tasks.AddRange(InputDirectory.Directories.Select(x => Task.Run(() => DownloadDir(x))));
			await Task.WhenAll(Tasks);
		}

		async Task<IoHash> PutOutput(NamespaceId NamespaceId, DirectoryReference BaseDir, IEnumerable<FileReference> Files)
		{
			List<FileReference> SortedFiles = Files.OrderBy(x => x.FullName, StringComparer.Ordinal).ToList();
			(_, IoHash Hash) = await PutDirectoryTree(NamespaceId, BaseDir.FullName.Length, SortedFiles, 0, SortedFiles.Count);
			return Hash;
		}

		async Task<(DirectoryTree, IoHash)> PutDirectoryTree(NamespaceId NamespaceId, int BaseDirLen, List<FileReference> SortedFiles, int MinIdx, int MaxIdx)
		{
			List<Task<FileNode>> Files = new List<Task<FileNode>>();
			List<Task<DirectoryNode>> Trees = new List<Task<DirectoryNode>>();

			while (MinIdx < MaxIdx)
			{
				FileReference File = SortedFiles[MinIdx];

				int NextMinIdx = MinIdx + 1;

				int NextDirLen = File.FullName.IndexOf(Path.DirectorySeparatorChar, BaseDirLen + 1);
				if (NextDirLen == -1)
				{
					string Name = File.FullName.Substring(BaseDirLen + 1);
					Files.Add(CreateFileNode(NamespaceId, Name, File));
				}
				else
				{
					string Name = File.FullName.Substring(BaseDirLen + 1, NextDirLen - (BaseDirLen + 1));
					while (NextMinIdx < MaxIdx)
					{
						string NextFile = SortedFiles[NextMinIdx].FullName;
						if (NextFile.Length < NextDirLen || String.Compare(Name, 0, NextFile, BaseDirLen, Name.Length, StringComparison.Ordinal) == 0)
						{
							break;
						}
						NextMinIdx++;
					}
					Trees.Add(CreateDirectoryNode(NamespaceId, Name, NextDirLen, SortedFiles, MinIdx, NextMinIdx));
				}

				MinIdx = NextMinIdx;
			}

			DirectoryTree Tree = new DirectoryTree();
			Tree.Files.AddRange(await Task.WhenAll(Files));
			Tree.Directories.AddRange(await Task.WhenAll(Trees));

			IoHash Hash = await StorageClient.WriteObjectAsync<DirectoryTree>(NamespaceId, Tree);
			return (Tree, Hash);
		}

		async Task<DirectoryNode> CreateDirectoryNode(NamespaceId NamespaceId, string Name, int BaseDirLen, List<FileReference> SortedFiles, int MinIdx, int MaxIdx)
		{
			(_, IoHash Hash) = await PutDirectoryTree(NamespaceId, BaseDirLen, SortedFiles, MinIdx, MaxIdx);
			return new DirectoryNode(Name, Hash);
		}

		async Task<FileNode> CreateFileNode(NamespaceId NamespaceId, string Name, FileReference File)
		{
			byte[] Data = await FileReference.ReadAllBytesAsync(File);
			IoHash Hash = await StorageClient.WriteBlobFromMemoryAsync(NamespaceId, Data);
			return new FileNode(Name, Hash, Data.Length, (int)FileReference.GetAttributes(File));
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
		internal static HashSet<FileReference> ResolveOutputPaths(DirectoryReference SandboxDir, IEnumerable<string> OutputPaths)
		{
			HashSet<FileReference> Files = new HashSet<FileReference>();
			foreach (string OutputPath in OutputPaths)
			{
				DirectoryReference DirRef = DirectoryReference.Combine(SandboxDir, OutputPath);
				if (DirectoryReference.Exists(DirRef))
				{
					IEnumerable<FileReference> ListedFiles = DirectoryReference.EnumerateFiles(DirRef, "*", SearchOption.AllDirectories);
					foreach (FileReference ListedFileRef in ListedFiles)
					{
						Files.Add(ListedFileRef);
					}
				}
				else
				{
					FileReference FileRef = FileReference.Combine(SandboxDir, OutputPath);
					if (FileReference.Exists(FileRef))
					{
						Files.Add(FileRef);
					}
				}
			}
			return Files;
		}
	}
}
