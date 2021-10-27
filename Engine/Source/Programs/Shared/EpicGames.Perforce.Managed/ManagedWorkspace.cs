// Copyright Epic Games, Inc. All Rights Reserved.

using HordeAgent.Utility;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Perforce;
using System.Runtime.InteropServices;
using System.Runtime.Serialization.Json;
using System.Text.RegularExpressions;

namespace EpicGames.Perforce.Managed
{
	/// <summary>
	/// Exception thrown when there is not enough free space on the drive
	/// </summary>
	public class InsufficientSpaceException : Exception
	{
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Message">Error message</param>
		public InsufficientSpaceException(string Message)
			: base(Message)
		{
		}
	}

	/// <summary>
	/// Information about a populate request
	/// </summary>
	public class PopulateRequest
	{
		/// <summary>
		/// The Perforce connection
		/// </summary>
		public PerforceClientConnection PerforceClient;

		/// <summary>
		/// Stream to sync it to
		/// </summary>
		public string StreamName;

		/// <summary>
		/// View for this client
		/// </summary>
		public IReadOnlyList<string> View;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="PerforceClient">The perforce connection</param>
		/// <param name="StreamName">Stream to be synced</param>
		/// <param name="View">List of filters for the stream</param>
		public PopulateRequest(PerforceClientConnection PerforceClient, string StreamName, IReadOnlyList<string> View)
		{
			this.PerforceClient = PerforceClient;
			this.StreamName = StreamName;
			this.View = View;
		}
	}

	/// <summary>
	/// Version number for managed workspace cache files
	/// </summary>
	enum ManagedWorkspaceVersion
	{
		/// <summary>
		/// Initial version number
		/// </summary>
		Initial = 2,

		/// <summary>
		/// Including stream directory digests in workspace directories
		/// </summary>
		AddDigest = 3,

		/// <summary>
		/// Changing hash algorithm from SHA1 to IoHash
		/// </summary>
		AddDigestIoHash = 4,
	}

	/// <summary>
	/// Represents a repository of streams and cached data
	/// </summary>
	public class ManagedWorkspace
	{
		/// <summary>
		/// The current transaction state. Used to determine whether a repository needs to be cleaned on startup.
		/// </summary>
		enum TransactionState
		{
			Dirty,
			Clean,
		}

		/// <summary>
		/// The file signature and version. Update this to introduce breaking changes and ignore old repositories.
		/// </summary>
		const int CurrentSignature = ('W' << 24) | ('T' << 16) | 2;

		/// <summary>
		/// The current revision number for cache archives.
		/// </summary>
		static int CurrentVersion { get; } = Enum.GetValues(typeof(ManagedWorkspaceVersion)).Cast<int>().Max();

		/// <summary>
		/// Maximum number of threads to sync in parallel
		/// </summary>
		const int NumParallelSyncThreads = 4;

		/// <summary>
		/// Minimum amount of space that must be on a drive after a branch is synced
		/// </summary>
		const long MinScratchSpace = 50L * 1024 * 1024 * 1024;

		/// <summary>
		/// Constant for syncing the latest change number
		/// </summary>
		public const int LatestChangeNumber = -1;

		/// <summary>
		/// Name of the signature file for a repository. This 
		/// </summary>
		const string SignatureFileName = "Repository.sig";

		/// <summary>
		/// Name of the main data file for a repository
		/// </summary>
		const string DataFileName = "Repository.dat";

		/// <summary>
		/// Name of the host
		/// </summary>
		string HostName;

		/// <summary>
		/// Incrementing number assigned to sequential operations that modify files. Used to age out files in the cache.
		/// </summary>
		uint NextSequenceNumber;

		/// <summary>
		/// Whether a repair operation should be run on this workspace. Set whenever the state may be inconsistent.
		/// </summary>
		bool bRequiresRepair;

		/// <summary>
		/// The log output device
		/// </summary>
		readonly ILogger Logger;

		/// <summary>
		/// The root directory for the stash
		/// </summary>
		readonly DirectoryReference BaseDir;

		/// <summary>
		/// Root directory for storing cache files
		/// </summary>
		readonly DirectoryReference CacheDir;

		/// <summary>
		/// Root directory for storing workspace files
		/// </summary>
		readonly DirectoryReference WorkspaceDir;

		/// <summary>
		/// Set of clients that we're created. Used to avoid updating multiple times during one run.
		/// </summary>
		Dictionary<string, ClientRecord> CreatedClients = new Dictionary<string, ClientRecord>();

		/// <summary>
		/// Set of unique cache entries. We use this to ensure new names in the cache are unique.
		/// </summary>
		HashSet<ulong> CacheEntries = new HashSet<ulong>();

		/// <summary>
		/// List of all the staged files
		/// </summary>
		WorkspaceDirectoryInfo Workspace;

		/// <summary>
		/// All the files which are currently being tracked
		/// </summary>
		Dictionary<FileContentId, CachedFileInfo> ContentIdToTrackedFile = new Dictionary<FileContentId, CachedFileInfo>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="HostName">Name of the current host</param>
		/// <param name="NextSequenceNumber">The next sequence number for operations</param>
		/// <param name="BaseDir">The root directory for the stash</param>
		/// <param name="Logger">The log output device</param>
		private ManagedWorkspace(string HostName, uint NextSequenceNumber, DirectoryReference BaseDir, ILogger Logger)
		{
			// Save the Perforce settings
			this.HostName = HostName;
			this.NextSequenceNumber = NextSequenceNumber;
			this.Logger = Logger;

			// Get all the directories
			this.BaseDir = BaseDir;
			DirectoryReference.CreateDirectory(BaseDir);

			this.CacheDir = DirectoryReference.Combine(BaseDir, "Cache");
			DirectoryReference.CreateDirectory(CacheDir);

			this.WorkspaceDir = DirectoryReference.Combine(BaseDir, "Sync");
			DirectoryReference.CreateDirectory(WorkspaceDir);

			// Create the workspace
			this.Workspace = new WorkspaceDirectoryInfo(WorkspaceDir);
		}

		/// <summary>
		/// Loads a repository from the given directory, or create it if it doesn't exist
		/// </summary>
		/// <param name="HostName">Name of the current machine. Will be automatically detected from the host settings if not present.</param>
		/// <param name="BaseDir">The base directory for the repository</param>
		/// <param name="bOverwrite">Whether to allow overwriting a repository that's not up to date</param>
		/// <param name="Logger">The logging interface</param>
		/// <param name="CancellationToken">Cancellation token for this operation</param>
		/// <returns></returns>
		public static async Task<ManagedWorkspace> LoadOrCreateAsync(string HostName, DirectoryReference BaseDir, bool bOverwrite, ILogger Logger, CancellationToken CancellationToken)
		{
			if (Exists(BaseDir))
			{
				try
				{
					return await LoadAsync(HostName, BaseDir, Logger, CancellationToken);
				}
				catch (Exception Ex)
				{
					if (bOverwrite)
					{
						Logger.LogWarning(Ex, "Unable to load existing repository.");
					}
					else
					{
						throw;
					}

				}
			}

			return await CreateAsync(HostName, BaseDir, Logger, CancellationToken);
		}
/*
		public static PerforceConnection GetPerforceConnection(PerforceConnection Perforce)
		{
			if (Perforce.UserName == null || HostName == null)
			{
				InfoRecord ServerInfo = await Perforce.GetInfoAsync(InfoOptions.ShortOutput, CancellationToken);
				if (Perforce.UserName == null)
				{
					Perforce = new PerforceConnection(Perforce) { UserName = ServerInfo.UserName };
				}
				if (HostName == null)
				{
					if (ServerInfo.ClientHost == null)
					{
						throw new Exception("Unable to determine host name");
					}
					else
					{
						HostName = ServerInfo.ClientHost;
					}
				}
			}
			return Perforce;
		}
*/

		/// <summary>
		/// Creates a repository at the given location
		/// </summary>
		/// <param name="HostName">Name of the current machine.</param>
		/// <param name="BaseDir">The base directory for the repository</param>
		/// <param name="Logger">The log output device</param>
		/// <param name="CancellationToken">Cancellation token for this operation</param>
		/// <returns>New repository instance</returns>
		public static async Task<ManagedWorkspace> CreateAsync(string HostName, DirectoryReference BaseDir, ILogger Logger, CancellationToken CancellationToken)
		{
			Logger.LogInformation("Creating repository at {Location}...", BaseDir);

			// Make sure all the fields are valid
			DirectoryReference.CreateDirectory(BaseDir);
			FileUtils.ForceDeleteDirectoryContents(BaseDir);

			ManagedWorkspace Repo = new ManagedWorkspace(HostName, 1, BaseDir, Logger);
			await Repo.SaveAsync(TransactionState.Clean, CancellationToken);
			Repo.CreateCacheHierarchy();

			FileReference SignatureFile = FileReference.Combine(BaseDir, SignatureFileName);
			using(BinaryWriter Writer = new BinaryWriter(File.Open(SignatureFile.FullName, FileMode.Create, FileAccess.Write, FileShare.Read)))
			{
				Writer.Write(CurrentSignature);
			}

			return Repo;
		}

		/// <summary>
		/// Tests whether a repository exists in the given directory
		/// </summary>
		/// <param name="BaseDir"></param>
		/// <returns></returns>
		public static bool Exists(DirectoryReference BaseDir)
		{
			FileReference SignatureFile = FileReference.Combine(BaseDir, SignatureFileName);
			if(FileReference.Exists(SignatureFile))
			{
				using(BinaryReader Reader = new BinaryReader(File.Open(SignatureFile.FullName, FileMode.Open, FileAccess.Read, FileShare.Read)))
				{
					int Signature = Reader.ReadInt32();
					if(Signature == CurrentSignature)
					{
						return true;
					}
				}
			}
			return false;
		}

		/// <summary>
		/// Loads a repository from disk
		/// </summary>
		/// <param name="HostName">Name of the current host. Will be obtained from a 'p4 info' call if not specified</param>
		/// <param name="BaseDir">The base directory for the repository</param>
		/// <param name="Logger">The log output device</param>
		/// <param name="CancellationToken">Cancellation token for this command</param>
		public static async Task<ManagedWorkspace> LoadAsync(string HostName, DirectoryReference BaseDir, ILogger Logger, CancellationToken CancellationToken)
		{
			if(!Exists(BaseDir))
			{
				throw new FatalErrorException("No valid repository found at {0}", BaseDir);
			}

			FileReference DataFile = FileReference.Combine(BaseDir, DataFileName);
			RestoreBackup(DataFile);

			byte[] Data = await FileReference.ReadAllBytesAsync(DataFile, CancellationToken);
			MemoryReader Reader = new MemoryReader(Data.AsMemory());

			int Version = Reader.ReadInt32();
			if(Version > CurrentVersion)
			{
				throw new FatalErrorException("Unsupported data format (version {0}, current {1})", Version, CurrentVersion);
			}

			bool bRequiresRepair = Reader.ReadBoolean();
			uint NextSequenceNumber = Reader.ReadUInt32();

			ManagedWorkspace Repo = new ManagedWorkspace(HostName, NextSequenceNumber, BaseDir, Logger);
			Repo.bRequiresRepair = bRequiresRepair;

			int NumTrackedFiles = Reader.ReadInt32();
			for(int Idx = 0; Idx < NumTrackedFiles; Idx++)
			{
				CachedFileInfo TrackedFile = Reader.ReadCachedFileInfo(Repo.CacheDir);
				Repo.ContentIdToTrackedFile.Add(TrackedFile.ContentId, TrackedFile);
				Repo.CacheEntries.Add(TrackedFile.CacheId);
			}

			Reader.ReadWorkspaceDirectoryInfo(Repo.Workspace, (ManagedWorkspaceVersion)Version);

			await Repo.RunOptionalRepairAsync(CancellationToken);
			return Repo;
		}

