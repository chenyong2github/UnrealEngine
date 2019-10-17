// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;
using Tools.DotNETCommon.Perforce;

namespace BuildAgent.Workspace.Common
{
	/// <summary>
	/// Represents a repository of streams and cached data
	/// </summary>
	class Repository
	{
		/// <summary>
		/// The current transaction state. Used to determine whether a repository needs to be cleaned on startup.
		/// </summary>
		enum RepositoryState
		{
			Dirty,
			Clean,
		}

		/// <summary>
		/// The file signature and version. Update this to introduce breaking changes and ignore old repositories.
		/// </summary>
		const int CurrentSignature = ('W' << 24) | ('T' << 16) | 1;

		/// <summary>
		/// The current revision number.
		/// </summary>
		const int CurrentRevision = 1;

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
		/// Name of the server and port for 
		/// </summary>
		string ServerAndPort;

		/// <summary>
		/// Name of the Perforce user
		/// </summary>
		string UserName;

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
		/// Connection to the Perforce server
		/// </summary>
		PerforceConnection Perforce;

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
		HashSet<string> ClientNames = new HashSet<string>();

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
		Dictionary<FileContentId, TrackedFileInfo> ContentIdToTrackedFile = new Dictionary<FileContentId, TrackedFileInfo>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="ServerAndPort">Perforce server and port to connect to</param>
		/// <param name="UserName">Name of the user to login to Perforce with</param>
		/// <param name="HostName">Name of the current host</param>
		/// <param name="NextSequenceNumber">The next sequence number for operations</param>
		/// <param name="BaseDir">The root directory for the stash</param>
		private Repository(string ServerAndPort, string UserName, string HostName, uint NextSequenceNumber, DirectoryReference BaseDir)
		{
			// Make sure all the fields are valid
			if(UserName == null || HostName == null)
			{
				PerforceConnection Perforce = CreatePerforceConnection(ServerAndPort, UserName, null);

				InfoRecord Info = Perforce.Info(InfoOptions.ShortOutput).Data;
				if(UserName == null)
				{
					UserName = Info.UserName;
				}
				if(HostName == null)
				{
					HostName = Info.ClientHost;
				}
			}

			// Save the Perforce settings
			this.ServerAndPort = ServerAndPort;
			this.UserName = UserName;
			this.HostName = HostName;
			this.NextSequenceNumber = NextSequenceNumber;

			// Create the perforce connection
			Perforce = CreatePerforceConnection(ServerAndPort, UserName, null);

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
		/// Creates a repository at the given location
		/// </summary>
		/// <param name="ServerAndPort">The Perforce server and port</param>
		/// <param name="UserName">The Perforce username to connect with</param>
		/// <param name="BaseDir">The base directory for the repository</param>
		/// <returns>New repository instance</returns>
		public static Repository Create(string ServerAndPort, string UserName, DirectoryReference BaseDir)
		{
			Log.TraceInformation("Creating repository at {0}...", BaseDir);

			DirectoryReference.CreateDirectory(BaseDir);
			FileUtils.ForceDeleteDirectoryContents(BaseDir);

			Repository Repo = new Repository(ServerAndPort, UserName, null, 1, BaseDir);
			Repo.Save(RepositoryState.Clean);
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
		/// <param name="ServerAndPort">The Perforce server and port</param>
		/// <param name="UserName">The Perforce username to connect with</param>
		/// <param name="BaseDir">The base directory for the repository</param>
		public static Repository Load(string ServerAndPort, string UserName, DirectoryReference BaseDir)
		{
			if(!Exists(BaseDir))
			{
				throw new FatalErrorException("No valid repository found at {0}", BaseDir);
			}

			FileReference DataFile = FileReference.Combine(BaseDir, DataFileName);
			RestoreBackup(DataFile);

			Repository Repo;
			using(BinaryReader Reader = new BinaryReader(File.Open(DataFile.FullName, FileMode.Open, FileAccess.Read, FileShare.Read)))
			{
				int Revision = Reader.ReadInt32();
				if(Revision != CurrentRevision)
				{
					throw new FatalErrorException("Unsupported data format (revision {0}, expected {1})", Revision, CurrentRevision);
				}

				bool bRequiresRepair = Reader.ReadBoolean();
				Reader.ReadString(); // Originally client name; now ignored
				uint NextSequenceNumber = Reader.ReadUInt32();

				Repo = new Repository(ServerAndPort, UserName, null, NextSequenceNumber, BaseDir);
				Repo.bRequiresRepair = bRequiresRepair;

				int NumTrackedFiles = Reader.ReadInt32();
				for(int Idx = 0; Idx < NumTrackedFiles; Idx++)
				{
					TrackedFileInfo TrackedFile = new TrackedFileInfo(Repo.CacheDir, Reader);
					Repo.ContentIdToTrackedFile.Add(TrackedFile.ContentId, TrackedFile);
					Repo.CacheEntries.Add(TrackedFile.CacheId);
				}

				Repo.Workspace.Read(Reader);
			}

			Repo.RunOptionalRepair();
			return Repo;
		}

		/// <summary>
		/// Save the state of the repository
		/// </summary>
		private void Save(RepositoryState State)
		{
			FileReference DataFile = FileReference.Combine(BaseDir, DataFileName);

			BeginTransaction(DataFile);
			using(BinaryWriter Writer = new BinaryWriter(File.Open(DataFile.FullName, FileMode.Create, FileAccess.Write, FileShare.Read)))
			{
				Writer.Write(CurrentRevision);
				Writer.Write(bRequiresRepair || (State != RepositoryState.Clean));
				Writer.Write(""); // Originally client name; now ignored
				Writer.Write(NextSequenceNumber);

				Writer.Write(ContentIdToTrackedFile.Count);
				foreach(TrackedFileInfo TrackedFile in ContentIdToTrackedFile.Values)
				{
					TrackedFile.Write(Writer);
				}

				Workspace.Write(Writer);
			}
			CompleteTransaction(DataFile);
		}

		/// <summary>
		/// Creates a Perforce connection for the given settings
		/// </summary>
		/// <param name="ServerAndPort">The name and port for the server</param>
		/// <param name="UserName">The username to connect to the server with</param>
		/// <param name="ClientName">Name of the client to use when syncing files</param>
		/// <returns>New Perforce connection instance</returns>
		static PerforceConnection CreatePerforceConnection(string ServerAndPort, string UserName, string ClientName)
		{
			// Create the Perforce connection
			List<string> GlobalOptions = new List<string>();
			GlobalOptions.Add("-zprog=WorkspaceTool");
			GlobalOptions.Add(String.Format("-zversion={0}", Assembly.GetExecutingAssembly().GetName().Version.ToString()));
			if(ServerAndPort != null)
			{
				GlobalOptions.Add(String.Format("-p {0}", ServerAndPort));
			}
			if(UserName != null)
			{
				GlobalOptions.Add(String.Format("-u {0}", UserName));
			}
			if(ClientName != null)
			{
				GlobalOptions.Add(String.Format("-c {0}", ClientName));
			}
			return new PerforceConnection(String.Join(" ", GlobalOptions));
		}

		#region Commands

		/// <summary>
		/// Cleans the current workspace
		/// </summary>
		public void Clean()
		{
			Stopwatch Timer = Stopwatch.StartNew();

			Log.TraceInformation("Cleaning workspace...");
			using(LogIndentScope Indent = new LogIndentScope("  "))
			{
				CleanInternal();
			}

			Log.TraceInformation("Completed in {0:0.0}s", Timer.Elapsed.TotalSeconds);
		}

		/// <summary>
		/// Cleans the current workspace
		/// </summary>
		private void CleanInternal()
		{
			FileInfo[] FilesToDelete;
			DirectoryInfo[] DirectoriesToDelete;
			using(LogStatusScope Status = new LogStatusScope("Finding files to clean..."))
			{
				Stopwatch Timer = Stopwatch.StartNew();

				Workspace.Refresh(out FilesToDelete, out DirectoriesToDelete);

				Status.SetProgress("({0:0.0}s)", Timer.Elapsed.TotalSeconds);
			}

			if(FilesToDelete.Length > 0 || DirectoriesToDelete.Length > 0)
			{
				List<string> Paths = new List<string>();
				Paths.AddRange(DirectoriesToDelete.Select(x => String.Format("/{0}/...", new DirectoryReference(x).MakeRelativeTo(WorkspaceDir).Replace(Path.DirectorySeparatorChar, '/'))));
				Paths.AddRange(FilesToDelete.Select(x => String.Format("/{0}", new FileReference(x).MakeRelativeTo(WorkspaceDir).Replace(Path.DirectorySeparatorChar, '/'))));

				foreach(string Path in Paths.OrderBy(x => x))
				{
					Log.TraceInformation("  {0}", Path);
				}

				using(LogStatusScope Scope = new LogStatusScope("Cleaning files..."))
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
					Scope.SetProgress("({0:0.0}s)", Timer.Elapsed.TotalSeconds);
				}

				Save(RepositoryState.Clean);
			}
		}

		/// <summary>
		/// Empties the staging directory of any staged files
		/// </summary>
		public void Clear()
		{
			Stopwatch Timer = Stopwatch.StartNew();

			Log.TraceInformation("Clearing workspace...");
			using(LogIndentScope Indent = new LogIndentScope("  "))
			{
				CleanInternal();
				RemoveFilesFromWorkspace(new StreamDirectoryInfo());
				Save(RepositoryState.Clean);
			}

			Log.TraceInformation("Completed in {0:0.0}s", Timer.Elapsed.TotalSeconds);
		}

		/// <summary>
		/// Dumps the contents of the repository to the log for analysis
		/// </summary>
		public void Dump()
		{
			Stopwatch Timer = Stopwatch.StartNew();

			Log.TraceInformation("Dumping repository to log...");

			WorkspaceFileInfo[] WorkspaceFiles = Workspace.GetFiles().OrderBy(x => x.GetLocation().FullName).ToArray();
			if(WorkspaceFiles.Length > 0)
			{
				Log.TraceLog("  Workspace:");
				foreach(WorkspaceFileInfo File in WorkspaceFiles)
				{
					Log.TraceLog("    {0,-128} [{1,-48}] [{2,20:n0}] [{3,20}]{4}", File.GetClientPath(), File.ContentId, File.Length, File.LastModifiedTicks, File.bReadOnly? "" : " [ writable ]");
				}
			}

			if(ContentIdToTrackedFile.Count > 0)
			{
				Log.TraceLog("  Cache:");
				foreach(KeyValuePair<FileContentId, TrackedFileInfo> Pair in ContentIdToTrackedFile)
				{
					Log.TraceLog("    {0,-128} [{1,-48}] [{2,20:n0}] [{3,20}]{4}", Pair.Value.GetLocation(), Pair.Key, Pair.Value.Length, Pair.Value.LastModifiedTicks, Pair.Value.bReadOnly? "" : "[ writable ]");
				}
			}

			Log.TraceInformation("Completed in {0:0.0}s", Timer.Elapsed.TotalSeconds);
		}

		/// <summary>
		/// Checks the integrity of the cache
		/// </summary>
		public void Repair()
		{
			using(LogStatusScope Status = new LogStatusScope("Checking cache..."))
			{
				// Make sure all the folders exist in the cache
				CreateCacheHierarchy();

				// Check that all the files in the cache appear as we expect them to
				List<TrackedFileInfo> TrackedFiles = ContentIdToTrackedFile.Values.ToList();
				foreach(TrackedFileInfo TrackedFile in TrackedFiles)
				{
					if(!TrackedFile.CheckIntegrity())
					{
						RemoveTrackedFile(TrackedFile);
					}
				}

				// Clear the repair flag
				bRequiresRepair = false;

				Save(RepositoryState.Clean);

				Status.SetProgress("Done");
			}
		}

		/// <summary>
		/// Cleans the current workspace
		/// </summary>
		public void Revert(string ClientName)
		{
			Stopwatch Timer = Stopwatch.StartNew();

			PerforceConnection Client = CreatePerforceConnection(ServerAndPort, UserName, ClientName);
			RevertInternal(Client, ClientName);

			Log.TraceInformation("Completed in {0:0.0}s", Timer.Elapsed.TotalSeconds);
		}

		/// <summary>
		/// Checks the bRequiresRepair flag, and repairs/resets it if set.
		/// </summary>
		private void RunOptionalRepair()
		{
			if(bRequiresRepair)
			{
				Repair();
			}
		}

		/// <summary>
		/// Shrink the size of the cache to the given size
		/// </summary>
		/// <param name="MaxSize">The maximum cache size, in bytes</param>
		public void Purge(long MaxSize)
		{
			Log.TraceInformation("Purging cache (limit {0:n0} bytes)...", MaxSize);
			using(new LogIndentScope("  "))
			{
				List<TrackedFileInfo> CachedFiles = ContentIdToTrackedFile.Values.OrderBy(x => x.SequenceNumber).ToList();

				int NumRemovedFiles = 0;
				long TotalSize = CachedFiles.Sum(x => x.Length);

				while(MaxSize < TotalSize && NumRemovedFiles < CachedFiles.Count)
				{
					TrackedFileInfo File = CachedFiles[NumRemovedFiles];

					RemoveTrackedFile(File);
					TotalSize -= File.Length;

					NumRemovedFiles++;
				}

				Save(RepositoryState.Clean);

				Log.TraceInformation("{0} files removed, {1} files remaining, new size {2:n0} bytes.", NumRemovedFiles, CachedFiles.Count - NumRemovedFiles, TotalSize);
			}
		}

		/// <summary>
		/// Configures the client for the given stream
		/// </summary>
		/// <param name="ClientName">Name of the client</param>
		/// <param name="StreamName">Name of the stream</param>
		public void Setup(string ClientName, string StreamName)
		{
			UpdateClient(ClientName, StreamName);
		}

		/// <summary>
		/// Prints stats showing coherence between different streams
		/// </summary>
		public void Stats(string ClientName, List<string> StreamNames, List<string> Filters)
		{
			Log.TraceInformation("Finding stats for {0} streams", StreamNames.Count);
			using (new LogIndentScope("  "))
			{
				// Update the list of files in each stream
				Tuple<int, StreamDirectoryInfo>[] StreamState = new Tuple<int, StreamDirectoryInfo>[StreamNames.Count];
				for (int Idx = 0; Idx < StreamNames.Count; Idx++)
				{
					string StreamName = StreamNames[Idx];
					Log.TraceInformation("Finding contents of {0}:", StreamName);

					using (new LogIndentScope("  "))
					{
						ClientNames.Remove(ClientName); // Force the client to be updated

						PerforceConnection Client = UpdateClient(ClientName, StreamName);

						int ChangeNumber = GetLatestClientChange(Client, ClientName);
						Log.TraceInformation("Latest change is CL {0}", ChangeNumber);

						RevertInternal(Client, ClientName);
						ClearClientHaveTable(Client, ClientName);
						UpdateClientHaveTable(Client, ClientName, ChangeNumber, Filters);

						StreamDirectoryInfo Contents = FindClientContents(Client, ClientName, ChangeNumber, false);
						StreamState[Idx] = Tuple.Create(ChangeNumber, Contents);

						GC.Collect();
					}
				}

				// Find stats for 
				using (LogStatusScope Scope = new LogStatusScope("Finding usage stats..."))
				{
					Stopwatch Timer = Stopwatch.StartNew();

					// Find the set of files in each stream
					HashSet<FileContentId>[] FilesInStream = new HashSet<FileContentId>[StreamNames.Count];
					for (int Idx = 0; Idx < StreamNames.Count; Idx++)
					{
						List<StreamFileInfo> Files = StreamState[Idx].Item2.GetFiles();
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
						List<StreamFileInfo> Files = StreamState[RowIdx].Item2.GetFiles();
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
					Log.TraceInformation("");
					Log.TraceInformation("Each row shows the size of files in a stream which are unique to that stream compared to each column:");
					Log.TraceInformation("");
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
						Log.TraceInformation("{0}", Row.ToString());
					}
					Log.TraceInformation("");

					Scope.SetProgress("({0:0.0}s)", Timer.Elapsed.TotalSeconds);
				}
			}
		}

		/// <summary>
		/// Prints information about the repository state
		/// </summary>
		public void Status()
		{
			// Print size stats
			Log.TraceInformation("Cache contains {0:n0} files, {1:n1}mb", ContentIdToTrackedFile.Count, ContentIdToTrackedFile.Values.Sum(x => x.Length) / (1024.0 * 1024.0));
			Log.TraceInformation("Stage contains {0:n0} files, {1:n1}mb", Workspace.GetFiles().Count, Workspace.GetFiles().Sum(x => x.Length) / (1024.0 * 1024.0));

			// Print the contents of the workspace
			string[] Differences = Workspace.FindDifferences();
			if(Differences.Length > 0)
			{
				Log.TraceInformation("Local changes:");
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
					Log.TraceInformation("  {0}", Difference);
				}
				Console.ResetColor();
			}
		}

