// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Bundles;
using EpicGames.Horde.Storage.Nodes;
using EpicGames.Perforce;
using EpicGames.Redis;
using EpicGames.Serialization;
using Horde.Build.Server;
using Horde.Build.Streams;
using Horde.Build.Users;
using Horde.Build.Utilities;
using HordeCommon;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using StackExchange.Redis;

namespace Horde.Build.Perforce
{
	using CommitId = ObjectId<ICommit>;
	using StreamId = StringId<IStream>;

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
		/// Whether to replicate content to Horde storage. Must also be enabled on a per-stream basis (see <see cref="IStream.ReplicationMode"/>).
		/// </summary>
		public bool Content { get; set; } = true;

		/// <summary>
		/// Namespace to store content replicated from Perforce
		/// </summary>
		public NamespaceId NamespaceId { get; set; } = new NamespaceId("horde.p4");

		/// <summary>
		/// Options for how objects are packed together
		/// </summary>
		public BundleOptions Bundle { get; set; } = new BundleOptions();

		/// <summary>
		/// Options for how objects are sliced
		/// </summary>
		public ChunkingOptions Chunking { get; set; } = new ChunkingOptions();
	}

	/// <summary>
	/// Key for a commit object. This is hashed to produce a ref id.
	/// </summary>
	public class CommitKey
	{
		/// <summary>
		/// Stream that this commit came from
		/// </summary>
		[CbField]
		public StreamId StreamId { get; set; }

		/// <summary>
		/// The change being mirrored
		/// </summary>
		[CbField]
		public int Change { get; set; }

		/// <summary>
		/// Filter for paths in the depot included in this tree
		/// </summary>
		[CbField]
		public string? Filter { get; set; }

		/// <summary>
		/// Whether this commit contains depot path and revision metadata rather than full file contents
		/// </summary>
		[CbField]
		public bool RevisionsOnly { get; set; }

		/// <summary>
		/// Default constructor
		/// </summary>
		public CommitKey()
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public CommitKey(StreamId streamId, int change, string? filter, bool revisionsOnly)
		{
			StreamId = streamId;
			Change = change;
			Filter = filter;
			RevisionsOnly = revisionsOnly;
		}

		/// <summary>
		/// Gets the ref id for this key
		/// </summary>
		/// <returns></returns>
		public RefId GetRefId()
		{
			CbObject obj = CbSerializer.Serialize(this);
			return new RefId(IoHash.Compute(obj.GetView().Span));
		}

		/// <summary>
		/// Gets the ref id for a particular commit
		/// </summary>
		public static RefId GetRefId(StreamId streamId, int change, string? filter, bool revisionsOnly) => new CommitKey(streamId, change, filter, revisionsOnly).GetRefId();
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

			public StreamInfo(IStream stream, ViewMap view)
			{
				Stream = stream;
				View = view;
			}
		}

		/// <summary>
		/// A registered listener for new commits
		/// </summary>
		class ListenerInfo : IDisposable
		{
			readonly List<ListenerInfo> _listeners;
			public Action<ICommit> _callback;

			public ListenerInfo(List<ListenerInfo> listeners, Action<ICommit> callback)
			{
				_listeners = listeners;
				_callback = callback;

				lock (listeners)
				{
					listeners.Add(this);
				}
			}

			public void Dispose()
			{
				lock (_listeners)
				{
					_listeners.Remove(this);
				}
			}
		}

		public CommitServiceOptions Options { get; }

		// Redis
		readonly IDatabase _redis;

		// Collections
		readonly ICommitCollection _commitCollection;
		readonly IStreamCollection _streamCollection;
		readonly IPerforceService _perforceService;
		readonly IUserCollection _userCollection;
		readonly ITreeStore _treeStore;
		readonly ILogger<CommitService> _logger;

		const int MaxBackgroundTasks = 2;

		/// <summary>
		/// Constructor
		/// </summary>
		public CommitService(RedisService redisService, ICommitCollection commitCollection, IStreamCollection streamCollection, IPerforceService perforceService, IUserCollection userCollection, ITreeStore<CommitService> treeStore, IClock clock, IOptions<CommitServiceOptions> options, ILogger<CommitService> logger)
		{
			Options = options.Value;

			_redis = redisService.Database;
			_redisDirtyStreams = new RedisSet<StreamId>(_redis, RedisBaseKey.Append("streams"));
			_redisReservations = new RedisSortedSet<StreamId>(_redis, RedisBaseKey.Append("reservations"));

			_commitCollection = commitCollection;
			_streamCollection = streamCollection;
			_perforceService = perforceService;
			_userCollection = userCollection;
			_treeStore = treeStore;
			_logger = logger;
			_updateMetadataTicker = clock.AddSharedTicker<CommitService>(TimeSpan.FromSeconds(30.0), UpdateMetadataAsync, logger);
		}

		/// <inheritdoc/>
		public void Dispose() => _updateMetadataTicker.Dispose();

		/// <inheritdoc/>
		public async Task StartAsync(CancellationToken cancellationToken)
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
		public async Task StopAsync(CancellationToken cancellationToken)
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

		readonly List<ListenerInfo> _listeners = new List<ListenerInfo>();
		RedisChannel<CommitId> CommitNotifyChannel { get; } = new RedisChannel<CommitId>("commits/notify");
		RedisChannelSubscription<CommitId>? _commitNotifySubscription;

		async Task StartNotificationsAsync()
		{
			ISubscriber subscriber = _redis.Multiplexer.GetSubscriber();
			_commitNotifySubscription = await subscriber.SubscribeAsync(CommitNotifyChannel, DispatchCommitNotification);
		}

		async Task StopNotificationsAsync()
		{
			if (_commitNotifySubscription != null)
			{
				await _commitNotifySubscription.DisposeAsync();
				_commitNotifySubscription = null;
			}
		}

		async void DispatchCommitNotification(RedisChannel<CommitId> channel, CommitId commitId)
		{
			ICommit? commit = await _commitCollection.GetCommitAsync(commitId);
			if (commit != null)
			{
				lock (_listeners)
				{
					foreach (ListenerInfo listener in _listeners)
					{
						listener._callback(commit);
					}
				}
			}
		}

		async Task NotifyListeners(CommitId commitId)
		{
			await _redis.PublishAsync(CommitNotifyChannel, commitId);
		}

		public IDisposable AddListener(Action<ICommit> onAddCommit)
		{
			return new ListenerInfo(_listeners, onAddCommit);
		}

		#endregion

		#region Metadata updates

		readonly ITicker _updateMetadataTicker;

		Task StartMetadataReplicationAsync()
		{
			return _updateMetadataTicker.StartAsync();
		}

		Task StopMetadataReplicationAsync()
		{
			return _updateMetadataTicker.StopAsync();
		}

		/// <summary>
		/// Polls Perforce for submitted changes
		/// </summary>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		async ValueTask UpdateMetadataAsync(CancellationToken cancellationToken)
		{
			List<IStream> streams = await _streamCollection.FindAllAsync();
			foreach (IGrouping<string, IStream> group in streams.GroupBy(x => x.ClusterName, StringComparer.OrdinalIgnoreCase))
			{
				try
				{
					await UpdateMetadataForClusterAsync(group.Key, group);
				}
				catch (Exception ex)
				{
					_logger.LogError(ex, "Error while updating cluster {ClusterName}", group.Key);
				}
			}
		}

		/// <summary>
		/// Updates metadata for all the streams on a particular server cluster
		/// </summary>
		/// <param name="clusterName"></param>
		/// <param name="streams"></param>
		/// <returns></returns>
		async Task UpdateMetadataForClusterAsync(string clusterName, IEnumerable<IStream> streams)
		{
			// Find the minimum changelist number to query
			Dictionary<IStream, int> streamToFirstChange = new Dictionary<IStream, int>();
			foreach (IStream stream in streams)
			{
				RedisList<int> changes = RedisStreamChanges(stream.Id);

				int firstChange = await changes.GetByIndexAsync(-1);
				if (firstChange != 0)
				{
					firstChange++;
				}

				streamToFirstChange[stream] = firstChange;
			}

			// Update the database with all the commits
			HashSet<StreamId> newStreamIds = new HashSet<StreamId>();
			await foreach (NewCommit newCommit in FindCommitsForClusterAsync(clusterName, streamToFirstChange))
			{
				ICommit commit = await _commitCollection.AddOrReplaceAsync(newCommit);
				_logger.LogInformation("Replicated CL {Change}: {Description} as {CommitId}", commit.Change, StringUtils.Truncate(commit.Description, 40), commit.Id);

				await NotifyListeners(commit.Id);

				RedisList<int> streamCommitsKey = RedisStreamChanges(commit.StreamId);
				await streamCommitsKey.RightPushAsync(commit.Change);

				newStreamIds.Add(commit.StreamId);
			}

			// Signal to any listeners that we have new data to process
			foreach (StreamId newStreamId in newStreamIds)
			{
				await _redisDirtyStreams.AddAsync(newStreamId);
				await _redis.PublishAsync(RedisUpdateChannel, newStreamId);
			}
		}

		/// <summary>
		/// Enumerate new commits to the given streams, using a stream view to deduplicate changes which affect multiple branches.
		/// </summary>
		/// <param name="clusterName"></param>
		/// <param name="streamToFirstChange"></param>
		/// <returns>List of new commits</returns>
		public async IAsyncEnumerable<NewCommit> FindCommitsForClusterAsync(string clusterName, Dictionary<IStream, int> streamToFirstChange)
		{
			_logger.LogInformation("Replicating metadata for cluster {Name}", clusterName);

			// Create a connection to the server
			using IPerforceConnection? connection = await _perforceService.GetServiceUserConnection(clusterName);
			if (connection == null)
			{
				throw new PerforceException($"Unable to create cluster connection for {clusterName}");
			}

			// Figure out the case settings for the server
			InfoRecord serverInfo = await connection.GetInfoAsync(InfoOptions.ShortOutput);

			// Get the view for each stream
			List<StreamInfo> streamInfoList = new List<StreamInfo>();
			foreach (IStream stream in streamToFirstChange.Keys)
			{
				ViewMap view = await GetStreamViewAsync(connection, stream.Name);
				streamInfoList.Add(new StreamInfo(stream, view));
			}

			// Find all the depot roots
			Dictionary<Utf8String, Utf8String> depotRoots = new Dictionary<Utf8String, Utf8String>(serverInfo.Utf8PathComparer);
			foreach (ViewMap view in streamInfoList.Select(x => x.View))
			{
				foreach (ViewMapEntry entry in view.Entries)
				{
					if (entry.SourcePrefix.Length >= 3)
					{
						int slashIdx = entry.SourcePrefix.IndexOf('/', 2);
						Utf8String depot = entry.SourcePrefix.Slice(0, slashIdx + 1);

						Utf8String depotRoot;
						if (depotRoots.TryGetValue(depot, out depotRoot))
						{
							depotRoot = GetCommonPrefix(depotRoot, entry.SourcePrefix);
						}
						else
						{
							depotRoot = entry.SourcePrefix;
						}

						int lastSlashIdx = depotRoot.LastIndexOf('/');
						depotRoots[depot] = depotRoot.Slice(0, lastSlashIdx + 1);
					}
				}
			}

			// Find the minimum changelist number to query
			int minChange = Int32.MaxValue;
			foreach (StreamInfo streamInfo in streamInfoList)
			{
				int firstChange = streamToFirstChange[streamInfo.Stream];
				if (firstChange == 0)
				{
					firstChange = await GetFirstCommitToReplicateAsync(connection, streamInfo.View, serverInfo.Utf8PathComparer);
				}
				if (firstChange != 0)
				{
					minChange = Math.Min(minChange, firstChange);
				}
			}

			// Find all the changes to consider
			SortedSet<int> changeNumbers = new SortedSet<int>();
			foreach (Utf8String depotRoot in depotRoots.Values)
			{
				List<ChangesRecord> changes = await connection.GetChangesAsync(ChangesOptions.None, null, minChange, -1, ChangeStatus.Submitted, null, $"{depotRoot}...");
				changeNumbers.UnionWith(changes.Select(x => x.Number));
			}

			// Add the changes in order
			List<string> relativePaths = new List<string>();
			foreach (int changeNumber in changeNumbers)
			{
				DescribeRecord describeRecord = await connection.DescribeAsync(changeNumber);
				foreach (StreamInfo streamInfo in streamInfoList)
				{
					IStream stream = streamInfo.Stream;

					Utf8String basePath = GetBasePath(describeRecord, streamInfo.View, serverInfo.Utf8PathComparer);
					if (!basePath.IsEmpty)
					{
						IUser author = await _userCollection.FindOrAddUserByLoginAsync(describeRecord.User);
						IUser owner = (await ParseRobomergeOwnerAsync(describeRecord.Description)) ?? author;

						int originalChange = ParseRobomergeSource(describeRecord.Description) ?? describeRecord.Number;

						yield return new NewCommit(stream.Id, describeRecord.Number, originalChange, author.Id, owner.Id, describeRecord.Description, basePath.ToString(), describeRecord.Time);
					}
				}
			}
		}

		/// <summary>
		/// Find the first commit to replicate from a branch
		/// </summary>
		/// <param name="connection"></param>
		/// <param name="view"></param>
		/// <param name="comparer"></param>
		/// <returns></returns>
		static async Task<int> GetFirstCommitToReplicateAsync(IPerforceConnection connection, ViewMap view, Utf8StringComparer comparer)
		{
			int minChange = 0;

			List<Utf8String> rootPaths = view.GetRootPaths(comparer);
			foreach (Utf8String rootPath in rootPaths)
			{
				IList<ChangesRecord> pathChanges = await connection.GetChangesAsync(ChangesOptions.None, 20, ChangeStatus.Submitted, $"{rootPath}...");
				minChange = Math.Max(minChange, pathChanges.Min(x => x.Number));
			}

			return minChange;
		}

		/// <summary>
		/// Find the base path for a change within a stream
		/// </summary>
		/// <param name="changelist">Information about the changelist</param>
		/// <param name="view">Mapping from depot syntax to client view</param>
		/// <param name="comparer">Path comparison type for the server</param>
		/// <returns>The base path for all files in the change</returns>
		static Utf8String GetBasePath(DescribeRecord changelist, ViewMap view, Utf8StringComparer comparer)
		{
			Utf8String basePath = default;
			foreach (DescribeFileRecord file in changelist.Files)
			{
				Utf8String streamFile;
				if (view.TryMapFile(file.DepotFile, comparer, out streamFile))
				{
					if (basePath.IsEmpty)
					{
						basePath = streamFile;
					}
					else
					{
						basePath = GetCommonPrefix(basePath, streamFile);
					}
				}
			}
			return basePath;
		}

		/// <summary>
		/// Gets the common prefix between two stringsc
		/// </summary>
		/// <param name="a"></param>
		/// <param name="b"></param>
		/// <returns></returns>
		static Utf8String GetCommonPrefix(Utf8String a, Utf8String b)
		{
			int index = 0;
			while (index < a.Length && index < b.Length && a[index] == b[index])
			{
				index++;
			}
			return a.Substring(0, index);
		}

		/// <inheritdoc/>
		public async Task<List<ICommit>> FindCommitsAsync(StreamId streamId, int? minChange = null, int? maxChange = null, string[]? paths = null, int? index = null, int? count = null)
		{
			if(paths != null)
			{
				throw new NotImplementedException();
			}

			return await _commitCollection.FindCommitsAsync(streamId, minChange, maxChange, index, count);
		}

		/// <summary>
		/// Attempts to parse the Robomerge source from this commit information
		/// </summary>
		/// <param name="description">Description text to parse</param>
		/// <returns>The parsed source changelist, or null if no #ROBOMERGE-SOURCE tag was present</returns>
		static int? ParseRobomergeSource(string description)
		{
			// #ROBOMERGE-SOURCE: CL 13232051 in //Fortnite/Release-12.60/... via CL 13232062 via CL 13242953
			Match match = Regex.Match(description, @"^#ROBOMERGE-SOURCE: CL (\d+)", RegexOptions.Multiline);
			if (match.Success)
			{
				return Int32.Parse(match.Groups[1].Value, CultureInfo.InvariantCulture);
			}
			else
			{
				return null;
			}
		}

		async Task<IUser?> ParseRobomergeOwnerAsync(string description)
		{
			string? originalAuthor = ParseRobomergeOwner(description);
			if (originalAuthor != null)
			{
				return await _userCollection.FindOrAddUserByLoginAsync(originalAuthor);
			}
			return null;
		}

		/// <summary>
		/// Attempts to parse the Robomerge owner from this commit information
		/// </summary>
		/// <param name="description">Description text to parse</param>
		/// <returns>The Robomerge owner, or null if no #ROBOMERGE-OWNER tag was present</returns>
		static string? ParseRobomergeOwner(string description)
		{
			// #ROBOMERGE-OWNER: ben.marsh
			Match match = Regex.Match(description, @"^#ROBOMERGE-OWNER:\s*([^\s]+)", RegexOptions.Multiline);
			if (match.Success)
			{
				return match.Groups[1].Value;
			}
			else
			{
				return null;
			}
		}

		/// <summary>
		/// Gets the view for a stream
		/// </summary>
		/// <param name="connection">The Perforce connection</param>
		/// <param name="streamName">Name of the stream to be mirrored</param>
		static async Task<ViewMap> GetStreamViewAsync(IPerforceConnection connection, string streamName)
		{
			StreamRecord record = await connection.GetStreamAsync(streamName, true);

			ViewMap view = new ViewMap();
			foreach (string viewLine in record.View)
			{
				Match match = Regex.Match(viewLine, "^(-?)([^ ]+) *([^ ]+)$");
				if (!match.Success)
				{
					throw new PerforceException($"Unable to parse stream view: '{viewLine}'");
				}
				view.Entries.Add(new ViewMapEntry(match.Groups[1].Length == 0, match.Groups[2].Value, match.Groups[3].Value));
			}

			return view;
		}

		#endregion

		#region Content Updates

		RedisKey RedisBaseKey { get; } = new RedisKey("commits/");
		RedisChannel<StreamId> RedisUpdateChannel { get; } = new RedisChannel<StreamId>("commits/streams");
		bool _stopping;
		readonly AsyncEvent _updateStreamsEvent = new AsyncEvent();
		readonly RedisSet<StreamId> _redisDirtyStreams;
		readonly RedisSortedSet<StreamId> _redisReservations;
		RedisChannelSubscription<StreamId>? _redisUpdateSubscription;
		Task? _streamUpdateTask;

		RedisList<int> RedisStreamChanges(StreamId streamId) => new RedisList<int>(_redis, RedisBaseKey.Append($"stream/{streamId}/changes"));

		async Task StartContentReplicationAsync()
		{
			_stopping = false;
			_updateStreamsEvent.Reset();
			_redisUpdateSubscription = await _redis.Multiplexer.GetSubscriber().SubscribeAsync(RedisUpdateChannel, (_, _) => _updateStreamsEvent.Pulse());
			_streamUpdateTask = Task.Run(() => UpdateContentAsync());
		}

		async Task StopContentReplicationAsync()
		{
			_stopping = true;
			_updateStreamsEvent.Latch();
			if (_redisUpdateSubscription != null)
			{
				await _redisUpdateSubscription.DisposeAsync();
				_redisUpdateSubscription = null;
			}
			await _streamUpdateTask!;
		}

		async Task UpdateContentAsync()
		{
			List<(StreamId, Task)> backgroundTasks = new List<(StreamId, Task)>(MaxBackgroundTasks);
			while (!_stopping || backgroundTasks.Count > 0)
			{
				try
				{
					await UpdateContentInternalAsync(backgroundTasks);
				}
				catch (Exception ex)
				{
					_logger.LogError(ex, "Exception when updating commit service");
				}
			}
		}

		async Task UpdateContentInternalAsync(List<(StreamId, Task)> backgroundTasks)
		{
			// Remove any complete background tasks
			for (int idx = backgroundTasks.Count - 1; idx >= 0; idx--)
			{
				(_, Task backgroundTask) = backgroundTasks[idx];
				if (backgroundTask.IsCompleted)
				{
					if (backgroundTask.Exception != null)
					{
						_logger.LogError(backgroundTask.Exception, "Update background task faulted");
					}
					backgroundTasks.RemoveAt(idx);
				}
			}

			// Create a list of events to wait for
			List<Task> waitTasks = new List<Task>(backgroundTasks.Select(x => x.Item2));
			if (!_stopping)
			{
				// If we have spare slots for executing background tasks, check if there are any dirty streams
				Task newStreamTask = _updateStreamsEvent.Task;
				if (backgroundTasks.Count < MaxBackgroundTasks)
				{
					// Expire any reservations that are no longer valid
					DateTime utcNow = DateTime.UtcNow;
					await _redisReservations.RemoveRangeByScoreAsync(Double.NegativeInfinity, utcNow.Ticks);

					// Find the streams that we can wait for
					HashSet<StreamId> checkStreams = new HashSet<StreamId>(await _redisDirtyStreams.MembersAsync());
					checkStreams.ExceptWith(backgroundTasks.Select(x => x.Item1));

					if (checkStreams.Count > 0)
					{
						// Try to start new background tasks
						double newScore = (utcNow + TimeSpan.FromSeconds(30.0)).Ticks;
						foreach (StreamId checkStream in checkStreams)
						{
							if (await _redisReservations.AddAsync(checkStream, newScore, When.NotExists))
							{
								Task newTask = Task.Run(() => UpdateStreamContentAsync(checkStream, CancellationToken.None));
								backgroundTasks.Add((checkStream, newTask));

								if (backgroundTasks.Count == MaxBackgroundTasks)
								{
									break;
								}
							}
						}
						checkStreams.ExceptWith(backgroundTasks.Select(x => x.Item1));

						// If we still have spare tasks, check how long we should wait for a reservation to expire
						if (backgroundTasks.Count < MaxBackgroundTasks)
						{
							long waitTime = Int64.MaxValue;
							await foreach (SortedSetEntry<StreamId> entry in _redisReservations.ScanAsync())
							{
								if (checkStreams.Contains(entry.Element))
								{
									waitTime = Math.Min(waitTime, (long)entry.Score - utcNow.Ticks);
								}
							}
							if (waitTime < Int32.MaxValue)
							{
								waitTasks.Add(Task.Delay(new TimeSpan(Math.Max(waitTime, 0))));
							}
						}
					}
				}

				// If we still have bandwidth to process more tasks, wait for new streams to become available
				if (backgroundTasks.Count < MaxBackgroundTasks)
				{
					waitTasks.Add(newStreamTask);
				}
			}

			// Wait until any task has completed
			if (waitTasks.Count > 0)
			{
				await Task.WhenAny(waitTasks);
			}
		}

		async Task UpdateStreamContentAsync(StreamId streamId, CancellationToken cancellationToken)
		{
			Stopwatch timer = Stopwatch.StartNew();

			RedisList<int> streamChanges = RedisStreamChanges(streamId);
			for (; ; )
			{
				// Update the stream, updating the reservation every 30 seconds
				Task internalTask = Task.Run(() => UpdateStreamContentInternalAsync(streamId, streamChanges, cancellationToken), cancellationToken);
				while (!internalTask.IsCompleted)
				{
					Task delayTask = Task.Delay(TimeSpan.FromSeconds(15.0), cancellationToken);
					if (await Task.WhenAny(internalTask, delayTask) == delayTask)
					{
						DateTime newTime = DateTime.UtcNow + TimeSpan.FromSeconds(30.0);
						await _redisReservations.AddAsync(streamId, newTime.Ticks);
						_logger.LogInformation("Extending reservation for content update of {StreamId} (elapsed: {Time}s)", streamId, (int)timer.Elapsed.TotalSeconds);
					}
				}

				// Log any error during the update
				if (internalTask.IsFaulted)
				{
					_logger.LogError(internalTask.Exception, "Exception while updating stream content for {StreamId}", streamId);
				}
				else
				{
					_logger.LogInformation("Finished update for {StreamId}", streamId);
				}

				// Remove this stream from the dirty list if it's empty
				ITransaction transaction = _redis.CreateTransaction();
				transaction.AddCondition(Condition.ListLengthLessThan(streamChanges.Key, 2));
				_ = transaction.With(_redisDirtyStreams).RemoveAsync(streamId);
				if (await transaction.ExecuteAsync())
				{
					break;
				}
			}
			await _redisReservations.RemoveAsync(streamId);
		}

		async Task UpdateStreamContentInternalAsync(StreamId streamId, RedisList<int> changes, CancellationToken cancellationToken)
		{
			IStream? stream = await _streamCollection.GetAsync(streamId);
			if (stream == null)
			{
				return;
			}

			int? prevChange = null;
			for (; ; )
			{
				// Get the first two changes to be mirrored. The first one should already exist, unless it's the start of replication for this stream.
				int[] values = await changes.RangeAsync(0, 1);
				if (values.Length < 2)
				{
					break;
				}

				// Check that the previous commit is still valid
				if (prevChange != null && prevChange.Value != values[0])
				{
					_logger.LogInformation("Invalidating previous commit; expected {Change}, actually {ActualChange}", prevChange.Value, values[0]);
					prevChange = null;
				}

				// If we don't have a previous commit tree yet, perform a full snapshot of that change
				if (prevChange == null)
				{
					await WriteCommitTreeAsync(stream, values[0], null, cancellationToken);
				}

				// Perform a snapshot of the new change, then remove it from the list
				await WriteCommitTreeAsync(stream, values[1], prevChange, cancellationToken);
				prevChange = values[1];
				await changes.LeftPopAsync();
			}
		}

		/// <summary>
		/// Replicates the contents of a stream to Horde storage, optionally using the given change as a starting point
		/// </summary>
		/// <param name="stream">The stream to replicate</param>
		/// <param name="change">Commit to store the tree ref</param>
		/// <param name="baseChange">The base change to update from</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Root tree object</returns>
		public async Task WriteCommitTreeAsync(IStream stream, int change, int? baseChange, CancellationToken cancellationToken)
		{
			if (stream.ReplicationMode != ContentReplicationMode.None)
			{
				await WriteCommitTreeAsync(stream, change, baseChange, stream.ReplicationFilter, stream.ReplicationMode == ContentReplicationMode.RevisionsOnly, cancellationToken);
			}
		}

		/// <summary>
		/// Gets the ref for a particular change
		/// </summary>
		/// <param name="streamId"></param>
		/// <param name="change"></param>
		/// <param name="filter"></param>
		/// <param name="revisionsOnly"></param>
		/// <returns></returns>
		static RefId GetRefId(StreamId streamId, int change, string? filter, bool revisionsOnly)
		{
			return new CommitKey(streamId, change, filter, revisionsOnly).GetRefId();
		}

		/// <summary>
		/// Replicates the contents of a stream to Horde storage, optionally using the given change as a starting point
		/// </summary>
		/// <param name="stream">The stream to replicate</param>
		/// <param name="change">Commit to store the tree ref</param>
		/// <param name="baseChange">The base change to update from</param>
		/// <param name="filter">Depot path to query for changes. Will default to the entire depot (but filtered by the workspace)</param>
		/// <param name="revisionsOnly">Whether to replicate file revisions only</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Root tree object</returns>
		public async Task WriteCommitTreeAsync(IStream stream, int change, int? baseChange, string? filter, bool revisionsOnly, CancellationToken cancellationToken)
		{
			// Create a client to replicate from this stream
			ReplicationClient clientInfo = await FindOrAddReplicationClientAsync(stream);

			// Connect to the server and flush the workspace
			using IPerforceConnection perforce = await PerforceConnection.CreateAsync(clientInfo.Settings, _logger);
			await FlushWorkspaceAsync(clientInfo, perforce, baseChange ?? 0);

			// Get the initial bundle state
			DirectoryNode contents;
			if (baseChange == null)
			{
				contents = new DirectoryNode();
			}
			else
			{
				contents = await _treeStore.ReadTreeAsync<DirectoryNode>(GetRefId(stream.Id, baseChange.Value, filter, revisionsOnly), cancellationToken);
			}

			// Apply all the updates
			_logger.LogInformation("Updating client {Client} from changelist {BaseChange} to {Change}", clientInfo.Client.Name, baseChange ?? 0, change);
			clientInfo.Change = -1;

			Utf8String clientRoot = new Utf8String(clientInfo.Client.Root);
			string filterOrDefault = filter ?? "...";
			string queryPath = $"//{clientInfo.Client.Name}/{filterOrDefault}";

			RefId refId = GetRefId(stream.Id, change, filter, revisionsOnly);
			if (revisionsOnly)
			{
				int count = 0;
				await foreach (PerforceResponse<SyncRecord> record in perforce.StreamCommandAsync<SyncRecord>("sync", new[] { "-k", $"{queryPath}@{change}" }, null, default, cancellationToken))
				{
					PerforceError? error = record.Error;
					if (error != null && error.Generic == PerforceGenericCode.Empty)
					{
						continue;
					}

					SyncRecord syncRecord = record.Data;
					if (!syncRecord.Path.StartsWith(clientRoot))
					{
						throw new ArgumentException($"Unable to make path {clientInfo.Client.Root} relative to client root {clientInfo.Client.Root}");
					}

					Utf8String path = syncRecord.Path.Substring(clientRoot.Length);
					byte[] data = Encoding.UTF8.GetBytes($"{syncRecord.DepotFile}#{syncRecord.Revision}");

					using ReadOnlyMemoryStream dataStream = new ReadOnlyMemoryStream(data);

					FileEntry entry = await contents.AddFileByPathAsync(path, FileEntryFlags.PerforceDepotPathAndRevision, cancellationToken);
					await entry.AppendAsync(dataStream, Options.Chunking, cancellationToken);

					if (++count > 1000)
					{
						await _treeStore.WriteTreeAsync(refId, contents, false, cancellationToken);
						count = 0;
					}
				}
			}
			else
			{
				const long TrimInterval = 1024 * 1024 * 256;

				long dataSize = 0;
				long trimDataSize = TrimInterval;

				Dictionary<int, FileEntry> files = new Dictionary<int, FileEntry>();
				await foreach (PerforceResponse response in perforce.StreamCommandAsync("sync", Array.Empty<string>(), new string[] { $"{queryPath}@{change}" }, null, typeof(SyncRecord), true, default))
				{
					PerforceError? error = response.Error;
					if (error != null)
					{
						_logger.LogWarning("Perforce: {Message}", error.Data);
						continue;
					}

					PerforceIo? io = response.Io;
					if (io != null)
					{
						if (io.Command == PerforceIoCommand.Open)
						{
							Utf8String path = GetClientRelativePath(io.Payload, clientInfo.Client.Root);
							files[io.File] = await contents.AddFileByPathAsync(path, FileEntryFlags.None, cancellationToken);
						}
						else if (io.Command == PerforceIoCommand.Write)
						{
							FileEntry file = files[io.File];
							await file.AppendAsync(io.Payload, Options.Chunking, cancellationToken);
							dataSize += io.Payload.Length;
						}
						else if (io.Command == PerforceIoCommand.Close)
						{
							files.Remove(io.File);
						}
						else if (io.Command == PerforceIoCommand.Unlink)
						{
							Utf8String path = GetClientRelativePath(io.Payload, clientInfo.Client.Root);
							await contents.DeleteFileByPathAsync(path, cancellationToken);
						}
						else
						{
							_logger.LogWarning("Unhandled command code {Code}", io.Command);
						}

						if (dataSize > trimDataSize)
						{
							_logger.LogInformation("Trimming working set after receiving {NumBytes:n0}mb...", dataSize / (1024 * 1024));
							await _treeStore.WriteTreeAsync(refId, contents, false, cancellationToken);
							GC.Collect();
							trimDataSize = dataSize + TrimInterval;
							_logger.LogInformation("Trimming complete. Next trim at {NextNumBytes:n0}mb.", trimDataSize / (1024 * 1024));
						}
					}
				}
			}

			clientInfo.Change = change;

			// Return the new root object
			_logger.LogInformation("Writing ref {RefId} for {StreamId} change {Change}", refId, stream.Id, change);
			await _treeStore.WriteTreeAsync(refId, contents, true, cancellationToken);
		}

		public QualifiedRefId? GetReplicatedContentRef(IStream stream, int change)
		{
			if (stream.ReplicationMode == ContentReplicationMode.None)
			{
				return null;
			}
			else
			{
				CommitKey key = new CommitKey(stream.Id, change, stream.ReplicationFilter, stream.ReplicationMode == ContentReplicationMode.RevisionsOnly);
				return new QualifiedRefId(Options.NamespaceId, GetStreamBucketId(stream.Id), key.GetRefId());
			}
		}

		async Task FlushWorkspaceAsync(ReplicationClient clientInfo, IPerforceConnection perforce, int change)
		{
			if (clientInfo.Change != change)
			{
				clientInfo.Change = -1;
				if (change == 0)
				{
					_logger.LogInformation("Flushing have table for {Client}", perforce.Settings.ClientName);
					await perforce.SyncQuietAsync(SyncOptions.Force | SyncOptions.KeepWorkspaceFiles, -1, $"//...#0");
				}
				else
				{
					_logger.LogInformation("Flushing have table for {Client} to change {Change}", perforce.Settings.ClientName, change);
					await perforce.SyncQuietAsync(SyncOptions.Force | SyncOptions.KeepWorkspaceFiles, -1, $"//...@{change}");
				}
				clientInfo.Change = change;
			}
		}

		static Utf8String GetClientRelativePath(ReadOnlyMemory<byte> data, Utf8String clientRoot)
		{
			int length = data.Span.IndexOf((byte)0);
			if (length != -1)
			{
				data = data.Slice(0, length);
			}

			Utf8String path = new Utf8String(data);
			if (!path.StartsWith(clientRoot, Utf8StringComparer.Ordinal))
			{
				throw new ArgumentException($"Unable to make path {path} relative to client root {clientRoot}");
			}

			return path.Substring(clientRoot.Length).Clone();
		}

		class ReplicationClient
		{
			public PerforceSettings Settings { get; }
			public string ClusterName { get; }
			public InfoRecord ServerInfo { get; }
			public ClientRecord Client { get; }
			public int Change { get; set; }

			public ReplicationClient(PerforceSettings settings, string clusterName, InfoRecord serverInfo, ClientRecord client, int change)
			{
				Settings = settings;
				ClusterName = clusterName;
				ServerInfo = serverInfo;
				Client = client;
				Change = change;
			}
		}

		readonly Dictionary<StreamId, ReplicationClient> _cachedPerforceClients = new Dictionary<StreamId, ReplicationClient>();

		async Task<ReplicationClient?> FindReplicationClientAsync(IStream stream)
		{
			ReplicationClient? clientInfo;
			if (_cachedPerforceClients.TryGetValue(stream.Id, out clientInfo))
			{
				if (!String.Equals(clientInfo.ClusterName, stream.ClusterName, StringComparison.Ordinal) && String.Equals(clientInfo.Client.Stream, stream.Name, StringComparison.Ordinal))
				{
					PerforceSettings serverSettings = new PerforceSettings(clientInfo.Settings);
					serverSettings.ClientName = null;

					using IPerforceConnection perforce = await PerforceConnection.CreateAsync(_logger);
					await perforce.DeleteClientAsync(DeleteClientOptions.None, clientInfo.Client.Name);

					_cachedPerforceClients.Remove(stream.Id);
					clientInfo = null;
				}
			}
			return clientInfo;
		}

		async Task<ReplicationClient> FindOrAddReplicationClientAsync(IStream stream)
		{
			ReplicationClient? clientInfo = await FindReplicationClientAsync(stream);
			if (clientInfo == null)
			{
				using IPerforceConnection? perforce = await _perforceService.GetServiceUserConnection(stream.ClusterName);
				if (perforce == null)
				{
					throw new PerforceException($"Unable to create connection to Perforce server");
				}

				InfoRecord serverInfo = await perforce.GetInfoAsync(InfoOptions.ShortOutput);

				ClientRecord newClient = new ClientRecord($"Horde.Build_{serverInfo.ClientHost}_{stream.Id}", perforce.Settings.UserName, "/p4/");
				newClient.Description = "Created to mirror Perforce content to Horde Storage";
				newClient.Owner = perforce.Settings.UserName;
				newClient.Host = serverInfo.ClientHost;
				newClient.Stream = stream.Name;
				await perforce.CreateClientAsync(newClient);
				_logger.LogInformation("Created client {ClientName} for {StreamName}", newClient.Name, stream.Name);

				PerforceSettings settings = new PerforceSettings(perforce.Settings);
				settings.ClientName = newClient.Name;
				settings.PreferNativeClient = true;

				clientInfo = new ReplicationClient(settings, stream.ClusterName, serverInfo, newClient, -1);
				_cachedPerforceClients.Add(stream.Id, clientInfo);
			}
			return clientInfo;
		}

		/// <summary>
		/// Get the bucket used for particular stream refs
		/// </summary>
		/// <param name="streamId"></param>
		/// <returns></returns>
		static BucketId GetStreamBucketId(StreamId streamId)
		{
			return new BucketId(streamId.ToString());
		}

		#endregion
	}
}
