// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using Perforce;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using HordeServer.Storage;
using HordeServer.Storage.Primitives;
using HordeServer.Services;
using EpicGames.Serialization;
using MongoDB.Bson.Serialization.Attributes;
using System.Collections.Concurrent;
using System.Globalization;
using Microsoft.Extensions.Hosting;
using EpicGames.Perforce.Managed;
using Microsoft.Extensions.Caching.Memory;
using System.Threading.Channels;
using StackExchange.Redis;
using EpicGames.Perforce;
using EpicGames.Redis;
using System.Diagnostics;

namespace HordeServer.Commits.Impl
{
	using P4 = Perforce.P4;
	using NamespaceId = StringId<INamespace>;
	using BucketId = StringId<IBucket>;
	using StreamId = StringId<IStream>;

	/// <summary>
	/// Service which mirrors changes from Perforce
	/// </summary>
	class CommitService : ICommitService, IHostedService, IDisposable
	{
		bool EnableUpdates = false;

		/// <summary>
		/// Metadata about a stream that needs to have commits mirrored
		/// </summary>
		class StreamInfo
		{
			public IStream Stream { get; set; }
			public ViewMap View { get; }

			public StreamInfo(IStream Stream, ViewMap View)
			{
				this.Stream = Stream;
				this.View = View;
			}
		}

		NamespaceId NamespaceId { get; } = new NamespaceId("default");
		BucketId BucketId { get; } = new BucketId("commits");

		// Redis
		IDatabase Redis;

		// Collections
		ICommitCollection CommitCollection;
		IObjectCollection ObjectCollection;
		IRefCollection RefCollection;
		IStreamCollection StreamCollection;
		IPerforceService PerforceService;
		IUserCollection UserCollection;
		ISingletonDocument<Globals> GlobalsDocument;
		ILogger<CommitService> Logger;
		ElectedTick UpdateCommitsTicker;

		const int MaxBackgroundTasks = 2;

		/// <summary>
		/// Constructor
		/// </summary>
		public CommitService(IDatabase Redis, ICommitCollection CommitCollection, IObjectCollection ObjectCollection, IRefCollection RefCollection, IStreamCollection StreamCollection, IPerforceService PerforceService, IUserCollection UserCollection, DatabaseService DatabaseService, ILogger<CommitService> Logger)
		{
			this.Redis = Redis;
			this.RedisDirtyStreams = new RedisSet<StreamId>(Redis, RedisBaseKey.Append("streams"));
			this.RedisReservations = new RedisSortedSet<StreamId>(Redis, RedisBaseKey.Append("reservations"));

			this.CommitCollection = CommitCollection;
			this.ObjectCollection = ObjectCollection;
			this.RefCollection = RefCollection;
			this.StreamCollection = StreamCollection;
			this.PerforceService = PerforceService;
			this.UserCollection = UserCollection;
			this.GlobalsDocument = new SingletonDocument<Globals>(DatabaseService);
			this.Logger = Logger;
			this.UpdateCommitsTicker = new ElectedTick(DatabaseService, new ObjectId("60f866c49e7268f71803b6ef"), UpdateCommitsAsync, TimeSpan.FromSeconds(30.0), Logger);
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			UpdateCommitsTicker.Dispose();
		}

		/// <inheritdoc/>
		public async Task StartAsync(CancellationToken CancellationToken)
		{
			if (EnableUpdates)
			{
				UpdateCommitsTicker.Start();
				await StartTreeUpdatesAsync();
			}
		}

		/// <inheritdoc/>
		public async Task StopAsync(CancellationToken CancellationToken)
		{
			if (EnableUpdates)
			{
				await StopTreeUpdatesAsync();
				await UpdateCommitsTicker.StopAsync();
			}
		}

		#region Commit updates

		/// <summary>
		/// Polls Perforce for submitted changes
		/// </summary>
		/// <param name="CancellationToken"></param>
		/// <returns></returns>
		async Task UpdateCommitsAsync(CancellationToken CancellationToken)
		{
			List<IStream> Streams = await StreamCollection.FindAllAsync();
			foreach (IGrouping<string, IStream> Group in Streams.GroupBy(x => x.ClusterName, StringComparer.OrdinalIgnoreCase))
			{
				try
				{
					await UpdateCommitsForClusterAsync(Group.Key, Group);
				}
				catch (Exception Ex)
				{
					Logger.LogError(Ex, "Error while updating cluster {ClusterName}", Group.Key);
				}
			}
		}