		/// <summary>
		/// Switches to the given stream
		/// </summary>
		/// <param name="ClientName">Name of the client to sync</param>
		/// <param name="StreamName">Name of the stream to sync</param>
		/// <param name="ChangeNumber">Changelist number to sync. -1 to sync to latest.</param>
		/// <param name="Filters">List of filters to be applied to the workspace</param>
		/// <param name="bFakeSync">Whether to simulate the syncing operation rather than actually getting files from the server</param>
		/// <param name="CacheFile">If set, uses the given file to cache the contents of the workspace. This can improve sync times when multiple machines sync the same workspace.</param>
		public void Sync(string ClientName, string StreamName, int ChangeNumber, List<string> Filters, bool bFakeSync, FileReference CacheFile)
		{
			Stopwatch Timer = Stopwatch.StartNew();
			if(ChangeNumber == -1)
			{
				Log.TraceInformation("Syncing to {0} at latest", StreamName);
			}
			else
			{
				Log.TraceInformation("Syncing to {0} at CL {1}", StreamName, ChangeNumber);
			}

			using(LogIndentScope Indent = new LogIndentScope("  "))
			{
				// Update the client to the current stream
				PerforceConnection Client = UpdateClient(ClientName, StreamName);

				// Get the latest change number
				if(ChangeNumber == -1)
				{
					ChangeNumber = GetLatestClientChange(Client, ClientName);
				}

				// Revert any open files
				RevertInternal(Client, ClientName);

				// Force the P4 metadata to match up
				UpdateClientHaveTable(Client, ClientName, ChangeNumber, Filters);

				// Clean the current workspace
				CleanInternal();

				// Update the state of the current stream, if necessary
				StreamDirectoryInfo Contents;
				if(CacheFile == null)
				{
					Contents = FindClientContents(Client, ClientName, ChangeNumber, bFakeSync);
				}
				else
				{
					if(FileReference.Exists(CacheFile))
					{
						Contents = LoadClientContents(CacheFile);
					}
					else
					{
						Contents = FindAndSaveClientContents(Client, ClientName, ChangeNumber, bFakeSync, CacheFile);
					}
				}

				// Sync all the appropriate files
				RemoveFilesFromWorkspace(Contents);
				AddFilesToWorkspace(Client, Contents, bFakeSync);
			}

			Log.TraceInformation("Completed in {0:0.0}s", Timer.Elapsed.TotalSeconds);
		}

