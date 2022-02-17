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
using HordeServer.Services;
using EpicGames.Serialization;
using MongoDB.Bson.Serialization.Attributes;
using System.Collections.Concurrent;
using System.Globalization;
using Microsoft.Extensions.Hosting;
using EpicGames.Perforce.Managed;
using System.Threading.Channels;
using StackExchange.Redis;
using EpicGames.Perforce;
using EpicGames.Redis;
using System.Diagnostics;
using System.IO.Compression;
using System.IO;
using HordeCommon;
using EpicGames.Horde.Storage;
using System.Text;
using Microsoft.Extensions.Options;
using Horde.Build.Utilities;

namespace HordeServer.Commits.Impl
{
	using P4 = Perforce.P4;
	using CommitId = ObjectId<ICommit>;
	using StreamId = StringId<IStream>;
	using IRef = EpicGames.Horde.Storage.IRef;

	/// <summary>
	/// Options for the commit service
	/// </summary>
	public class CommitServiceOptions
	{
		/// <summary>
		/// Whether to mirror commit metadata to the database
		/// </summary>
		public bool Metadata { get; set; } = true;

		/// <summary>
		/// Specific list of streams to enable commit mirroring for
		/// </summary>
		public List<StreamId> MetadataStreams { get; set; } = new List<StreamId>();

		/// <summary>
		/// Whether to mirror content to Horde storage
		/// </summary>
		public bool Content { get; set; }

		/// <summary>
		/// List of streams for which content mirroring should be enabled
		/// </summary>
		public List<StreamId> ContentStreams { get; set; } = new List<StreamId>();

		/// <summary>
		/// Write Perforce stub files rather than including the actual content.
		/// </summary>
		public bool WriteStubFiles { get; set; } = true;

		/// <summary>
		/// Namespace to store content replicated from Perforce
		/// </summary>
		public NamespaceId NamespaceId { get; set; } = new NamespaceId("horde.p4");

		/// <summary>
		/// Options for how objects are packed together
		/// </summary>
		public TreePackOptions Packing { get; set; } = new TreePackOptions();
	}

	/// <summary>
	/// Information about a commit tree
	/// </summary>
	class CommitTree
	{
		/// <summary>
		/// The stream id
		/// </summary>
		public StreamId StreamId { get; }

		/// <summary>
		/// The changelist number
		/// </summary>
		public int Change { get; }

		/// <summary>
		/// Root object describing the tree contents
		/// </summary>
		public TreePackObject RootObject { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public CommitTree(StreamId StreamId, int Change, TreePackObject RootObject)
		{
			this.StreamId = StreamId;
			this.Change = Change;
			this.RootObject = RootObject;
		}

		/// <summary>
		/// The ref id for storing this tree
		/// </summary>
		public RefId RefId => GetRefId(Change);

		/// <summary>
		/// The ref id for storing this tree
		/// </summary>
		public static RefId GetRefId(int Change) => new RefId(IoHash.Compute(Encoding.UTF8.GetBytes($"{Change}")));

		/// <summary>
		/// Gets the directory at the root of this tree
		/// </summary>
		public TreePackDirNode RootDirNode => TreePackDirNode.Parse(RootObject.GetRootNode());
	}

	/// <summary>
	/// Service which mirrors changes from Perforce
	/// </summary>
	class CommitService : ICommitService, IHostedService, IDisposable
	{
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

		/// <summary>
		/// A registered listener for new commits
		/// </summary>
		class ListenerInfo : IDisposable
		{
			List<ListenerInfo> Listeners;
			public Action<ICommit> Callback;

			public ListenerInfo(List<ListenerInfo> Listeners, Action<ICommit> Callback)
			{
				this.Listeners = Listeners;
				this.Callback = Callback;

				lock (Listeners)
				{
					Listeners.Add(this);
				}
			}

			public void Dispose()
			{
				lock (Listeners)
				{
					Listeners.Remove(this);
				}
			}
		}

		public CommitServiceOptions Options { get; }

		// Redis
		IDatabase Redis;