		/// <summary>
		/// Updates all the streams on a particular server cluster
		/// </summary>
		/// <param name="ClusterName"></param>
		/// <param name="Streams"></param>
		/// <returns></returns>
		async Task UpdateCommitsForClusterAsync(string ClusterName, IEnumerable<IStream> Streams)
		{
			// Create a connection to the server
			IPerforceConnection? Connection = await PerforceService.GetServiceUserConnection(ClusterName);
			if (Connection == null)
			{
				throw new PerforceException($"Unable to create cluster connection for {ClusterName}");
			}

			// Figure out the case settings for the server
			InfoRecord ServerInfo = await Connection.GetInfoAsync(InfoOptions.ShortOutput);

			// Get the view for each stream
			List<StreamInfo> StreamInfoList = new List<StreamInfo>();
			foreach (IStream Stream in Streams)
			{
				ViewMap View = await GetStreamViewAsync(Connection, Stream.Name);
				StreamInfoList.Add(new StreamInfo(Stream, View));
			}

			// Find all the depot roots
			Dictionary<string, string> DepotRoots = new Dictionary<string, string>(ServerInfo.PathComparer);
			foreach (ViewMap View in StreamInfoList.Select(x => x.View))
			{
				foreach (ViewMapEntry Entry in View.Entries)
				{
					if (Entry.SourcePrefix.Length >= 3)
					{
						int SlashIdx = Entry.SourcePrefix.IndexOf('/', 2);
						string Depot = Entry.SourcePrefix.Substring(0, SlashIdx + 1);

						string? DepotRoot;
						if (DepotRoots.TryGetValue(Depot, out DepotRoot))
						{
							DepotRoot = GetCommonPrefix(DepotRoot, Entry.SourcePrefix);
						}
						else
						{
							DepotRoot = Entry.SourcePrefix;
						}

						int LastSlashIdx = DepotRoot.LastIndexOf('/');
						DepotRoots[Depot] = DepotRoot.Substring(0, LastSlashIdx + 1);
					}
				}
			}

			// Find the minimum changelist number to query
			int MinChange = int.MaxValue;
			foreach (StreamInfo StreamInfo in StreamInfoList)
			{
				RedisList<int> Changes = RedisStreamChanges(StreamInfo.Stream.Id);

				int FirstChange = await Changes.GetByIndexAsync(-1);
				if (FirstChange == 0)
				{
					FirstChange = await GetFirstCommitToReplicateAsync(Connection, StreamInfo.View, ServerInfo.PathComparison);
				}
				else
				{
					FirstChange++;
				}

				if (FirstChange != 0)
				{
					MinChange = Math.Min(MinChange, FirstChange);
				}
			}

			// Find all the changes to consider
			SortedSet<int> ChangeNumbers = new SortedSet<int>();
			foreach (string DepotRoot in DepotRoots.Values)
			{
				List<ChangesRecord> Changes = await Connection.GetChangesAsync(ChangesOptions.None, null, MinChange, -1, ChangeStatus.Submitted, null, $"{DepotRoot}...");
				ChangeNumbers.UnionWith(Changes.Select(x => x.Number));
			}

			// Add the changes in order
			List<string> RelativePaths = new List<string>();
			foreach (int ChangeNumber in ChangeNumbers)
			{
				DescribeRecord DescribeRecord = await Connection.DescribeAsync(ChangeNumber);
				foreach (StreamInfo StreamInfo in StreamInfoList)
				{
					IStream Stream = StreamInfo.Stream;

					string? BasePath = GetBasePath(DescribeRecord, StreamInfo.View, ServerInfo.PathComparison);
					if (BasePath != null)
					{
						await AddCommitAsync(Stream, DescribeRecord, BasePath);

						RedisList<int> StreamCommitsKey = RedisStreamChanges(Stream.Id);
						await StreamCommitsKey.RightPushAsync(DescribeRecord.Number);

						await RedisDirtyStreams.AddAsync(Stream.Id);
						await Redis.PublishAsync(RedisUpdateChannel, Stream.Id);
					}
				}
			}
		}

		/// <summary>
		/// Find the first commit to replicate from a branch
		/// </summary>
		/// <param name="Connection"></param>
		/// <param name="View"></param>
		/// <param name="Comparison"></param>
		/// <returns></returns>
		static async Task<int> GetFirstCommitToReplicateAsync(IPerforceConnection Connection, ViewMap View, StringComparison Comparison)
		{
			int MinChange = 0;

			List<string> RootPaths = View.GetRootPaths(Comparison);
			foreach (string RootPath in RootPaths)
			{
				IList<ChangesRecord> PathChanges = await Connection.GetChangesAsync(ChangesOptions.None, 20, ChangeStatus.Submitted, $"{RootPath}...");
				MinChange = Math.Max(MinChange, PathChanges.Min(x => x.Number));
			}

			return MinChange;
		}