		/// <summary>
		/// Populates the cache with the head revision of the given streams.
		/// </summary>
		public void Populate(List<KeyValuePair<string, string>> ClientAndStreamNames, List<string> Filters, bool bFakeSync)
		{
			Log.TraceInformation("Populating with {0} streams", ClientAndStreamNames.Count);
			using(new LogIndentScope("  "))
			{
				// Clean the current workspace
				Clean();

				// Update the list of files in each stream
				Tuple<int, StreamDirectoryInfo>[] StreamState = new Tuple<int, StreamDirectoryInfo>[ClientAndStreamNames.Count];
				for(int Idx = 0; Idx < ClientAndStreamNames.Count; Idx++)
				{
					string ClientName = ClientAndStreamNames[Idx].Key;
					string StreamName = ClientAndStreamNames[Idx].Value;
					Log.TraceInformation("Finding contents of {0}:", StreamName);

					using(new LogIndentScope("  "))
					{
						PerforceConnection Client = UpdateClient(ClientName, StreamName);

						int ChangeNumber = GetLatestClientChange(Client, ClientName);
						Log.TraceInformation("Latest change is CL {0}", ChangeNumber);

						RevertInternal(Client, ClientName);
						ClearClientHaveTable(Client, ClientName);
						UpdateClientHaveTable(Client, ClientName, ChangeNumber, Filters);

						StreamDirectoryInfo Contents = FindClientContents(Client, ClientName, ChangeNumber, bFakeSync);
						StreamState[Idx] = Tuple.Create(ChangeNumber, Contents);

						GC.Collect();
					}
				}

				// Remove any files from the workspace not referenced by the first stream. This ensures we can purge things from the cache that we no longer need.
				if(ClientAndStreamNames.Count > 0)
				{
					RemoveFilesFromWorkspace(StreamState[0].Item2);
				}

				// Shrink the contents of the cache
				using(LogStatusScope Status = new LogStatusScope("Updating cache..."))
				{
					Stopwatch Timer = Stopwatch.StartNew();

					HashSet<FileContentId> CommonContentIds = new HashSet<FileContentId>();
					Dictionary<FileContentId, long> ContentIdToLength = new Dictionary<FileContentId, long>();
					for(int Idx = 0; Idx < ClientAndStreamNames.Count; Idx++)
					{
						List<StreamFileInfo> Files = StreamState[Idx].Item2.GetFiles();
						foreach(StreamFileInfo File in Files)
						{
							ContentIdToLength[File.ContentId] = File.Length;
						}

						if(Idx == 0)
						{
							CommonContentIds.UnionWith(Files.Select(x => x.ContentId));
						}
						else
						{
							CommonContentIds.IntersectWith(Files.Select(x => x.ContentId));
						}
					}

					List<TrackedFileInfo> TrackedFiles = ContentIdToTrackedFile.Values.ToList();
					foreach(TrackedFileInfo TrackedFile in TrackedFiles)
					{
						if(!ContentIdToLength.ContainsKey(TrackedFile.ContentId))
						{
							RemoveTrackedFile(TrackedFile);
						}
					}

					GC.Collect();

					double TotalSize = ContentIdToLength.Sum(x => x.Value) / (1024.0 * 1024.0);
					Status.SetProgress("{0:n1}mb total, {1:n1}mb differences ({2:0.0}s)", TotalSize, TotalSize - CommonContentIds.Sum(x => ContentIdToLength[x]) / (1024.0 * 1024.0), Timer.Elapsed.TotalSeconds);
				}

				// Sync all the new files
				for(int Idx = 0; Idx < ClientAndStreamNames.Count; Idx++)
				{
					string ClientName = ClientAndStreamNames[Idx].Key;
					string StreamName = ClientAndStreamNames[Idx].Value;
					Log.TraceInformation("Syncing files for {0}:", StreamName);

					using(new LogIndentScope("  "))
					{
						PerforceConnection Client = UpdateClient(ClientName, StreamName);

						int ChangeNumber = StreamState[Idx].Item1;
						UpdateClientHaveTable(Client, ClientName, ChangeNumber, Filters);

						StreamDirectoryInfo Contents = StreamState[Idx].Item2;
						RemoveFilesFromWorkspace(Contents);
						AddFilesToWorkspace(Client, Contents, bFakeSync);
					}
				}

				// Save the new repo state
				Save(RepositoryState.Clean);
			}
		}