		/// <summary>
		/// Save the state of the repository
		/// </summary>
		private async Task SaveAsync(TransactionState State, CancellationToken CancellationToken)
		{
			// Allocate the buffer for writing
			int SerializedSize = sizeof(int) + sizeof(byte) + sizeof(int) + sizeof(int) + ContentIdToTrackedFile.Values.Sum(x => x.GetSerializedSize()) + Workspace.GetSerializedSize();
			byte[] Buffer = new byte[SerializedSize];

			// Write the data to memory
			MemoryWriter Writer = new MemoryWriter(Buffer.AsMemory());
			Writer.WriteInt32(CurrentVersion);
			Writer.WriteBoolean(bRequiresRepair || (State != TransactionState.Clean));
			Writer.WriteUInt32(NextSequenceNumber);
			Writer.WriteInt32(ContentIdToTrackedFile.Count);
			foreach (CachedFileInfo TrackedFile in ContentIdToTrackedFile.Values)
			{
				Writer.WriteCachedFileInfo(TrackedFile);
			}
			Writer.WriteWorkspaceDirectoryInfo(Workspace);
			Writer.CheckOffset(SerializedSize);

			// Write it to disk
			FileReference DataFile = FileReference.Combine(BaseDir, DataFileName);
			BeginTransaction(DataFile);
			await FileReference.WriteAllBytesAsync(DataFile, Buffer, CancellationToken);
			CompleteTransaction(DataFile);
		}

		#region Commands

		/// <summary>
		/// Cleans the current workspace
		/// </summary>
		/// <param name="bRemoveUntracked">Whether to remove untracked files</param>
		/// <param name="CancellationToken">Cancellation token</param>
		public async Task CleanAsync(bool bRemoveUntracked, CancellationToken CancellationToken)
		{
			Stopwatch Timer = Stopwatch.StartNew();

			Logger.LogInformation("Cleaning workspace...");
			using (Logger.BeginIndentScope("  "))
			{
				await CleanInternalAsync(bRemoveUntracked, CancellationToken);
			}

			Logger.LogInformation("Completed in {ElapsedTime:0.0}s", Timer.Elapsed.TotalSeconds);
		}

		/// <summary>
		/// Cleans the current workspace
		/// </summary>
		/// <param name="bRemoveUntracked">Whether to remove untracked files</param>
		/// <param name="CancellationToken">Cancellation token</param>
		private async Task CleanInternalAsync(bool bRemoveUntracked, CancellationToken CancellationToken)
		{
			FileInfo[] FilesToDelete;
			DirectoryInfo[] DirectoriesToDelete;
			using (Trace("FindFilesToClean"))
			using (ILoggerProgress Status = Logger.BeginProgressScope("Finding files to clean..."))
			{
				Stopwatch Timer = Stopwatch.StartNew();

				Workspace.Refresh(bRemoveUntracked, out FilesToDelete, out DirectoriesToDelete);

				Status.Progress = $"({Timer.Elapsed.TotalSeconds:0.0}s)";
			}

			if(FilesToDelete.Length > 0 || DirectoriesToDelete.Length > 0)
			{
				List<string> Paths = new List<string>();
				Paths.AddRange(DirectoriesToDelete.Select(x => String.Format("/{0}/...", new DirectoryReference(x).MakeRelativeTo(WorkspaceDir).Replace(Path.DirectorySeparatorChar, '/'))));
				Paths.AddRange(FilesToDelete.Select(x => String.Format("/{0}", new FileReference(x).MakeRelativeTo(WorkspaceDir).Replace(Path.DirectorySeparatorChar, '/'))));

				const int MaxDisplay = 1000;
				foreach (string Path in Paths.OrderBy(x => x).Take(MaxDisplay))
				{
					Logger.LogInformation($"  {Path}");
				}
				if (Paths.Count > MaxDisplay)
				{
					Logger.LogInformation("  +{NumPaths:n0} more", Paths.Count - MaxDisplay);
				}

				using(Trace("CleanFiles"))
				using(ILoggerProgress Scope = Logger.BeginProgressScope("Cleaning files..."))
				{
					Stopwatch Timer = Stopwatch.StartNew();
					using(ThreadPoolWorkQueue Queue = new ThreadPoolWorkQueue())
					{
						foreach(FileInfo FileToDelete in FilesToDelete)
						{
							Queue.Enqueue(() => FileUtils.ForceDeleteFile(FileToDelete));
						}
						foreach(DirectoryInfo DirectoryToDelete in DirectoriesToDelete)
						{
							Queue.Enqueue(() => FileUtils.ForceDeleteDirectory(DirectoryToDelete));
						}
					}
					Scope.Progress = $"({Timer.Elapsed.TotalSeconds:0.0}s)";
				}

				await SaveAsync(TransactionState.Clean, CancellationToken);
			}
		}

		/// <summary>
		/// Empties the staging directory of any staged files
		/// </summary>
		public async Task ClearAsync(CancellationToken CancellationToken)
		{
			Stopwatch Timer = Stopwatch.StartNew();

			Logger.LogInformation("Clearing workspace...");
			using (Trace("Clear"))
			using (Logger.BeginIndentScope("  "))
			{
				await CleanInternalAsync(true, CancellationToken);
				await RemoveFilesFromWorkspaceAsync(StreamSnapshot.Empty, CancellationToken);
				await SaveAsync(TransactionState.Clean, CancellationToken);
			}

			Logger.LogInformation("Completed in {ElapsedTime}s", $"{Timer.Elapsed.TotalSeconds:0.0}");
		}

		/// <summary>
		/// Dumps the contents of the repository to the log for analysis
		/// </summary>
		public void Dump()
		{
			Stopwatch Timer = Stopwatch.StartNew();

			Logger.LogInformation("Dumping repository to log...");

			WorkspaceFileInfo[] WorkspaceFiles = Workspace.GetFiles().OrderBy(x => x.GetLocation().FullName).ToArray();
			if(WorkspaceFiles.Length > 0)
			{
				Logger.LogDebug("  Workspace:");
				foreach(WorkspaceFileInfo File in WorkspaceFiles)
				{
					Logger.LogDebug("    {File,-128} [{ContentId,-48}] [{Length,20:n0}] [{LastModified,20}]{Writable}", File.GetClientPath(), File.ContentId, File.Length, File.LastModifiedTicks, File.bReadOnly? "" : " [ writable ]");
				}
			}

			if(ContentIdToTrackedFile.Count > 0)
			{
				Logger.LogDebug("  Cache:");
				foreach(KeyValuePair<FileContentId, CachedFileInfo> Pair in ContentIdToTrackedFile)
				{
					Logger.LogDebug("    {File,-128} [{ContentId,-48}] [{Length,20:n0}] [{LastModified,20}]{Writable}", Pair.Value.GetLocation(), Pair.Key, Pair.Value.Length, Pair.Value.LastModifiedTicks, Pair.Value.bReadOnly? "" : "[ writable ]");
				}
			}

			Logger.LogInformation("Completed in {ElapsedTime}s", $"{Timer.Elapsed.TotalSeconds:0.0}");
		}

		/// <summary>
		/// Checks the integrity of the cache
		/// </summary>
		public async Task RepairAsync(CancellationToken CancellationToken)
		{
			using (Trace("Repair"))
			using (ILoggerProgress Status = Logger.BeginProgressScope("Checking cache..."))
			{
				// Make sure all the folders exist in the cache
				CreateCacheHierarchy();

				// Check that all the files in the cache appear as we expect them to
				List<CachedFileInfo> TrackedFiles = ContentIdToTrackedFile.Values.ToList();
				foreach(CachedFileInfo TrackedFile in TrackedFiles)
				{
					if(!TrackedFile.CheckIntegrity(Logger))
					{
						RemoveTrackedFile(TrackedFile);
					}
				}

				// Clear the repair flag
				bRequiresRepair = false;

				await SaveAsync(TransactionState.Clean, CancellationToken);

				Status.Progress = "Done";
			}
		}

		/// <summary>
		/// Cleans the current workspace
		/// </summary>
		public async Task RevertAsync(PerforceClientConnection PerforceClient, CancellationToken CancellationToken)
		{
			Stopwatch Timer = Stopwatch.StartNew();

			await RevertInternalAsync(PerforceClient, CancellationToken);

			Logger.LogInformation("Completed in {ElapsedTime}s", $"{Timer.Elapsed.TotalSeconds:0.0}");
		}

		/// <summary>
		/// Checks the bRequiresRepair flag, and repairs/resets it if set.
		/// </summary>
		private async Task RunOptionalRepairAsync(CancellationToken CancellationToken)
		{
			if(bRequiresRepair)
			{
				await RepairAsync(CancellationToken);
			}
		}

		/// <summary>
		/// Shrink the size of the cache to the given size
		/// </summary>
		/// <param name="MaxSize">The maximum cache size, in bytes</param>
		/// <param name="CancellationToken">Cancellation token</param>
		public async Task PurgeAsync(long MaxSize, CancellationToken CancellationToken)
		{
			Logger.LogInformation("Purging cache (limit {MaxSize:n0} bytes)...", MaxSize);
			using (Trace("Purge"))
			using (Logger.BeginIndentScope("  "))
			{
				List<CachedFileInfo> CachedFiles = ContentIdToTrackedFile.Values.OrderBy(x => x.SequenceNumber).ToList();

				int NumRemovedFiles = 0;
				long TotalSize = CachedFiles.Sum(x => x.Length);

				while(MaxSize < TotalSize && NumRemovedFiles < CachedFiles.Count)
				{
					CachedFileInfo File = CachedFiles[NumRemovedFiles];

					RemoveTrackedFile(File);
					TotalSize -= File.Length;

					NumRemovedFiles++;
				}

				await SaveAsync(TransactionState.Clean, CancellationToken);

				Logger.LogInformation("{NumFilesRemoved} files removed, {NumFilesRemaining} files remaining, new size {NewSize:n0} bytes.", NumRemovedFiles, CachedFiles.Count - NumRemovedFiles, TotalSize);
			}
		}

		/// <summary>
		/// Configures the client for the given stream
		/// </summary>
		/// <param name="PerforceClient">The Perforce connection</param>
		/// <param name="StreamName">Name of the stream</param>
		/// <param name="CancellationToken">Cancellation token</param>
		public async Task SetupAsync(PerforceClientConnection PerforceClient, string StreamName, CancellationToken CancellationToken)
		{
			await UpdateClientAsync(PerforceClient, StreamName, CancellationToken);
		}