		/// <summary>
		/// Find the base path for a change within a stream
		/// </summary>
		/// <param name="Changelist">Information about the changelist</param>
		/// <param name="View">Mapping from depot syntax to client view</param>
		/// <param name="Comparison">Path comparison type for the server</param>
		/// <returns>The base path for all files in the change</returns>
		static string? GetBasePath(DescribeRecord Changelist, ViewMap View, StringComparison Comparison)
		{
			string? BasePath = null;
			foreach (DescribeFileRecord File in Changelist.Files)
			{
				string? StreamFile;
				if (View.TryMapFile(File.DepotFile, Comparison, out StreamFile))
				{
					if (BasePath == null)
					{
						BasePath = StreamFile;
					}
					else
					{
						BasePath = GetCommonPrefix(BasePath, StreamFile);
					}
				}
			}
			return BasePath;
		}

		/// <summary>
		/// Gets the common prefix between two stringsc
		/// </summary>
		/// <param name="A"></param>
		/// <param name="B"></param>
		/// <returns></returns>
		static string GetCommonPrefix(string A, string B)
		{
			int Index = 0;
			while (Index < A.Length && Index < B.Length && A[Index] == B[Index])
			{
				Index++;
			}
			return A.Substring(0, Index);
		}

		/// <summary>
		/// Adds metadata for a particular commit
		/// </summary>
		/// <param name="Stream"></param>
		/// <param name="Changelist"></param>
		/// <param name="BasePath"></param>
		/// <returns></returns>
		async Task<ICommit> AddCommitAsync(IStream Stream, DescribeRecord Changelist, string BasePath)
		{
			IUser Author = await UserCollection.FindOrAddUserByLoginAsync(Changelist.User);
			IUser Owner = (await ParseRobomergeOwnerAsync(Changelist.Description)) ?? Author;

			int OriginalChange = ParseRobomergeSource(Changelist.Description) ?? Changelist.Number;

			NewCommit NewCommit = new NewCommit(Stream.Id, Changelist.Number, OriginalChange, Author.Id, Owner.Id, Changelist.Description, BasePath, Changelist.Time);
			return await CommitCollection.AddOrReplaceAsync(NewCommit);
		}

		/// <inheritdoc/>
		public async Task<List<ICommit>> FindCommitsAsync(StreamId StreamId, int? MinChange = null, int? MaxChange = null, string[]? Paths = null, int? Index = null, int? Count = null)
		{
			if(Paths != null)
			{
				throw new NotImplementedException();
			}

			return await CommitCollection.FindCommitsAsync(StreamId, MinChange, MaxChange, Index, Count);
		}

		/// <summary>
		/// Attempts to parse the Robomerge source from this commit information
		/// </summary>
		/// <param name="Description">Description text to parse</param>
		/// <returns>The parsed source changelist, or null if no #ROBOMERGE-SOURCE tag was present</returns>
		static int? ParseRobomergeSource(string Description)
		{
			// #ROBOMERGE-SOURCE: CL 13232051 in //Fortnite/Release-12.60/... via CL 13232062 via CL 13242953
			Match Match = Regex.Match(Description, @"^#ROBOMERGE-SOURCE: CL (\d+)", RegexOptions.Multiline);
			if (Match.Success)
			{
				return int.Parse(Match.Groups[1].Value, CultureInfo.InvariantCulture);
			}
			else
			{
				return null;
			}
		}

		async Task<IUser?> ParseRobomergeOwnerAsync(string Description)
		{
			string? OriginalAuthor = ParseRobomergeOwner(Description);
			if (OriginalAuthor != null)
			{
				return await UserCollection.FindOrAddUserByLoginAsync(OriginalAuthor);
			}
			return null;
		}

		/// <summary>
		/// Attempts to parse the Robomerge owner from this commit information
		/// </summary>
		/// <param name="Description">Description text to parse</param>
		/// <returns>The Robomerge owner, or null if no #ROBOMERGE-OWNER tag was present</returns>
		static string? ParseRobomergeOwner(string Description)
		{
			// #ROBOMERGE-OWNER: ben.marsh
			Match Match = Regex.Match(Description, @"^#ROBOMERGE-OWNER:\s*([^\s]+)", RegexOptions.Multiline);
			if (Match.Success)
			{
				return Match.Groups[1].Value;
			}
			else
			{
				return null;
			}
		}
		#endregion

		#region Tree Updates

		RedisKey RedisBaseKey { get; } = new RedisKey("commits/");
		RedisChannel<StreamId> RedisUpdateChannel { get; } = new RedisChannel<StreamId>("commits/streams");
		readonly RedisSet<StreamId> RedisDirtyStreams;
		readonly RedisSortedSet<StreamId> RedisReservations;
		Channel<StreamId>? RedisUpdateStreams;
		RedisChannelSubscription<StreamId>? RedisUpdateSubscription;
		Task? StreamUpdateTask;
		RedisList<int> RedisStreamChanges(StreamId StreamId) => new RedisList<int>(Redis, RedisBaseKey.Append($"stream/{StreamId}/changes"));