		#endregion

		#region Core operations

		/// <summary>
		/// Sets the stream for the current client
		/// </summary>
		/// <param name="ClientName">Name of the client</param>
		/// <param name="StreamName">New stream for the client</param>
		private PerforceConnection UpdateClient(string ClientName, string StreamName)
		{
			// Create or update the client if it doesn't exist already
			if(ClientNames.Add(ClientName))
			{
				using(LogStatusScope Status = new LogStatusScope("Updating client..."))
				{
					Stopwatch Timer = Stopwatch.StartNew();

					ClientRecord Client = new ClientRecord();
					Client.Name = ClientName;
					Client.Owner = UserName;
					Client.Host = HostName;
					Client.Stream = StreamName;
					Client.Root = WorkspaceDir.FullName;
					Client.Type = "partitioned";

					PerforceResponse Response = Perforce.CreateClient(Client);
					if(!Response.Succeeded)
					{
						Perforce.DeleteClient(DeleteClientOptions.None, ClientName);
						Perforce.CreateClient(Client).RequireSuccess();
					}

					Status.SetProgress("({0:0.0}s)", Timer.Elapsed.TotalSeconds);
				}
			}

			// Update the config file with the name of the client
			FileReference ConfigFile = FileReference.Combine(BaseDir, "p4.ini");
			using(StreamWriter Writer = new StreamWriter(ConfigFile.FullName))
			{
				Writer.WriteLine("P4CLIENT={0}", ClientName);
			}

			// Return a Perforce connection for this client
			return CreatePerforceConnection(ServerAndPort, UserName, ClientName);
		}