		/// <summary>
		/// Prints stats showing coherence between different streams
		/// </summary>
		public async Task StatsAsync(PerforceClientConnection PerforceClient, List<string> StreamNames, List<string> View, CancellationToken CancellationToken)
		{
			Logger.LogInformation("Finding stats for {NumStreams} streams", StreamNames.Count);
			using (Logger.BeginIndentScope("  "))
			{
				// Update the list of files in each stream
				Tuple<int, StreamSnapshot>[] StreamState = new Tuple<int, StreamSnapshot>[StreamNames.Count];
				for (int Idx = 0; Idx < StreamNames.Count; Idx++)
				{
					string StreamName = StreamNames[Idx];
					Logger.LogInformation("Finding contents of {StreamName}:", StreamName);

					using (Logger.BeginIndentScope("  "))
					{
						CreatedClients.Remove(PerforceClient.ClientName); // Force the client to be updated

						await UpdateClientAsync(PerforceClient, StreamName, CancellationToken);

						int ChangeNumber = await GetLatestClientChangeAsync(PerforceClient, CancellationToken);
						Logger.LogInformation("Latest change is CL {ChangeNumber}", ChangeNumber);

						await RevertInternalAsync(PerforceClient, CancellationToken);
						await ClearClientHaveTableAsync(PerforceClient, CancellationToken);
						await UpdateClientHaveTableAsync(PerforceClient, ChangeNumber, View, CancellationToken);

						StreamSnapshot Contents = await FindClientContentsAsync(PerforceClient, ChangeNumber, false, CancellationToken);
						StreamState[Idx] = Tuple.Create(ChangeNumber, Contents);

						GC.Collect();
					}
				}

				// Find stats for 
				using (Trace("Stats"))
				using (ILoggerProgress Scope = Logger.BeginProgressScope("Finding usage stats..."))
				{
					Stopwatch Timer = Stopwatch.StartNew();

					// Find the set of files in each stream
					HashSet<FileContentId>[] FilesInStream = new HashSet<FileContentId>[StreamNames.Count];
					for (int Idx = 0; Idx < StreamNames.Count; Idx++)
					{
						List<StreamFile> Files = StreamState[Idx].Item2.GetFiles();
						FilesInStream[Idx] = new HashSet<FileContentId>(Files.Select(x => x.ContentId));
					}

					// Build a table showing amount of unique content in each stream
					string[,] Cells = new string[StreamNames.Count + 1, StreamNames.Count + 1];
					Cells[0, 0] = "";
					for (int Idx = 0; Idx < StreamNames.Count; Idx++)
					{
						Cells[Idx + 1, 0] = StreamNames[Idx];
						Cells[0, Idx + 1] = StreamNames[Idx];
					}

					// Populate the table
					for (int RowIdx = 0; RowIdx < StreamNames.Count; RowIdx++)
					{
						List<StreamFile> Files = StreamState[RowIdx].Item2.GetFiles();
						for (int ColIdx = 0; ColIdx < StreamNames.Count; ColIdx++)
						{
							long DiffSize = Files.Where(x => !FilesInStream[ColIdx].Contains(x.ContentId)).Sum(x => x.Length);
							Cells[RowIdx + 1, ColIdx + 1] = String.Format("{0:0.0}mb", DiffSize / (1024.0 * 1024.0));
						}
					}

					// Find the width of each row
					int[] ColWidths = new int[StreamNames.Count + 1];
					for (int ColIdx = 0; ColIdx < StreamNames.Count + 1; ColIdx++)
					{
						for (int RowIdx = 0; RowIdx < StreamNames.Count + 1; RowIdx++)
						{
							ColWidths[ColIdx] = Math.Max(ColWidths[ColIdx], Cells[RowIdx, ColIdx].Length);
						}
					}

					// Print the table
					Logger.LogInformation("");
					Logger.LogInformation("Each row shows the size of files in a stream which are unique to that stream compared to each column:");
					Logger.LogInformation("");
					for (int RowIdx = 0; RowIdx < StreamNames.Count + 1; RowIdx++)
					{
						StringBuilder Row = new StringBuilder();
						for (int ColIdx = 0; ColIdx < StreamNames.Count + 1; ColIdx++)
						{
							string Cell = Cells[RowIdx, ColIdx];
							Row.Append(' ', ColWidths[ColIdx] - Cell.Length);
							Row.Append(Cell);
							Row.Append(" | ");
						}
						Logger.LogInformation(Row.ToString());
					}
					Logger.LogInformation("");

					Scope.Progress = $"({Timer.Elapsed.TotalSeconds:0.0}s)";
				}
			}
		}

		/// <summary>
		/// Prints information about the repository state
		/// </summary>
		public void Status()
		{
			// Print size stats
			Logger.LogInformation("Cache contains {NumFiles:n0} files, {TotalSize:n1}mb", ContentIdToTrackedFile.Count, ContentIdToTrackedFile.Values.Sum(x => x.Length) / (1024.0 * 1024.0));
			Logger.LogInformation("Stage contains {NumFiles:n0} files, {TotalSize:n1}mb", Workspace.GetFiles().Count, Workspace.GetFiles().Sum(x => x.Length) / (1024.0 * 1024.0));

			// Print the contents of the workspace
			string[] Differences = Workspace.FindDifferences();
			if(Differences.Length > 0)
			{
				Logger.LogInformation("Local changes:");
				foreach(string Difference in Differences)
				{
					if(Difference.StartsWith("+"))
					{
						Console.ForegroundColor = ConsoleColor.Green;
					}
					else if(Difference.StartsWith("-"))
					{
						Console.ForegroundColor = ConsoleColor.Red;
					}
					else if(Difference.StartsWith("!"))
					{
						Console.ForegroundColor = ConsoleColor.Yellow;
					}
					else
					{
						Console.ResetColor();
					}
					Logger.LogInformation("  {0}", Difference);
				}
				Console.ResetColor();
			}
		}

		/// <summary>
		/// Debug code
		/// </summary>
		/// <param name="Perforce"></param>
		/// <returns></returns>
		public async Task<bool> LogFortniteStatsInfoAsync(PerforceClientConnection Perforce)
		{
			bool bError = false;
			FileReference LocalFile = FileReference.Combine(WorkspaceDir, "FortniteGame", "Content", "Backend", "StatsV2.json");

			PerforceResponseList<FStatRecord> Record = await Perforce.TryFStatAsync(FStatOptions.None, new[] { LocalFile.FullName }, CancellationToken.None);

			string PerforceState;
			if (Record.Count == 0)
			{
				PerforceState = "no records";
			}
			else if (Record[0].Error != null)
			{
				PerforceState = Record[0].Error?.ToString() ?? "null";
			}
			else
			{
				try
				{
					PerforceState = $"head {Record[0].Data.HeadRevision}, have {Record[0].Data.HaveRevision}";
					bError = Record[0].Data.HaveRevision == 0;
				}
				catch (Exception Ex)
				{
					PerforceState = $"Ex: {Ex}";
				}
			}

			Logger.LogInformation("Check {File}: {State}, {Perforce}", LocalFile, FileReference.Exists(LocalFile) ? "exists" : "does not exist", PerforceState);
			return bError;
		}

		/// <summary>
		/// Switches to the given stream
		/// </summary>
		/// <param name="Perforce">The perforce connection</param>
		/// <param name="StreamName">Name of the stream to sync</param>
		/// <param name="ChangeNumber">Changelist number to sync. -1 to sync to latest.</param>
		/// <param name="View">View of the workspace</param>
		/// <param name="bRemoveUntracked">Whether to remove untracked files from the workspace</param>
		/// <param name="bFakeSync">Whether to simulate the syncing operation rather than actually getting files from the server</param>
		/// <param name="CacheFile">If set, uses the given file to cache the contents of the workspace. This can improve sync times when multiple machines sync the same workspace.</param>
		/// <param name="CancellationToken">Cancellation token</param>
		public async Task SyncAsync(PerforceClientConnection Perforce, string StreamName, int ChangeNumber, IReadOnlyList<string> View, bool bRemoveUntracked, bool bFakeSync, FileReference? CacheFile, CancellationToken CancellationToken)
		{
			Stopwatch Timer = Stopwatch.StartNew();
			if(ChangeNumber == -1)
			{
				Logger.LogInformation("Syncing to {StreamName} at latest", StreamName);
			}
			else
			{
				Logger.LogInformation("Syncing to {StreamName} at CL {CL}", StreamName, ChangeNumber);
			}

			using (Logger.BeginIndentScope("  "))
			{
				// Update the client to the current stream
				await UpdateClientAsync(Perforce, StreamName, CancellationToken);

				// Get the latest change number
				if(ChangeNumber == -1)
				{
					ChangeNumber = await GetLatestClientChangeAsync(Perforce, CancellationToken);
				}

				// Revert any open files
				await RevertInternalAsync(Perforce, CancellationToken);

				// Force the P4 metadata to match up
				Task UpdateHaveTableTask = Task.Run(() => UpdateClientHaveTableAsync(Perforce, ChangeNumber, View, CancellationToken), CancellationToken);

				// Clean the current workspace
				await CleanInternalAsync(bRemoveUntracked, CancellationToken);

				// Wait for the have table update to finish
				await UpdateHaveTableTask;

				await LogFortniteStatsInfoAsync(Perforce);

				// Update the state of the current stream, if necessary
				StreamSnapshot? Contents;
				if(CacheFile == null)
				{
					Contents = await FindClientContentsAsync(Perforce, ChangeNumber, bFakeSync, CancellationToken);
				}
				else
				{
					Contents = await TryLoadClientContentsAsync(CacheFile, StreamName, CancellationToken);
					if(Contents == null)
					{
						Contents = await FindAndSaveClientContentsAsync(Perforce, StreamName, ChangeNumber, bFakeSync, CacheFile, CancellationToken);
					}
				}

				await LogFortniteStatsInfoAsync(Perforce);

				// Sync all the appropriate files
				await RemoveFilesFromWorkspaceAsync(Contents, CancellationToken);
				await AddFilesToWorkspaceAsync(Perforce, Contents, bFakeSync, CancellationToken);

				await LogFortniteStatsInfoAsync(Perforce);
			}

			Logger.LogInformation("Completed in {ElapsedTime}s", $"{Timer.Elapsed.TotalSeconds:0.0}");
		}