		async Task StartTreeUpdatesAsync()
		{
			RedisUpdateStreams = Channel.CreateUnbounded<StreamId>();
			RedisUpdateSubscription = await Redis.Multiplexer.GetSubscriber().SubscribeAsync(RedisUpdateChannel, (_, StreamId) => RedisUpdateStreams.Writer.TryWrite(StreamId));
			StreamUpdateTask = Task.Run(() => UpdateTreesAsync());
		}

		async Task StopTreeUpdatesAsync()
		{
			if (RedisUpdateStreams != null)
			{
				RedisUpdateStreams.Writer.Complete();
				RedisUpdateStreams = null;
			}
			if (RedisUpdateSubscription != null)
			{
				await RedisUpdateSubscription.DisposeAsync();
				RedisUpdateSubscription = null;
			}
			await StreamUpdateTask!;
		}

		async Task UpdateTreesAsync()
		{
			List<(StreamId, Task)> BackgroundTasks = new List<(StreamId, Task)>(MaxBackgroundTasks);
			while (!RedisUpdateStreams!.Reader.Completion.IsCompleted || BackgroundTasks.Count > 0)
			{
				try
				{
					await UpdateStreamsInternalAsync(BackgroundTasks);
				}
				catch (Exception Ex)
				{
					Logger.LogError(Ex, "Exception when updating commit service");
				}
			}
		}

		async Task UpdateStreamsInternalAsync(List<(StreamId, Task)> BackgroundTasks)
		{
			// Remove any complete background tasks
			for (int Idx = BackgroundTasks.Count - 1; Idx >= 0; Idx--)
			{
				(_, Task BackgroundTask) = BackgroundTasks[Idx];
				if (BackgroundTask.IsCompleted)
				{
					await BackgroundTask;
					BackgroundTasks.RemoveAt(Idx);
				}
			}

			// Create a list of events to wait for
			List<Task> WaitTasks = new List<Task>(BackgroundTasks.Select(x => x.Item2));
			if (!RedisUpdateStreams!.Reader.Completion.IsCompleted)
			{
				WaitTasks.Add(RedisUpdateStreams.Reader.WaitToReadAsync().AsTask());

				// If we have spare slots for executing background tasks, check if there are any dirty streams
				if (BackgroundTasks.Count < MaxBackgroundTasks)
				{
					// Expire any reservations that are no longer valid
					DateTime UtcNow = DateTime.UtcNow;
					await RedisReservations.RemoveRangeByScoreAsync(Double.NegativeInfinity, UtcNow.Ticks);

					// Find the streams that we can wait for
					HashSet<StreamId> CheckStreams = new HashSet<StreamId>(await RedisDirtyStreams.MembersAsync());
					CheckStreams.ExceptWith(BackgroundTasks.Select(x => x.Item1));

					if (CheckStreams.Count > 0)
					{
						// Try to start new background tasks
						double NewScore = (UtcNow + TimeSpan.FromSeconds(30.0)).Ticks;
						foreach (StreamId CheckStream in CheckStreams)
						{
							if (await RedisReservations.AddAsync(CheckStream, NewScore, When.NotExists))
							{
								Task NewTask = Task.Run(() => UpdateStreamAsync(CheckStream));
								BackgroundTasks.Add((CheckStream, NewTask));

								if (BackgroundTasks.Count == MaxBackgroundTasks)
								{
									break;
								}
							}
						}
						CheckStreams.ExceptWith(BackgroundTasks.Select(x => x.Item1));

						// If we still have spare tasks, check how long we should wait for a reservation to expire
						if (BackgroundTasks.Count < MaxBackgroundTasks)
						{
							long WaitTime = long.MaxValue;
							await foreach (SortedSetEntry<StreamId> Entry in RedisReservations.ScanAsync())
							{
								if (CheckStreams.Contains(Entry.Element))
								{
									WaitTime = Math.Min(WaitTime, (long)Entry.Score - UtcNow.Ticks);
								}
							}
							if (WaitTime < int.MaxValue)
							{
								WaitTasks.Add(Task.Delay(new TimeSpan(Math.Max(WaitTime, 0))));
							}
						}
					}
				}
			}

			// Wait until any task has completed
			if (WaitTasks.Count > 0)
			{
				await Task.WhenAny(WaitTasks);
			}
		}