		/// <summary>
		/// Get the latest change number in the current client
		/// </summary>
		/// <param name="Client">The perforce client connection</param>
		/// <param name="ClientName">Name of the current client</param>
		/// <returns>The latest submitted change number</returns>
		private int GetLatestClientChange(PerforceConnection Client, string ClientName)
		{
			int ChangeNumber;
			using(LogStatusScope Status = new LogStatusScope("Finding latest change..."))
			{
				Stopwatch Timer = Stopwatch.StartNew();

				PerforceResponseList<ChangesRecord> Changes = Client.Changes(ChangesOptions.None, 1, ChangeStatus.Submitted, String.Format("//{0}/...", ClientName));
				Changes.RequireSuccess();
				ChangeNumber = Changes[0].Data.Number;

				Status.SetProgress("CL {0} ({1:0.0}s)", ChangeNumber, Timer.Elapsed.TotalSeconds);
			}
			return ChangeNumber;
		}

		/// <summary>
		/// Revert all files that are open in the current workspace. Does not replace them with valid revisions.
		/// </summary>
		/// <param name="Client">The current client connection</param>
		/// <param name="ClientName">Name of the current client</param>
		private void RevertInternal(PerforceConnection Client, string ClientName)
		{
			using(LogStatusScope Status = new LogStatusScope("Reverting changes..."))
			{
				Stopwatch Timer = Stopwatch.StartNew();

				// Get a list of open files
				PerforceResponseList<FStatRecord> OpenedFilesResponse = Client.Opened(OpenedOptions.ShortOutput, -1, ClientName, null, 1);
				OpenedFilesResponse.RequireSuccess();

				// If there are any files, revert them
				if(OpenedFilesResponse.Any())
				{
					Client.Revert(-1, null, RevertOptions.KeepWorkspaceFiles, "//...").RequireSuccess();
				}

				// Find all the open changes
				PerforceResponseList<ChangesRecord> Changes = Client.Changes(ChangesOptions.None, ClientName, -1, ChangeStatus.Pending, null);
				Changes.RequireSuccess();

				// Delete the changelist
				foreach(ChangesRecord Change in Changes.Data)
				{
					// Find a list of shelved changes
					PerforceResponseList<DescribeRecord> DescribeResponse = Client.Describe(DescribeOptions.Shelved, -1, Change.Number);
					DescribeResponse.RequireSuccess();

					// Delete the shelved files
					foreach(DescribeRecord Record in DescribeResponse.Data)
					{
						if(Record.Files.Count > 0)
						{
							Client.DeleteShelvedFiles(Record.Number).RequireSuccess();
						}
					}

					// Delete the changelist
					Client.DeleteChange(DeleteChangeOptions.None, Change.Number).RequireSuccess();
				}

				Status.SetProgress("({0:0.0}s)", Timer.Elapsed.TotalSeconds);
			}
		}

		/// <summary>
		/// Clears the have table. This ensures that we'll always fetch the names of files at head revision, which aren't updated otherwise.
		/// </summary>
		/// <param name="Client">The client connection</param>
		/// <param name="ClientName">Name of the current client</param>
		private void ClearClientHaveTable(PerforceConnection Client, string ClientName)
		{
			using(LogStatusScope Scope = new LogStatusScope("Clearing have table..."))
			{
				Stopwatch Timer = Stopwatch.StartNew();
				Client.SyncQuiet(SyncOptions.KeepWorkspaceFiles, -1, String.Format("//{0}/...#0", ClientName)).RequireSuccess();
				Scope.SetProgress("({0:0.0}s)", Timer.Elapsed.TotalSeconds);
			}
		}

		/// <summary>
		/// Updates the have table to reflect the given stream
		/// </summary>
		/// <param name="Client">The client connection</param>
		/// <param name="ClientName">Name of the current client</param>
		/// <param name="ChangeNumber">The change number to sync. May be -1, for latest.</param>
		/// <param name="Filters">List of filters to apply to the workspace. Each entry should be a path relative to the stream root, with an optional '-'prefix.</param>
		private void UpdateClientHaveTable(PerforceConnection Client, string ClientName, int ChangeNumber, List<string> Filters)
		{
			using(LogStatusScope Scope = new LogStatusScope("Updating have table..."))
			{
				Stopwatch Timer = Stopwatch.StartNew();

				// Sync an initial set of files. Either start with a full workspace and remove files, or start with nothing and add files.
				if(Filters.Count == 0 || Filters[0].StartsWith("-"))
				{
					Client.SyncQuiet(SyncOptions.KeepWorkspaceFiles, -1, String.Format("//{0}/...@{1}", ClientName, ChangeNumber)).RequireSuccess();
				}
				else
				{
					Client.SyncQuiet(SyncOptions.KeepWorkspaceFiles, -1, String.Format("//{0}/...#0", ClientName)).RequireSuccess();
				}

				// Update with the contents of each filter
				foreach(string Filter in Filters)
				{
					string SyncPath;
					if(Filter.StartsWith("-"))
					{
						SyncPath = String.Format("//{0}/{1}#0", ClientName, RemoveLeadingSlash(Filter.Substring(1)));
					}
					else
					{
						SyncPath = String.Format("//{0}/{1}@{2}", ClientName, RemoveLeadingSlash(Filter), ChangeNumber);
					}
					Client.SyncQuiet(SyncOptions.KeepWorkspaceFiles, -1, SyncPath);
				}

				Scope.SetProgress("({0:0.0}s)", Timer.Elapsed.TotalSeconds);
			}
		}