		/// <summary>
		/// Replays the effects of unshelving a changelist, but clobbering files in the workspace rather than actually unshelving them (to prevent problems with multiple machines locking them)
		/// </summary>
		/// <returns>Async task</returns>
		public async Task UnshelveAsync(PerforceClientConnection Perforce, string StreamName, int UnshelveChangelist, CancellationToken CancellationToken)
		{
			// Need to mark those files as dirty - update the workspace with those files 
			// Delete is fine, but need to flag anything added

			Stopwatch Timer = Stopwatch.StartNew();
			Logger.LogInformation("Unshelving changelist {Change}...", UnshelveChangelist);

			// query the contents of the shelved changelist
			List<DescribeRecord> Records = await Perforce.DescribeAsync(DescribeOptions.Shelved, -1, new int[] { UnshelveChangelist }, CancellationToken);
			if (Records.Count != 1)
			{
				throw new PerforceException($"Changelist {UnshelveChangelist} is not shelved");
			}
			DescribeRecord LastRecord = Records[0];
			if (LastRecord.Files.Count == 0)
			{
				throw new PerforceException($"Changelist {UnshelveChangelist} does not contain any shelved files");
			}

			// query the location of each file
			PerforceResponseList<WhereRecord> WhereRecords = await Perforce.TryWhereAsync(LastRecord.Files.Select(x => x.DepotFile).ToArray(), CancellationToken);

			// parse out all the list of deleted and modified files
			List<WhereRecord> DeleteFiles = new List<WhereRecord>();
			List<WhereRecord> WriteFiles = new List<WhereRecord>();
			for(int FileIdx = 0; FileIdx < LastRecord.Files.Count; FileIdx++)
			{
				if (FileIdx >= WhereRecords.Count)
				{
					throw new PerforceException($"Unable to get location of {LastRecord.Files[FileIdx].DepotFile} within {StreamName}. Check the correct stream is specified.");
				}

				PerforceResponse<WhereRecord> Response = WhereRecords[FileIdx];
				if (!Response.Succeeded)
				{
					Logger.LogInformation("Unable to get location of {File} in current workspace; ignoring.", LastRecord.Files[FileIdx].DepotFile);
					continue;
				}

				WhereRecord WhereRecord = Response.Data;
				switch (LastRecord.Files[FileIdx].Action)
				{
					case FileAction.Delete:
					case FileAction.MoveDelete:
						DeleteFiles.Add(WhereRecord);
						break;
					case FileAction.Add:
					case FileAction.Edit:
					case FileAction.MoveAdd:
					case FileAction.Branch:
					case FileAction.Integrate:
						WriteFiles.Add(WhereRecord);
						break;
					default:
						throw new Exception($"Unknown action '{LastRecord.Files[FileIdx].Action}' for shelved file {WhereRecord.DepotFile}");
				}
			}

			// Add all the files to be written to the workspace with invalid metadata. This will ensure they're removed on next clean.
			if(WriteFiles.Count > 0)
			{
				Logger.LogInformation("Removing {NumFiles} files from tracked workspace", WriteFiles.Count);
				foreach (WhereRecord WriteFile in WriteFiles)
				{
					string Path = Regex.Replace(WriteFile.ClientFile, "^//[^/]+/", "");
					Workspace.AddFile(new Utf8String(Path), 0, 0, false, new FileContentId(Md5Hash.Zero, default));
				}
				await SaveAsync(TransactionState.Clean, CancellationToken.None);
			}

			// Delete all the files
			foreach (WhereRecord DeleteFile in DeleteFiles)
			{
				string LocalPath = DeleteFile.Path;
				if (File.Exists(LocalPath))
				{
					Logger.LogInformation("  Deleting {LocalPath}", LocalPath);
					FileUtils.ForceDeleteFile(LocalPath);
				}
			}

			// Add all the new files
			foreach (WhereRecord WriteFile in WriteFiles)
			{
				string LocalPath = WriteFile.Path;
				Logger.LogInformation("  Writing {LocalPath}", LocalPath);

				Directory.CreateDirectory(Path.GetDirectoryName(LocalPath));

				PerforceResponse PrintResponse = await Perforce.TryPrintAsync(LocalPath, $"{WriteFile}@={UnshelveChangelist}", CancellationToken);
				if (!PrintResponse.Succeeded)
				{
					Logger.LogWarning("Unable to print {LocalPath}: {Error}", LocalPath, PrintResponse.ToString());
				}
			}

			Logger.LogInformation("Completed in {TimeSeconds}s", $"{Timer.Elapsed.TotalSeconds:0.0}");
		}

		/// <summary>
		/// Populates the cache with the head revision of the given streams.
		/// </summary>
		public async Task PopulateAsync(List<PopulateRequest> Requests, bool bFakeSync, CancellationToken CancellationToken)
		{
			Logger.LogInformation("Populating with {NumStreams} streams", Requests.Count);
			using (Logger.BeginIndentScope("  "))
			{
				Tuple<int, StreamSnapshot>[] StreamState = await PopulateCleanAsync(Requests, bFakeSync, CancellationToken);
				await PopulateSyncAsync(Requests, StreamState, bFakeSync, CancellationToken);
			}
		}

		/// <summary>
		/// Perform the clean part of a populate command
		/// </summary>
		public async Task<Tuple<int, StreamSnapshot>[]> PopulateCleanAsync(List<PopulateRequest> Requests, bool bFakeSync, CancellationToken CancellationToken)
		{
			// Revert all changes in each of the unique clients
			foreach (PopulateRequest Request in Requests)
			{
				PerforceConnection Perforce = Request.PerforceClient.WithoutClient();

				PerforceResponse<ClientRecord> Response = await Perforce.TryGetClientAsync(Request.PerforceClient.ClientName, CancellationToken);
				if (Response.Succeeded)
				{
					await RevertInternalAsync(Request.PerforceClient, CancellationToken);
				}
			}

			// Clean the current workspace
			await CleanAsync(true, CancellationToken);

			// Update the list of files in each stream
			Tuple<int, StreamSnapshot>[] StreamState = new Tuple<int, StreamSnapshot>[Requests.Count];
			for (int Idx = 0; Idx < Requests.Count; Idx++)
			{
				PopulateRequest Request = Requests[Idx];
				string StreamName = Request.StreamName;
				Logger.LogInformation("Finding contents of {StreamName}:", StreamName);

				using (Logger.BeginIndentScope("  "))
				{
					await DeleteClientAsync(Request.PerforceClient, CancellationToken);
					await UpdateClientAsync(Request.PerforceClient, StreamName, CancellationToken);

					int ChangeNumber = await GetLatestClientChangeAsync(Request.PerforceClient, CancellationToken);
					Logger.LogInformation("Latest change is CL {CL}", ChangeNumber);

					await UpdateClientHaveTableAsync(Request.PerforceClient, ChangeNumber, Request.View, CancellationToken);

					StreamSnapshot Contents = await FindClientContentsAsync(Request.PerforceClient, ChangeNumber, bFakeSync, CancellationToken);
					StreamState[Idx] = Tuple.Create(ChangeNumber, Contents);

					GC.Collect();
				}
			}

			// Remove any files from the workspace not referenced by the first stream. This ensures we can purge things from the cache that we no longer need.
			if (Requests.Count > 0)
			{
				await RemoveFilesFromWorkspaceAsync(StreamState[0].Item2, CancellationToken);
			}

			// Shrink the contents of the cache
			using (Trace("UpdateCache"))
			using (ILoggerProgress Status = Logger.BeginProgressScope("Updating cache..."))
			{
				Stopwatch Timer = Stopwatch.StartNew();

				HashSet<FileContentId> CommonContentIds = new HashSet<FileContentId>();
				Dictionary<FileContentId, long> ContentIdToLength = new Dictionary<FileContentId, long>();
				for (int Idx = 0; Idx < Requests.Count; Idx++)
				{
					List<StreamFile> Files = StreamState[Idx].Item2.GetFiles();
					foreach (StreamFile File in Files)
					{
						ContentIdToLength[File.ContentId] = File.Length;
					}

					if (Idx == 0)
					{
						CommonContentIds.UnionWith(Files.Select(x => x.ContentId));
					}
					else
					{
						CommonContentIds.IntersectWith(Files.Select(x => x.ContentId));
					}
				}

				List<CachedFileInfo> TrackedFiles = ContentIdToTrackedFile.Values.ToList();
				foreach (CachedFileInfo TrackedFile in TrackedFiles)
				{
					if (!ContentIdToLength.ContainsKey(TrackedFile.ContentId))
					{
						RemoveTrackedFile(TrackedFile);
					}
				}

				GC.Collect();

				double TotalSize = ContentIdToLength.Sum(x => x.Value) / (1024.0 * 1024.0);
				Status.Progress = String.Format("{0:n1}mb total, {1:n1}mb differences ({2:0.0}s)", TotalSize, TotalSize - CommonContentIds.Sum(x => ContentIdToLength[x]) / (1024.0 * 1024.0), Timer.Elapsed.TotalSeconds);
			}

			return StreamState;
		}

		/// <summary>
		/// Perform the sync part of a populate command
		/// </summary>
		public async Task PopulateSyncAsync(List<PopulateRequest> Requests, Tuple<int, StreamSnapshot>[] StreamState, bool bFakeSync, CancellationToken CancellationToken)
		{
			// Sync all the new files
			for (int Idx = 0; Idx < Requests.Count; Idx++)
			{
				PopulateRequest Request = Requests[Idx];
				string StreamName = Request.StreamName;
				Logger.LogInformation("Syncing files for {StreamName}:", StreamName);

				using(Logger.BeginIndentScope("  "))
				{
					await DeleteClientAsync(Request.PerforceClient, CancellationToken);
					await UpdateClientAsync(Request.PerforceClient, StreamName, CancellationToken);

					int ChangeNumber = StreamState[Idx].Item1;
					await UpdateClientHaveTableAsync(Request.PerforceClient, ChangeNumber, Requests[Idx].View, CancellationToken);

					StreamSnapshot Contents = StreamState[Idx].Item2;
					await RemoveFilesFromWorkspaceAsync(Contents, CancellationToken);
					await AddFilesToWorkspaceAsync(Request.PerforceClient, Contents, bFakeSync, CancellationToken);
				}
			}

			// Save the new repo state
			await SaveAsync(TransactionState.Clean, CancellationToken);
		}

		#endregion

		#region Core operations

		/// <summary>
		/// Deletes a client
		/// </summary>
		/// <param name="PerforceClient">The Perforce connection</param>
		/// <param name="CancellationToken">Cancellation token</param>
		/// <returns>Async task</returns>
		public async Task DeleteClientAsync(PerforceClientConnection PerforceClient, CancellationToken CancellationToken)
		{
			PerforceResponse Response = await PerforceClient.TryDeleteClientAsync(DeleteClientOptions.None, PerforceClient.ClientName, CancellationToken);
			if (Response.Error != null && Response.Error.Generic != PerforceGenericCode.Unknown)
			{
				if(Response.Error.Generic == PerforceGenericCode.NotYet)
				{
					await RevertInternalAsync(PerforceClient, CancellationToken);
					Response = await PerforceClient.TryDeleteClientAsync(DeleteClientOptions.None, PerforceClient.ClientName, CancellationToken);
				}
				Response.EnsureSuccess();
			}
			CreatedClients.Remove(PerforceClient.ClientName);
		}

		/// <summary>
		/// Sets the stream for the current client
		/// </summary>
		/// <param name="PerforceClient">The Perforce connection</param>
		/// <param name="StreamName">New stream for the client</param>
		/// <param name="CancellationToken">Cancellation token</param>
		private async Task UpdateClientAsync(PerforceClientConnection PerforceClient, string StreamName, CancellationToken CancellationToken)
		{
			// Create or update the client if it doesn't exist already
			ClientRecord? Client;
			if(!CreatedClients.TryGetValue(PerforceClient.ClientName, out Client) || Client.Stream != StreamName)
			{
				using (Trace("UpdateClient"))
				using (ILoggerProgress Status = Logger.BeginProgressScope("Updating client..."))
				{
					Stopwatch Timer = Stopwatch.StartNew();

					Client = new ClientRecord(PerforceClient.ClientName, PerforceClient.UserName!, WorkspaceDir.FullName);
					Client.Host = HostName;
					Client.Stream = StreamName;
					Client.Type = "partitioned";

					PerforceConnection Perforce = PerforceClient.WithoutClient();

					PerforceResponse Response = await Perforce.TryCreateClientAsync(Client, CancellationToken);
					if (!Response.Succeeded)
					{
						await PerforceClient.TryDeleteClientAsync(DeleteClientOptions.None, PerforceClient.ClientName, CancellationToken);
						await PerforceClient.CreateClientAsync(Client, CancellationToken);
					}

					Status.Progress = $"({Timer.Elapsed.TotalSeconds:0.0}s)";
				}
				CreatedClients[PerforceClient.ClientName] = Client;
			}

			// Update the config file with the name of the client
			FileReference ConfigFile = FileReference.Combine(BaseDir, "p4.ini");
			using(StreamWriter Writer = new StreamWriter(ConfigFile.FullName))
			{
				Writer.WriteLine("P4CLIENT={0}", PerforceClient.ClientName);
			}
		}