		async Task UpdateStreamAsync(StreamId StreamId)
		{
			Stopwatch Timer = Stopwatch.StartNew();

			RedisList<int> StreamChanges = RedisStreamChanges(StreamId);
			for (; ; )
			{
				// Update the stream, updating the reservation every 30 seconds
				Task InternalTask = Task.Run(() => UpdateStreamInternalAsync(StreamId, StreamChanges));
				while (!InternalTask.IsCompleted)
				{
					Task DelayTask = Task.Delay(TimeSpan.FromSeconds(15.0));
					if (await Task.WhenAny(InternalTask, DelayTask) == DelayTask)
					{
						DateTime NewTime = DateTime.UtcNow + TimeSpan.FromSeconds(30.0);
						await RedisReservations.AddAsync(StreamId, NewTime.Ticks);
						Logger.LogInformation("Extending reservation for tree update of {StreamId} (elapsed: {Time}s)", StreamId, (int)Timer.Elapsed.TotalSeconds);
					}
				}
				await InternalTask;

				// Remove this stream from the dirty list if it's empty
				ITransaction Transaction = Redis.CreateTransaction();
				Transaction.AddCondition(Condition.ListLengthLessThan(StreamChanges.Key, 2));
				_ = Transaction.With(RedisDirtyStreams).RemoveAsync(StreamId);
				if (await Transaction.ExecuteAsync())
				{
					break;
				}
			}
			await RedisReservations.RemoveAsync(StreamId);
		}

		async Task UpdateStreamInternalAsync(StreamId StreamId, RedisList<int> Changes)
		{
			IStream? Stream = await StreamCollection.GetAsync(StreamId);
			if (Stream == null)
			{
				return;
			}

			NativePerforceConnection? Perforce = await PerforceService.GetServiceUserConnection(Stream.ClusterName);
			if (Perforce == null)
			{
				return;
			}

			InfoRecord ServerInfo = await Perforce.GetInfoAsync(InfoOptions.ShortOutput);

			ViewMap View = await GetStreamViewAsync(Perforce, "//UE5/Dev-Main-Minimal");// Stream.Name);

			ICommit? Commit = null;
			StreamTreeRef? TreeRef = null;
			for (; ; )
			{
				// Get the first two changelists to update
				int[] Values = await Changes.RangeAsync(0, 1);
				if (Values.Length < 2)
				{
					break;
				}

				// Get the previous tree if we don't have it from the previous iteration
				if (Commit == null || Commit.Change != Values[0])
				{
					Commit = await CommitCollection.GetCommitAsync(Stream.Id, Values[0]);
					if (Commit == null)
					{
						TreeRef = null;
					}
					else if (Commit.TreeRef == null)
					{
						TreeRef = await AddTreeRefAsync(Perforce, Stream, View, null, null, Commit, ServerInfo.PathComparison);
					}
					else
					{
						TreeRef = await GetTreeRefAsync(Commit);
					}
				}

				// Copy the previous commit and tree ref
				ICommit? PrevCommit = Commit;
				StreamTreeRef? PrevTreeRef = TreeRef;

				// Move to the next commit
				Commit = await CommitCollection.GetCommitAsync(StreamId, Values[1]);
				if (Commit == null)
				{
					TreeRef = null;
				}
				else
				{
					TreeRef = await AddTreeRefAsync(Perforce, Stream, View, PrevCommit, PrevTreeRef, Commit, ServerInfo.PathComparison);
				}

				// Remove the first change number from this queue
				await Changes.LeftPopAsync();
			}
		}

		async Task<StreamTreeRef?> GetTreeRefAsync(ICommit Commit)
		{
			if (Commit.TreeRef == null)
			{
				return null;
			}

			IRef? Ref = await RefCollection.GetAsync(NamespaceId, BucketId, Commit.TreeRef);
			if (Ref == null)
			{
				return null;
			}

			return CbSerializer.Deserialize<StreamTreeRef>(Ref.Value.AsField());
		}

		async Task<StreamTreeRef?> AddTreeRefAsync(IPerforceConnection Perforce, IStream Stream, ViewMap View, ICommit? PrevCommit, StreamTreeRef? PrevTreeRef, ICommit Commit, StringComparison PathComparison)
		{
			StreamTreeRef TreeRef;
			if (PrevCommit == null || PrevTreeRef == null)
			{
				Logger.LogInformation("Performing snapshot of {StreamId} at CL {Change}", Commit.StreamId, Commit.Change);
				TreeRef = await CreateTreeAsync(Perforce, View, Commit.Change, PathComparison);
			}
			else
			{
				Logger.LogInformation("Performing incremental update of {StreamId} from CL {PrevChange} to CL {Change}", Commit.StreamId, PrevCommit.Change, Commit.Change);
				TreeRef = await UpdateTreeAsync(PrevTreeRef, Perforce, View, Commit.Change, PathComparison);
			}

			string RefName = $"tree_{Commit.StreamId}_{Commit.Change}";

			CbWriter Writer = new CbWriter();
			Writer.BeginObject();
			TreeRef.Write(Writer, Stream.Name);
			Writer.EndObject();

			List<IoHash> MissingHashes = await RefCollection.SetAsync(NamespaceId, BucketId, RefName, Writer.ToObject());
			if (MissingHashes.Count > 0)
			{
				Logger.LogWarning("Missing hashes when attempting to add ref: {MissingHashes}", String.Join(", ", MissingHashes.Select(x => x.ToString())));
				return null;
			}

			NewCommit NewCommit = new NewCommit(Commit);
			NewCommit.TreeRef = RefName;
			await CommitCollection.AddOrReplaceAsync(NewCommit);

			return TreeRef;
		}