		/// <summary>
		/// Get the contents of the client, as synced.
		/// </summary>
		/// <param name="Client">The client connection</param>
		/// <param name="ClientName">Name of the current client</param>
		/// <param name="ChangeNumber">The change number being synced. This must be specified in order to get the digest at the correct revision.</param>
		/// <param name="bFakeSync">Whether this is for a fake sync. Poisons the file type to ensure that the cache is not corrupted.</param>
		private StreamDirectoryInfo FindClientContents(PerforceConnection Client, string ClientName, int ChangeNumber, bool bFakeSync)
		{
			StreamDirectoryInfo Contents = new StreamDirectoryInfo();
			using(LogStatusScope Scope = new LogStatusScope("Fetching metadata..."))
			{
				Stopwatch Timer = Stopwatch.StartNew();

				// Find all the files in the branch
				PerforceResponseList<FStatRecord> Response = Client.FStat(FStatOptions.OnlyHave | FStatOptions.IncludeFileSizes | FStatOptions.ClientFileInPerforceSyntax | FStatOptions.ShortenOutput, string.Format("//{0}/...@{1}", ClientName, ChangeNumber));
				Response.RequireSuccess();

				// Get the expected prefix for all files in client syntax
				string ClientPrefix = string.Format("//{0}/", ClientName);

				// Create the workspace, and add records for all the files. Exclude deleted files with digest = null.
				foreach(FStatRecord Record in Response.Data)
				{
					if(!Record.ClientFile.StartsWith(ClientPrefix))
					{
						throw new InvalidDataException(String.Format("Client path returned by Perforce ('{0}') does not begin with client name ('{1}')", Record.ClientFile, ClientPrefix));
					}
					if(Record.Digest != null)
					{
						StreamDirectoryInfo LastStreamDirectory = Contents;
					
						string[] Fragments = PerforceUtils.UnescapePath(Record.ClientFile.Substring(ClientPrefix.Length)).Split('/');
						for(int Idx = 0; Idx < Fragments.Length - 1; Idx++)
						{
							StreamDirectoryInfo NextStreamDirectory;
							if(!LastStreamDirectory.NameToSubDirectory.TryGetValue(Fragments[Idx], out NextStreamDirectory))
							{
								NextStreamDirectory = new StreamDirectoryInfo(Fragments[Idx], LastStreamDirectory);
								LastStreamDirectory.NameToSubDirectory.Add(Fragments[Idx], NextStreamDirectory);
							}
							LastStreamDirectory = NextStreamDirectory;
						}

						byte[] Digest = StringUtils.ParseHexString(Record.Digest);
						FileContentId ContentId = new FileContentId(Digest, Record.HeadType + (bFakeSync? "+fake" : ""));

						string DepotFileAndRevision = String.Format("{0}#{1}", Record.DepotFile, Record.HaveRevision);
						LastStreamDirectory.NameToFile.Add(Fragments[Fragments.Length - 1], new StreamFileInfo(Fragments[Fragments.Length - 1], Record.FileSize, ContentId, LastStreamDirectory, DepotFileAndRevision));
					}
				}

				Scope.SetProgress("({0:0.0}s)", Timer.Elapsed.TotalSeconds);
			}
			return Contents;
		}

		/// <summary>
		/// Loads the contents of a client from disk
		/// </summary>
		/// <param name="CacheFile">The cache file to read from</param>
		/// <returns>Contents of the workspace</returns>
		StreamDirectoryInfo LoadClientContents(FileReference CacheFile)
		{
			StreamDirectoryInfo Contents;
			using(LogStatusScope Scope = new LogStatusScope("Reading cached metadata from {0}...", CacheFile))
			{
				Stopwatch Timer = Stopwatch.StartNew();
				Contents = StreamDirectoryInfo.Load(CacheFile);
				Scope.SetProgress("({0:0.0}s)", Timer.Elapsed.TotalSeconds);
			}
			return Contents;
		}

		/// <summary>
		/// Finds the contents of a workspace, and saves it to disk
		/// </summary>
		/// <param name="Client">The client connection</param>
		/// <param name="ClientName">Name of the current client</param>
		/// <param name="ChangeNumber">The change number being synced. This must be specified in order to get the digest at the correct revision.</param>
		/// <param name="bFakeSync">Whether this is for a fake sync. Poisons the file type to ensure that the cache is not corrupted.</param>
		/// <param name="CacheFile">Location of the file to save the cached contents</param>
		/// <returns>Contents of the workspace</returns>
		private StreamDirectoryInfo FindAndSaveClientContents(PerforceConnection Client, string ClientName, int ChangeNumber, bool bFakeSync, FileReference CacheFile)
		{
			StreamDirectoryInfo Contents = FindClientContents(Client, ClientName, ChangeNumber, bFakeSync);
			using(LogStatusScope Scope = new LogStatusScope("Saving metadata to {0}...", CacheFile))
			{
				Stopwatch Timer = Stopwatch.StartNew();

				// Handle the case where two machines may try to write to the cache file at once by writing to a temporary file
				FileReference TempCacheFile = new FileReference(String.Format("{0}.{1}", CacheFile, Guid.NewGuid()));
				Contents.Save(TempCacheFile);

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

				Scope.SetProgress("({0:0.0}s)", Timer.Elapsed.TotalSeconds);
			}
			return Contents;
		}