		/// <summary>
		/// Gets the latest change submitted for the given stream
		/// </summary>
		/// <param name="PerforceClient">The Perforce connection</param>
		/// <param name="StreamName">The stream to sync</param>
		/// <param name="CancellationToken">Cancellation token</param>
		/// <returns>The latest changelist number</returns>
		public async Task<int> GetLatestChangeAsync(PerforceClientConnection PerforceClient, string StreamName, CancellationToken CancellationToken)
		{
			// Update the client to the current stream
			await UpdateClientAsync(PerforceClient, StreamName, CancellationToken);

			// Get the latest change number
			return await GetLatestClientChangeAsync(PerforceClient, CancellationToken);
		}

		/// <summary>
		/// Get the latest change number in the current client
		/// </summary>
		/// <param name="PerforceClient">The perforce client connection</param>
		/// <param name="CancellationToken">Cancellation token</param>
		/// <returns>The latest submitted change number</returns>
		private async Task<int> GetLatestClientChangeAsync(PerforceClientConnection PerforceClient, CancellationToken CancellationToken)
		{
			int ChangeNumber;
			using (Trace("FindChange"))
			using (ILoggerProgress Status = Logger.BeginProgressScope("Finding latest change..."))
			{
				Stopwatch Timer = Stopwatch.StartNew();

				List<ChangesRecord> Changes = await PerforceClient.GetChangesAsync(ChangesOptions.None, 1, ChangeStatus.Submitted, new[] { String.Format("//{0}/...", PerforceClient.ClientName) }, CancellationToken);
				ChangeNumber = Changes[0].Number;

				Status.Progress = String.Format("CL {0} ({1:0.0}s)", ChangeNumber, Timer.Elapsed.TotalSeconds);
			}
			return ChangeNumber;
		}

		/// <summary>
		/// Revert all files that are open in the current workspace. Does not replace them with valid revisions.
		/// </summary>
		/// <param name="PerforceClient">The current client connection</param>
		/// <param name="CancellationToken">Cancellation token</param>
		private async Task RevertInternalAsync(PerforceClientConnection PerforceClient, CancellationToken CancellationToken)
		{
			using (Trace("Revert"))
			using (ILoggerProgress Status = Logger.BeginProgressScope("Reverting changes..."))
			{
				Stopwatch Timer = Stopwatch.StartNew();

				// Get a list of open files
				List<FStatRecord> OpenedFilesResponse = await PerforceClient.GetOpenFilesAsync(OpenedOptions.ShortOutput, -1, PerforceClient.ClientName, null, 1, new string[0], CancellationToken);

				// If there are any files, revert them
				if(OpenedFilesResponse.Any())
				{
					await PerforceClient.RevertAsync(-1, null, RevertOptions.KeepWorkspaceFiles, new[] { "//..." }, CancellationToken);
				}

				// Find all the open changes
				List<ChangesRecord> Changes = await PerforceClient.GetChangesAsync(ChangesOptions.None, PerforceClient.ClientName, -1, ChangeStatus.Pending, null, new string[0], CancellationToken);

				// Delete the changelist
				foreach(ChangesRecord Change in Changes)
				{
					// Delete the shelved files
					List<DescribeRecord> DescribeResponse = await PerforceClient.DescribeAsync(DescribeOptions.Shelved, -1, new[] { Change.Number }, CancellationToken);
					foreach(DescribeRecord Record in DescribeResponse)
					{
						if(Record.Files.Count > 0)
						{
							await PerforceClient.DeleteShelvedFilesAsync(Record.Number, new string[0], CancellationToken);
						}
					}

					// Delete the changelist
					await PerforceClient.DeleteChangeAsync(DeleteChangeOptions.None, Change.Number, CancellationToken);
				}

				Status.Progress = $"({Timer.Elapsed.TotalSeconds:0.0}s)";
			}
		}

		/// <summary>
		/// Clears the have table. This ensures that we'll always fetch the names of files at head revision, which aren't updated otherwise.
		/// </summary>
		/// <param name="PerforceClient">The client connection</param>
		/// <param name="CancellationToken">The cancellation token</param>
		private async Task ClearClientHaveTableAsync(PerforceClientConnection PerforceClient, CancellationToken CancellationToken)
		{
			using (Trace("ClearHaveTable"))
			using (ILoggerProgress Scope = Logger.BeginProgressScope("Clearing have table..."))
			{
				Stopwatch Timer = Stopwatch.StartNew();
				await PerforceClient.SyncQuietAsync(SyncOptions.KeepWorkspaceFiles, -1, new[] { String.Format("//{0}/...#0", PerforceClient.ClientName) }, CancellationToken);
				Scope.Progress = $"({Timer.Elapsed.TotalSeconds:0.0}s)";
			}
		}

		/// <summary>
		/// Updates the have table to reflect the given stream
		/// </summary>
		/// <param name="PerforceClient">The client connection</param>
		/// <param name="ChangeNumber">The change number to sync. May be -1, for latest.</param>
		/// <param name="View">View of the stream. Each entry should be a path relative to the stream root, with an optional '-'prefix.</param>
		/// <param name="CancellationToken">Cancellation token</param>
		private async Task UpdateClientHaveTableAsync(PerforceClientConnection PerforceClient, int ChangeNumber, IReadOnlyList<string> View, CancellationToken CancellationToken)
		{
			using (Trace("UpdateHaveTable"))
			using (ILoggerProgress Scope = Logger.BeginProgressScope("Updating have table..."))
			{
				Stopwatch Timer = Stopwatch.StartNew();

				// Sync an initial set of files. Either start with a full workspace and remove files, or start with nothing and add files.
				if (View.Count == 0 || View[0].StartsWith("-"))
				{
					await UpdateHaveTablePathAsync(PerforceClient, $"//{PerforceClient.ClientName}/...@{ChangeNumber}", CancellationToken);
				}
				else
				{
					await UpdateHaveTablePathAsync(PerforceClient, $"//{PerforceClient.ClientName}/...#0", CancellationToken);
				}

				// Update with the contents of each filter
				foreach(string Filter in View)
				{
					string SyncPath;
					if(Filter.StartsWith("-"))
					{
						SyncPath = String.Format("//{0}/{1}#0", PerforceClient.ClientName, RemoveLeadingSlash(Filter.Substring(1)));
					}
					else
					{
						SyncPath = String.Format("//{0}/{1}@{2}", PerforceClient.ClientName, RemoveLeadingSlash(Filter), ChangeNumber);
					}
					await UpdateHaveTablePathAsync(PerforceClient, SyncPath, CancellationToken);
				}

				Scope.Progress = $"({Timer.Elapsed.TotalSeconds:0.0}s)";
			}
		}

		/// <summary>
		/// Update a path in the have table
		/// </summary>
		/// <param name="PerforceClient">The Perforce client</param>
		/// <param name="SyncPath">Path to sync</param>
		/// <param name="CancellationToken">Cancellation token</param>
		/// <returns>Async task</returns>
		private async Task UpdateHaveTablePathAsync(PerforceClientConnection PerforceClient, string SyncPath, CancellationToken CancellationToken)
		{
			PerforceResponseList<SyncSummaryRecord> ResponseList = await PerforceClient.TrySyncQuietAsync(SyncOptions.KeepWorkspaceFiles, -1, new[] { SyncPath }, CancellationToken);
			foreach (PerforceResponse<SyncSummaryRecord> Response in ResponseList)
			{
				PerforceError? Error = Response.Error;
				if(Error != null && Error.Generic != PerforceGenericCode.Empty)
				{
					throw new PerforceException(Error);
				}
			}
		}

		/// <summary>
		/// Optimized record definition for fstat calls when populating a workspace. Since there are so many files in a typical branch,
		/// the speed of serializing these records is crucial for performance. Rather than deseralizing everything, we filter to just
		/// the fields we need, and avoid any unnecessary conversions from their primitive data types.
		/// </summary>
		class FStatIndexedRecord
		{
			enum Field
			{
				code,
				depotFile,
				clientFile,
				headType,
				haveRev,
				fileSize,
				digest
			}

			public static readonly string[] FieldNames = Enum.GetNames(typeof(Field));
			public static readonly Utf8String[] Utf8FieldNames = Array.ConvertAll(FieldNames, x => new Utf8String(x));

			public PerforceValue[] Values = new PerforceValue[FieldNames.Length];

			public Utf8String DepotFile
			{
				get { return Values[(int)Field.depotFile].GetString(); }
			}

			public Utf8String ClientFile
			{
				get { return Values[(int)Field.clientFile].GetString(); }
			}

			public Utf8String HeadType
			{
				get { return Values[(int)Field.headType].GetString(); }
			}

			public Utf8String HaveRev
			{
				get { return Values[(int)Field.haveRev].GetString(); }
			}

			public long FileSize
			{
				get { return Values[(int)Field.fileSize].AsLong(); }
			}

			public Utf8String Digest
			{
				get { return Values[(int)Field.digest].GetString(); }
			}
		}