		#endregion

		#region Snapshot generation

		/// <summary>
		/// Number of files to query at a time before recursing down the tree. Tuning this value allows finding a threshold between request/response lengths and memory usage.
		/// </summary>
		const int BatchSize = 256 * 1024;

		/// <summary>
		/// Creates a snapshot of a stream at a particular changelist
		/// </summary>
		/// <param name="Connection">The Perforce repository to mirror</param>
		/// <param name="View">View for the stream</param>
		/// <param name="Change">Changelist to replicate</param>
		/// <param name="Comparison">Comparison type for paths</param>
		async Task<StreamTreeRef> CreateTreeAsync(IPerforceConnection Connection, ViewMap View, int Change, StringComparison Comparison)
		{
			// In order to optimize for the common case where we're adding multiple files to the same directory, we keep an open list of
			// StreamTreeBuilder objects down to the last modified node. Whenever we need to move to a different node, we write the 
			// previous path to the object store.
			List<StreamTreeBuilder> TreePath = new List<StreamTreeBuilder>();
			TreePath.Add(new StreamTreeBuilder());

			// Generate a snapshot using all the root paths
			List<string> RootPaths = View.GetRootPaths(Comparison);
			foreach (string RootPath in RootPaths)
			{
				ViewMap FilteredView = new ViewMap();
				foreach (ViewMapEntry Entry in View.Entries)
				{
					if (Entry.SourcePrefix.StartsWith(RootPath, Comparison))
					{
						FilteredView.Entries.Add(Entry);
					}
				}
				await AddDepotDirToSnapshotAsync(Connection, RootPath, Change, FilteredView, Comparison, TreePath);
			}

			// Encode the tree
			return await EncodeTreeAsync(TreePath[0]);
		}

		/// <summary>
		/// Adds all files under a directory in the depot to a snapshot
		/// </summary>
		/// <param name="Connection">The perforce connection</param>
		/// <param name="DepotDir">Depot directory to recurse through, with a trailing slash</param>
		/// <param name="Change">Changelist number to capture</param>
		/// <param name="View">View for the stream</param>
		/// <param name="Comparison">Comparison type to use for names</param>
		/// <param name="TreePath">The last computed path through the tree</param>
		async Task AddDepotDirToSnapshotAsync(IPerforceConnection Connection, string DepotDir, int Change, ViewMap View, StringComparison Comparison, List<StreamTreeBuilder> TreePath)
		{
			const string Filter = "^headAction=delete ^headAction=move/delete";
			const string Fields = "depotFile,fileSize,digest,headType,headRev";

			if (View.MayMatchAnyFilesInSubDirectory(DepotDir, Comparison))
			{
				List<FStatRecord> Files = await Connection.FStatAsync(-1, -1, Filter, Fields, BatchSize, FStatOptions.IncludeFileSizes, $"{DepotDir}...@{Change}");
				if (Files.Count < BatchSize)
				{
					Logger.LogInformation("Added {DepotDir}... to snapshot", DepotDir);
					await AddDepotFilesToSnapshotAsync(Files, View, Comparison, TreePath);
					return;
				}

				Files.Clear(); // Don't keep around in memory
				Logger.LogInformation("Querying subdirectories of {DepotDir} (wasted results)", DepotDir);

				IList<DirsRecord> Dirs = await Connection.GetDirsAsync(DirsOptions.None, null, $"{DepotDir}*@{Change}");
				foreach (DirsRecord Dir in Dirs)
				{
					await AddDepotDirToSnapshotAsync(Connection, Dir.Dir + "/", Change, View, Comparison, TreePath);
				}
			}

			if (View.MayMatchAnyFilesInDirectory(DepotDir, Comparison))
			{
				List<FStatRecord> Files = await Connection.FStatAsync(-1, -1, Filter, Fields, -1, FStatOptions.IncludeFileSizes, $"{DepotDir}*@{Change}");
				await AddDepotFilesToSnapshotAsync(Files, View, Comparison, TreePath);
			}
		}

		/// <summary>
		/// Adds a set of files in the depot to a snapshot
		/// </summary>
		/// <param name="Files">The files to add</param>
		/// <param name="View">View for the stream</param>
		/// <param name="Comparison">Comparison type to use for names</param>
		/// <param name="TreePath">The last computed path through the tree</param>
		async Task AddDepotFilesToSnapshotAsync(List<FStatRecord> Files, ViewMap View, StringComparison Comparison, List<StreamTreeBuilder> TreePath)
		{
			foreach (FStatRecord File in Files)
			{
				if (!String.IsNullOrEmpty(File.Digest))
				{
					string? TargetFile;
					if (File.DepotFile != null && View.TryMapFile(File.DepotFile, Comparison, out TargetFile))
					{
						await AddFileAsync(TargetFile, File, TreePath);
					}
				}
			}
		}