		// Collections
		ICommitCollection CommitCollection;
		IStorageClient StorageClient;
		IStreamCollection StreamCollection;
		IPerforceService PerforceService;
		IUserCollection UserCollection;
		ILogger<CommitService> Logger;

		const int MaxBackgroundTasks = 2;

		/// <summary>
		/// Constructor
		/// </summary>
		public CommitService(IDatabase Redis, ICommitCollection CommitCollection, IStorageClient StorageClient, IStreamCollection StreamCollection, IPerforceService PerforceService, IUserCollection UserCollection, IClock Clock, IOptions<CommitServiceOptions> Options, ILogger<CommitService> Logger)
		{
			this.Options = Options.Value;

			this.Redis = Redis;
			this.RedisDirtyStreams = new RedisSet<StreamId>(Redis, RedisBaseKey.Append("streams"));
			this.RedisReservations = new RedisSortedSet<StreamId>(Redis, RedisBaseKey.Append("reservations"));

			this.CommitCollection = CommitCollection;
			this.StorageClient = StorageClient;
			this.StreamCollection = StreamCollection;
			this.PerforceService = PerforceService;
			this.UserCollection = UserCollection;
			this.Logger = Logger;
			this.UpdateMetadataTicker = Clock.AddSharedTicker<CommitService>(TimeSpan.FromSeconds(30.0), UpdateMetadataAsync, Logger);
		}

		/// <inheritdoc/>
		public void Dispose() => UpdateMetadataTicker.Dispose();

		/// <inheritdoc/>
		public async Task StartAsync(CancellationToken CancellationToken)
		{
			await StartNotificationsAsync();
			if (Options.Metadata)
			{
				await StartMetadataReplicationAsync();
			}
			if (Options.Content)
			{
				await StartContentReplicationAsync();
			}
		}

		/// <inheritdoc/>
		public async Task StopAsync(CancellationToken CancellationToken)
		{
			if (Options.Content)
			{
				await StopContentReplicationAsync();
			}
			if (Options.Metadata)
			{
				await StopMetadataReplicationAsync();
			}
			await StopNotificationsAsync();
		}

		#region Notifications

		List<ListenerInfo> Listeners = new List<ListenerInfo>();
		RedisChannel<CommitId> CommitNotifyChannel { get; } = new RedisChannel<CommitId>("commits/notify");
		RedisChannelSubscription<CommitId>? CommitNotifySubscription;

		async Task StartNotificationsAsync()
		{
			ISubscriber Subscriber = Redis.Multiplexer.GetSubscriber();
			CommitNotifySubscription = await Subscriber.SubscribeAsync(CommitNotifyChannel, DispatchCommitNotification);
		}

		async Task StopNotificationsAsync()
		{
			if (CommitNotifySubscription != null)
			{
				await CommitNotifySubscription.DisposeAsync();
				CommitNotifySubscription = null;
			}
		}

		async void DispatchCommitNotification(RedisChannel<CommitId> Channel, CommitId CommitId)
		{
			ICommit? Commit = await CommitCollection.GetCommitAsync(CommitId);
			if (Commit != null)
			{
				lock (Listeners)
				{
					foreach (ListenerInfo Listener in Listeners)
					{
						Listener.Callback(Commit);
					}
				}
			}
		}

		async Task NotifyListeners(CommitId CommitId)
		{
			await Redis.PublishAsync(CommitNotifyChannel, CommitId);
		}

		public IDisposable AddListener(Action<ICommit> OnAddCommit)
		{
			return new ListenerInfo(Listeners, OnAddCommit);
		}

		#endregion

		#region Metadata updates

		ITicker UpdateMetadataTicker;

		Task StartMetadataReplicationAsync()
		{
			return UpdateMetadataTicker.StartAsync();
		}

		Task StopMetadataReplicationAsync()
		{
			return UpdateMetadataTicker.StopAsync();
		}