		/// <summary>
		/// Get the contents of the client, as synced.
		/// </summary>
		/// <param name="PerforceClient">The client connection</param>
		/// <param name="ChangeNumber">The change number being synced. This must be specified in order to get the digest at the correct revision.</param>
		/// <param name="bFakeSync">Whether this is for a fake sync. Poisons the file type to ensure that the cache is not corrupted.</param>
		/// <param name="CancellationToken">Cancellation token</param>
		private async Task<StreamSnapshotFromMemory> FindClientContentsAsync(PerforceClientConnection PerforceClient, int ChangeNumber, bool bFakeSync, CancellationToken CancellationToken)
		{
			StreamTreeBuilder Builder = new StreamTreeBuilder();

			using (Trace("FetchMetadata"))
			using (ILoggerProgress Scope = Logger.BeginProgressScope("Fetching metadata..."))
			{
				Stopwatch Timer = Stopwatch.StartNew();

				// Get the expected prefix for any paths in client syntax
				Utf8String ClientPrefix = $"//{PerforceClient.ClientName}/";

				// List of the last path fragments. Since file records that are returned are typically sorted by their position in the tree, we can save quite a lot of processing by
				// reusing as many fragemnts as possible.
				List<(Utf8String, StreamTreeBuilder)> Fragments = new List<(Utf8String, StreamTreeBuilder)>();

				// Handler for each returned record
				FStatIndexedRecord Record = new FStatIndexedRecord();
				Action<PerforceRecord> HandleRecord = RawRecord =>
				{
					// Copy into the values array
					RawRecord.CopyInto(FStatIndexedRecord.Utf8FieldNames, Record.Values);

					// Make sure it has all the fields we're interested in
					if (Record.Digest.IsEmpty)
					{
						return;
					}
					if (Record.ClientFile.IsEmpty)
					{
						throw new InvalidDataException("Record returned by Peforce does not have ClientFile set");
					}
					if (!Record.ClientFile.StartsWith(ClientPrefix))
					{
						throw new InvalidDataException($"Client path returned by Perforce ('{Record.ClientFile}') does not begin with client name ('{ClientPrefix}')");
					}

					// Duplicate the client path. If we reference into the raw record, we'll prevent all the raw P4 output from being garbage collected.
					Utf8String ClientFile = Record.ClientFile.Clone();

					// Get the client path after the initial client prefix
					ReadOnlySpan<byte> PathSpan = ClientFile.Span;

					// Parse out the data
					StreamTreeBuilder LastStreamDirectory = Builder;

					// Try to match up as many fragments from the last file.
					int FragmentMinIdx = ClientPrefix.Length;
					for (int FragmentIdx = 0; ; FragmentIdx++)
					{
						// Find the next directory separator
						int FragmentMaxIdx = FragmentMinIdx;
						while (FragmentMaxIdx < PathSpan.Length && PathSpan[FragmentMaxIdx] != '/')
						{
							FragmentMaxIdx++;
						}
						if(FragmentMaxIdx == PathSpan.Length)
						{
							Fragments.RemoveRange(FragmentIdx, Fragments.Count - FragmentIdx);
							break;
						}

						// Get the fragment text
						Utf8String Fragment = new Utf8String(ClientFile.Memory.Slice(FragmentMinIdx, FragmentMaxIdx - FragmentMinIdx));

						// If this fragment matches the same fragment from the previous iteration, take the last stream directory straight away
						if (FragmentIdx < Fragments.Count)
						{
							if (Fragments[FragmentIdx].Item1 == Fragment)
							{
								LastStreamDirectory = Fragments[FragmentIdx].Item2;
							}
							else
							{
								Fragments.RemoveRange(FragmentIdx, Fragments.Count - FragmentIdx);
							}
						}

						// Otherwise, find or add a directory for this fragment into the last directory
						if (FragmentIdx >= Fragments.Count)
						{
							Utf8String UnescapedFragment = PerforceUtils.UnescapePath(Fragment);

							StreamTreeBuilder? NextStreamDirectory;
							if (!LastStreamDirectory.NameToTreeBuilder.TryGetValue(UnescapedFragment, out NextStreamDirectory))
							{
								NextStreamDirectory = new StreamTreeBuilder();
								LastStreamDirectory.NameToTreeBuilder.Add(UnescapedFragment, NextStreamDirectory);
							}
							LastStreamDirectory = NextStreamDirectory;

							Fragments.Add((Fragment, LastStreamDirectory));
						}

						// Move to the next fragment
						FragmentMinIdx = FragmentMaxIdx + 1;
					}

					Md5Hash Digest = Md5Hash.Parse(Record.Digest);
					FileContentId ContentId = new FileContentId(Digest, Record.HeadType.Clone());
					int Revision = (int)Utf8String.ParseUnsignedInt(Record.HaveRev);

					// Add a new StreamFileInfo to the last directory object
					Utf8String FileName = PerforceUtils.UnescapePath(ClientFile.Slice(FragmentMinIdx));
					LastStreamDirectory.NameToFile.Add(FileName, new StreamFile(Record.DepotFile.Clone(), Record.FileSize, ContentId, Revision));
				};

				// Create the workspace, and add records for all the files. Exclude deleted files with digest = null.
				List<string> Arguments = new List<string>();
				Arguments.Add("-Ol");
				Arguments.Add("-Op");
				Arguments.Add("-Os");
				Arguments.Add("-Rh");
				Arguments.Add("-T");
				Arguments.Add(String.Join(",", FStatIndexedRecord.FieldNames));
				Arguments.Add($"//{PerforceClient.ClientName}/...@{ChangeNumber}");
				await PerforceClient.RecordCommandAsync("fstat", Arguments, null, HandleRecord, CancellationToken);

				// Output the elapsed time
				Scope.Progress = $"({Timer.Elapsed.TotalSeconds:0.0}s)";
			}

			return new StreamSnapshotFromMemory(Builder);
		}

		/// <summary>
		/// Loads the contents of a client from disk
		/// </summary>
		/// <param name="CacheFile">The cache file to read from</param>
		/// <param name="BasePath">Default path for the stream</param>
		/// <param name="CancellationToken">Cancellation token</param>
		/// <returns>Contents of the workspace</returns>
		async Task<StreamSnapshot?> TryLoadClientContentsAsync(FileReference CacheFile, Utf8String BasePath, CancellationToken CancellationToken)
		{
			StreamSnapshot? Contents = null;
			if (FileReference.Exists(CacheFile))
			{
				using (Trace("ReadMetadata"))
				using (ILoggerProgress Scope = Logger.BeginProgressScope($"Reading cached metadata from {CacheFile}..."))
				{
					Stopwatch Timer = Stopwatch.StartNew();
					Contents = await StreamSnapshotFromMemory.TryLoadAsync(CacheFile, BasePath, CancellationToken);
					Scope.Progress = $"({Timer.Elapsed.TotalSeconds:0.0}s)";
				}
			}
			return Contents;
		}

		/// <summary>
		/// Finds the contents of a workspace, and saves it to disk
		/// </summary>
		/// <param name="PerforceClient">The client connection</param>
		/// <param name="BasePath">Base path for the stream</param>
		/// <param name="ChangeNumber">The change number being synced. This must be specified in order to get the digest at the correct revision.</param>
		/// <param name="bFakeSync">Whether this is for a fake sync. Poisons the file type to ensure that the cache is not corrupted.</param>
		/// <param name="CacheFile">Location of the file to save the cached contents</param>
		/// <param name="CancellationToken">Cancellation token</param>
		/// <returns>Contents of the workspace</returns>
		private async Task<StreamSnapshotFromMemory> FindAndSaveClientContentsAsync(PerforceClientConnection PerforceClient, Utf8String BasePath, int ChangeNumber, bool bFakeSync, FileReference CacheFile, CancellationToken CancellationToken)
		{
			StreamSnapshotFromMemory Contents = await FindClientContentsAsync(PerforceClient, ChangeNumber, bFakeSync, CancellationToken);

			using (Trace("WriteMetadata"))
			using (ILoggerProgress Scope = Logger.BeginProgressScope($"Saving metadata to {CacheFile}..."))
			{
				Stopwatch Timer = Stopwatch.StartNew();

				// Handle the case where two machines may try to write to the cache file at once by writing to a temporary file
				FileReference TempCacheFile = new FileReference(String.Format("{0}.{1}", CacheFile, Guid.NewGuid()));
				await Contents.Save(TempCacheFile, BasePath);

				// Try to move it into place
				try
				{
					FileReference.Move(TempCacheFile, CacheFile);
				}
				catch(IOException)
				{
					if(!FileReference.Exists(CacheFile))
					{
						throw;
					}
					FileReference.Delete(TempCacheFile);
				}

				Scope.Progress = $"({Timer.Elapsed.TotalSeconds:0.0}s)";
			}
			return Contents;
		}

		/// <summary>
		/// Remove files from the workspace
		/// </summary>
		/// <param name="Contents">Contents of the target stream</param>
		/// <param name="CancellationToken">Cancellation token</param>
		private async Task RemoveFilesFromWorkspaceAsync(StreamSnapshot Contents, CancellationToken CancellationToken)
		{
			// Make sure the repair flag is clear before we start
			await RunOptionalRepairAsync(CancellationToken);

			// Figure out what to remove
			RemoveTransaction Transaction;
			using (Trace("GatherFilesToRemove"))
			using (ILoggerProgress Scope = Logger.BeginProgressScope("Gathering files to remove..."))
			{
				Stopwatch Timer = Stopwatch.StartNew();

				Transaction = new RemoveTransaction(Workspace, Contents, ContentIdToTrackedFile);

				Scope.Progress = $"({Timer.Elapsed.TotalSeconds:0.0}s)";
			}

			// Move files into the cache
			KeyValuePair<FileContentId, WorkspaceFileInfo>[] FilesToMove = Transaction.FilesToMove.ToArray();
			if(FilesToMove.Length > 0)
			{
				using (Trace("MoveToCache"))
				using (ILoggerProgress Scope = Logger.BeginProgressScope(String.Format("Moving {0} {1} to cache...", FilesToMove.Length, (FilesToMove.Length == 1)? "file" : "files")))
				{
					Stopwatch Timer = Stopwatch.StartNew();

					// Add any new files to the cache
					List<KeyValuePair<FileReference, FileReference>> SourceAndTargetFiles = new List<KeyValuePair<FileReference, FileReference>>();
					foreach(KeyValuePair<FileContentId, WorkspaceFileInfo> FileToMove in FilesToMove)
					{
						ulong CacheId = GetUniqueCacheId(FileToMove.Key);
						CachedFileInfo NewTrackingInfo = new CachedFileInfo(CacheDir, FileToMove.Key, CacheId, FileToMove.Value.Length, FileToMove.Value.LastModifiedTicks, FileToMove.Value.bReadOnly, NextSequenceNumber);
						ContentIdToTrackedFile.Add(FileToMove.Key, NewTrackingInfo);
						SourceAndTargetFiles.Add(new KeyValuePair<FileReference, FileReference>(FileToMove.Value.GetLocation(), NewTrackingInfo.GetLocation()));
					}
					NextSequenceNumber++;

					// Save the current state of the repository as dirty. If we're interrupted, we will have two places to check for each file (the cache and workspace).
					await SaveAsync(TransactionState.Dirty, CancellationToken);

					// Execute all the moves and deletes
					await ParallelTask.ForEachAsync(SourceAndTargetFiles, SourceAndTargetFile => FileUtils.ForceMoveFile(SourceAndTargetFile.Key, SourceAndTargetFile.Value));

					Scope.Progress = $"({Timer.Elapsed.TotalSeconds:0.0}s)";
				}
			}

			// Remove files which are no longer needed
			WorkspaceFileInfo[] FilesToDelete = Transaction.FilesToDelete.ToArray();
			if(FilesToDelete.Length > 0)
			{
				using (Trace("DeleteFiles"))
				using (ILoggerProgress Scope = Logger.BeginProgressScope(String.Format("Deleting {0} {1}...", FilesToDelete.Length, (FilesToDelete.Length == 1)? "file" : "files")))
				{
					Stopwatch Timer = Stopwatch.StartNew();

					await ParallelTask.ForEachAsync(FilesToDelete, FileToDelete => RemoveFile(FileToDelete));

					Scope.Progress = $"({Timer.Elapsed.TotalSeconds:0.0}s)";
				}
			}

			// Remove directories which are no longer needed
			WorkspaceDirectoryInfo[] DirectoriesToDelete = Transaction.DirectoriesToDelete.ToArray();
			if(DirectoriesToDelete.Length > 0)
			{
				using (Trace("DeleteDirectories"))
				using (ILoggerProgress Scope = Logger.BeginProgressScope(String.Format("Deleting {0} {1}...", DirectoriesToDelete.Length, (DirectoriesToDelete.Length == 1)? "directory" : "directories")))
				{
					Stopwatch Timer = Stopwatch.StartNew();

					foreach(string DirectoryToDelete in DirectoriesToDelete.Select(x => x.GetFullName()).OrderByDescending(x => x.Length))
					{
						RemoveDirectory(DirectoryToDelete);
					}

					Scope.Progress = $"({Timer.Elapsed.TotalSeconds:0.0}s)";
				}
			}

			// Update the workspace and save the new state
			Workspace = Transaction.NewWorkspaceRootDir;
			await SaveAsync(TransactionState.Clean, CancellationToken);
		}

