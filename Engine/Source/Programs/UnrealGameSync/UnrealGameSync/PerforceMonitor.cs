// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace UnrealGameSync
{
	interface IArchiveInfoSource
	{
		IReadOnlyList<IArchiveInfo> AvailableArchives { get; }
	}

	class PerforceMonitor : IDisposable, IArchiveInfoSource
	{
		internal class PerforceArchiveInfo : IArchiveInfo
		{
			public string Name { get; }
			public string Type { get; }
			public string DepotPath { get; }
			public string? Target { get; }
			public PerforceMonitor Outer { get; }

			public string BasePath
			{
				get { return DepotPath; }
			}

			// TODO: executable/configuration?
			public SortedList<int, string> ChangeNumberToFileRevision = new SortedList<int, string>();

			public PerforceArchiveInfo(string Name, string Type, string DepotPath, string? Target, PerforceMonitor Outer)
			{
				this.Name = Name;
				this.Type = Type;
				this.DepotPath = DepotPath;
				this.Target = Target;
				this.Outer = Outer;
			}

			public override bool Equals(object? Other)
			{
				PerforceArchiveInfo? OtherArchive = Other as PerforceArchiveInfo;
				return OtherArchive != null && Name == OtherArchive.Name && Type == OtherArchive.Type && DepotPath == OtherArchive.DepotPath && Target == OtherArchive.Target && Enumerable.SequenceEqual(ChangeNumberToFileRevision, OtherArchive.ChangeNumberToFileRevision);
			}

			public override int GetHashCode()
			{
				throw new NotSupportedException();
			}

			public bool Exists()
			{
				return ChangeNumberToFileRevision.Count > 0;
			}

			public static bool TryParseConfigEntry(string Text, PerforceMonitor Outer, [NotNullWhen(true)] out PerforceArchiveInfo? Info)
			{
				ConfigObject Object = new ConfigObject(Text);

				string? Name = Object.GetValue("Name", null);
				if (Name == null)
				{
					Info = null;
					return false;
				}

				string? DepotPath = Object.GetValue("DepotPath", null);
				if (DepotPath == null)
				{
					Info = null;
					return false;
				}

				string? Target = Object.GetValue("Target", null);

				string Type = Object.GetValue("Type", null) ?? Name;

				Info = new PerforceArchiveInfo(Name, Type, DepotPath, Target, Outer);
				return true;
			}

			public bool TryGetArchiveKeyForChangeNumber(int ChangeNumber, [NotNullWhen(true)] out string? ArchiveKey)
			{
				return ChangeNumberToFileRevision.TryGetValue(ChangeNumber, out ArchiveKey);
			}

			public async Task<bool> DownloadArchive(IPerforceConnection Perforce, string ArchiveKey, DirectoryReference LocalRootPath, FileReference ManifestFileName, ILogger Logger, ProgressValue Progress, CancellationToken CancellationToken)
			{
				FileReference TempZipFileName = new FileReference(Path.GetTempFileName());
				try
				{
					PrintRecord Record = await Perforce.PrintAsync(TempZipFileName.FullName, ArchiveKey, CancellationToken);
					if (TempZipFileName.ToFileInfo().Length == 0)
					{
						return false;
					}
					ArchiveUtils.ExtractFiles(TempZipFileName, LocalRootPath, ManifestFileName, Progress, Logger);
				}
				finally
				{
					FileReference.SetAttributes(TempZipFileName, FileAttributes.Normal);
					FileReference.Delete(TempZipFileName);
				}

				return true;
			}

			public override string ToString()
			{
				return Name;
			}
		}

		class PerforceChangeSorter : IComparer<ChangesRecord>
		{
			public int Compare(ChangesRecord? SummaryA, ChangesRecord? SummaryB)
			{
				return SummaryB!.Number - SummaryA!.Number;
			}
		}

		public int InitialMaxChangesValue = 100;

		IPerforceSettings PerforceSettings;
		readonly string BranchClientPath;
		readonly string SelectedClientFileName;
		readonly string SelectedProjectIdentifier;
		Task? WorkerTask;
		CancellationTokenSource CancellationSource;
		int PendingMaxChangesValue;
		SortedSet<ChangesRecord> Changes = new SortedSet<ChangesRecord>(new PerforceChangeSorter());
		SortedDictionary<int, PerforceChangeDetails> ChangeDetails = new SortedDictionary<int,PerforceChangeDetails>();
		SortedSet<int> PromotedChangeNumbers = new SortedSet<int>();
		List<PerforceArchiveInfo> Archives = new List<PerforceArchiveInfo>();
		AsyncEvent RefreshEvent = new AsyncEvent();
		ILogger Logger;
		bool bIsEnterpriseProject;
		DirectoryReference CacheFolder;
		List<KeyValuePair<FileReference, DateTime>> LocalConfigFiles;
		IAsyncDisposer AsyncDisposeTasks;

		SynchronizationContext SynchronizationContext;
		public event Action? OnUpdate;
		public event Action? OnUpdateMetadata;
		public event Action? OnStreamChange;
		public event Action? OnLoginExpired;

		public TimeSpan ServerTimeZone
		{
			get;
			private set;
		}

		public PerforceMonitor(IPerforceSettings InPerforceSettings, ProjectInfo ProjectInfo, ConfigFile InProjectConfigFile, DirectoryReference InCacheFolder, List<KeyValuePair<FileReference, DateTime>> InLocalConfigFiles, IServiceProvider InServiceProvider)
		{
			PerforceSettings = InPerforceSettings;
			BranchClientPath = ProjectInfo.ClientRootPath;
			SelectedClientFileName = ProjectInfo.ClientFileName;
			SelectedProjectIdentifier = ProjectInfo.ProjectIdentifier;
			PendingMaxChangesValue = InitialMaxChangesValue;
			LastChangeByCurrentUser = -1;
			LastCodeChangeByCurrentUser = -1;
			Logger = InServiceProvider.GetRequiredService<ILogger<PerforceMonitor>>();
			bIsEnterpriseProject = ProjectInfo.bIsEnterpriseProject;
			LatestProjectConfigFile = InProjectConfigFile;
			CacheFolder = InCacheFolder;
			LocalConfigFiles = InLocalConfigFiles;
			AsyncDisposeTasks = InServiceProvider.GetRequiredService<IAsyncDisposer>();
			SynchronizationContext = SynchronizationContext.Current!;
			CancellationSource = new CancellationTokenSource();

			AvailableArchives = (new List<IArchiveInfo>()).AsReadOnly();
		}

		public void Start()
		{
			if (WorkerTask == null)
			{
				WorkerTask = Task.Run(() => PollForUpdates(CancellationSource.Token));
			}
		}

		public void Dispose()
		{
			OnUpdate = null;
			OnUpdateMetadata = null;
			OnStreamChange = null;
			OnLoginExpired = null;

			if (WorkerTask != null)
			{
				CancellationSource.Cancel();
				AsyncDisposeTasks.Add(WorkerTask.ContinueWith(_ => CancellationSource.Dispose()));
				WorkerTask = null;
			}
		}

		public bool IsActive
		{
			get;
			set;
		}

		public string LastStatusMessage
		{
			get;
			private set;
		} = "";

		public int CurrentMaxChanges
		{
			get;
			private set;
		}

		public int PendingMaxChanges
		{
			get { return PendingMaxChangesValue; }
			set { lock(this){ if(value != PendingMaxChangesValue){ PendingMaxChangesValue = value; RefreshEvent.Set(); } } }
		}

		async Task PollForUpdates(CancellationToken CancellationToken)
		{
			bool bCoolDown = false;
			while (!CancellationToken.IsCancellationRequested)
			{
				try
				{
					if (bCoolDown)
					{
						await Task.Delay(TimeSpan.FromSeconds(20.0), CancellationToken);
					}
					else
					{
						await PollForUpdatesInner(CancellationToken);
					}
				}
				catch (OperationCanceledException) when (CancellationToken.IsCancellationRequested)
				{
					break;
				}
				catch (Exception Ex)
				{
					Logger.LogError(Ex, "Unhandled exception in PollForUpdatesInner()");
					if (!(Ex is PerforceException))
					{
						Program.CaptureException(Ex);
					}
					bCoolDown = true;
				}
			}
		}

		async Task PollForUpdatesInner(CancellationToken CancellationToken)
		{
			string? StreamName;
			using (IPerforceConnection Perforce = await PerforceConnection.CreateAsync(PerforceSettings, Logger))
			{
				StreamName = await Perforce.GetCurrentStreamAsync(CancellationToken);

				// Get the perforce server settings
				PerforceResponse<InfoRecord> InfoResponse = await Perforce.TryGetInfoAsync(InfoOptions.ShortOutput, CancellationToken);
				if (InfoResponse.Succeeded)
				{
					DateTimeOffset? ServerDate = InfoResponse.Data.ServerDate;
					if (ServerDate.HasValue)
					{
						ServerTimeZone = ServerDate.Value.Offset;
					}
				}

				// Try to update the zipped binaries list before anything else, because it causes a state change in the UI
				await UpdateArchivesAsync(Perforce, CancellationToken);
			}

			while(!CancellationToken.IsCancellationRequested)
			{
				Stopwatch Timer = Stopwatch.StartNew();
				Task NextRefreshTask = RefreshEvent.Task;

				using (IPerforceConnection Perforce = await PerforceConnection.CreateAsync(PerforceSettings, Logger))
				{
					// Check we still have a valid login ticket
					PerforceResponse<LoginRecord> LoginState = await Perforce.TryGetLoginStateAsync(CancellationToken);
					if (!LoginState.Succeeded)
					{
						LastStatusMessage = "User is not logged in";
						SynchronizationContext.Post(_ => OnLoginExpired?.Invoke(), null);
					}
					else
					{
						// Check we haven't switched streams
						string? NewStreamName = await Perforce.GetCurrentStreamAsync(CancellationToken);
						if (NewStreamName != StreamName)
						{
							SynchronizationContext.Post(_ => OnStreamChange?.Invoke(), null);
						}

						// Check for any p4 changes
						if (!await UpdateChangesAsync(Perforce, CancellationToken))
						{
							LastStatusMessage = "Failed to update changes";
						}
						else if (!await UpdateChangeTypesAsync(Perforce, CancellationToken))
						{
							LastStatusMessage = "Failed to update change types";
						}
						else if (!await UpdateArchivesAsync(Perforce, CancellationToken))
						{
							LastStatusMessage = "Failed to update zipped binaries list";
						}
						else
						{
							LastStatusMessage = String.Format("Last update took {0}ms", Timer.ElapsedMilliseconds);
						}
					}
				}

				// Wait for another request, or scan for new builds after a timeout
				Task DelayTask = Task.Delay(TimeSpan.FromMinutes(IsActive ? 2 : 10), CancellationToken);
				await Task.WhenAny(NextRefreshTask, DelayTask);
			}
		}

		async Task<bool> UpdateChangesAsync(IPerforceConnection Perforce, CancellationToken CancellationToken)
		{
			// Get the current status of the build
			int MaxChanges;
			int OldestChangeNumber = -1;
			int NewestChangeNumber = -1;
			HashSet<int> CurrentChangelists;
			SortedSet<int> PrevPromotedChangelists;
			lock(this)
			{
				MaxChanges = PendingMaxChanges;
				if(Changes.Count > 0)
				{
					NewestChangeNumber = Changes.First().Number;
					OldestChangeNumber = Changes.Last().Number;
				}
				CurrentChangelists = new HashSet<int>(Changes.Select(x => x.Number));
				PrevPromotedChangelists = new SortedSet<int>(PromotedChangeNumbers);
			}

			// Build a full list of all the paths to sync
			List<string> DepotPaths = new List<string>();
			if (SelectedClientFileName.EndsWith(".uprojectdirs", StringComparison.InvariantCultureIgnoreCase))
			{
				DepotPaths.Add(String.Format("{0}/...", BranchClientPath));
			}
			else
			{
				DepotPaths.Add(String.Format("{0}/*", BranchClientPath));
				DepotPaths.Add(String.Format("{0}/Engine/...", BranchClientPath));
				DepotPaths.Add(String.Format("{0}/...", PerforceUtils.GetClientOrDepotDirectoryName(SelectedClientFileName)));
				if (bIsEnterpriseProject)
				{
					DepotPaths.Add(String.Format("{0}/Enterprise/...", BranchClientPath));
				}

				// Add in additional paths property
				ConfigSection ProjectConfigSection = LatestProjectConfigFile.FindSection("Perforce");
				if (ProjectConfigSection != null)
				{
					IEnumerable<string> AdditionalPaths = ProjectConfigSection.GetValues("AdditionalPathsToSync", new string[0]);

					// turn into //ws/path
					DepotPaths.AddRange(AdditionalPaths.Select(P => string.Format("{0}/{1}", BranchClientPath, P.TrimStart('/'))));
				}
			}

			// Read any new changes
			List<ChangesRecord> NewChanges;
			if(MaxChanges > CurrentMaxChanges)
			{
				NewChanges = await Perforce.GetChangesAsync(ChangesOptions.IncludeTimes | ChangesOptions.LongOutput, MaxChanges, ChangeStatus.Submitted, DepotPaths, CancellationToken);
			}
			else
			{
				NewChanges = await Perforce.GetChangesAsync(ChangesOptions.IncludeTimes | ChangesOptions.LongOutput, -1, ChangeStatus.Submitted, DepotPaths.Select(x => $"{x}@>{NewestChangeNumber}").ToArray(), CancellationToken);
			}

			// Remove anything we already have
			NewChanges.RemoveAll(x => CurrentChangelists.Contains(x.Number));

			// Update the change ranges
			if(NewChanges.Count > 0)
			{
				OldestChangeNumber = Math.Max(OldestChangeNumber, NewChanges.Last().Number);
				NewestChangeNumber = Math.Min(NewestChangeNumber, NewChanges.First().Number);
			}

			// If we are using zipped binaries, make sure we have every change since the last zip containing them. This is necessary for ensuring that content changes show as
			// syncable in the workspace view if there have been a large number of content changes since the last code change.
			int MinZippedChangeNumber = -1;
			foreach (PerforceArchiveInfo Archive in Archives)
			{
				foreach (int ChangeNumber in Archive.ChangeNumberToFileRevision.Keys)
				{
					if (ChangeNumber > MinZippedChangeNumber && ChangeNumber <= OldestChangeNumber)
					{
						MinZippedChangeNumber = ChangeNumber;
					}
				}
			}
			if(MinZippedChangeNumber != -1 && MinZippedChangeNumber < OldestChangeNumber)
			{
				string[] FilteredPaths = DepotPaths.Select(x => $"{x}@{MinZippedChangeNumber},{OldestChangeNumber - 1}").ToArray();
				List<ChangesRecord> ZipChanges = await Perforce.GetChangesAsync(ChangesOptions.None, -1, ChangeStatus.Submitted, FilteredPaths, CancellationToken);
				NewChanges.AddRange(ZipChanges);
			}

			// Fixup any ROBOMERGE authors
			const string RoboMergePrefix = "#ROBOMERGE-AUTHOR:";
			foreach (ChangesRecord Change in NewChanges)
			{
				if(Change.Description != null && Change.Description.StartsWith(RoboMergePrefix))
				{
					int StartIdx = RoboMergePrefix.Length;
					while(StartIdx < Change.Description.Length && Change.Description[StartIdx] == ' ')
					{
						StartIdx++;
					}

					int EndIdx = StartIdx;
					while(EndIdx < Change.Description.Length && !Char.IsWhiteSpace(Change.Description[EndIdx]))
					{
						EndIdx++;
					}

					if(EndIdx > StartIdx)
					{
						Change.User = Change.Description.Substring(StartIdx, EndIdx - StartIdx);
						Change.Description = "ROBOMERGE: " + Change.Description.Substring(EndIdx).TrimStart();
					}
				}
			}

			// Process the new changes received
			if(NewChanges.Count > 0 || MaxChanges < CurrentMaxChanges)
			{
				// Insert them into the builds list
				lock(this)
				{
					Changes.UnionWith(NewChanges);
					if(Changes.Count > MaxChanges)
					{
						// Remove changes to shrink it to the max requested size, being careful to avoid removing changes that would affect our ability to correctly
						// show the availability for content changes using zipped binaries.
						SortedSet<ChangesRecord> TrimmedChanges = new SortedSet<ChangesRecord>(new PerforceChangeSorter());
						foreach(ChangesRecord Change in Changes)
						{
							TrimmedChanges.Add(Change);
							if(TrimmedChanges.Count >= MaxChanges && Archives.Any(x => x.ChangeNumberToFileRevision.Count == 0 || x.ChangeNumberToFileRevision.ContainsKey(Change.Number) || x.ChangeNumberToFileRevision.First().Key > Change.Number))
							{
								break;
							}
						}
						Changes = TrimmedChanges;
					}
					CurrentMaxChanges = MaxChanges;
				}

				// Find the last submitted change by the current user
				int NewLastChangeByCurrentUser = -1;
				foreach(ChangesRecord Change in Changes)
				{
					if(String.Compare(Change.User, Perforce.Settings.UserName, StringComparison.InvariantCultureIgnoreCase) == 0)
					{
						NewLastChangeByCurrentUser = Math.Max(NewLastChangeByCurrentUser, Change.Number);
					}
				}
				LastChangeByCurrentUser = NewLastChangeByCurrentUser;

				// Notify the main window that we've got more data
				SynchronizationContext.Post(_ => OnUpdate?.Invoke(), null);
			}
			return true;
		}

		public async Task<bool> UpdateChangeTypesAsync(IPerforceConnection Perforce, CancellationToken CancellationToken)
		{
			// Find the changes we need to query
			List<int> QueryChangeNumbers = new List<int>();
			lock(this)
			{
				foreach(ChangesRecord Change in Changes)
				{
					if(!ChangeDetails.ContainsKey(Change.Number))
					{
						QueryChangeNumbers.Add(Change.Number);
					}
				}
			}

			// Update them in batches
			bool bUpdatedConfigFile = false;
			using (CancellationTokenSource cancellationSource = new CancellationTokenSource())
			{
				Task notifyTask = Task.CompletedTask;
				foreach (IReadOnlyList<int> QueryChangeNumberBatch in QueryChangeNumbers.OrderByDescending(x => x).Batch(10))
				{
					CancellationToken.ThrowIfCancellationRequested();

					// Skip this stuff if the user wants us to query for more changes
					if (PendingMaxChanges != CurrentMaxChanges)
					{
						break;
					}

					// If there's something to check for, find all the content changes after this changelist
					const int MaxFiles = 100;

					List<DescribeRecord> DescribeRecords = await Perforce.DescribeAsync(DescribeOptions.None, MaxFiles, QueryChangeNumberBatch.ToArray(), CancellationToken);
					foreach (DescribeRecord DescribeRecord in DescribeRecords.OrderByDescending(x => x.Number))
					{
						int QueryChangeNumber = DescribeRecord.Number;

						PerforceChangeDetails Details = new PerforceChangeDetails(DescribeRecord);
						if (DescribeRecord.Files.Count >= MaxFiles)
						{
							// Assume it's a code/content change if it has more files than we queried
							Details.bContainsCode = Details.bContainsContent = true;
						}

						lock (this)
						{
							if (!ChangeDetails.ContainsKey(QueryChangeNumber))
							{
								ChangeDetails.Add(QueryChangeNumber, Details);
							}
						}

						// Reload the config file if it changes
						if (DescribeRecord.Files.Any(x => x.DepotFile.EndsWith("/UnrealGameSync.ini", StringComparison.OrdinalIgnoreCase)) && !bUpdatedConfigFile)
						{
							await UpdateProjectConfigFileAsync(Perforce, CancellationToken);
							bUpdatedConfigFile = true;
						}

						// Notify the caller after a fixed period of time, in case further updates are slow to arrive
						if (notifyTask.IsCompleted)
						{
							notifyTask = Task.Delay(TimeSpan.FromSeconds(5.0), cancellationSource.Token).ContinueWith(_ => SynchronizationContext.Post(_ => OnUpdateMetadata?.Invoke(), null));
						}
					}
				}
				cancellationSource.Cancel();
				await notifyTask.ContinueWith(_ => { }); // Ignore exceptions
			}

			// Find the last submitted code change by the current user
			int NewLastCodeChangeByCurrentUser = -1;
			foreach(ChangesRecord Change in Changes)
			{
				if(String.Compare(Change.User, Perforce.Settings.UserName, StringComparison.InvariantCultureIgnoreCase) == 0)
				{
					PerforceChangeDetails? OtherDetails;
					if(ChangeDetails.TryGetValue(Change.Number, out OtherDetails) && OtherDetails.bContainsCode)
					{
						NewLastCodeChangeByCurrentUser = Math.Max(NewLastCodeChangeByCurrentUser, Change.Number);
					}
				}
			}
			LastCodeChangeByCurrentUser = NewLastCodeChangeByCurrentUser;

			// Notify the main window that we've got an update
			SynchronizationContext.Post(_ => OnUpdateMetadata?.Invoke(), null);

			if(LocalConfigFiles.Any(x => FileReference.GetLastWriteTimeUtc(x.Key) != x.Value))
			{
				await UpdateProjectConfigFileAsync(Perforce, CancellationToken);
				SynchronizationContext.Post(_ => OnUpdateMetadata?.Invoke(), null);
			}

			return true;
		}

		async Task<bool> UpdateArchivesAsync(IPerforceConnection Perforce, CancellationToken CancellationToken)
		{
			List<PerforceArchiveInfo> NewArchives = new List<PerforceArchiveInfo>();

			// Find all the zipped binaries under this stream
			ConfigSection ProjectConfigSection = LatestProjectConfigFile.FindSection(SelectedProjectIdentifier);
			if (ProjectConfigSection != null)
			{
				// Legacy
				string? LegacyEditorArchivePath = ProjectConfigSection.GetValue("ZippedBinariesPath", null);
				if (LegacyEditorArchivePath != null)
				{
					NewArchives.Add(new PerforceArchiveInfo("Editor", "Editor", LegacyEditorArchivePath, null, this));
				}

				// New style
				foreach (string ArchiveValue in ProjectConfigSection.GetValues("Archives", new string[0]))
				{
					PerforceArchiveInfo? Archive;
					if (PerforceArchiveInfo.TryParseConfigEntry(ArchiveValue, this, out Archive))
					{
						NewArchives.Add(Archive);
					}
				}

				// Make sure the zipped binaries path exists
				foreach (PerforceArchiveInfo NewArchive in NewArchives)
				{
					PerforceResponseList<FileLogRecord> Response = await Perforce.TryFileLogAsync(30, FileLogOptions.FullDescriptions, NewArchive.DepotPath, CancellationToken);
					if (Response.Succeeded)
					{
 						// Build a new list of zipped binaries
						foreach (FileLogRecord File in Response.Data)
						{
							foreach(RevisionRecord Revision in File.Revisions)
							{
								if (Revision.Action != FileAction.Purge)
								{
									string[] Tokens = Revision.Description.Split(' ');
									if (Tokens[0].StartsWith("[CL") && Tokens[1].EndsWith("]"))
									{
										int OriginalChangeNumber;
										if (int.TryParse(Tokens[1].Substring(0, Tokens[1].Length - 1), out OriginalChangeNumber) && !NewArchive.ChangeNumberToFileRevision.ContainsKey(OriginalChangeNumber))
										{
											NewArchive.ChangeNumberToFileRevision[OriginalChangeNumber] = $"{NewArchive.DepotPath}#{Revision.RevisionNumber}";
										}
									}
								}
							}
						}
					}
				}
			}

			// Check if the information has changed
			if (!Enumerable.SequenceEqual(Archives, NewArchives))
			{
				Archives = NewArchives;
				AvailableArchives = Archives.Select(x => (IArchiveInfo)x).ToList();

				if (Changes.Count > 0)
				{
					SynchronizationContext.Post(_ => OnUpdateMetadata?.Invoke(), null);
				}
			}

			return true;
		}

		async Task UpdateProjectConfigFileAsync(IPerforceConnection Perforce, CancellationToken CancellationToken)
		{
			LocalConfigFiles.Clear();
			LatestProjectConfigFile = await ConfigUtils.ReadProjectConfigFileAsync(Perforce, BranchClientPath, SelectedClientFileName, CacheFolder, LocalConfigFiles, Logger, CancellationToken);
		}

		public List<ChangesRecord> GetChanges()
		{
			lock(this)
			{
				return new List<ChangesRecord>(Changes);
			}
		}

		public bool TryGetChangeDetails(int Number, [NotNullWhen(true)] out PerforceChangeDetails? Details)
		{
			lock(this)
			{
				return ChangeDetails.TryGetValue(Number, out Details);
			}
		}

		public HashSet<int> GetPromotedChangeNumbers()
		{
			lock(this)
			{
				return new HashSet<int>(PromotedChangeNumbers);
			}
		}

		public int LastChangeByCurrentUser
		{
			get;
			private set;
		}

		public int LastCodeChangeByCurrentUser
		{
			get;
			private set;
		}

		public ConfigFile LatestProjectConfigFile
		{
			get;
			private set;
		}

		public IReadOnlyList<IArchiveInfo> AvailableArchives
		{
			get;
			private set;
		}

		public void Refresh()
		{
			RefreshEvent.Set();
		}
	}
}
