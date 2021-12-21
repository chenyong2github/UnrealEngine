// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace UnrealGameSync
{
	[Flags]
	enum WorkspaceUpdateOptions
	{
		Sync = 0x01,
		SyncSingleChange = 0x02,
		AutoResolveChanges = 0x04,
		GenerateProjectFiles = 0x08,
		SyncArchives = 0x10,
		Build = 0x20,
		Clean = 0x40,
		ScheduledBuild = 0x80,
		RunAfterSync = 0x100,
		OpenSolutionAfterSync = 0x200,
		ContentOnly = 0x400,
		UpdateFilter = 0x800,
		SyncAllProjects = 0x1000,
		IncludeAllProjectsInSolution = 0x2000,
	}

	enum WorkspaceUpdateResult
	{
		Canceled,
		FailedToSync,
		FailedToSyncLoginExpired,
		FilesToDelete,
		FilesToResolve,
		FilesToClobber,
		FailedToCompile,
		FailedToCompileWithCleanWorkspace,
		Success,
	}

	class WorkspaceUpdateContext
	{
		public DateTime StartTime = DateTime.UtcNow;
		public int ChangeNumber;
		public WorkspaceUpdateOptions Options;
		public string[] SyncFilter;
		public Dictionary<string, Tuple<IArchiveInfo, string>> ArchiveTypeToArchive = new Dictionary<string, Tuple<IArchiveInfo, string>>();
		public Dictionary<string, bool> DeleteFiles = new Dictionary<string,bool>();
		public Dictionary<string, bool> ClobberFiles = new Dictionary<string,bool>();
		public Dictionary<Guid,ConfigObject> DefaultBuildSteps;
		public List<ConfigObject> UserBuildStepObjects;
		public HashSet<Guid> CustomBuildSteps;
		public Dictionary<string, string> Variables;
		public PerforceSyncOptions PerforceSyncOptions;
		public List<PerforceFileRecord> HaveFiles; // Cached when sync filter has changed

		// May be updated during sync
		public ConfigFile ProjectConfigFile;
		public IReadOnlyList<string> ProjectStreamFilter;

		public WorkspaceUpdateContext(int InChangeNumber, WorkspaceUpdateOptions InOptions, string[] InSyncFilter, Dictionary<Guid, ConfigObject> InDefaultBuildSteps, List<ConfigObject> InUserBuildSteps, HashSet<Guid> InCustomBuildSteps, Dictionary<string, string> InVariables)
		{
			ChangeNumber = InChangeNumber;
			Options = InOptions;
			SyncFilter = InSyncFilter;
			DefaultBuildSteps = InDefaultBuildSteps;
			UserBuildStepObjects = InUserBuildSteps;
			CustomBuildSteps = InCustomBuildSteps;
			Variables = InVariables;
		}
	}

	class WorkspaceSyncCategory
	{
		public Guid UniqueId;
		public bool bEnable;
		public string Name;
		public string[] Paths;
		public bool bHidden;
		public Guid[] Requires;

		public WorkspaceSyncCategory(Guid UniqueId) : this(UniqueId, "Unnamed")
		{
		}

		public WorkspaceSyncCategory(Guid UniqueId, string Name, params string[] Paths)
		{
			this.UniqueId = UniqueId;
			this.bEnable = true;
			this.Name = Name;
			this.Paths = Paths;
			this.Requires = new Guid[0];
		}

		public static Dictionary<Guid, bool> GetDefault(IEnumerable<WorkspaceSyncCategory> Categories)
		{
			return Categories.ToDictionary(x => x.UniqueId, x => x.bEnable);
		}

		public static Dictionary<Guid, bool> GetDelta(Dictionary<Guid, bool> Source, Dictionary<Guid, bool> Target)
		{
			Dictionary<Guid, bool> Changes = new Dictionary<Guid, bool>();
			foreach (KeyValuePair<Guid, bool> Pair in Target)
			{
				bool bValue;
				if (!Source.TryGetValue(Pair.Key, out bValue) || bValue != Pair.Value)
				{
					Changes[Pair.Key] = Pair.Value;
				}
			}
			return Changes;
		}

		public static void ApplyDelta(Dictionary<Guid, bool> Categories, Dictionary<Guid, bool> Delta)
		{
			foreach(KeyValuePair<Guid, bool> Pair in Delta)
			{
				Categories[Pair.Key] = Pair.Value;
			}
		}

		public override string ToString()
		{
			return Name;
		}
	}

	public class ProjectInfo
	{
		public DirectoryReference LocalRootPath { get; }
		public FileReference SelectedLocalFileName { get; }
		public string ClientRootPath { get; }
		public string SelectedClientFileName { get; }
		public string TelemetryProjectPath { get; }
		public bool bIsEnterpriseProject { get; }

		public ProjectInfo(DirectoryReference InLocalRootPath, FileReference InSelectedLocalFileName, string InClientRootPath, string InSelectedClientFileName, string InTelemetryProjectPath, bool bInIsEnterpriseProject)
		{
			LocalRootPath = InLocalRootPath;
			SelectedLocalFileName = InSelectedLocalFileName;
			ClientRootPath = InClientRootPath;
			SelectedClientFileName = InSelectedClientFileName;
			TelemetryProjectPath = InTelemetryProjectPath;
			bIsEnterpriseProject = bInIsEnterpriseProject;
		}
	}

	class Workspace : IDisposable
	{
		const string BuildVersionFileName = "/Engine/Build/Build.version";
		const string VersionHeaderFileName = "/Engine/Source/Runtime/Launch/Resources/Version.h";
		const string ObjectVersionFileName = "/Engine/Source/Runtime/Core/Private/UObject/ObjectVersion.cpp";

		static readonly string LocalVersionHeaderFileName = VersionHeaderFileName.Replace('/', '\\');
		static readonly string LocalObjectVersionFileName = ObjectVersionFileName.Replace('/', '\\');

		public readonly PerforceConnection Perforce;
		public ProjectInfo Project { get; }
		public UserWorkspaceState State { get; }
		Thread WorkerThread;
		TextWriter Log;
		bool bSyncing;
		ProgressValue Progress = new ProgressValue();

		static UserWorkspaceState ActiveWorkspaceState;

		public event Action<WorkspaceUpdateContext, WorkspaceUpdateResult, string> OnUpdateComplete;

		class RecordCounter : IDisposable
		{
			ProgressValue Progress;
			string Message;
			int Count;
			Stopwatch Timer = Stopwatch.StartNew();

			public RecordCounter(ProgressValue Progress, string Message)
			{
				this.Progress = Progress;
				this.Message = Message;

				Progress.Set(Message);
			}

			public void Dispose()
			{
				UpdateMessage();
			}

			public void Increment()
			{
				Count++;
				if(Timer.ElapsedMilliseconds > 250)
				{
					UpdateMessage();
				}
			}

			public void UpdateMessage()
			{
				Progress.Set(String.Format("{0} ({1:N0})", Message, Count));
				Timer.Restart();
			}
		}

		class SyncBatchBuilder
		{
			public int MaxCommandsPerList { get; }
			public long MaxSizePerList { get; }
			public Queue<List<string>> Batches { get; }

			List<string> Commands;
			List<string> DeleteCommands;
			long Size;

			public SyncBatchBuilder(int MaxCommandsPerList, long MaxSizePerList)
			{
				this.MaxCommandsPerList = MaxCommandsPerList;
				this.MaxSizePerList = MaxSizePerList;
				this.Batches = new Queue<List<string>>();
			}

			public void Add(string NewCommand, long NewSize)
			{
				if (NewSize == 0)
				{
					if (DeleteCommands == null || DeleteCommands.Count >= MaxCommandsPerList)
					{
						DeleteCommands = new List<string>();
						Batches.Enqueue(DeleteCommands);
					}

					DeleteCommands.Add(NewCommand);
				}
				else
				{
					if (Commands == null || Commands.Count >= MaxCommandsPerList || Size + NewSize >= MaxSizePerList)
					{
						Commands = new List<string>();
						Batches.Enqueue(Commands);
						Size = 0;
					}

					Commands.Add(NewCommand);
					Size += NewSize;
				}
			}
		}

		class SyncTree
		{
			public bool bCanUseWildcard;
			public int TotalIncludedFiles;
			public long TotalSize;
			public int TotalExcludedFiles;
			public Dictionary<string, long> IncludedFiles = new Dictionary<string, long>();
			public Dictionary<string, SyncTree> NameToSubTree = new Dictionary<string, SyncTree>(StringComparer.OrdinalIgnoreCase);

			public SyncTree(bool bCanUseWildcard)
			{
				this.bCanUseWildcard = bCanUseWildcard;
			}

			public SyncTree FindOrAddSubTree(string Name)
			{
				SyncTree Result;
				if (!NameToSubTree.TryGetValue(Name, out Result))
				{
					Result = new SyncTree(bCanUseWildcard);
					NameToSubTree.Add(Name, Result);
				}
				return Result;
			}

			public void IncludeFile(string Path, long Size)
			{
				int Idx = Path.IndexOf('/');
				if (Idx == -1)
				{
					IncludedFiles.Add(Path, Size);
				}
				else
				{
					SyncTree SubTree = FindOrAddSubTree(Path.Substring(0, Idx));
					SubTree.IncludeFile(Path.Substring(Idx + 1), Size);
				}
				TotalIncludedFiles++;
				TotalSize += Size;
			}

			public void ExcludeFile(string Path)
			{
				int Idx = Path.IndexOf('/');
				if (Idx != -1)
				{
					SyncTree SubTree = FindOrAddSubTree(Path.Substring(0, Idx));
					SubTree.ExcludeFile(Path.Substring(Idx + 1));
				}
				TotalExcludedFiles++;
			}

			public void GetOptimizedSyncCommands(string Prefix, int ChangeNumber, SyncBatchBuilder Builder)
			{
				if (bCanUseWildcard && TotalExcludedFiles == 0 && TotalSize < Builder.MaxSizePerList)
				{
					Builder.Add(String.Format("{0}/...@{1}", Prefix, ChangeNumber), TotalSize);
				}
				else
				{
					foreach (KeyValuePair<string, long> File in IncludedFiles)
					{
						Builder.Add(String.Format("{0}/{1}@{2}", Prefix, File.Key, ChangeNumber), File.Value);
					}
					foreach (KeyValuePair<string, SyncTree> Pair in NameToSubTree)
					{
						Pair.Value.GetOptimizedSyncCommands(String.Format("{0}/{1}", Prefix, PerforceUtils.EscapePath(Pair.Key)), ChangeNumber, Builder);
					}
				}
			}
		}

		public Workspace(PerforceConnection InPerforce, ProjectInfo InProject, UserWorkspaceState InState, TextWriter InLog)
		{
			Perforce = InPerforce;
			Project = InProject;
			State = InState;
			Log = InLog;

			ProjectConfigFile = ReadProjectConfigFile(Project.LocalRootPath, Project.SelectedLocalFileName, Log);
			ProjectStreamFilter = ReadProjectStreamFilter(Perforce, ProjectConfigFile, Log);
		}

		public void Dispose()
		{
			CancelUpdate();
		}

		public ConfigFile ProjectConfigFile
		{
			get; private set;
		}

		public IReadOnlyList<string> ProjectStreamFilter
		{
			get; private set;
		}

		public void Update(WorkspaceUpdateContext Context)
		{
			// Kill any existing sync
			CancelUpdate();

			// Set the initial progress message
			if(CurrentChangeNumber != Context.ChangeNumber)
			{
				PendingChangeNumber = Context.ChangeNumber;
			}
			Progress.Clear();
			bSyncing = true;

			// Spawn the new thread
			WorkerThread = new Thread(x => UpdateWorkspace(Context));
			WorkerThread.Name = "Workspace Update";
			WorkerThread.Start();
		}

		public void CancelUpdate()
		{
			if(bSyncing)
			{
				Log.WriteLine("OPERATION ABORTED");
				if(WorkerThread != null)
				{
					WorkerThread.Interrupt();
					WorkerThread.Join();
					WorkerThread = null;
				}
				PendingChangeNumber = CurrentChangeNumber;
				bSyncing = false;
				Interlocked.CompareExchange(ref ActiveWorkspaceState, null, State);
			}
		}

		void UpdateWorkspace(WorkspaceUpdateContext Context)
		{
			string StatusMessage;

			Context.ProjectConfigFile = ProjectConfigFile;
			Context.ProjectStreamFilter = ProjectStreamFilter;

			WorkspaceUpdateResult Result = WorkspaceUpdateResult.FailedToSync;
			try
			{
				Result = UpdateWorkspaceInternal(Perforce, Project, State, Context, Progress, Log, out StatusMessage);
				if(Result != WorkspaceUpdateResult.Success)
				{
					Log.WriteLine("{0}", StatusMessage);
				}
			}
			catch (ThreadAbortException)
			{
				StatusMessage = "Canceled.";
				Log.WriteLine("Canceled.");
			}
			catch (ThreadInterruptedException)
			{
				StatusMessage = "Canceled.";
				Log.WriteLine("Canceled.");
			}
			catch (Exception Ex)
			{
				StatusMessage = "Failed with exception - " + Ex.ToString();
				Log.WriteException(Ex, "Failed with exception");
			}

			ProjectConfigFile = Context.ProjectConfigFile;
			ProjectStreamFilter = Context.ProjectStreamFilter;

			State.LastSyncChangeNumber = Context.ChangeNumber;
			State.LastSyncResult = Result;
			State.LastSyncResultMessage = StatusMessage;
			State.LastSyncTime = DateTime.UtcNow;
			State.LastSyncDurationSeconds = (int)(State.LastSyncTime.Value - Context.StartTime).TotalSeconds;
			State.Save();

			bSyncing = false;
			PendingChangeNumber = CurrentChangeNumber;
			Interlocked.CompareExchange(ref ActiveWorkspaceState, null, State);

			if(OnUpdateComplete != null)
			{
				OnUpdateComplete(Context, Result, StatusMessage);
			}
		}

		static WorkspaceUpdateResult UpdateWorkspaceInternal(PerforceConnection Perforce, ProjectInfo Project, UserWorkspaceState State, WorkspaceUpdateContext Context, ProgressValue Progress, TextWriter Log, out string StatusMessage)
		{
//			string LocalRootPrefix = Project.LocalRootPath.FullName.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;

			string CmdExe = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "cmd.exe");
			if(!File.Exists(CmdExe))
			{
				StatusMessage = String.Format("Missing {0}.", CmdExe);
				return WorkspaceUpdateResult.FailedToSync;
			}

			List<Tuple<string, TimeSpan>> Times = new List<Tuple<string,TimeSpan>>();

			int NumFilesSynced = 0;
			if(Context.Options.HasFlag(WorkspaceUpdateOptions.Sync) || Context.Options.HasFlag(WorkspaceUpdateOptions.SyncSingleChange))
			{
				using(TelemetryStopwatch SyncTelemetryStopwatch = new TelemetryStopwatch("Workspace_Sync", Project.TelemetryProjectPath))
				{
					Log.WriteLine("Syncing to {0}...", Context.ChangeNumber);

					// Make sure we're logged in
					bool bLoggedIn;
					if(!Perforce.GetLoggedInState(out bLoggedIn, Log))
					{
						StatusMessage = "Unable to get login status.";
						return WorkspaceUpdateResult.FailedToSync;
					}
					if(!bLoggedIn)
					{
						StatusMessage = "User is not logged in.";
						return WorkspaceUpdateResult.FailedToSyncLoginExpired;
					}

					// Figure out which paths to sync
					List<string> RelativeSyncPaths = GetRelativeSyncPaths(Project, (Context.Options & WorkspaceUpdateOptions.SyncAllProjects) != 0, Context.SyncFilter);
					List<string> SyncPaths = new List<string>(RelativeSyncPaths.Select(x => Project.ClientRootPath + x));

					// Get the user's sync filter
					FileFilter UserFilter = new FileFilter(FileFilterType.Include);
					if(Context.SyncFilter != null)
					{
						UserFilter.AddRules(Context.SyncFilter.Select(x => x.Trim()).Where(x => x.Length > 0 && !x.StartsWith(";") && !x.StartsWith("#")));
					}

					// Check if the new sync filter matches the previous one. If not, we'll enumerate all files in the workspace and make sure there's nothing extra there.
					string NextSyncFilterHash = null;
					using (SHA1Managed SHA = new SHA1Managed())
					{
						StringBuilder CombinedFilter = new StringBuilder();
						foreach(string RelativeSyncPath in RelativeSyncPaths)
						{
							CombinedFilter.AppendFormat("{0}\n", RelativeSyncPath);
						}
						if(Context.SyncFilter != null)
						{
							CombinedFilter.Append("--FROM--\n");
							CombinedFilter.Append(String.Join("\n", Context.SyncFilter));
						}
						NextSyncFilterHash = BitConverter.ToString(SHA.ComputeHash(Encoding.UTF8.GetBytes(CombinedFilter.ToString()))).Replace("-", "");
					}

					// If the hash differs, enumerate everything in the workspace to find what needs to be removed
					if (NextSyncFilterHash != State.CurrentSyncFilterHash)
					{
						using (TelemetryStopwatch FilterStopwatch = new TelemetryStopwatch("Workspace_Sync_FilterChanged", Project.TelemetryProjectPath))
						{
							Log.WriteLine("Filter has changed ({0} -> {1}); finding files in workspace that need to be removed.", (String.IsNullOrEmpty(State.CurrentSyncFilterHash)) ? "None" : State.CurrentSyncFilterHash, NextSyncFilterHash);

							// Find all the files that are in this workspace
							List<PerforceFileRecord> HaveFiles = Context.HaveFiles;
							if (HaveFiles == null)
							{
								HaveFiles = new List<PerforceFileRecord>();
								using (RecordCounter HaveCounter = new RecordCounter(Progress, "Sync filter changed; checking workspace..."))
								{
									if (!Perforce.Have("//...", Record => { HaveFiles.Add(Record); HaveCounter.Increment(); }, Log))
									{
										StatusMessage = "Unable to query files.";
										return WorkspaceUpdateResult.FailedToSync;
									}
								}
								Context.HaveFiles = HaveFiles;
							}

							// Build a filter for the current sync paths
							FileFilter SyncPathsFilter = new FileFilter(FileFilterType.Exclude);
							foreach (string RelativeSyncPath in RelativeSyncPaths)
							{
								SyncPathsFilter.Include(RelativeSyncPath);
							}

							// Remove all the files that are not included by the filter
							List<string> RemoveDepotPaths = new List<string>();
							foreach (PerforceFileRecord HaveFile in HaveFiles)
							{
								try
								{
									FileReference FullPath = new FileReference(HaveFile.Path);
									if (MatchFilter(Project, FullPath, SyncPathsFilter) && !MatchFilter(Project, FullPath, UserFilter))
									{
										Log.WriteLine("  {0}", HaveFile.DepotPath);
										RemoveDepotPaths.Add(HaveFile.DepotPath);
									}
								}
								catch (PathTooLongException)
								{
									// We don't actually care about this when looking for files to remove. Perforce may think that it's synced the path, and silently failed. Just ignore it.
								}
							}

							// Check if there are any paths outside the regular sync paths
							if (RemoveDepotPaths.Count > 0)
							{
								bool bDeleteListMatches = true;

								Dictionary<string, bool> NewDeleteFiles = new Dictionary<string, bool>(StringComparer.OrdinalIgnoreCase);
								foreach (string RemoveDepotPath in RemoveDepotPaths)
								{
									bool bDelete;
									if (!Context.DeleteFiles.TryGetValue(RemoveDepotPath, out bDelete))
									{
										bDeleteListMatches = false;
										bDelete = true;
									}
									NewDeleteFiles[RemoveDepotPath] = bDelete;
								}
								Context.DeleteFiles = NewDeleteFiles;

								if (!bDeleteListMatches)
								{
									StatusMessage = String.Format("Cancelled after finding {0} files excluded by filter", NewDeleteFiles.Count);
									return WorkspaceUpdateResult.FilesToDelete;
								}

								RemoveDepotPaths.RemoveAll(x => !Context.DeleteFiles[x]);
							}

							// Actually delete any files that we don't want
							if (RemoveDepotPaths.Count > 0)
							{
								// Clear the current sync filter hash. If the sync is canceled, we'll be in an indeterminate state, and we should always clean next time round.
								State.CurrentSyncFilterHash = "INVALID";
								State.Save();

								// Find all the depot paths that will be synced
								HashSet<string> RemainingDepotPathsToRemove = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
								RemainingDepotPathsToRemove.UnionWith(RemoveDepotPaths);

								// Build the list of revisions to sync
								List<string> RevisionsToRemove = new List<string>();
								RevisionsToRemove.AddRange(RemoveDepotPaths.Select(x => String.Format("{0}#0", x)));

								WorkspaceUpdateResult RemoveResult = SyncFileRevisions(Perforce, "Removing files...", Context, RevisionsToRemove, RemainingDepotPathsToRemove, Progress, Log, out StatusMessage);
								if (RemoveResult != WorkspaceUpdateResult.Success)
								{
									return RemoveResult;
								}
							}

							// Update the sync filter hash. We've removed any files we need to at this point.
							State.CurrentSyncFilterHash = NextSyncFilterHash;
							State.Save();
						}
					}

					// Create a filter for all the files we don't want
					FileFilter Filter = new FileFilter(UserFilter);
					Filter.Exclude(BuildVersionFileName);
					if (Context.Options.HasFlag(WorkspaceUpdateOptions.ContentOnly))
					{
						Filter.Exclude("*.usf");
						Filter.Exclude("*.ush");
					}

					// Create a tree to store the sync path
					SyncTree SyncTree = new SyncTree(false);
					if (!Context.Options.HasFlag(WorkspaceUpdateOptions.SyncSingleChange))
					{
						foreach (string RelativeSyncPath in RelativeSyncPaths)
						{
							const string WildcardSuffix = "/...";
							if (RelativeSyncPath.EndsWith(WildcardSuffix, StringComparison.Ordinal))
							{
								SyncTree Leaf = SyncTree;

								string[] Fragments = RelativeSyncPath.Split('/');
								for (int Idx = 1; Idx < Fragments.Length - 1; Idx++)
								{
									Leaf = Leaf.FindOrAddSubTree(Fragments[Idx]);
								}

								Leaf.bCanUseWildcard = true;
							}
						}
					}

					// Find all the server changes, and anything that's opened for edit locally. We need to sync files we have open to schedule a resolve.
					SyncBatchBuilder BatchBuilder = new SyncBatchBuilder(Context.PerforceSyncOptions.MaxCommandsPerBatch, Context.PerforceSyncOptions.MaxSizePerBatch);
					List<string> SyncDepotPaths = new List<string>();
					using(RecordCounter Counter = new RecordCounter(Progress, "Filtering files..."))
					{
						// Track the total new bytes that will be required on disk when syncing. Add an extra 100MB for padding.
						long RequiredFreeSpace = 100 * 1024 * 1024;

						foreach(string SyncPath in SyncPaths)
						{
							List<PerforceFileRecord> SyncRecords = new List<PerforceFileRecord>();
							if(!Perforce.SyncPreview(SyncPath, Context.ChangeNumber, !Context.Options.HasFlag(WorkspaceUpdateOptions.Sync), Record => { SyncRecords.Add(Record); Counter.Increment(); }, Log))
							{
								StatusMessage = String.Format("Couldn't enumerate changes matching {0}.", SyncPath);
								return WorkspaceUpdateResult.FailedToSync;
							}

							List<PerforceFileRecord> OpenRecords;
							if (!Perforce.GetOpenFiles(SyncPath, out OpenRecords, Log))
							{
								StatusMessage = String.Format("Couldn't find open files matching {0}.", SyncPath);
								return WorkspaceUpdateResult.FailedToSync;
							}

							SyncRecords.AddRange(OpenRecords.Where(x => x.Action != "add" && x.Action != "branch" && x.Action != "move/add"));

							// Enumerate all the files to be synced. NOTE: depotPath is escaped, whereas clientPath is not.
							foreach (PerforceFileRecord SyncRecord in SyncRecords)
							{
								// If it doesn't exist locally, just add a sync command for it
								if (String.IsNullOrEmpty(SyncRecord.ClientPath))
								{
									BatchBuilder.Add(String.Format("{0}@{1}", SyncRecord.DepotPath, Context.ChangeNumber), SyncRecord.FileSize);
									SyncDepotPaths.Add(SyncRecord.DepotPath);
									RequiredFreeSpace += SyncRecord.FileSize;
									continue;
								}

								// Get the full local path
								FileReference SyncFile;
								try
								{
									SyncFile = new FileReference(SyncRecord.ClientPath);
								}
								catch(PathTooLongException)
								{
									Log.WriteLine("The local path for {0} exceeds the maximum allowed by Windows. Re-sync your workspace to a directory with a shorter name, or delete the file from the server.", SyncRecord.ClientPath);
									StatusMessage = "File exceeds maximum path length allowed by Windows.";
									return WorkspaceUpdateResult.FailedToSync;
								}

								// Make sure it's under the current directory. Not sure why this would happen, just being safe.
								// This occurs for files returned from GetOpenFiles as SyncRecord.ClientPath is the client workspace path not local path.
								if (!SyncFile.IsUnderDirectory(Project.LocalRootPath))
								{
									BatchBuilder.Add(String.Format("{0}@{1}", SyncRecord.DepotPath, Context.ChangeNumber), SyncRecord.FileSize);
									SyncDepotPaths.Add(SyncRecord.DepotPath);
									RequiredFreeSpace += SyncRecord.FileSize;
									continue;
								}

								// Check that it matches the filter
								string RelativePath = SyncFile.MakeRelativeTo(Project.LocalRootPath).Replace('\\', '/');
								if (Filter.Matches(RelativePath))
								{
									long FileSize = SyncRecord.FileSize;
									if (SyncRecord.Action == "deleted" || SyncRecord.Action == "move/delete")
									{
										FileSize = 0;
									}

									SyncTree.IncludeFile(PerforceUtils.EscapePath(RelativePath), FileSize);
									SyncDepotPaths.Add(SyncRecord.DepotPath);
									RequiredFreeSpace += FileSize;
									FileInfo LocalFileInfo = SyncFile.ToFileInfo();

									// If the file exists the required free space can be reduced as those bytes will be replaced.
									if (LocalFileInfo.Exists)
									{
										RequiredFreeSpace -= LocalFileInfo.Length;
									}
								}
								else
								{
									SyncTree.ExcludeFile(PerforceUtils.EscapePath(RelativePath));
								}
							}
						}

						try
						{
							DirectoryInfo LocalRootInfo = Project.LocalRootPath.ToDirectoryInfo();
							DriveInfo Drive = new DriveInfo(LocalRootInfo.Root.FullName);

							if (Drive.AvailableFreeSpace < RequiredFreeSpace)
							{
								Log.WriteLine("Syncing requires {0} which exceeds the {1} available free space on {2}.", EpicGames.Core.StringUtils.FormatBytesString(RequiredFreeSpace), EpicGames.Core.StringUtils.FormatBytesString(Drive.AvailableFreeSpace), Drive.Name);
								StatusMessage = "Not enough available free space.";
								return WorkspaceUpdateResult.FailedToSync;
							}
						}
						catch (SystemException)
						{
							Log.WriteLine("Unable to check available free space for {0}.", Project.LocalRootPath);
						}
					}
					SyncTree.GetOptimizedSyncCommands(Project.ClientRootPath, Context.ChangeNumber, BatchBuilder);

					// Clear the current sync changelist, in case we cancel
					if (!Context.Options.HasFlag(WorkspaceUpdateOptions.SyncSingleChange))
					{
						State.CurrentChangeNumber = -1;
						State.AdditionalChangeNumbers.Clear();
						State.Save();
					}

					// Find all the depot paths that will be synced
					HashSet<string> RemainingDepotPaths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
					RemainingDepotPaths.UnionWith(SyncDepotPaths);

					using (TelemetryStopwatch TransferStopwatch = new TelemetryStopwatch("Workspace_Sync_TransferFiles", Project.TelemetryProjectPath))
					{
						TransferStopwatch.AddData(new { MachineName = Environment.MachineName, DomainName = Environment.UserDomainName, ServerAndPort = Perforce.ServerAndPort, UserName = Perforce.UserName, IncludedFiles = SyncTree.TotalIncludedFiles, ExcludedFiles = SyncTree.TotalExcludedFiles, Size = SyncTree.TotalSize, NumThreads = Context.PerforceSyncOptions.NumThreads });

						WorkspaceUpdateResult SyncResult = SyncFileRevisions(Perforce, "Syncing files...", Context, BatchBuilder.Batches, RemainingDepotPaths, Progress, Log, out StatusMessage);
						if (SyncResult != WorkspaceUpdateResult.Success)
						{
							TransferStopwatch.AddData(new { SyncResult = SyncResult.ToString(), CompletedFilesFiles = SyncDepotPaths.Count - RemainingDepotPaths.Count });
							return SyncResult;
						}

						TransferStopwatch.Stop("Ok");
						TransferStopwatch.AddData(new { TransferRate = SyncTree.TotalSize / Math.Max(TransferStopwatch.Elapsed.TotalSeconds, 0.0001f) });
					}

					int VersionChangeNumber = -1;
					if(Context.Options.HasFlag(WorkspaceUpdateOptions.Sync) && !Context.Options.HasFlag(WorkspaceUpdateOptions.UpdateFilter))
					{
						// Read the new config file
						Context.ProjectConfigFile = ReadProjectConfigFile(Project.LocalRootPath, Project.SelectedLocalFileName, Log);
						Context.ProjectStreamFilter = ReadProjectStreamFilter(Perforce, Context.ProjectConfigFile, Log);

						// Get the branch name
						string BranchOrStreamName;
						if(Perforce.GetActiveStream(out BranchOrStreamName, Log))
						{
							// If it's a virtual stream, take the concrete parent stream instead
							for (;;)
							{
								PerforceSpec StreamSpec;
								if (!Perforce.TryGetStreamSpec(BranchOrStreamName, out StreamSpec, Log))
								{
									StatusMessage = String.Format("Unable to get stream spec for {0}.", BranchOrStreamName);
									return WorkspaceUpdateResult.FailedToSync;
								}
								if (StreamSpec.GetField("Type") != "virtual")
								{
									break;
								}
								BranchOrStreamName = StreamSpec.GetField("Parent");
							}
						}
						else
						{
							// Otherwise use the depot path for GenerateProjectFiles.bat in the root of the workspace
							string DepotFileName;
							if(!Perforce.ConvertToDepotPath(Project.ClientRootPath + "/GenerateProjectFiles.bat", out DepotFileName, Log))
							{
								StatusMessage = String.Format("Couldn't determine branch name for {0}.", Project.SelectedClientFileName);
								return WorkspaceUpdateResult.FailedToSync;
							}
							BranchOrStreamName = PerforceUtils.GetClientOrDepotDirectoryName(DepotFileName);
						}

						// Find the last code change before this changelist. For consistency in versioning between local builds and precompiled binaries, we need to use the last submitted code changelist as our version number.
						List<PerforceChangeSummary> CodeChanges;
						if(!Perforce.FindChanges(new string[]{ ".cs", ".h", ".cpp", ".usf", ".ush", ".uproject", ".uplugin" }.SelectMany(x => SyncPaths.Select(y => String.Format("{0}{1}@<={2}", y, x, Context.ChangeNumber))), 1, out CodeChanges, Log))
						{
							StatusMessage = String.Format("Couldn't determine last code changelist before CL {0}.", Context.ChangeNumber);
							return WorkspaceUpdateResult.FailedToSync;
						}
						if(CodeChanges.Count == 0)
						{
							StatusMessage = String.Format("Could not find any code changes before CL {0}.", Context.ChangeNumber);
							return WorkspaceUpdateResult.FailedToSync;
						}

						// Get the last code change
						if(Context.ProjectConfigFile.GetValue("Options.VersionToLastCodeChange", true))
						{
							VersionChangeNumber = CodeChanges.Max(x => x.Number);
						}
						else
						{
							VersionChangeNumber = Context.ChangeNumber;
						}

						// Update the version files
						if(Context.ProjectConfigFile.GetValue("Options.UseFastModularVersioningV2", false))
						{
							bool bIsLicenseeVersion = IsLicenseeVersion(Perforce, Project, Log);
							if (!UpdateVersionFile(Perforce, Project.ClientRootPath + BuildVersionFileName, Context.ChangeNumber, Text => UpdateBuildVersion(Text, Context.ChangeNumber, VersionChangeNumber, BranchOrStreamName, bIsLicenseeVersion), Log))
							{
								StatusMessage = String.Format("Failed to update {0}.", BuildVersionFileName);
								return WorkspaceUpdateResult.FailedToSync;
							}
						}
						else if(Context.ProjectConfigFile.GetValue("Options.UseFastModularVersioning", false))
						{
							bool bIsLicenseeVersion = IsLicenseeVersion(Perforce, Project, Log);
							if (!UpdateVersionFile(Perforce, Project.ClientRootPath + BuildVersionFileName, Context.ChangeNumber, Text => UpdateBuildVersion(Text, Context.ChangeNumber, VersionChangeNumber, BranchOrStreamName, bIsLicenseeVersion), Log))
							{
								StatusMessage = String.Format("Failed to update {0}.", BuildVersionFileName);
								return WorkspaceUpdateResult.FailedToSync;
							}

							Dictionary<string, string> VersionHeaderStrings = new Dictionary<string,string>();
							VersionHeaderStrings["#define ENGINE_IS_PROMOTED_BUILD"] = " (0)";
							VersionHeaderStrings["#define BUILT_FROM_CHANGELIST"] = " 0";
							VersionHeaderStrings["#define BRANCH_NAME"] = " \"" + BranchOrStreamName.Replace('/', '+') + "\"";
							if(!UpdateVersionFile(Perforce, Project.ClientRootPath + VersionHeaderFileName, VersionHeaderStrings, Context.ChangeNumber, Log))
							{
								StatusMessage = String.Format("Failed to update {0}.", VersionHeaderFileName);
								return WorkspaceUpdateResult.FailedToSync;
							}
							if(!UpdateVersionFile(Perforce, Project.ClientRootPath + ObjectVersionFileName, new Dictionary<string,string>(), Context.ChangeNumber, Log))
							{
								StatusMessage = String.Format("Failed to update {0}.", ObjectVersionFileName);
								return WorkspaceUpdateResult.FailedToSync;
							}
						}
						else
						{
							if(!UpdateVersionFile(Perforce, Project.ClientRootPath + BuildVersionFileName, new Dictionary<string, string>(), Context.ChangeNumber, Log))
							{
								StatusMessage = String.Format("Failed to update {0}", BuildVersionFileName);
								return WorkspaceUpdateResult.FailedToSync;
							}

							Dictionary<string, string> VersionStrings = new Dictionary<string,string>();
							VersionStrings["#define ENGINE_VERSION"] = " " + VersionChangeNumber.ToString();
							VersionStrings["#define ENGINE_IS_PROMOTED_BUILD"] = " (0)";
							VersionStrings["#define BUILT_FROM_CHANGELIST"] = " " + VersionChangeNumber.ToString();
							VersionStrings["#define BRANCH_NAME"] = " \"" + BranchOrStreamName.Replace('/', '+') + "\"";
							if(!UpdateVersionFile(Perforce, Project.ClientRootPath + VersionHeaderFileName, VersionStrings, Context.ChangeNumber, Log))
							{
								StatusMessage = String.Format("Failed to update {0}", VersionHeaderFileName);
								return WorkspaceUpdateResult.FailedToSync;
							}
							if(!UpdateVersionFile(Perforce, Project.ClientRootPath + ObjectVersionFileName, VersionStrings, Context.ChangeNumber, Log))
							{
								StatusMessage = String.Format("Failed to update {0}", ObjectVersionFileName);
								return WorkspaceUpdateResult.FailedToSync;
							}
						}

						// Remove all the receipts for build targets in this branch
						if(Project.SelectedClientFileName.EndsWith(".uproject", StringComparison.OrdinalIgnoreCase))
						{
							Perforce.Sync(PerforceUtils.GetClientOrDepotDirectoryName(Project.SelectedClientFileName) + "/Build/Receipts/...#0", Log);
						}
					}

					// Check if there are any files which need resolving
					List<PerforceFileRecord> UnresolvedFiles;
					if(!FindUnresolvedFiles(Perforce, SyncPaths, Log, out UnresolvedFiles))
					{
						StatusMessage = "Couldn't get list of unresolved files.";
						return WorkspaceUpdateResult.FailedToSync;
					}
					if(UnresolvedFiles.Count > 0 && Context.Options.HasFlag(WorkspaceUpdateOptions.AutoResolveChanges))
					{
						foreach (PerforceFileRecord UnresolvedFile in UnresolvedFiles)
						{
							Perforce.AutoResolveFile(UnresolvedFile.DepotPath, Log);
						}
						if(!FindUnresolvedFiles(Perforce, SyncPaths, Log, out UnresolvedFiles))
						{
							StatusMessage = "Couldn't get list of unresolved files.";
							return WorkspaceUpdateResult.FailedToSync;
						}
					}
					if(UnresolvedFiles.Count > 0)
					{
						Log.WriteLine("{0} files need resolving:", UnresolvedFiles.Count);
						foreach(PerforceFileRecord UnresolvedFile in UnresolvedFiles)
						{
							Log.WriteLine("  {0}", UnresolvedFile.ClientPath);
						}
						StatusMessage = "Files need resolving.";
						return WorkspaceUpdateResult.FilesToResolve;
					}

					// Continue processing sync-only actions
					if (Context.Options.HasFlag(WorkspaceUpdateOptions.Sync) && !Context.Options.HasFlag(WorkspaceUpdateOptions.UpdateFilter))
					{
						// Execute any project specific post-sync steps
						string[] PostSyncSteps = Context.ProjectConfigFile.GetValues("Sync.Step", null);
						if (PostSyncSteps != null)
						{
							Log.WriteLine();
							Log.WriteLine("Executing post-sync steps...");

							Dictionary<string, string> PostSyncVariables = new Dictionary<string, string>(Context.Variables);
							PostSyncVariables["Change"] = Context.ChangeNumber.ToString();
							PostSyncVariables["CodeChange"] = VersionChangeNumber.ToString();

							foreach (string PostSyncStep in PostSyncSteps.Select(x => x.Trim()))
							{
								ConfigObject PostSyncStepObject = new ConfigObject(PostSyncStep);

								string ToolFileName = Utility.ExpandVariables(PostSyncStepObject.GetValue("FileName", ""), PostSyncVariables);
								if (ToolFileName != null)
								{
									string ToolArguments = Utility.ExpandVariables(PostSyncStepObject.GetValue("Arguments", ""), PostSyncVariables);

									Log.WriteLine("post-sync> Running {0} {1}", ToolFileName, ToolArguments);

									int ResultFromTool = Utility.ExecuteProcess(ToolFileName, null, ToolArguments, null, new ProgressTextWriter(Progress, new PrefixedTextWriter("post-sync> ", Log)));
									if (ResultFromTool != 0)
									{
										StatusMessage = String.Format("Post-sync step terminated with exit code {0}.", ResultFromTool);
										return WorkspaceUpdateResult.FailedToSync;
									}
								}
							}
						}
					}

					// Update the current state
					if (Context.Options.HasFlag(WorkspaceUpdateOptions.SyncSingleChange))
					{
						State.AdditionalChangeNumbers.Add(Context.ChangeNumber);
					}
					else
					{
						State.CurrentChangeNumber = Context.ChangeNumber;
					}
					State.Save();

					// Update the timing info
					Times.Add(new Tuple<string,TimeSpan>("Sync", SyncTelemetryStopwatch.Stop("Success")));

					// Save the number of files synced
					NumFilesSynced = SyncDepotPaths.Count;
					Log.WriteLine();
				}
			}

			// Extract an archive from the depot path
			if(Context.Options.HasFlag(WorkspaceUpdateOptions.SyncArchives))
			{
				using(TelemetryStopwatch Stopwatch = new TelemetryStopwatch("Workspace_SyncArchives", Project.TelemetryProjectPath))
				{
					// Create the directory for extracted archive manifests
					DirectoryReference ManifestDirectoryName;
					if(Project.SelectedLocalFileName.HasExtension(".uproject"))
					{
						ManifestDirectoryName = DirectoryReference.Combine(Project.SelectedLocalFileName.Directory, "Saved", "UnrealGameSync");
					}
					else
					{
						ManifestDirectoryName = DirectoryReference.Combine(Project.SelectedLocalFileName.Directory, "Engine", "Saved", "UnrealGameSync");
					}
					DirectoryReference.CreateDirectory(ManifestDirectoryName);

					// Sync and extract (or just remove) the given archives
					foreach(KeyValuePair<string, Tuple<IArchiveInfo, string>> ArchiveTypeAndArchive in Context.ArchiveTypeToArchive)
					{
						string ArchiveType = ArchiveTypeAndArchive.Key;

						// Remove any existing binaries
						FileReference ManifestFileName = FileReference.Combine(ManifestDirectoryName, String.Format("{0}.zipmanifest", ArchiveType));
						if(FileReference.Exists(ManifestFileName))
						{
							Log.WriteLine("Removing {0} binaries...", ArchiveType);
							Progress.Set(String.Format("Removing {0} binaries...", ArchiveType), 0.0f);
							ArchiveUtils.RemoveExtractedFiles(Project.LocalRootPath, ManifestFileName, Progress, Log);
							FileReference.Delete(ManifestFileName);
							Log.WriteLine();
						}

						// If we have a new depot path, sync it down and extract it
						if(ArchiveTypeAndArchive.Value != null)
						{
							IArchiveInfo ArchiveInfo = ArchiveTypeAndArchive.Value.Item1;
							string ArchiveKey = ArchiveTypeAndArchive.Value.Item2;

							Log.WriteLine("Syncing {0} binaries...", ArchiveType.ToLowerInvariant());
							Progress.Set(String.Format("Syncing {0} binaries...", ArchiveType.ToLowerInvariant()), 0.0f);
							if (!ArchiveInfo.DownloadArchive(ArchiveKey, Project.LocalRootPath, ManifestFileName, Log, Progress))
							{
								StatusMessage = String.Format("Couldn't read {0}", ArchiveKey);
								return WorkspaceUpdateResult.FailedToSync;
							}
						}
					}

					// Update the state
					State.ExpandedArchiveTypes = Context.ArchiveTypeToArchive.Where(x => x.Value != null).Select(x => x.Key).ToArray();
					State.Save();

					// Add the finish time
					Times.Add(new Tuple<string,TimeSpan>("Archive", Stopwatch.Stop("Success")));
				}
			}

			// Take the lock before doing anything else. Building and generating project files can only be done on one workspace at a time.
			if(Context.Options.HasFlag(WorkspaceUpdateOptions.GenerateProjectFiles) || Context.Options.HasFlag(WorkspaceUpdateOptions.Build))
			{
				if(Interlocked.CompareExchange(ref ActiveWorkspaceState, State, null) != null)
				{
					Log.WriteLine("Waiting for other workspaces to finish...");
					while(Interlocked.CompareExchange(ref ActiveWorkspaceState, State, null) != null)
					{
						Thread.Sleep(100);
					}
				}
			}

			// Generate project files in the workspace
			if(Context.Options.HasFlag(WorkspaceUpdateOptions.GenerateProjectFiles))
			{
				using(TelemetryStopwatch Stopwatch = new TelemetryStopwatch("Workspace_GenerateProjectFiles", Project.TelemetryProjectPath))
				{
					Progress.Set("Generating project files...", 0.0f);

					StringBuilder CommandLine = new StringBuilder();
					CommandLine.AppendFormat("/C \"\"{0}\"", FileReference.Combine(Project.LocalRootPath, "GenerateProjectFiles.bat"));
					if((Context.Options & WorkspaceUpdateOptions.SyncAllProjects) == 0 && (Context.Options & WorkspaceUpdateOptions.IncludeAllProjectsInSolution) == 0)
					{
						if(Project.SelectedLocalFileName.HasExtension(".uproject"))
						{
							CommandLine.AppendFormat(" \"{0}\"", Project.SelectedLocalFileName);
						}
					}
					CommandLine.Append(" -progress\"");

					Log.WriteLine("Generating project files...");
					Log.WriteLine("gpf> Running {0} {1}", CmdExe, CommandLine);

					int GenerateProjectFilesResult = Utility.ExecuteProcess(CmdExe, null, CommandLine.ToString(), null, new ProgressTextWriter(Progress, new PrefixedTextWriter("gpf> ", Log)));
					if(GenerateProjectFilesResult != 0)
					{
						StatusMessage = String.Format("Failed to generate project files (exit code {0}).", GenerateProjectFilesResult);
						return WorkspaceUpdateResult.FailedToCompile;
					}

					Log.WriteLine();
					Times.Add(new Tuple<string,TimeSpan>("Prj gen", Stopwatch.Stop("Success")));
				}
			}

			// Build everything using MegaXGE
			if(Context.Options.HasFlag(WorkspaceUpdateOptions.Build))
			{
				// Compile all the build steps together
				Dictionary<Guid, ConfigObject> BuildStepObjects = Context.DefaultBuildSteps.ToDictionary(x => x.Key, x => new ConfigObject(x.Value));
				BuildStep.MergeBuildStepObjects(BuildStepObjects, Context.ProjectConfigFile.GetValues("Build.Step", new string[0]).Select(x => new ConfigObject(x)));
				BuildStep.MergeBuildStepObjects(BuildStepObjects, Context.UserBuildStepObjects);

				// Construct build steps from them
				List<BuildStep> BuildSteps = BuildStepObjects.Values.Select(x => new BuildStep(x)).OrderBy(x => (x.OrderIndex == -1) ? 10000 : x.OrderIndex).ToList();
				if(Context.CustomBuildSteps != null && Context.CustomBuildSteps.Count > 0)
				{
					BuildSteps.RemoveAll(x => !Context.CustomBuildSteps.Contains(x.UniqueId));
				}
				else if(Context.Options.HasFlag(WorkspaceUpdateOptions.ScheduledBuild))
				{
					BuildSteps.RemoveAll(x => !x.bScheduledSync);
				}
				else
				{
					BuildSteps.RemoveAll(x => !x.bNormalSync);
				}

				// Check if the last successful build was before a change that we need to force a clean for
				bool bForceClean = false;
				if(State.LastBuiltChangeNumber != 0)
				{
					foreach(string CleanBuildChange in Context.ProjectConfigFile.GetValues("ForceClean.Changelist", new string[0]))
					{
						int ChangeNumber;
						if(int.TryParse(CleanBuildChange, out ChangeNumber))
						{
							if((State.LastBuiltChangeNumber >= ChangeNumber) != (State.CurrentChangeNumber >= ChangeNumber))
							{
								Log.WriteLine("Forcing clean build due to changelist {0}.", ChangeNumber);
								Log.WriteLine();
								bForceClean = true;
								break;
							}
						}
					}
				}

				// Execute them all
				using(TelemetryStopwatch Stopwatch = new TelemetryStopwatch("Workspace_Build", Project.TelemetryProjectPath))
				{
					Progress.Set("Starting build...", 0.0f);

					// Execute all the steps
					float MaxProgressFraction = 0.0f;
					foreach (BuildStep Step in BuildSteps)
					{
						MaxProgressFraction += (float)Step.EstimatedDuration / (float)Math.Max(BuildSteps.Sum(x => x.EstimatedDuration), 1);

						Progress.Set(Step.StatusText);
						Progress.Push(MaxProgressFraction);

						Log.WriteLine(Step.StatusText);

						if(Step.IsValid())
						{
							switch(Step.Type)
							{
								case BuildStepType.Compile:
									using(TelemetryStopwatch StepStopwatch = new TelemetryStopwatch("Workspace_Execute_Compile", Project.TelemetryProjectPath))
									{
										StepStopwatch.AddData(new { Target = Step.Target });

										FileReference BuildBat = FileReference.Combine(Project.LocalRootPath, "Engine", "Build", "BatchFiles", "Build.bat");
										string CommandLine = String.Format("{0} {1} {2} {3} -NoHotReloadFromIDE", Step.Target, Step.Platform, Step.Configuration, Utility.ExpandVariables(Step.Arguments ?? "", Context.Variables));
										if(Context.Options.HasFlag(WorkspaceUpdateOptions.Clean) || bForceClean)
										{
											Log.WriteLine("ubt> Running {0} {1} -clean", BuildBat, CommandLine);
											Utility.ExecuteProcess(CmdExe, null, "/C \"\"" + BuildBat + "\" " + CommandLine + " -clean\"", null, new ProgressTextWriter(Progress, new PrefixedTextWriter("ubt> ", Log)));
										}

										Log.WriteLine("ubt> Running {0} {1} -progress", BuildBat, CommandLine);

										int ResultFromBuild = Utility.ExecuteProcess(CmdExe, null, "/C \"\"" + BuildBat + "\" "+ CommandLine + " -progress\"", null, new ProgressTextWriter(Progress, new PrefixedTextWriter("ubt> ", Log)));
										if(ResultFromBuild != 0)
										{
											StepStopwatch.Stop("Failed");
											StatusMessage = String.Format("Failed to compile {0}.", Step.Target);
											return (HasModifiedSourceFiles(Perforce, Project, Log) || Context.UserBuildStepObjects.Count > 0)? WorkspaceUpdateResult.FailedToCompile : WorkspaceUpdateResult.FailedToCompileWithCleanWorkspace;
										}

										StepStopwatch.Stop("Success");
									}
									break;
								case BuildStepType.Cook:
									using(TelemetryStopwatch StepStopwatch = new TelemetryStopwatch("Workspace_Execute_Cook", Project.TelemetryProjectPath))
									{
										StepStopwatch.AddData(new { Project = Path.GetFileNameWithoutExtension(Step.FileName) });

										FileReference LocalRunUAT = FileReference.Combine(Project.LocalRootPath, "Engine", "Build", "BatchFiles", "RunUAT.bat");
										string Arguments = String.Format("/C \"\"{0}\" -profile=\"{1}\"\"", LocalRunUAT, FileReference.Combine(Project.LocalRootPath, Step.FileName));
										Log.WriteLine("uat> Running {0} {1}", LocalRunUAT, Arguments);

										int ResultFromUAT = Utility.ExecuteProcess(CmdExe, null, Arguments, null, new ProgressTextWriter(Progress, new PrefixedTextWriter("uat> ", Log)));
										if(ResultFromUAT != 0)
										{
											StepStopwatch.Stop("Failed");
											StatusMessage = String.Format("Cook failed. ({0})", ResultFromUAT);
											return WorkspaceUpdateResult.FailedToCompile;
										}

										StepStopwatch.Stop("Success");
									}
									break;
								case BuildStepType.Other:
									using(TelemetryStopwatch StepStopwatch = new TelemetryStopwatch("Workspace_Execute_Custom", Project.TelemetryProjectPath))
									{
										StepStopwatch.AddData(new { FileName = Path.GetFileNameWithoutExtension(Step.FileName) });

										FileReference ToolFileName = FileReference.Combine(Project.LocalRootPath, Utility.ExpandVariables(Step.FileName, Context.Variables));
										string ToolWorkingDir = String.IsNullOrWhiteSpace(Step.WorkingDir) ? ToolFileName.Directory.FullName : Utility.ExpandVariables(Step.WorkingDir, Context.Variables);
										string ToolArguments = Utility.ExpandVariables(Step.Arguments ?? "", Context.Variables);
										Log.WriteLine("tool> Running {0} {1}", ToolFileName, ToolArguments);

										if(Step.bUseLogWindow)
										{
											int ResultFromTool = Utility.ExecuteProcess(ToolFileName.FullName, ToolWorkingDir, ToolArguments, null, new ProgressTextWriter(Progress, new PrefixedTextWriter("tool> ", Log)));
											if(ResultFromTool != 0)
											{
												StepStopwatch.Stop("Failed");
												StatusMessage = String.Format("Tool terminated with exit code {0}.", ResultFromTool);
												return WorkspaceUpdateResult.FailedToCompile;
											}
										}
										else
										{
											ProcessStartInfo StartInfo = new ProcessStartInfo(ToolFileName.FullName, ToolArguments);
											StartInfo.WorkingDirectory = ToolWorkingDir;
											using(Process.Start(StartInfo))
											{
											}
										}

										StepStopwatch.Stop("Success");
									}
									break;
							}
						}

						Log.WriteLine();
						Progress.Pop();
					}

					Times.Add(new Tuple<string,TimeSpan>("Build", Stopwatch.Stop("Success")));
				}

				// Update the last successful build change number
				if(Context.CustomBuildSteps == null || Context.CustomBuildSteps.Count == 0)
				{
					State.LastBuiltChangeNumber = State.CurrentChangeNumber;
					State.Save();
				}
			}

			// Write out all the timing information
			Log.WriteLine("Total time : " + FormatTime(Times.Sum(x => (long)(x.Item2.TotalMilliseconds / 1000))));
			foreach(Tuple<string, TimeSpan> Time in Times)
			{
				Log.WriteLine("   {0,-8}: {1}", Time.Item1, FormatTime((long)(Time.Item2.TotalMilliseconds / 1000)));
			}
			if(NumFilesSynced > 0)
			{
				Log.WriteLine("{0} files synced.", NumFilesSynced);
			}

			DateTime FinishTime = DateTime.Now;
			Log.WriteLine();
			Log.WriteLine("UPDATE SUCCEEDED ({0} {1})", FinishTime.ToShortDateString(), FinishTime.ToShortTimeString());

			StatusMessage = "Update succeeded";
			return WorkspaceUpdateResult.Success;
		}

		static bool IsLicenseeVersion(PerforceConnection Perforce, ProjectInfo Project, TextWriter Log)
		{
			bool bIsEpicInternal;
			if (Perforce.FileExists(Project.ClientRootPath + "/Engine/Build/NotForLicensees/EpicInternal.txt", out bIsEpicInternal, Log) && bIsEpicInternal)
			{
				return false;
			}
			if (Perforce.FileExists(Project.ClientRootPath + "/Engine/Restricted/NotForLicensees/Build/EpicInternal.txt", out bIsEpicInternal, Log) && bIsEpicInternal)
			{
				return false;
			}
			return true;
		}

		public static List<string> GetSyncPaths(ProjectInfo Project, bool bSyncAllProjects, string[] SyncFilter)
		{
			List<string> SyncPaths = GetRelativeSyncPaths(Project, bSyncAllProjects, SyncFilter);
			return SyncPaths.Select(x => Project.ClientRootPath + x).ToList();
		}

		public static List<string> GetRelativeSyncPaths(ProjectInfo Project, bool bSyncAllProjects, string[] SyncFilter)
		{
			List<string> SyncPaths = new List<string>();

			// Check the client path is formatted correctly
			if (!Project.SelectedClientFileName.StartsWith(Project.ClientRootPath + "/"))
			{
				throw new Exception(String.Format("Expected '{0}' to start with '{1}'", Project.SelectedClientFileName, Project.ClientRootPath));
			}

			// Add the default project paths
			int LastSlashIdx = Project.SelectedClientFileName.LastIndexOf('/');
			if (bSyncAllProjects || !Project.SelectedClientFileName.EndsWith(".uproject", StringComparison.OrdinalIgnoreCase) || LastSlashIdx <= Project.ClientRootPath.Length)
			{
				SyncPaths.Add("/...");
			}
			else
			{
				SyncPaths.Add("/*");
				SyncPaths.Add("/Engine/...");
				if(Project.bIsEnterpriseProject)
				{
					SyncPaths.Add("/Enterprise/...");
				}
				SyncPaths.Add(Project.SelectedClientFileName.Substring(Project.ClientRootPath.Length, LastSlashIdx - Project.ClientRootPath.Length) + "/...");
			}

			// Apply the sync filter to that list. We only want inclusive rules in the output list, but we can manually apply exclusions to previous entries.
			if(SyncFilter != null)
			{
				foreach(string SyncPath in SyncFilter)
				{
					string TrimSyncPath = SyncPath.Trim();
					if(TrimSyncPath.StartsWith("/"))
					{
						SyncPaths.Add(TrimSyncPath);
					}
					else if(TrimSyncPath.StartsWith("-/") && TrimSyncPath.EndsWith("..."))
					{
						SyncPaths.RemoveAll(x => x.StartsWith(TrimSyncPath.Substring(1, TrimSyncPath.Length - 4)));
					}
				}
			}

			// Sort the remaining paths by length, and remove any paths which are included twice
			SyncPaths = SyncPaths.OrderBy(x => x.Length).ToList();
			for(int Idx = 0; Idx < SyncPaths.Count; Idx++)
			{
				string SyncPath = SyncPaths[Idx];
				if(SyncPath.EndsWith("..."))
				{
					string SyncPathPrefix = SyncPath.Substring(0, SyncPath.Length - 3);
					for(int OtherIdx = SyncPaths.Count - 1; OtherIdx > Idx; OtherIdx--)
					{
						if(SyncPaths[OtherIdx].StartsWith(SyncPathPrefix))
						{
							SyncPaths.RemoveAt(OtherIdx);
						}
					}
				}
			}

			return SyncPaths;
		}

		public static bool MatchFilter(ProjectInfo Project, FileReference FileName, FileFilter Filter)
		{
			bool bMatch = true;
			if(FileName.IsUnderDirectory(Project.LocalRootPath))
			{
				string RelativePath = FileName.MakeRelativeTo(Project.LocalRootPath);
				if(!Filter.Matches(RelativePath))
				{
					bMatch = false;
				}
			}
			return bMatch;
		}

		class SyncState
		{
			public int TotalDepotPaths;
			public HashSet<string> RemainingDepotPaths;
			public Queue<List<string>> SyncCommandLists;
			public string StatusMessage;
			public WorkspaceUpdateResult Result = WorkspaceUpdateResult.Success;
		}

		static WorkspaceUpdateResult SyncFileRevisions(PerforceConnection Perforce, string Prefix, WorkspaceUpdateContext Context, List<string> SyncCommands, HashSet<string> RemainingDepotPaths, ProgressValue Progress, TextWriter Log, out string StatusMessage)
		{
			Queue<List<string>> SyncCommandLists = new Queue<List<string>>();
			SyncCommandLists.Enqueue(SyncCommands);
			return SyncFileRevisions(Perforce, Prefix, Context, SyncCommandLists, RemainingDepotPaths, Progress, Log, out StatusMessage);
		}

		static WorkspaceUpdateResult SyncFileRevisions(PerforceConnection Perforce, string Prefix, WorkspaceUpdateContext Context, Queue<List<string>> SyncCommandLists, HashSet<string> RemainingDepotPaths, ProgressValue Progress, TextWriter Log, out string StatusMessage)
		{
			// Figure out the number of additional background threads we want to run with. We can run worker on the current thread.
			int NumExtraThreads = Math.Max(Math.Min(SyncCommandLists.Count, Context.PerforceSyncOptions.NumThreads) - 1, 0);

			List<Thread> ChildThreads = new List<Thread>(NumExtraThreads);
			try
			{
				// Create the state object shared by all the worker threads
				SyncState State = new SyncState();
				State.TotalDepotPaths = RemainingDepotPaths.Count;
				State.RemainingDepotPaths = RemainingDepotPaths;
				State.SyncCommandLists = SyncCommandLists;

				// Wrapper writer around the log class to prevent multiple threads writing to it at once
				ThreadSafeTextWriter LogWrapper = new ThreadSafeTextWriter(Log);

				// Initialize Sync Progress
				UpdateSyncState(Prefix, State, Progress);

				// Delegate for updating the sync state after a file has been synced
				Action<PerforceFileRecord, LineBasedTextWriter> SyncOutput = (Record, LocalLog) => { UpdateSyncState(Prefix, Record, State, Progress, LocalLog); };

				// Create all the child threads
				for (int ThreadIdx = 0; ThreadIdx < NumExtraThreads; ThreadIdx++)
				{
					int ThreadNumber = ThreadIdx + 2;
					Thread ChildThread = new Thread(() => StaticSyncWorker(ThreadNumber, Perforce, Context, State, SyncOutput, LogWrapper));
					ChildThread.Name = "Sync File Revisions";
					ChildThreads.Add(ChildThread);
					ChildThread.Start();
				}

				// Run one worker on the current thread
				StaticSyncWorker(1, Perforce, Context, State, SyncOutput, LogWrapper);

				// Wait for all the background threads to finish
				foreach (Thread ChildThread in ChildThreads)
				{
					ChildThread.Join();
				}

				// Return the result that was set on the state object
				StatusMessage = State.StatusMessage;
				return State.Result;
			}
			finally
			{
				foreach (Thread ChildThread in ChildThreads)
				{
					ChildThread.Interrupt();
				}
				foreach (Thread ChildThread in ChildThreads)
				{
					ChildThread.Join();
				}
			}
		}

		static void UpdateSyncState(string Prefix, SyncState State, ProgressValue Progress)
		{
			lock (State)
			{
				string Message = String.Format("{0} ({1}/{2})", Prefix, State.TotalDepotPaths - State.RemainingDepotPaths.Count, State.TotalDepotPaths);
				float Fraction = Math.Min((float)(State.TotalDepotPaths - State.RemainingDepotPaths.Count) / (float)State.TotalDepotPaths, 1.0f);
				Progress.Set(Message, Fraction);
			}
		}

		static void UpdateSyncState(string Prefix, PerforceFileRecord Record, SyncState State, ProgressValue Progress, TextWriter Log)
		{
			lock (State)
			{
				State.RemainingDepotPaths.Remove(Record.DepotPath);

				string Message = String.Format("{0} ({1}/{2})", Prefix, State.TotalDepotPaths - State.RemainingDepotPaths.Count, State.TotalDepotPaths);
				float Fraction = Math.Min((float)(State.TotalDepotPaths - State.RemainingDepotPaths.Count) / (float)State.TotalDepotPaths, 1.0f);
				Progress.Set(Message, Fraction);

				Log.WriteLine("p4>   {0} {1}", Record.Action, Record.ClientPath);
			}
		}

		static void StaticSyncWorker(int ThreadNumber, PerforceConnection Perforce, WorkspaceUpdateContext Context, SyncState State, Action<PerforceFileRecord, LineBasedTextWriter> SyncOutput, LineBasedTextWriter GlobalLog)
		{
			PrefixedTextWriter ThreadLog = new PrefixedTextWriter(String.Format("{0}:", ThreadNumber), GlobalLog);
			for (; ; )
			{
				// Remove the next batch that needs to be synced
				List<string> SyncCommands;
				lock (State)
				{
					if (State.Result == WorkspaceUpdateResult.Success && State.SyncCommandLists.Count > 0)
					{
						SyncCommands = State.SyncCommandLists.Dequeue();
					}
					else
					{
						break;
					}
				}

				// Sync the files
				string StatusMessage;
				WorkspaceUpdateResult Result;
				try
				{
					Result = StaticSyncFileRevisions(Perforce, Context, SyncCommands, Record => SyncOutput(Record, ThreadLog), ThreadLog, out StatusMessage);
				}
				catch (ThreadInterruptedException)
				{
					StatusMessage = "Sync cancelled";
					Result = WorkspaceUpdateResult.Canceled;
				}

				// If it failed, try to set it on the state if nothing else has failed first
				if (Result != WorkspaceUpdateResult.Success)
				{
					lock (State)
					{
						if (State.Result == WorkspaceUpdateResult.Success)
						{
							State.Result = Result;
							State.StatusMessage = StatusMessage;
						}
					}
					break;
				}
			}
		}

		static WorkspaceUpdateResult StaticSyncFileRevisions(PerforceConnection Perforce, WorkspaceUpdateContext Context, List<string> SyncCommands, Action<PerforceFileRecord> SyncOutput, TextWriter Log, out string StatusMessage)
		{
			// Sync them all
			List<string> TamperedFiles = new List<string>();
			if(!Perforce.Sync(SyncCommands, SyncOutput, TamperedFiles, false, Context.PerforceSyncOptions, Log))
			{
				StatusMessage = "Aborted sync due to errors.";
				return WorkspaceUpdateResult.FailedToSync;
			}

			// If any files need to be clobbered, defer to the main thread to figure out which ones
			if(TamperedFiles.Count > 0)
			{
				int NumNewFilesToClobber = 0;
				foreach(string TamperedFile in TamperedFiles)
				{
					if(!Context.ClobberFiles.ContainsKey(TamperedFile))
					{
						Context.ClobberFiles[TamperedFile] = true;
						if(TamperedFile.EndsWith(LocalObjectVersionFileName, StringComparison.OrdinalIgnoreCase) || TamperedFile.EndsWith(LocalVersionHeaderFileName, StringComparison.OrdinalIgnoreCase))
						{
							// Hack for UseFastModularVersioningV2; we don't need to update these files any more.
							continue;
						}
						NumNewFilesToClobber++;
					}
				}
				if(NumNewFilesToClobber > 0)
				{
					StatusMessage = String.Format("Cancelled sync after checking files to clobber ({0} new files).", NumNewFilesToClobber);
					return WorkspaceUpdateResult.FilesToClobber;
				}
				foreach(string TamperedFile in TamperedFiles)
				{
					if(Context.ClobberFiles[TamperedFile] && !Perforce.ForceSync(TamperedFile, Context.ChangeNumber, Log))
					{
						StatusMessage = String.Format("Couldn't sync {0}.", TamperedFile);
						return WorkspaceUpdateResult.FailedToSync;
					}
				}
			}

			// All succeeded
			StatusMessage = null;
			return WorkspaceUpdateResult.Success;
		}

		static ConfigFile ReadProjectConfigFile(DirectoryReference LocalRootPath, FileReference SelectedLocalFileName, TextWriter Log)
		{
			// Find the valid config file paths
			DirectoryInfo EngineDir = DirectoryReference.Combine(LocalRootPath, "Engine").ToDirectoryInfo();
			List<FileInfo> LocalConfigFiles = Utility.GetLocalConfigPaths(EngineDir, SelectedLocalFileName.ToFileInfo());

			// Read them in
			ConfigFile ProjectConfig = new ConfigFile();
			foreach(FileInfo LocalConfigFile in LocalConfigFiles)
			{
				try
				{
					string[] Lines = File.ReadAllLines(LocalConfigFile.FullName);
					ProjectConfig.Parse(Lines);
					Log.WriteLine("Read config file from {0}", LocalConfigFile.FullName);
				}
				catch(Exception Ex)
				{
					Log.WriteLine("Failed to read config file from {0}: {1}", LocalConfigFile.FullName, Ex.ToString());
				}
			}
			return ProjectConfig;
		}

		static IReadOnlyList<string> ReadProjectStreamFilter(PerforceConnection Perforce, ConfigFile ProjectConfigFile, TextWriter Log)
		{
			string StreamListDepotPath = ProjectConfigFile.GetValue("Options.QuickSelectStreamList", null);
			if(StreamListDepotPath == null)
			{
				return null;
			}

			List<string> Lines;
			if(!Perforce.Print(StreamListDepotPath, out Lines, Log))
			{
				return null;
			}

			return Lines.Select(x => x.Trim()).Where(x => x.Length > 0).ToList().AsReadOnly();
		}

		static string FormatTime(long Seconds)
		{
			if(Seconds >= 60)
			{
				return String.Format("{0,3}m {1:00}s", Seconds / 60, Seconds % 60);
			}
			else
			{
				return String.Format("     {0,2}s", Seconds);
			}
		}

		static bool HasModifiedSourceFiles(PerforceConnection Perforce, ProjectInfo Project, TextWriter Log)
		{
			List<PerforceFileRecord> OpenFiles;
			if(!Perforce.GetOpenFiles(Project.ClientRootPath + "/...", out OpenFiles, Log))
			{
				return true;
			}
			if(OpenFiles.Any(x => x.DepotPath.IndexOf("/Source/", StringComparison.OrdinalIgnoreCase) != -1))
			{
				return true;
			}
			return false;
		}

		static bool FindUnresolvedFiles(PerforceConnection Perforce, IEnumerable<string> SyncPaths, TextWriter Log, out List<PerforceFileRecord> UnresolvedFiles)
		{
			UnresolvedFiles = new List<PerforceFileRecord>();
			foreach(string SyncPath in SyncPaths)
			{
				List<PerforceFileRecord> Records;
				if(!Perforce.GetUnresolvedFiles(SyncPath, out Records, Log))
				{
					Log.WriteLine("Couldn't find open files matching {0}", SyncPath);
					return false;
				}
				UnresolvedFiles.AddRange(Records);
			}
			return true;
		}

		static bool UpdateVersionFile(PerforceConnection Perforce, string ClientPath, Dictionary<string, string> VersionStrings, int ChangeNumber, TextWriter Log)
		{
			return UpdateVersionFile(Perforce, ClientPath, ChangeNumber, Text => UpdateVersionStrings(Text, VersionStrings), Log);
		}

		static bool UpdateVersionFile(PerforceConnection Perforce, string ClientPath, int ChangeNumber, Func<string, string> Update, TextWriter Log)
		{
			List<PerforceFileRecord> Records;
			if(!Perforce.Stat(ClientPath, out Records, Log))
			{
				Log.WriteLine("Failed to query records for {0}", ClientPath);
				return false;
			}
			if (Records.Count > 1)
			{
				// Attempt to remove any existing file which is synced
				Perforce.ForceSync(String.Format("{0}#0", ClientPath), Log);

				// Try to get the mapped files again
				if (!Perforce.Stat(ClientPath, out Records, Log))
				{
					Log.WriteLine("Failed to query records for {0}", ClientPath);
					return false;
				}
			}
			if (Records.Count == 0)
			{
				Log.WriteLine("Ignoring {0}; not found on server.", ClientPath);
				return true;
			}

			string LocalPath = Records[0].ClientPath; // Actually a filesystem path
			string DepotPath = Records[0].DepotPath;

			List<string> Lines;
			if(!Perforce.Print(String.Format("{0}@{1}", DepotPath, ChangeNumber), out Lines, Log))
			{
				Log.WriteLine("Couldn't get default contents of {0}", DepotPath);
				return false;
			}

			string Text = String.Join("\n", Lines);
			Text = Update(Text);
			return WriteVersionFile(Perforce, LocalPath, DepotPath, Text, Log);
		}

		static string UpdateVersionStrings(string Text, Dictionary<string, string> VersionStrings)
		{
			StringWriter Writer = new StringWriter();
			foreach (string Line in Text.Split('\n'))
			{
				string NewLine = Line;
				foreach (KeyValuePair<string, string> VersionString in VersionStrings)
				{
					if (UpdateVersionLine(ref NewLine, VersionString.Key, VersionString.Value))
					{
						break;
					}
				}
				Writer.WriteLine(NewLine);
			}
			return Writer.ToString();
		}

		static string UpdateBuildVersion(string Text, int Changelist, int CodeChangelist, string BranchOrStreamName, bool bIsLicenseeVersion)
		{
			Dictionary<string, object> Object = JsonSerializer.Deserialize<Dictionary<string, object>>(Text, Program.DefaultJsonSerializerOptions);

			object PrevCompatibleChangelistObj;
			int PrevCompatibleChangelist = Object.TryGetValue("CompatibleChangelist", out PrevCompatibleChangelistObj) ? (int)Convert.ChangeType(PrevCompatibleChangelistObj.ToString(), typeof(int)) : 0;

			object PrevIsLicenseeVersionObj;
			bool PrevIsLicenseeVersion = Object.TryGetValue("IsLicenseeVersion", out PrevIsLicenseeVersionObj)? ((int)Convert.ChangeType(PrevIsLicenseeVersionObj.ToString(), typeof(int)) != 0) : false;

			Object["Changelist"] = Changelist;
			if(PrevCompatibleChangelist == 0 || PrevIsLicenseeVersion != bIsLicenseeVersion)
			{
				// Don't overwrite the compatible changelist if we're in a hotfix release
				Object["CompatibleChangelist"] = CodeChangelist;
			}
			Object["BranchName"] = BranchOrStreamName.Replace('/', '+');
			Object["IsPromotedBuild"] = 0;
			Object["IsLicenseeVersion"] = bIsLicenseeVersion ? 1 : 0;

			return JsonSerializer.Serialize(Object, new JsonSerializerOptions
			{
				WriteIndented = true, 
				// do not escape +
				Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping
			});
		}

		static bool WriteVersionFile(PerforceConnection Perforce, string LocalPath, string DepotPath, string NewText, TextWriter Log)
		{
			try
			{
				if(File.Exists(LocalPath) && File.ReadAllText(LocalPath) == NewText)
				{
					Log.WriteLine("Ignored {0}; contents haven't changed", LocalPath);
				}
				else
				{
					Directory.CreateDirectory(Path.GetDirectoryName(LocalPath));
					Utility.ForceDeleteFile(LocalPath);
					if(DepotPath != null)
					{
						Perforce.Sync(DepotPath + "#0", Log);
					}
					File.WriteAllText(LocalPath, NewText);
					Log.WriteLine("Written {0}", LocalPath);
				}
				return true;
			}
			catch(Exception Ex)
			{
				Log.WriteException(Ex, "Failed to write to {0}.", LocalPath);
				return false;
			}
		}

		static bool UpdateVersionLine(ref string Line, string Prefix, string Suffix)
		{
			int LineIdx = 0;
			int PrefixIdx = 0;
			for(;;)
			{
				string PrefixToken = ReadToken(Prefix, ref PrefixIdx);
				if(PrefixToken == null)
				{
					break;
				}

				string LineToken = ReadToken(Line, ref LineIdx);
				if(LineToken == null || LineToken != PrefixToken)
				{
					return false;
				}
			}
			Line = Line.Substring(0, LineIdx) + Suffix;
			return true;
		}

		static string ReadToken(string Line, ref int LineIdx)
		{
			for(;; LineIdx++)
			{
				if(LineIdx == Line.Length)
				{
					return null;
				}
				else if(!Char.IsWhiteSpace(Line[LineIdx]))
				{
					break;
				}
			}

			int StartIdx = LineIdx++;
			if(Char.IsLetterOrDigit(Line[StartIdx]) || Line[StartIdx] == '_')
			{
				while(LineIdx < Line.Length && (Char.IsLetterOrDigit(Line[LineIdx]) || Line[LineIdx] == '_'))
				{
					LineIdx++;
				}
			}

			return Line.Substring(StartIdx, LineIdx - StartIdx);
		}

		public bool IsBusy()
		{
			return bSyncing;
		}

		public Tuple<string, float> CurrentProgress
		{
			get { return Progress.Current; }
		}

		public int CurrentChangeNumber => State.CurrentChangeNumber;

		public int PendingChangeNumber
		{
			get;
			private set;
		}

		public string ClientName
		{
			get { return Perforce.ClientName; }
		}
	}
}