		/// <summary>
		/// Polls Perforce for submitted changes
		/// </summary>
		/// <param name="CancellationToken"></param>
		/// <returns></returns>
		async ValueTask UpdateMetadataAsync(CancellationToken CancellationToken)
		{
			List<IStream> Streams = await StreamCollection.FindAllAsync();
			if (Options.MetadataStreams.Count > 0)
			{
				Streams.RemoveAll(x => !Options.MetadataStreams.Contains(x.Id));
			}

			foreach (IGrouping<string, IStream> Group in Streams.GroupBy(x => x.ClusterName, StringComparer.OrdinalIgnoreCase))
			{
				try
				{
					await UpdateMetadataForClusterAsync(Group.Key, Group);
				}
				catch (Exception Ex)
				{
					Logger.LogError(Ex, "Error while updating cluster {ClusterName}", Group.Key);
				}
			}
		}

		/// <summary>
		/// Updates metadata for all the streams on a particular server cluster
		/// </summary>
		/// <param name="ClusterName"></param>
		/// <param name="Streams"></param>
		/// <returns></returns>
		async Task UpdateMetadataForClusterAsync(string ClusterName, IEnumerable<IStream> Streams)
		{
			// Find the minimum changelist number to query
			Dictionary<IStream, int> StreamToFirstChange = new Dictionary<IStream, int>();
			foreach (IStream Stream in Streams)
			{
				RedisList<int> Changes = RedisStreamChanges(Stream.Id);

				int FirstChange = await Changes.GetByIndexAsync(-1);
				if (FirstChange != 0)
				{
					FirstChange++;
				}

				StreamToFirstChange[Stream] = FirstChange;
			}

			// Update the database with all the commits
			await foreach (NewCommit NewCommit in FindCommitsForClusterAsync(ClusterName, StreamToFirstChange))
			{
				ICommit Commit = await CommitCollection.AddOrReplaceAsync(NewCommit);
				await NotifyListeners(Commit.Id);

				RedisList<int> StreamCommitsKey = RedisStreamChanges(Commit.StreamId);
				await StreamCommitsKey.RightPushAsync(Commit.Change);

				await RedisDirtyStreams.AddAsync(Commit.StreamId);
				await Redis.PublishAsync(RedisUpdateChannel, Commit.StreamId);
			}
		}

		/// <summary>
		/// Enumerate new commits to the given streams, using a stream view to deduplicate changes which affect multiple branches.
		/// </summary>
		/// <param name="ClusterName"></param>
		/// <param name="StreamToFirstChange"></param>
		/// <returns>List of new commits</returns>
		public async IAsyncEnumerable<NewCommit> FindCommitsForClusterAsync(string ClusterName, Dictionary<IStream, int> StreamToFirstChange)
		{
			// Create a connection to the server
			using IPerforceConnection? Connection = await PerforceService.GetServiceUserConnection(ClusterName);
			if (Connection == null)
			{
				throw new PerforceException($"Unable to create cluster connection for {ClusterName}");
			}

			// Figure out the case settings for the server
			InfoRecord ServerInfo = await Connection.GetInfoAsync(InfoOptions.ShortOutput);

			// Get the view for each stream
			List<StreamInfo> StreamInfoList = new List<StreamInfo>();
			foreach (IStream Stream in StreamToFirstChange.Keys)
			{
				ViewMap View = await GetStreamViewAsync(Connection, Stream.Name);
				StreamInfoList.Add(new StreamInfo(Stream, View));
			}

			// Find all the depot roots
			Dictionary<Utf8String, Utf8String> DepotRoots = new Dictionary<Utf8String, Utf8String>(ServerInfo.Utf8PathComparer);
			foreach (ViewMap View in StreamInfoList.Select(x => x.View))
			{
				foreach (ViewMapEntry Entry in View.Entries)
				{
					if (Entry.SourcePrefix.Length >= 3)
					{
						int SlashIdx = Entry.SourcePrefix.IndexOf('/', 2);
						Utf8String Depot = Entry.SourcePrefix.Slice(0, SlashIdx + 1);

						Utf8String DepotRoot;
						if (DepotRoots.TryGetValue(Depot, out DepotRoot))
						{
							DepotRoot = GetCommonPrefix(DepotRoot, Entry.SourcePrefix);
						}
						else
						{
							DepotRoot = Entry.SourcePrefix;
						}

						int LastSlashIdx = DepotRoot.LastIndexOf('/');
						DepotRoots[Depot] = DepotRoot.Slice(0, LastSlashIdx + 1);
					}
				}
			}

			// Find the minimum changelist number to query
			int MinChange = int.MaxValue;
			foreach (StreamInfo StreamInfo in StreamInfoList)
			{
				int FirstChange = StreamToFirstChange[StreamInfo.Stream];
				if (FirstChange == 0)
				{
					FirstChange = await GetFirstCommitToReplicateAsync(Connection, StreamInfo.View, ServerInfo.Utf8PathComparer);
				}
				if (FirstChange != 0)
				{
					MinChange = Math.Min(MinChange, FirstChange);
				}
			}

			// Find all the changes to consider
			SortedSet<int> ChangeNumbers = new SortedSet<int>();
			foreach (Utf8String DepotRoot in DepotRoots.Values)
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

					Utf8String BasePath = GetBasePath(DescribeRecord, StreamInfo.View, ServerInfo.Utf8PathComparer);
					if (!BasePath.IsEmpty)
					{
						IUser Author = await UserCollection.FindOrAddUserByLoginAsync(DescribeRecord.User);
						IUser Owner = (await ParseRobomergeOwnerAsync(DescribeRecord.Description)) ?? Author;

						int OriginalChange = ParseRobomergeSource(DescribeRecord.Description) ?? DescribeRecord.Number;

						yield return new NewCommit(Stream.Id, DescribeRecord.Number, OriginalChange, Author.Id, Owner.Id, DescribeRecord.Description, BasePath.ToString(), DescribeRecord.Time);
					}
				}
			}
		}