		/// <summary>
		/// Helper function to delete a file from the workspace, and output any failure as a warning.
		/// </summary>
		/// <param name="FileToDelete">The file to be deleted</param>
		void RemoveFile(WorkspaceFileInfo FileToDelete)
		{
			try
			{
				FileUtils.ForceDeleteFile(FileToDelete.GetLocation());
			}
			catch(Exception Ex)
			{
				Logger.LogWarning(Ex, "warning: Unable to delete file {FileName}.", FileToDelete.GetFullName());
				bRequiresRepair = true;
			}
		}

		/// <summary>
		/// Helper function to delete a directory from the workspace, and output any failure as a warning.
		/// </summary>
		/// <param name="DirectoryToDelete">The directory to be deleted</param>
		void RemoveDirectory(string DirectoryToDelete)
		{
			try
			{
				Directory.Delete(DirectoryToDelete, false);
			}
			catch(Exception Ex)
			{
				Logger.LogWarning(Ex, "warning: Unable to delete directory {0}", DirectoryToDelete);
				bRequiresRepair = true;
			}
		}

		/// <summary>
		/// Update the workspace to match the given stream, syncing files and moving to/from the cache as necessary.
		/// </summary>
		/// <param name="Client">The client connection</param>
		/// <param name="Stream">Contents of the stream</param>
		/// <param name="bFakeSync">Whether to simulate the sync operation, rather than actually syncing files</param>
		/// <param name="CancellationToken">Cancellation token</param>
		private async Task AddFilesToWorkspaceAsync(PerforceClientConnection Client, StreamSnapshot Stream, bool bFakeSync, CancellationToken CancellationToken)
		{
			// Make sure the repair flag is reset
			await RunOptionalRepairAsync(CancellationToken);

			// Figure out what we need to do
			AddTransaction Transaction;
			using (Trace("GatherFilesToAdd"))
			using (ILoggerProgress Status = Logger.BeginProgressScope("Gathering files to add..."))
			{
				Stopwatch Timer = Stopwatch.StartNew();

				Transaction = new AddTransaction(Workspace, Stream, ContentIdToTrackedFile);
				Workspace = Transaction.NewWorkspaceRootDir;
				await SaveAsync(TransactionState.Dirty, CancellationToken);

				Status.Progress = $"({Timer.Elapsed.TotalSeconds:0.0}s)";
			}

			// Swap files in and out of the cache
			WorkspaceFileToMove[] FilesToMove = Transaction.FilesToMove.Values.ToArray();
			if(FilesToMove.Length > 0)
			{
				using (Trace("MoveFromCache"))
				using (ILoggerProgress Status = Logger.BeginProgressScope(String.Format("Moving {0} {1} from cache...", FilesToMove.Length, (FilesToMove.Length == 1)? "file" : "files")))
				{
					Stopwatch Timer = Stopwatch.StartNew();
					await ParallelTask.ForEachAsync(FilesToMove, FileToMove => MoveFileFromCache(FileToMove, Transaction.FilesToSync));
					ContentIdToTrackedFile = ContentIdToTrackedFile.Where(x => !Transaction.FilesToMove.ContainsKey(x.Value)).ToDictionary(x => x.Key, x => x.Value);
					Status.Progress = $"({Timer.Elapsed.TotalSeconds:0.0}s)";
				}
			}

			// Swap files in and out of the cache
			WorkspaceFileToCopy[] FilesToCopy = Transaction.FilesToCopy.ToArray();
			if(FilesToCopy.Length > 0)
			{
				using (Trace("CopyFiles"))
				using (ILoggerProgress Status = Logger.BeginProgressScope(String.Format("Copying {0} {1} within workspace...", FilesToCopy.Length, (FilesToCopy.Length == 1)? "file" : "files")))
				{
					Stopwatch Timer = Stopwatch.StartNew();
					await ParallelTask.ForEachAsync(FilesToCopy, FileToCopy => CopyFileWithinWorkspace(FileToCopy, Transaction.FilesToSync));
					Status.Progress = $"({Timer.Elapsed.TotalSeconds:0.0}s)";
				}
			}

			// Find all the files we want to sync
			WorkspaceFileToSync[] FilesToSync = Transaction.FilesToSync.ToArray();
			if(FilesToSync.Length > 0)
			{
				long SyncSize = FilesToSync.Sum(x => x.StreamFile.Length);

				// Make sure there's enough space on this drive
				if(RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
				{
					long FreeSpace = new DriveInfo(Path.GetPathRoot(BaseDir.FullName)).AvailableFreeSpace;
					if (FreeSpace - SyncSize < MinScratchSpace)
					{
						throw new InsufficientSpaceException($"Not enough space to sync new files (free space: {FreeSpace / (1024.0 * 1024.0):n1}mb, sync size: {SyncSize / (1024.0 * 1024.0):n1}mb, min scratch space: {MinScratchSpace / (1024.0 * 1024.0):n1}mb)");
					}
				}

				// Sync all the files
				using (Trace("SyncFiles"))
				using (ILoggerProgress Status = Logger.BeginProgressScope(String.Format("Syncing {0} {1} using {2} threads...", FilesToSync.Length, (FilesToSync.Length == 1)? "file" : "files", NumParallelSyncThreads)))
				{
					Stopwatch Timer = Stopwatch.StartNew();

					// Remove all the previous response files
					foreach (FileReference File in DirectoryReference.EnumerateFiles(BaseDir, "SyncList-*.txt"))
					{
						FileUtils.ForceDeleteFile(File);
					}

					// Create a list of all the batches that we want to sync
					List<(int, int)> Batches = new List<(int, int)>();
					for (int EndIdx = 0; EndIdx < FilesToSync.Length;)
					{
						int BeginIdx = EndIdx;

						// Figure out the next batch of files to sync
						long BatchSize = 0;
						for (; EndIdx < FilesToSync.Length && BatchSize < 256 * 1024 * 1024; EndIdx++)
						{
							BatchSize += FilesToSync[EndIdx].StreamFile.Length;
						}

						// Add this batch to the list
						Batches.Add((BeginIdx, EndIdx));
					}

					// The next batch to be synced
					int NextBatchIdx = 0;

					// Total size of synced files
					long SyncedSize = 0;

					// Spawn some background threads to sync them
					Dictionary<Task, int> Tasks = new Dictionary<Task, int>();
					while (Tasks.Count > 0 || NextBatchIdx < Batches.Count)
					{
						// Create new tasks
						while (Tasks.Count < NumParallelSyncThreads && NextBatchIdx < Batches.Count)
						{
							(int BatchBeginIdx, int BatchEndIdx) = Batches[NextBatchIdx];

							Task Task = Task.Run(() => SyncBatch(Client, FilesToSync, BatchBeginIdx, BatchEndIdx, bFakeSync, CancellationToken));
							Tasks[Task] = NextBatchIdx++;
						}

						// Wait for anything to complete
						Task CompleteTask = await Task.WhenAny(Tasks.Keys);
						int BatchIdx = Tasks[CompleteTask];
						Tasks.Remove(CompleteTask);

						// Update metadata for the complete batch
						(int BeginIdx, int EndIdx) = Batches[BatchIdx];
						await ParallelTask.ForAsync(BeginIdx, EndIdx, Idx => FilesToSync[Idx].WorkspaceFile.UpdateMetadata());

						// Save the current state every minute
						TimeSpan Elapsed = Timer.Elapsed;
						if (Elapsed > TimeSpan.FromMinutes(1.0))
						{
							await SaveAsync(TransactionState.Dirty, CancellationToken);
							Logger.LogInformation("Saved workspace state ({Elapsed:0.0}s)", (Timer.Elapsed - Elapsed).TotalSeconds);
							Timer.Restart();
						}

						// Update the status
						for (int Idx = BeginIdx; Idx < EndIdx; Idx++)
						{
							SyncedSize += FilesToSync[Idx].StreamFile.Length;
						}
						Status.Progress = String.Format("{0:n1}% ({1:n1}mb/{2:n1}mb)", SyncedSize * 100.0 / SyncSize, SyncedSize / (1024.0 * 1024.0), SyncSize / (1024.0 * 1024.0));
					}
				}
			}

			// Save the clean state
			Workspace = Transaction.NewWorkspaceRootDir;
			await SaveAsync(TransactionState.Clean, CancellationToken);
		}

		static readonly Utf8String StatsFileName = "StatsV2.json";

		static async Task LogPerforceCommandAsync(string ArgumentList, ILogger Logger)
		{
			Logger.LogInformation("Running command: {ArgList}", ArgumentList);
			using (ManagedProcessGroup ChildProcessGroup = new ManagedProcessGroup())
			{
				using (ManagedProcess ChildProcess = new ManagedProcess(ChildProcessGroup, "p4.exe", ArgumentList, null, null, null, ProcessPriorityClass.Normal))
				{
					for (; ; )
					{
						string? Line = await ChildProcess.ReadLineAsync();
						if (Line == null)
						{
							break;
						}
						Logger.LogInformation("{0}", Line);
					}
					ChildProcess.WaitForExit();
				}
			}
		}

		/// <summary>
		/// Syncs a batch of files
		/// </summary>
		/// <param name="Client">The client to sync</param>
		/// <param name="FilesToSync">List of files to sync</param>
		/// <param name="BeginIdx">First file to sync</param>
		/// <param name="EndIdx">Index of the last file to sync (exclusive)</param>
		/// <param name="bFakeSync">Whether to fake a sync</param>
		/// <param name="CancellationToken">Cancellation token for the request</param>
		/// <returns>Async task</returns>
		async Task SyncBatch(PerforceClientConnection Client, WorkspaceFileToSync[] FilesToSync, int BeginIdx, int EndIdx, bool bFakeSync, CancellationToken CancellationToken)
		{
			if (bFakeSync)
			{
				for (int Idx = BeginIdx; Idx < EndIdx; Idx++)
				{
					FileReference LocalFile = FilesToSync[Idx].WorkspaceFile.GetLocation();
					DirectoryReference.CreateDirectory(LocalFile.Directory);
					FileReference.WriteAllBytes(LocalFile, new byte[0]);
				}
			}
			else
			{
				FileReference SyncFileName = FileReference.Combine(BaseDir, $"SyncList-{BeginIdx}.txt");
				using (StreamWriter Writer = new StreamWriter(SyncFileName.FullName))
				{
					for (int Idx = BeginIdx; Idx < EndIdx; Idx++)
					{
						Writer.WriteLine("{0}#{1}", FilesToSync[Idx].StreamFile.Path, FilesToSync[Idx].StreamFile.Revision);
					}
				}

				WorkspaceFileToSync? StatsFile = FilesToSync[BeginIdx..EndIdx].FirstOrDefault(x => x.StreamFile.Path.EndsWith(StatsFileName));
				if(StatsFile != null)
				{
					for (int Idx = BeginIdx; Idx < EndIdx; Idx++)
					{
						Logger.LogInformation("Sync: {DepotFile}#{Revision}", FilesToSync[Idx].StreamFile.Path, FilesToSync[Idx].StreamFile.Revision);
					}
					await LogFortniteStatsInfoAsync(Client);
				}

				PerforceConnection ClientWithFileList = new PerforceConnection(Client);
				ClientWithFileList.GlobalOptions.Add($"-x\"{SyncFileName}\"");
				if (StatsFile != null)
				{
					ClientWithFileList.GlobalOptions.Add($"-Zdebug=dm=2");
				}

				List<SyncRecord> Records = await ClientWithFileList.SyncAsync(SyncOptions.Force | SyncOptions.FullDepotSyntax, -1, new string[0], CancellationToken);
				if (StatsFile != null)
				{
					try
					{
						foreach (SyncRecord Record in Records)
						{
							Logger.LogInformation("File: {DepotFile} {Revision} {Action}", Record.DepotFile, Record.Revision, Record.Action);
						}
						bool bMissing = await LogFortniteStatsInfoAsync(Client);

						Action<List<SyncRecord>> PrintSyncRecords = Records =>
						{
							foreach (SyncRecord Record in Records)
							{
								Logger.LogInformation("File: {DepotFile} {Revision} {Action}", Record.DepotFile, Record.Revision, Record.Action);
							}
						};

						FileReference LocalFile = FileReference.Combine(WorkspaceDir, "FortniteGame", "Content", "Backend", "StatsV2.json");
						if (!FileReference.Exists(LocalFile) || bMissing)
						{
							string[] Lines = await FileReference.ReadAllLinesAsync(SyncFileName);
							foreach (string Line in Lines)
							{
								Logger.LogInformation("SyncList: {Line}", Line);
							}

							string Connection = $"-p {Client.ServerAndPort} -u {Client.UserName} -c {Client.ClientName}";
							await LogPerforceCommandAsync($"{Connection} protects -M {LocalFile}", Logger);
							await LogPerforceCommandAsync($"{Connection} protects -M {LocalFile.FullName.ToLower()}", Logger);
							await LogPerforceCommandAsync($"{Connection} protects -M {LocalFile.FullName.Replace('\\', '/')}", Logger);

							for (int Idx = 0; ; Idx++)
							{
								Logger.LogInformation("Loop {Idx}: Check {File} - {State}", Idx, LocalFile, FileReference.Exists(LocalFile) ? "exists" : "does not exist");

								if (Idx == 0)
								{
									//
								}
								else if (Idx == 1)
								{
									Records = await ClientWithFileList.SyncAsync(SyncOptions.Force | SyncOptions.FullDepotSyntax, -1, new string[0], CancellationToken);
									PrintSyncRecords(Records);
								}
								else if (Idx == 2)
								{
									await Task.Delay(5000);
									Records = await ClientWithFileList.SyncAsync(SyncOptions.Force | SyncOptions.FullDepotSyntax, -1, new string[0], CancellationToken);
									PrintSyncRecords(Records);
								}
								else if (Idx == 3)
								{
									Records = await ClientWithFileList.SyncAsync(SyncOptions.Force, -1, new string[0], CancellationToken);
									PrintSyncRecords(Records);
								}
								else if (Idx == 4)
								{
									Records = await Client.SyncAsync(SyncOptions.Force, -1, new string[] { $"{StatsFile.StreamFile.Path}#{StatsFile.StreamFile.Revision}" }, CancellationToken);
									PrintSyncRecords(Records);
								}
								else if (Idx == 5)
								{
									Records = await Client.SyncAsync(SyncOptions.Force, -1, new string[] { LocalFile.FullName }, CancellationToken);
									PrintSyncRecords(Records);
								}
								else
								{
									break;
								}

								PerforceResponseList<FStatRecord> StatRecords = await Client.TryFStatAsync(FStatOptions.None, new[] { LocalFile.FullName }, CancellationToken.None);
								foreach (PerforceResponse<FStatRecord> StatResponse in StatRecords)
								{
									if (StatResponse.Succeeded)
									{
										FStatRecord StatRecord = StatResponse.Data;
										Logger.LogInformation("  File {File}, Action {Action}, Chg {Chg}, HeadChg {HeadChg}, HeadRev {HeadRev}, HaveRev {HaveRev}", StatRecord.DepotFile, StatRecord.Action, StatRecord.ChangeNumber, StatRecord.HeadChange, StatRecord.HeadRevision, StatRecord.HaveRevision);
									}
									else
									{
										Logger.LogInformation("  Response: {Response}", StatResponse.ToString());
									}
								}
							}
						}
					}
					catch (Exception Ex)
					{
						Logger.LogDebug(Ex, "Exception while checking for stats file");
					}
				}
			}
		}

		/// <summary>
		/// Helper function to move a file from the cache into the workspace. If it fails, adds the file to a list to be synced.
		/// </summary>
		/// <param name="FileToMove">Information about the file to move</param>
		/// <param name="FilesToSync">List of files to be synced. If the move fails, the file will be added to this list of files to sync.</param>
		void MoveFileFromCache(WorkspaceFileToMove FileToMove, ConcurrentQueue<WorkspaceFileToSync> FilesToSync)
		{
			try
			{
				FileReference.Move(FileToMove.TrackedFile.GetLocation(), FileToMove.WorkspaceFile.GetLocation());
			}
			catch(Exception Ex)
			{
				Logger.LogWarning(Ex, "warning: Unable to move {CacheFile} from cache to {WorkspaceFile}. Syncing instead.", FileToMove.TrackedFile.GetLocation(), FileToMove.WorkspaceFile.GetLocation());
				FilesToSync.Enqueue(new WorkspaceFileToSync(FileToMove.StreamFile, FileToMove.WorkspaceFile));
				bRequiresRepair = true;
			}
		}

		/// <summary>
		/// Helper function to copy a file within the workspace. If it fails, adds the file to a list to be synced.
		/// </summary>
		/// <param name="FileToCopy">Information about the file to move</param>
		/// <param name="FilesToSync">List of files to be synced. If the move fails, the file will be added to this list of files to sync.</param>
		void CopyFileWithinWorkspace(WorkspaceFileToCopy FileToCopy, ConcurrentQueue<WorkspaceFileToSync> FilesToSync)
		{
			try
			{
				FileReference.Copy(FileToCopy.SourceWorkspaceFile.GetLocation(), FileToCopy.TargetWorkspaceFile.GetLocation());
				FileToCopy.TargetWorkspaceFile.UpdateMetadata();
			}
			catch(Exception Ex)
			{
				Logger.LogWarning(Ex, "warning: Unable to copy {SourceFile} to {TargetFile}. Syncing instead.", FileToCopy.SourceWorkspaceFile.GetLocation(), FileToCopy.TargetWorkspaceFile.GetLocation());
				FilesToSync.Enqueue(new WorkspaceFileToSync(FileToCopy.StreamFile, FileToCopy.TargetWorkspaceFile));
				bRequiresRepair = true;
			}
		}

		void RemoveTrackedFile(CachedFileInfo TrackedFile)
		{
			ContentIdToTrackedFile.Remove(TrackedFile.ContentId);
			CacheEntries.Remove(TrackedFile.CacheId);
			FileUtils.ForceDeleteFile(TrackedFile.GetLocation());
		}

		void CreateCacheHierarchy()
		{
			for(int IdxA = 0; IdxA < 16; IdxA++)
			{
				DirectoryReference DirA = DirectoryReference.Combine(CacheDir, String.Format("{0:X}", IdxA));
				DirectoryReference.CreateDirectory(DirA);
				for(int IdxB = 0; IdxB < 16; IdxB++)
				{
					DirectoryReference DirB = DirectoryReference.Combine(DirA, String.Format("{0:X}", IdxB));
					DirectoryReference.CreateDirectory(DirB);
					for(int IdxC = 0; IdxC < 16; IdxC++)
					{
						DirectoryReference DirC = DirectoryReference.Combine(DirB, String.Format("{0:X}", IdxC));
						DirectoryReference.CreateDirectory(DirC);
					}
				}
			}
		}

		/// <summary>
		/// Determines a unique cache id for a file content id
		/// </summary>
		/// <param name="ContentId">File content id to get a unique id for</param>
		/// <returns>The unique cache id</returns>
		ulong GetUniqueCacheId(FileContentId ContentId)
		{
			// Initialize the cache id to the top 16 bytes of the digest, then increment it until we find a unique id
			ulong CacheId = 0;
			for(int Idx = 0; Idx < 8; Idx++)
			{
				CacheId = (CacheId << 8) | ContentId.Digest.Span[Idx];
			}
			while(!CacheEntries.Add(CacheId))
			{
				CacheId++;
			}
			return CacheId;
		}

		/// <summary>
		/// Sanitizes a string for use in a Perforce client name
		/// </summary>
		/// <param name="Text">Text to sanitize</param>
		/// <returns>Sanitized text</returns>
		static string Sanitize(string Text)
		{
			StringBuilder Result = new StringBuilder();
			for(int Idx = 0; Idx < Text.Length; Idx++)
			{
				if((Text[Idx] >= '0' && Text[Idx] <= '9') || (Text[Idx] >= 'a' && Text[Idx] <= 'z') || (Text[Idx] >= 'A' && Text[Idx] <= 'Z') || Text[Idx] == '-' || Text[Idx] == '_' || Text[Idx] == '.')
				{
					Result.Append(Text[Idx]);
				}
			}
			return Result.ToString();
		}

		/// <summary>
		/// Removes the leading slash from a path
		/// </summary>
		/// <param name="Path">The path to remove a slash from</param>
		/// <returns>The path without a leading slash</returns>
		static string RemoveLeadingSlash(string Path)
		{
			if(Path.Length > 0 && Path[0] == '/')
			{
				return Path.Substring(1);
			}
			else
			{
				return Path;
			}
		}

		/// <summary>
		/// Gets the path to a backup file used while a new file is being written out
		/// </summary>
		/// <param name="TargetFile">The file being written to</param>
		/// <returns>The path to a backup file</returns>
		private static FileReference GetBackupFile(FileReference TargetFile)
		{
			return new FileReference(TargetFile.FullName + ".transaction");
		}

		/// <summary>
		/// Begins a write transaction on the given file. Assumes only one process will be reading/writing at a time, but the operation can be interrupted.
		/// </summary>
		/// <param name="TargetFile">The file being written to</param>
		public static void BeginTransaction(FileReference TargetFile)
		{
			FileReference TransactionFile = GetBackupFile(TargetFile);
			if (FileReference.Exists(TargetFile))
			{
				FileUtils.ForceMoveFile(TargetFile, TransactionFile);
			}
			else if (FileReference.Exists(TransactionFile))
			{
				FileUtils.ForceDeleteFile(TransactionFile);
			}
		}

		/// <summary>
		/// Mark a transaction on the given file as complete, and removes the backup file.
		/// </summary>
		/// <param name="TargetFile">The file being written to</param>
		public static void CompleteTransaction(FileReference TargetFile)
		{
			FileReference TransactionFile = GetBackupFile(TargetFile);
			FileUtils.ForceDeleteFile(TransactionFile);
		}

		/// <summary>
		/// Restores the backup for a target file, if it exists. This allows recovery from an incomplete transaction.
		/// </summary>
		/// <param name="TargetFile">The file being written to</param>
		public static void RestoreBackup(FileReference TargetFile)
		{
			FileReference TransactionFile = GetBackupFile(TargetFile);
			if (FileReference.Exists(TransactionFile))
			{
				FileUtils.ForceMoveFile(TransactionFile, TargetFile);
			}
		}

		/// <summary>
		/// Creates a scoped trace object
		/// </summary>
		/// <param name="Operation">Name of the operation</param>
		/// <returns>Disposable object for the trace</returns>
		private IDisposable Trace(string Operation)
		{
			return TraceSpan.Create(Operation, Service: "hordeagent_repository");
		}

		#endregion
	}
}