		/// <summary>
		/// Remove files from the workspace
		/// </summary>
		/// <param name="Contents">Contents of the target stream</param>
		private void RemoveFilesFromWorkspace(StreamDirectoryInfo Contents)
		{
			// Make sure the repair flag is clear before we start
			RunOptionalRepair();

			// Figure out what to remove
			WorkspaceTransactionRemove Transaction;
			using(LogStatusScope Scope = new LogStatusScope("Gathering files to remove..."))
			{
				Stopwatch Timer = Stopwatch.StartNew();

				Transaction = new WorkspaceTransactionRemove(Workspace, Contents, ContentIdToTrackedFile);

				Scope.SetProgress("({0:0.0}s)", Timer.Elapsed.TotalSeconds);
			}

			// Move files into the cache
			KeyValuePair<FileContentId, WorkspaceFileInfo>[] FilesToMove = Transaction.FilesToMove.ToArray();
			if(FilesToMove.Length > 0)
			{
				using(LogStatusScope Scope = new LogStatusScope("Moving {0} {1} to cache...", FilesToMove.Length, (FilesToMove.Length == 1)? "file" : "files"))
				{
					Stopwatch Timer = Stopwatch.StartNew();

					// Add any new files to the cache
					List<KeyValuePair<FileReference, FileReference>> SourceAndTargetFiles = new List<KeyValuePair<FileReference, FileReference>>();
					foreach(KeyValuePair<FileContentId, WorkspaceFileInfo> FileToMove in FilesToMove)
					{
						ulong CacheId = GetUniqueCacheId(FileToMove.Key);
						TrackedFileInfo NewTrackingInfo = new TrackedFileInfo(CacheDir, FileToMove.Key, CacheId, FileToMove.Value.Length, FileToMove.Value.LastModifiedTicks, FileToMove.Value.bReadOnly, NextSequenceNumber);
						ContentIdToTrackedFile.Add(FileToMove.Key, NewTrackingInfo);
						SourceAndTargetFiles.Add(new KeyValuePair<FileReference, FileReference>(FileToMove.Value.GetLocation(), NewTrackingInfo.GetLocation()));
					}
					NextSequenceNumber++;

					// Save the current state of the repository as dirty. If we're interrupted, we will have two places to check for each file (the cache and workspace).
					Save(RepositoryState.Dirty);

					// Execute all the moves and deletes
					Parallel.ForEach(SourceAndTargetFiles, SourceAndTargetFile => FileUtils.ForceMoveFile(SourceAndTargetFile.Key, SourceAndTargetFile.Value));

					Scope.SetProgress("({0:0.0}s)", Timer.Elapsed.TotalSeconds);
				}
			}

			// Remove files which are no longer needed
			WorkspaceFileInfo[] FilesToDelete = Transaction.FilesToDelete.ToArray();
			if(FilesToDelete.Length > 0)
			{
				using(LogStatusScope Scope = new LogStatusScope("Deleting {0} {1}...", FilesToDelete.Length, (FilesToDelete.Length == 1)? "file" : "files"))
				{
					Stopwatch Timer = Stopwatch.StartNew();

					Parallel.ForEach(FilesToDelete, FileToDelete => RemoveFile(FileToDelete));

					Scope.SetProgress("({0:0.0}s)", Timer.Elapsed.TotalSeconds);
				}
			}

			// Remove directories which are no longer needed
			WorkspaceDirectoryInfo[] DirectoriesToDelete = Transaction.DirectoriesToDelete.ToArray();
			if(DirectoriesToDelete.Length > 0)
			{
				using(LogStatusScope Scope = new LogStatusScope("Deleting {0} {1}...", DirectoriesToDelete.Length, (DirectoriesToDelete.Length == 1)? "directory" : "directories"))
				{
					Stopwatch Timer = Stopwatch.StartNew();

					foreach(string DirectoryToDelete in DirectoriesToDelete.Select(x => x.GetFullName()).OrderByDescending(x => x.Length))
					{
						RemoveDirectory(DirectoryToDelete);
					}

					Scope.SetProgress("({0:0.0}s)", Timer.Elapsed.TotalSeconds);
				}
			}

			// Update the workspace and save the new state
			Workspace = Transaction.NewWorkspaceRootDir;
			Save(RepositoryState.Clean);
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
				Log.TraceWarning("warning: Unable to delete file {0}.", FileToDelete.GetFullName());
				Log.TraceVerbose(ExceptionUtils.FormatExceptionDetails(Ex));
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
				Log.TraceWarning("warning: Unable to delete directory {0}", DirectoryToDelete);
				Log.TraceVerbose(ExceptionUtils.FormatExceptionDetails(Ex));
				bRequiresRepair = true;
			}
		}