		/// <summary>
		/// Adds a single depot file to a snapshot
		/// </summary>
		/// <param name="Path">The stream path for the file</param>
		/// <param name="MetaData">The file metadata</param>
		/// <param name="TreePath">The last computed path through the tree</param>
		async Task AddFileAsync(string Path, FStatRecord MetaData, List<StreamTreeBuilder> TreePath)
		{
			string[] Segments = Path.Split('/');

			StreamTreeBuilder Node = TreePath[0];
			for (int Idx = 0; Idx < Segments.Length - 1; Idx++)
			{
				Utf8String Segment = Segments[Idx];

				StreamTreeBuilder NextNode = await FindOrAddTreeAsync(Node, Segment);
				if (Idx + 1 < TreePath.Count && TreePath[Idx + 1] != NextNode)
				{
					await CollapseTreeAsync(Node, TreePath[Idx + 1]);
				}
				if (Idx + 1 >= TreePath.Count)
				{
					TreePath.Add(NextNode);
				}

				Node = NextNode;
			}

			Node.NameToFile[Segments[Segments.Length - 1]] = CreateStreamFile(MetaData);
		}

		#endregion

		#region Incremental snapshot generation

		/// <summary>
		/// Creates a snapshot for the given stream, given an initial state and list of modified files
		/// </summary>
		/// <param name="PrevTreeRef"></param>
		/// <param name="Perforce"></param>
		/// <param name="Change"></param>
		/// <param name="View"></param>
		/// <param name="Comparison"></param>
		/// <returns></returns>
		async Task<StreamTreeRef> UpdateTreeAsync(StreamTreeRef PrevTreeRef, IPerforceConnection Perforce, ViewMap View, int Change, StringComparison Comparison)
		{
			List<FStatRecord> Records = await Perforce.FStatAsync(-1, Change, null, null, -1, FStatOptions.IncludeFileSizes, FileSpecList.Empty);
			return await UpdateTreeAsync(PrevTreeRef, Records, View, Comparison);
		}

		/// <summary>
		/// Creates a snapshot for the given stream, given an initial state and list of modified files
		/// </summary>
		/// <param name="PrevTreeRef"></param>
		/// <param name="Files"></param>
		/// <param name="View"></param>
		/// <param name="Comparison"></param>
		/// <returns></returns>
		async Task<StreamTreeRef> UpdateTreeAsync(StreamTreeRef PrevTreeRef, IList<FStatRecord> Files, ViewMap View, StringComparison Comparison)
		{
			StreamTreeBuilder Tree = await ReadTreeAsync(PrevTreeRef);

			// Update the tree
			foreach (FStatRecord File in Files)
			{
				string? LocalPathStr;
				if (File.DepotFile != null && View.TryMapFile(File.DepotFile, Comparison, out LocalPathStr))
				{
					Utf8String LocalPath = LocalPathStr;

					StreamTreeBuilder Node = Tree;
					for (; ; )
					{
						int NextIdx = LocalPath.IndexOf('/');
						if (NextIdx == -1)
						{
							break;
						}

						Utf8String Name = LocalPath[0..NextIdx];
						Node = await FindOrAddTreeAsync(Node, Name);

						LocalPath = LocalPath[(NextIdx + 1)..];
					}

					Utf8String FileName = LocalPath;
					if (File.Action != FileAction.Delete && File.Action != FileAction.MoveDelete)
					{
						Node.NameToFile[FileName] = CreateStreamFile(File);
					}
					else
					{
						if (!Node.NameToFile.Remove(FileName))
						{
							// TODO: Handle mismatched case?
							throw new NotImplementedException();
						}
					}
				}
			}

			return await EncodeTreeAsync(Tree);
		}

#endregion

		#region Misc tree manipulation

		/// <summary>
		/// Gets the view for a stream
		/// </summary>
		/// <param name="Connection">The Perforce connection</param>
		/// <param name="StreamName">Name of the stream to be mirrored</param>
		static async Task<ViewMap> GetStreamViewAsync(IPerforceConnection Connection, string StreamName)
		{
			StreamRecord Record = await Connection.GetStreamAsync(StreamName, true);

			ViewMap View = new ViewMap();
			foreach (string ViewLine in Record.View)
			{
				Match Match = Regex.Match(ViewLine, "^(-?)([^ ]+) *([^ ]+)$");
				if (!Match.Success)
				{
					throw new PerforceException($"Unable to parse stream view: '{ViewLine}'");
				}
				View.Entries.Add(new ViewMapEntry(Match.Groups[1].Length == 0, Match.Groups[2].Value, Match.Groups[3].Value));
			}

			return View;
		}