		/// <summary>
		/// Find the first commit to replicate from a branch
		/// </summary>
		/// <param name="Connection"></param>
		/// <param name="View"></param>
		/// <param name="Comparer"></param>
		/// <returns></returns>
		static async Task<int> GetFirstCommitToReplicateAsync(IPerforceConnection Connection, ViewMap View, Utf8StringComparer Comparer)
		{
			int MinChange = 0;

			List<Utf8String> RootPaths = View.GetRootPaths(Comparer);
			foreach (Utf8String RootPath in RootPaths)
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
		/// <param name="Comparer">Path comparison type for the server</param>
		/// <returns>The base path for all files in the change</returns>
		static Utf8String GetBasePath(DescribeRecord Changelist, ViewMap View, Utf8StringComparer Comparer)
		{
			Utf8String BasePath = default;
			foreach (DescribeFileRecord File in Changelist.Files)
			{
				Utf8String StreamFile;
				if (View.TryMapFile(File.DepotFile, Comparer, out StreamFile))
				{
					if (BasePath.IsEmpty)
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
		static Utf8String GetCommonPrefix(Utf8String A, Utf8String B)
		{
			int Index = 0;
			while (Index < A.Length && Index < B.Length && A[Index] == B[Index])
			{
				Index++;
			}
			return A.Substring(0, Index);
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

		#endregion

		#region Content Updates

		RedisKey RedisBaseKey { get; } = new RedisKey("commits/");
		RedisChannel<StreamId> RedisUpdateChannel { get; } = new RedisChannel<StreamId>("commits/streams");
		readonly RedisSet<StreamId> RedisDirtyStreams;
		readonly RedisSortedSet<StreamId> RedisReservations;
		Channel<StreamId>? RedisUpdateStreams;
		RedisChannelSubscription<StreamId>? RedisUpdateSubscription;
		Task? StreamUpdateTask;

		RedisList<int> RedisStreamChanges(StreamId StreamId) => new RedisList<int>(Redis, RedisBaseKey.Append($"stream/{StreamId}/changes"));

		async Task StartContentReplicationAsync()
		{
			RedisUpdateStreams = Channel.CreateUnbounded<StreamId>();
			RedisUpdateSubscription = await Redis.Multiplexer.GetSubscriber().SubscribeAsync(RedisUpdateChannel, (_, StreamId) => RedisUpdateStreams.Writer.TryWrite(StreamId));
			StreamUpdateTask = Task.Run(() => UpdateContentAsync());
		}

		async Task StopContentReplicationAsync()
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

		async Task UpdateContentAsync()
		{
			List<(StreamId, Task)> BackgroundTasks = new List<(StreamId, Task)>(MaxBackgroundTasks);
			while (!RedisUpdateStreams!.Reader.Completion.IsCompleted || BackgroundTasks.Count > 0)
			{
				try
				{
					await UpdateContentInternalAsync(BackgroundTasks);
				}
				catch (Exception Ex)
				{
					Logger.LogError(Ex, "Exception when updating commit service");
				}
			}
		}

		async Task UpdateContentInternalAsync(List<(StreamId, Task)> BackgroundTasks)
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
								Task NewTask = Task.Run(() => UpdateStreamContentAsync(CheckStream));
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

		async Task UpdateStreamContentAsync(StreamId StreamId)
		{
			Stopwatch Timer = Stopwatch.StartNew();

			RedisList<int> StreamChanges = RedisStreamChanges(StreamId);
			for (; ; )
			{
				// Update the stream, updating the reservation every 30 seconds
				Task InternalTask = Task.Run(() => UpdateStreamContentInternalAsync(StreamId, StreamChanges));
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

		async Task UpdateStreamContentInternalAsync(StreamId StreamId, RedisList<int> Changes)
		{
			IStream? Stream = await StreamCollection.GetAsync(StreamId);
			if (Stream == null)
			{
				return;
			}

			ICommit? PrevCommit = null;
			for (; ; )
			{
				// Get the first two changes to be mirrored. The first one should already exist, unless it's the start of replication for this stream.
				int[] Values = await Changes.RangeAsync(0, 1);
				if (Values.Length < 2)
				{
					break;
				}
				if (PrevCommit != null && PrevCommit.Change != Values[0])
				{
					PrevCommit = null;
				}

				// Get the commit we want to mirror
				int Change = Values[(PrevCommit == null) ? 0 : 1];

				// Check if the tree already exists. If it does, we don't need to run replication again.
				ICommit? Commit = await CommitCollection.GetCommitAsync(StreamId, Change);
				if (Commit == null)
				{
					Logger.LogWarning("Missing commit for {Stream} change {Change}", StreamId, Change);
				}
				else if (Commit.TreeRefId != null)
				{
					Logger.LogInformation("Skipping replication for {Stream} change {Change}; tree already exists", StreamId, Change);
				}
				else
				{
					Commit = await UpdateCommitTreeAsync(Stream, Commit, PrevCommit);
				}

				// Remove the first change number from this queue
				await Changes.LeftPopAsync();
				PrevCommit = Commit;
			}
		}

		/// <summary>
		/// Gets the tree for a particular commit
		/// </summary>
		/// <param name="Commit">The commit instance</param>
		/// <returns>The corresponding commit tree</returns>
		public async Task<CommitTree?> GetTreeAsync(ICommit Commit)
		{
			if (Commit.TreeRefId == null)
			{
				return null;
			}

			IRef? Ref = await StorageClient.GetRefAsync(Options.NamespaceId, GetStreamBucketId(Commit.StreamId), Commit.TreeRefId.Value);
			if(Ref == null)
			{
				return null;
			}

			return new CommitTree(Commit.StreamId, Commit.Change, TreePackObject.Parse(Ref));
		}

		public async Task<CommitTree> ReadTreeAsync(StreamId StreamId, int Change)
		{
			RefId TreeRefId = new RefId(IoHash.Compute(Encoding.UTF8.GetBytes($"{Change}")));
			IRef Ref = await StorageClient.GetRefAsync(Options.NamespaceId, new BucketId(StreamId.ToString()), TreeRefId);
			return new CommitTree(StreamId, Change, TreePackObject.Parse(Ref));
		}

		public async Task WriteTreeAsync(StreamId StreamId, CommitTree Tree)
		{
			CbObject Object = Tree.RootObject.ToCbObject();
			await StorageClient.SetRefAsync(Options.NamespaceId, new BucketId(StreamId.ToString()), Tree.RefId, Object);
		}

		/// <summary>
		/// Replicates the contents of a stream to Horde storage, optionally using the given change as a starting point
		/// </summary>
		/// <param name="Stream">The stream to replicate</param>
		/// <param name="Commit">Commit to store the tree ref</param>
		/// <param name="BaseCommit">Previous commit to use as a base</param>
		/// <returns>New ref id for the given commit</returns>
		public async Task<ICommit> UpdateCommitTreeAsync(IStream Stream, ICommit Commit, ICommit? BaseCommit = null)
		{
			// Get the ref corresponding to BaseChange
			CommitTree? BaseTree = null;
			if (BaseCommit != null)
			{
				if (BaseCommit.TreeRefId == null)
				{
					Logger.LogWarning("Base commit for stream {StreamId} change {Change} does not have a mirrored tree", Stream.Id, BaseCommit.Change);
				}
				else
				{
					BaseTree = new CommitTree(BaseCommit.StreamId, BaseCommit.Change, TreePackObject.Parse(await StorageClient.GetRefAsync(Options.NamespaceId, GetStreamBucketId(Stream.Id), BaseCommit.TreeRefId.Value)));
				}
			}

			// Create the new root object
			CommitTree Tree = await FindCommitTreeAsync(Stream, Commit.Change, BaseTree);

			// Write the new ref
			string RefName = $"{Commit.Change}";
			RefId TreeRefId = new RefId(IoHash.Compute(Encoding.UTF8.GetBytes(RefName)));
			await StorageClient.SetRefAsync(Options.NamespaceId, new BucketId(Stream.Id.ToString()), TreeRefId, Tree.RootObject.ToCbObject());

			// Update the commit
			NewCommit NewCommit = new NewCommit(Commit);
			NewCommit.TreeRefId = TreeRefId;
			return await CommitCollection.AddOrReplaceAsync(NewCommit);
		}

		/// <summary>
		/// Replicates the contents of a stream to Horde storage, optionally using the given change as a starting point
		/// </summary>
		/// <param name="Stream">The stream to replicate</param>
		/// <param name="Change">Commit to store the tree ref</param>
		/// <param name="BaseTree">The base tree to update from</param>
		/// <returns>Root tree object</returns>
		public async Task<CommitTree> FindCommitTreeAsync(IStream Stream, int Change, CommitTree? BaseTree)
		{
			// Create a client to replicate from this stream, and connect to the server
			ReplicationClient ClientInfo = await FindOrAddReplicationClientAsync(Stream);
			using IPerforceConnection Perforce = await PerforceConnection.CreateAsync(ClientInfo.Settings, Logger);

			// Get the initial directory state
			TreePackDirWriter DirWriter;
			if (BaseTree == null)
			{
				await FlushWorkspaceAsync(ClientInfo, Perforce, 0);
				DirWriter = new TreePackDirWriter(ClientInfo.TreePack);
			}
			else
			{
				await FlushWorkspaceAsync(ClientInfo, Perforce, BaseTree.Change);
				DirWriter = new TreePackDirWriter(ClientInfo.TreePack, BaseTree.RootDirNode);
			}

			// Apply all the updates
			Logger.LogInformation("Updating client {Client} to changelist {Change}", ClientInfo.Client.Name, Change);
			ClientInfo.Change = -1;

			if (Options.WriteStubFiles)
			{
				await foreach (PerforceResponse<SyncRecord> Record in Perforce.StreamCommandAsync<SyncRecord>("sync", new[] { "-k", $"//...@{Change}" }, null, default))
				{
					SyncRecord SyncRecord = Record.Data;
					if (!SyncRecord.Path.StartsWith(ClientInfo.Client.Root, StringComparison.Ordinal))
					{
						throw new ArgumentException($"Unable to make path {ClientInfo.Client.Root} relative to client root {ClientInfo.Client.Root}");
					}

					string RelativePath = SyncRecord.Path.Substring(ClientInfo.Client.Root.Length).Replace('\\', '/').TrimStart('/');

					string DepotPath = $"{SyncRecord.DepotFile}#{SyncRecord.Revision}";
					byte[] Data = new byte[Encoding.UTF8.GetMaxByteCount(DepotPath.Length) + 1];
					Data[0] = (byte)TreePackNodeType.Binary;
					int Length = Encoding.UTF8.GetBytes(DepotPath, Data.AsSpan(1));
					ReadOnlyMemory<byte> EncodedData = Data.AsMemory(0, Length + 1);

					IoHash Hash = await ClientInfo.TreePack.AddNodeAsync(EncodedData);
					await DirWriter.FindOrAddFileByPathAsync(RelativePath, TreePackDirEntryFlags.File | TreePackDirEntryFlags.PerforceDepotPathAndRevision, Hash, Sha1Hash.Zero);
				}
			}
			else
			{
				Dictionary<int, (string Path, TreePackFileWriter Writer)> Files = new Dictionary<int, (string, TreePackFileWriter)>();
				await foreach (PerforceResponse Response in Perforce.StreamCommandAsync("sync", Array.Empty<string>(), new string[] { $"//...@{Change}" }, null, typeof(SyncRecord), true, default))
				{
					PerforceIo? Io = Response.Io;
					if (Io != null)
					{
						if (Io.Command == PerforceIoCommand.Open)
						{
							string Path = GetClientRelativePath(Io.Payload, ClientInfo.Client.Root);
							TreePackFileWriter FileWriter = new TreePackFileWriter(ClientInfo.TreePack);
							Files[Io.File] = (Path, FileWriter);
						}
						else if (Io.Command == PerforceIoCommand.Write)
						{
							TreePackFileWriter FileWriter = Files[Io.File].Writer;
							await FileWriter.WriteAsync(Io.Payload, false);
						}
						else if (Io.Command == PerforceIoCommand.Close)
						{
							(string Path, TreePackFileWriter FileWriter) = Files[Io.File];
							IoHash FileHash = await FileWriter.FinalizeAsync();
							await DirWriter.FindOrAddFileByPathAsync(Path, TreePackDirEntryFlags.File, FileHash, Sha1Hash.Zero);
							Files.Remove(Io.File);
						}
						else if (Io.Command == PerforceIoCommand.Unlink)
						{
							string Path = GetClientRelativePath(Io.Payload, ClientInfo.Client.Root);
							await DirWriter.RemoveFileByPathAsync(Path);
						}
					}
				}
			}

			ClientInfo.Change = Change;

			// Return the new root object
			IoHash RootHash = await DirWriter.FlushAsync();
			TreePackObject RootObject = await ClientInfo.TreePack.FlushAsync(RootHash, DateTime.UtcNow);
			return new CommitTree(Stream.Id, Change, RootObject);
		}

		async Task FlushWorkspaceAsync(ReplicationClient ClientInfo, IPerforceConnection Perforce, int Change)
		{
			if (ClientInfo.Change != Change)
			{
				ClientInfo.Change = -1;
				if (Change == 0)
				{
					Logger.LogInformation("Flushing have table for {Client}", Perforce.Settings.ClientName);
					await Perforce.SyncQuietAsync(SyncOptions.Force | SyncOptions.KeepWorkspaceFiles, -1, $"//...#0");
				}
				else
				{
					Logger.LogInformation("Flushing have table for {Client} to change {Change}", Perforce.Settings.ClientName, Change);
					await Perforce.SyncQuietAsync(SyncOptions.Force | SyncOptions.KeepWorkspaceFiles, -1, $"//...@{Change}");
				}
				ClientInfo.Change = Change;
			}
		}

		static string GetClientRelativePath(ReadOnlyMemory<byte> Data, string ClientRoot)
		{
			ReadOnlySpan<byte> Span = Data.Span;

			string Path = Encoding.UTF8.GetString(Span.Slice(0, Span.IndexOf((byte)0)));
			if (!Path.StartsWith(ClientRoot, StringComparison.Ordinal))
			{
				throw new ArgumentException($"Unable to make path {Path} relative to client root {ClientRoot}");
			}

			return Path.Substring(ClientRoot.Length).Replace('\\', '/').TrimStart('/');
		}

		class ReplicationClient
		{
			public PerforceSettings Settings { get; }
			public string ClusterName { get; }
			public InfoRecord ServerInfo { get; }
			public ClientRecord Client { get; }
			public int Change { get; set; }
			public TreePack TreePack { get; }

			public ReplicationClient(PerforceSettings Settings, string ClusterName, InfoRecord ServerInfo, ClientRecord Client, int Change, TreePack TreePack)
			{
				this.Settings = Settings;
				this.ClusterName = ClusterName;
				this.ServerInfo = ServerInfo;
				this.Client = Client;
				this.Change = Change;
				this.TreePack = TreePack;
			}
		}

		Dictionary<StreamId, ReplicationClient> CachedPerforceClients = new Dictionary<StreamId, ReplicationClient>();

		async Task<ReplicationClient?> FindReplicationClientAsync(IStream Stream)
		{
			ReplicationClient? ClientInfo;
			if (CachedPerforceClients.TryGetValue(Stream.Id, out ClientInfo))
			{
				if (!String.Equals(ClientInfo.ClusterName, Stream.ClusterName, StringComparison.Ordinal) && String.Equals(ClientInfo.Client.Stream, Stream.Name, StringComparison.Ordinal))
				{
					PerforceSettings ServerSettings = new PerforceSettings(ClientInfo.Settings);
					ServerSettings.ClientName = null;

					using IPerforceConnection Perforce = await PerforceConnection.CreateAsync(Logger);
					await Perforce.DeleteClientAsync(DeleteClientOptions.None, ClientInfo.Client.Name);

					CachedPerforceClients.Remove(Stream.Id);
					ClientInfo = null;
				}
			}
			return ClientInfo;
		}

		async Task<ReplicationClient> FindOrAddReplicationClientAsync(IStream Stream)
		{
			ReplicationClient? ClientInfo = await FindReplicationClientAsync(Stream);
			if (ClientInfo == null)
			{
				using IPerforceConnection? Perforce = await PerforceService.GetServiceUserConnection(Stream.ClusterName);
				if (Perforce == null)
				{
					throw new PerforceException($"Unable to create connection to Perforce server");
				}

				InfoRecord ServerInfo = await Perforce.GetInfoAsync(InfoOptions.ShortOutput);

				ClientRecord NewClient = new ClientRecord($"Horde.Build_{ServerInfo.ClientHost}_{Stream.Id}", Perforce.Settings.UserName, "/p4/");
				NewClient.Description = "Created to mirror Perforce content to Horde Storage";
				NewClient.Owner = Perforce.Settings.UserName;
				NewClient.Host = ServerInfo.ClientHost;
				NewClient.Stream = Stream.Name;
				await Perforce.CreateClientAsync(NewClient);
				Logger.LogInformation("Created client {ClientName} for {StreamName}", NewClient.Name, Stream.Name);

				PerforceSettings Settings = new PerforceSettings(Perforce.Settings);
				Settings.ClientName = NewClient.Name;
				Settings.PreferNativeClient = true;

				TreePack TreePack = new TreePack(StorageClient, Options.NamespaceId, Options.Packing);
				ClientInfo = new ReplicationClient(Settings, Stream.ClusterName, ServerInfo, NewClient, -1, TreePack);
				CachedPerforceClients.Add(Stream.Id, ClientInfo);
			}
			return ClientInfo;
		}

		/// <summary>
		/// Get the bucket used for particular stream refs
		/// </summary>
		/// <param name="StreamId"></param>
		/// <returns></returns>
		static BucketId GetStreamBucketId(StreamId StreamId)
		{
			return new BucketId(StreamId.ToString());
		}

		#endregion
	}
}