		/// <summary>
		/// Update the workspace to match the given stream, syncing files and moving to/from the cache as necessary.
		/// </summary>
		/// <param name="Client">The client connection</param>
		/// <param name="Stream">Contents of the stream</param>
		/// <param name="bFakeSync">Whether to simulate the sync operation, rather than actually syncing files</param>
		private void AddFilesToWorkspace(PerforceConnection Client, StreamDirectoryInfo Stream, bool bFakeSync)
		{
			// Make sure the repair flag is reset
			RunOptionalRepair();

			// Figure out what we need to do
			WorkspaceTransactionAdd Transaction;
			using(LogStatusScope Status = new LogStatusScope("Gathering files to add..."))
			{
				Stopwatch Timer = Stopwatch.StartNew();

				Transaction = new WorkspaceTransactionAdd(Workspace, Stream, ContentIdToTrackedFile);
				Workspace = Transaction.NewWorkspaceRootDir;
				Save(RepositoryState.Dirty);

				Status.SetProgress("({0:0.0}s)", Timer.Elapsed.TotalSeconds);
			}

			// Swap files in and out of the cache
			WorkspaceFileToMove[] FilesToMove = Transaction.FilesToMove.Values.ToArray();
			if(FilesToMove.Length > 0)
			{
				using(LogStatusScope Status = new LogStatusScope("Moving {0} {1} from cache...", FilesToMove.Length, (FilesToMove.Length == 1)? "file" : "files"))
				{
					Stopwatch Timer = Stopwatch.StartNew();
					Parallel.ForEach(FilesToMove, FileToMove => MoveFileFromCache(FileToMove, Transaction.FilesToSync));
					ContentIdToTrackedFile = ContentIdToTrackedFile.Where(x => !Transaction.FilesToMove.ContainsKey(x.Value)).ToDictionary(x => x.Key, x => x.Value);
					Status.SetProgress("({0:0.0}s)", Timer.Elapsed.TotalSeconds);
				}
			}

			// Swap files in and out of the cache
			WorkspaceFileToCopy[] FilesToCopy = Transaction.FilesToCopy.ToArray();
			if(FilesToCopy.Length > 0)
			{
				using(LogStatusScope Status = new LogStatusScope("Copying {0} {1} within workspace...", FilesToCopy.Length, (FilesToCopy.Length == 1)? "file" : "files"))
				{
					Stopwatch Timer = Stopwatch.StartNew();
					Parallel.ForEach(FilesToCopy, FileToCopy => CopyFileWithinWorkspace(FileToCopy, Transaction.FilesToSync));
					Status.SetProgress("({0:0.0}s)", Timer.Elapsed.TotalSeconds);
				}
			}

			// Find all the files we want to sync
			WorkspaceFileToSync[] FilesToSync = Transaction.FilesToSync.ToArray();
			if(FilesToSync.Length > 0)
			{
				// Sync all the files
				long TotalSize = FilesToSync.Sum(x => x.StreamFile.Length);
				using(LogStatusScope Status = new LogStatusScope("Syncing {0} {1}...", FilesToSync.Length, (FilesToSync.Length == 1)? "file" : "files"))
				{
					Stopwatch Timer = Stopwatch.StartNew();

					long SyncedSize = 0;
					for(int EndIdx = 0; EndIdx < FilesToSync.Length; )
					{
						int BeginIdx = EndIdx;

						// Update the progress
						Status.SetProgress("{0:n1}% ({1:n1}mb/{2:n1}mb)", SyncedSize * 100.0 / TotalSize, SyncedSize / (1024.0 * 1024.0), TotalSize / (1024.0 * 1024.0));

						// Figure out the next batch of files to sync
						long BatchSize = 0;
						for(; EndIdx < FilesToSync.Length && BatchSize < 256 * 1024 * 1024; EndIdx++)
						{
							BatchSize += FilesToSync[EndIdx].StreamFile.Length;
						}

						// Print the list of files we're syncing to the log
						Log.TraceLog("Batch {0}-{1}, {2:n1}mb:", BeginIdx, EndIdx, BatchSize / (1024.0 * 1024.0));
						for(int Idx = BeginIdx; Idx < EndIdx; Idx++)
						{
							Log.TraceLog("  {0,-128} [{1,-48}]", FilesToSync[Idx].StreamFile.DepotFileAndRevision, FilesToSync[Idx].StreamFile.ContentId);
						}

						// Sync the files
						if(bFakeSync)
						{
							for(int Idx = BeginIdx; Idx < EndIdx; Idx++)
							{
								FileReference LocalFile = FilesToSync[Idx].WorkspaceFile.GetLocation();
								DirectoryReference.CreateDirectory(LocalFile.Directory);
								FileReference.WriteAllBytes(LocalFile, new byte[0]);
							}
						}
						else
						{
							FileReference SyncFileName = FileReference.Combine(BaseDir, "SyncList.txt");

							using(StreamWriter Writer = new StreamWriter(SyncFileName.FullName))
							{
								for(int Idx = BeginIdx; Idx < EndIdx; Idx++)
								{
									Writer.WriteLine(FilesToSync[Idx].StreamFile.DepotFileAndRevision);
								}
							}

							PerforceConnection TempPerforce = new PerforceConnection(String.Format("{0} -x\"{1}\"", Client.GlobalOptions, SyncFileName));
							TempPerforce.Sync(SyncOptions.Force | SyncOptions.FullDepotSyntax, -1).RequireSuccess();
						}

						// Update metadata for the current state
						Parallel.For(BeginIdx, EndIdx, Idx => FilesToSync[Idx].WorkspaceFile.UpdateMetadata());

						// Save the current state
						Save(RepositoryState.Dirty);

						// Update the status
						SyncedSize += BatchSize;
					}

					Status.SetProgress("100% ({0:n1}mb) ({1:0.0}s)", TotalSize / (1024 * 1024.0), Timer.Elapsed.TotalSeconds);
				}
			}

			// Save the clean state
			Workspace = Transaction.NewWorkspaceRootDir;
			Save(RepositoryState.Clean);
		}

		/// <summary>
		/// Helper function to move a file from the cache into the workspace. If it fails, adds the file to a list to be synced.
		/// </summary>
		/// <param name="FileToMove">Information about the file to move</param>
		/// <param name="FilesToSync">List of files to be synced. If the move fails, the file will be added to this list of files to sync.</param>
		void MoveFileFromCache(WorkspaceFileToMove FileToMove, ConcurrentBag<WorkspaceFileToSync> FilesToSync)
		{
			try
			{
				FileReference.Move(FileToMove.TrackedFile.GetLocation(), FileToMove.WorkspaceFile.GetLocation());
			}
			catch(Exception Ex)
			{
				Log.TraceWarning("warning: Unable to move {0} from cache to {1}. Syncing instead.", FileToMove.TrackedFile.GetLocation(), FileToMove.WorkspaceFile.GetLocation());
				Log.TraceVerbose(ExceptionUtils.FormatExceptionDetails(Ex));
				FilesToSync.Add(new WorkspaceFileToSync(FileToMove.StreamFile, FileToMove.WorkspaceFile));
				bRequiresRepair = true;
			}
		}

		/// <summary>
		/// Helper function to copy a file within the workspace. If it fails, adds the file to a list to be synced.
		/// </summary>
		/// <param name="FileToCopy">Information about the file to move</param>
		/// <param name="FilesToSync">List of files to be synced. If the move fails, the file will be added to this list of files to sync.</param>
		void CopyFileWithinWorkspace(WorkspaceFileToCopy FileToCopy, ConcurrentBag<WorkspaceFileToSync> FilesToSync)
		{
			try
			{
				FileReference.Copy(FileToCopy.SourceWorkspaceFile.GetLocation(), FileToCopy.TargetWorkspaceFile.GetLocation());
				FileToCopy.TargetWorkspaceFile.UpdateMetadata();
			}
			catch(Exception Ex)
			{
				Log.TraceWarning("warning: Unable to copy {0} to {1}. Syncing instead.", FileToCopy.SourceWorkspaceFile.GetLocation(), FileToCopy.TargetWorkspaceFile.GetLocation());
				Log.TraceVerbose(ExceptionUtils.FormatExceptionDetails(Ex));
				FilesToSync.Add(new WorkspaceFileToSync(FileToCopy.StreamFile, FileToCopy.TargetWorkspaceFile));
				bRequiresRepair = true;
			}
		}

		void RemoveTrackedFile(TrackedFileInfo TrackedFile)
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
				CacheId = (CacheId << 8) | ContentId.Digest[Idx];
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

		#endregion
	}
}