		/// <summary>
		/// Gets a <see cref="StreamTreeBuilder"/> for a subtree with the given name
		/// </summary>
		/// <param name="Tree"></param>
		/// <param name="Name"></param>
		/// <returns></returns>
		async Task<StreamTreeBuilder> FindOrAddTreeAsync(StreamTreeBuilder Tree, Utf8String Name)
		{
			StreamTreeBuilder? Result;
			if (Tree.NameToTreeBuilder.TryGetValue(Name, out Result))
			{
				return Result;
			}

			StreamTreeRef? TreeRef;
			if (Tree.NameToTree.TryGetValue(Name, out TreeRef))
			{
				Result = await ReadTreeAsync(TreeRef);

				Tree.NameToTree.Remove(Name);
				Tree.NameToTreeBuilder.Add(Name, Result);

				return Result;
			}

			Result = new StreamTreeBuilder();
			Tree.NameToTreeBuilder[Name] = Result;

			return Result;
		}

		/// <summary>
		/// Reads a tree builder for the given tree reference
		/// </summary>
		/// <param name="TreeRef"></param>
		/// <returns></returns>
		async Task<StreamTreeBuilder> ReadTreeAsync(StreamTreeRef TreeRef)
		{
			CbObject? Object = await ObjectCollection.GetAsync(NamespaceId, TreeRef.Hash);
			if (Object == null)
			{
				throw new Exception($"Missing object {TreeRef.Hash} referenced by tree for {TreeRef.Path}");
			}
			return new StreamTreeBuilder(new StreamTree(Object, TreeRef.Path + "/"));
		}

		/// <summary>
		/// Collapse a subtree under a given child
		/// </summary>
		/// <param name="Parent"></param>
		/// <param name="Child"></param>
		/// <returns></returns>
		async Task CollapseTreeAsync(StreamTreeBuilder Parent, StreamTreeBuilder Child)
		{
			foreach ((Utf8String Name, StreamTreeBuilder Node) in Parent.NameToTreeBuilder)
			{
				if (Node == Child)
				{
					StreamTreeRef ChildRef = await EncodeTreeAsync(Child);
					Parent.NameToTreeBuilder.Remove(Name);
					Parent.NameToTree.Add(Name, ChildRef);
					break;
				}
			}
		}

		/// <summary>
		/// Writes a subtree to the object store
		/// </summary>
		/// <param name="TreeBuilder">Root of the tree to write</param>
		/// <returns>New tree reference for the serialized tree</returns>
		async Task<StreamTreeRef> EncodeTreeAsync(StreamTreeBuilder TreeBuilder)
		{
			Dictionary<IoHash, CbObject> Objects = new Dictionary<IoHash, CbObject>();
			StreamTreeRef Ref = TreeBuilder.Encode(Objects);

			foreach ((IoHash Hash, CbObject Object) in Objects)
			{
				await ObjectCollection.AddAsync(NamespaceId, Hash, Object);
			}

			return Ref;
		}

		/// <summary>
		/// Creates a StreamFile from a FileMetaData object
		/// </summary>
		/// <param name="MetaData"></param>
		/// <returns></returns>
		static StreamFile CreateStreamFile(FStatRecord MetaData)
		{
			string FileType = MetaData.Type ?? MetaData.HeadType ?? String.Empty;
			return new StreamFile(MetaData.DepotFile!, MetaData.FileSize, new FileContentId(Md5Hash.Parse(MetaData.Digest!), FileType.ToString()), MetaData.HeadRevision);
		}

		/// <summary>
		/// Print the contents of a snapshot
		/// </summary>
		/// <param name="TreeRef"></param>
		/// <returns></returns>
		Task PrintSnapshotAsync(StreamTreeRef TreeRef)
		{
			return PrintSnapshotInternalAsync(TreeRef, "/");
		}

		/// <summary>
		/// Print the contents of a snapshot
		/// </summary>
		/// <param name="TreeRef"></param>
		/// <param name="Prefix"></param>
		/// <returns></returns>
		async Task PrintSnapshotInternalAsync(StreamTreeRef TreeRef, string Prefix)
		{
			StreamTree Tree = new StreamTree(await ObjectCollection.GetRequiredObjectAsync(NamespaceId, TreeRef.Hash), TreeRef.Path);

			foreach ((Utf8String Name, StreamFile File) in Tree.NameToFile)
			{
				Logger.LogInformation($"{Prefix}/{Name} = {File.Path}#{File.Revision}");
			}
			foreach ((Utf8String Name, StreamTreeRef ChildTreeRef) in Tree.NameToTree)
			{
				await PrintSnapshotInternalAsync(ChildTreeRef, $"{Prefix}/{Name}");
			}
		}

		#endregion
	}
}
