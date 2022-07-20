// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
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
		/// Options for how objects are packed together
		/// </summary>
		public BundleOptions Bundle { get; set; } = new BundleOptions();

		/// <summary>
		/// Options for how objects are sliced
		/// </summary>
		public ChunkingOptions Chunking { get; set; } = new ChunkingOptions();
	}

	/// <summary>
	/// Root node for a commit snapshot
	/// </summary>
	[TreeSerializer(typeof(CommitNodeSerializer))]
	public class CommitNode : TreeNode
	{
		/// <summary>
		/// Paths that have been synced. Empty once complete.
		/// </summary>
		public List<Utf8String> Paths { get; }

		/// <summary>
		/// Contents of this snapshot
		/// </summary>
		public TreeNodeRef<DirectoryNode> Contents { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public CommitNode()
			: this(new DirectoryNode())
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public CommitNode(DirectoryNode contents)
		{
			Paths = new List<Utf8String>();
			Contents = new TreeNodeRef<DirectoryNode>(this, contents);
		}

		private CommitNode(IEnumerable<Utf8String> paths, ITreeBlob contents)
		{
			Paths = new List<Utf8String>(paths);
			Contents = new TreeNodeRef<DirectoryNode>(this, contents);
		}

		internal async ValueTask<ITreeBlob> SerializeAsync(ITreeBlobWriter writer, CancellationToken cancellationToken)
		{
			List<ITreeBlob> references = new List<ITreeBlob>();
			references.Add(await Contents.CollapseAsync(writer, cancellationToken));

			ByteArrayBuilder builder = new ByteArrayBuilder();
			builder.WriteVariableLengthArray(Paths, x => builder.WriteUtf8String(x));
			return await writer.WriteBlobAsync(builder.AsSequence(), references, cancellationToken);
		}

		internal static async ValueTask<CommitNode> DeserializeAsync(ITreeBlob blob, CancellationToken cancellationToken)
		{
			IReadOnlyList<ITreeBlob> children = await blob.GetReferencesAsync(cancellationToken);
			ReadOnlySequence<byte> data = await blob.GetDataAsync(cancellationToken);

			MemoryReader reader = new MemoryReader(data.AsSingleSegment());
			Utf8String[] paths = reader.ReadVariableLengthArray(() => reader.ReadUtf8String());

			return new CommitNode(paths, children[0]);
		}

		/// <inheritdoc/>
		public override IReadOnlyList<TreeNodeRef> GetReferences()
		{
			List<TreeNodeRef> refs = new List<TreeNodeRef>();
			refs.Add(Contents);
			return refs;
		}
	}

	class CommitNodeSerializer : TreeNodeSerializer<CommitNode>
	{
		/// <inheritdoc/>
		public override ValueTask<CommitNode> DeserializeAsync(ITreeBlob node, CancellationToken cancellationToken)
			=> CommitNode.DeserializeAsync(node, cancellationToken);

		/// <inheritdoc/>
		public override ValueTask<ITreeBlob> SerializeAsync(ITreeBlobWriter writer, CommitNode node, CancellationToken cancellationToken)
			=> node.SerializeAsync(writer, cancellationToken);
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
				await StartContentReplicationAsync();
			}
		}

		/// <inheritdoc/>
		public async Task StopAsync(CancellationToken cancellationToken)
		{
			if (Options.Content)
			{
				await StopContentReplicationAsync();
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
				IStream? stream = await _streamCollection.GetAsync(streamId);
				if (stream == null || stream.ReplicationMode == ContentReplicationMode.None)
				{
					// Remove all but the last item
					await streamChanges.TrimAsync(0, -2);
				}
				else
				{
					// Update the stream, updating the reservation every 30 seconds
					Task internalTask = Task.Run(() => UpdateStreamContentInternalAsync(stream, streamChanges, cancellationToken), cancellationToken);
					while (!internalTask.IsCompleted)
					{
						Task delayTask = Task.Delay(TimeSpan.FromSeconds(60.0), cancellationToken);
						if (await Task.WhenAny(internalTask, delayTask) == delayTask)
						{
							DateTime newTime = DateTime.UtcNow + TimeSpan.FromSeconds(90.0);
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

		async Task UpdateStreamContentInternalAsync(IStream stream, RedisList<int> changes, CancellationToken cancellationToken)
		{
			int prevChange = 0;
			CommitNode prevContents = new CommitNode();
			for (; ; )
			{
				// Get the first two changes to be mirrored. The first one should already exist, unless it's the start of replication for this stream.
				int[] values = await changes.RangeAsync(0, 1);
				if(values.Length == 0)
				{
					break;
				}

				// Get or add the tree for the first change.
				if (prevChange != values[0])
				{
					RefName prevRefName = GetRefName(stream, values[0]);

					CommitNode? contents = await _treeStore.TryReadTreeAsync<CommitNode>(prevRefName, cancellationToken);
					if (contents == null)
					{
						_logger.LogInformation("No content for CL {Change}; creating full snapshot", values[0]);
						prevContents = await WriteCommitTreeAsync(stream, values[0], 0, new CommitNode(), cancellationToken);
					}
					else
					{
						_logger.LogInformation("Reading existing commit tree for CL {Change} from ref {RefName}", values[0], prevRefName);
						prevContents = contents;
					}

					prevChange = values[0];
				}
				else if (values.Length == 2)
				{
					// Perform a snapshot of the new change, then remove it from the list
					prevContents = await WriteCommitTreeAsync(stream, values[1], prevChange, prevContents, cancellationToken);
					prevChange = values[1];

					// Remove the first item from the list
					ITransaction transaction = _redis.CreateTransaction();
					transaction.AddCondition(Condition.ListIndexEqual(changes.Key, 0, values[0]));
					transaction.AddCondition(Condition.ListIndexEqual(changes.Key, 1, values[1]));
					_ = transaction.With(changes).LeftPopAsync();
					await transaction.ExecuteAsync();
				}
				else
				{
					// Nothing to do
					break;
				}
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
		static RefName GetRefName(StreamId streamId, int change, string? filter, bool revisionsOnly)
		{
			StringBuilder builder = new StringBuilder($"v2/{streamId}/{change}");
			if (filter != null)
			{
				builder.Append($"_filter_{IoHash.Compute(Encoding.UTF8.GetBytes(filter))}");
			}
			if (revisionsOnly)
			{
				builder.Append("_revs");
			}
			return new RefName(builder.ToString());
		}

		/// <summary>
		/// Gets the ref for a particular change
		/// </summary>
		/// <param name="stream"></param>
		/// <param name="change"></param>
		/// <returns></returns>
		static RefName GetRefName(IStream stream, int change)
		{
			return GetRefName(stream.Id, change, stream.ReplicationFilter, stream.ReplicationMode == ContentReplicationMode.RevisionsOnly);
		}

		/// <summary>
		/// Reads a tree from the given stream
		/// </summary>
		/// <param name="stream"></param>
		/// <param name="change"></param>
		/// <param name="filter"></param>
		/// <param name="revisionsOnly"></param>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		public async Task<CommitNode> ReadCommitTreeAsync(IStream stream, int change, string? filter, bool revisionsOnly, CancellationToken cancellationToken)
		{
			RefName name = GetRefName(stream.Id, change, filter, revisionsOnly);
			return await _treeStore.ReadTreeAsync<CommitNode>(name, cancellationToken);
		}

		/// <summary>
		/// Replicates the contents of a stream to Horde storage, optionally using the given change as a starting point
		/// </summary>
		/// <param name="stream">The stream to replicate</param>
		/// <param name="change">Commit to store the tree ref</param>
		/// <param name="baseChange">The base change to update from</param>
		/// <param name="baseContents">Initial contents of the tree at baseChange</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Root tree object</returns>
		public async Task<CommitNode> WriteCommitTreeAsync(IStream stream, int change, int baseChange, CommitNode baseContents, CancellationToken cancellationToken)
		{
			bool revisionsOnly = stream.ReplicationMode == ContentReplicationMode.RevisionsOnly;
			return await WriteCommitTreeAsync(stream, change, baseChange, baseContents, stream.ReplicationFilter, revisionsOnly, cancellationToken);
		}

		/// <summary>
		/// Sorts paths by their folder, then by their filename.
		/// </summary>
		class FolderFirstSorter : IComparer<Utf8String>
		{
			public static FolderFirstSorter Instance { get; } = new FolderFirstSorter();

			public int Compare(Utf8String x, Utf8String y)
			{
				Utf8String pathX = x.Substring(0, x.LastIndexOf('/') + 1);
				Utf8String pathY = y.Substring(0, y.LastIndexOf('/') + 1);

				int result = pathX.CompareTo(pathY);
				if (result != 0)
				{
					return result;
				}

				return x.CompareTo(y);
			}
		}

		/// <summary>
		/// Replicates the contents of a stream to Horde storage, optionally using the given change as a starting point
		/// </summary>
		/// <param name="stream">The stream to replicate</param>
		/// <param name="change">Commit to store the tree ref</param>
		/// <param name="baseChange">The base change to update from</param>
		/// <param name="baseContents">Initial contents of the tree at baseChange</param>
		/// <param name="filter"></param>
		/// <param name="revisionsOnly"></param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Root tree object</returns>
		public async Task<CommitNode> WriteCommitTreeAsync(IStream stream, int change, int? baseChange, CommitNode baseContents, string? filter, bool revisionsOnly, CancellationToken cancellationToken)
		{
			// Create a client to replicate from this stream
			ReplicationClient clientInfo = await FindOrAddReplicationClientAsync(stream);

			// Connect to the server and flush the workspace
			using IPerforceConnection perforce = await PerforceConnection.CreateAsync(clientInfo.Settings, _logger);
			await FlushWorkspaceAsync(clientInfo, perforce, baseChange ?? 0);

			// Apply all the updates
			_logger.LogInformation("Updating client {Client} from changelist {BaseChange} to {Change}", clientInfo.Client.Name, baseChange ?? 0, change);
			clientInfo.Change = -1;

			Utf8String clientRoot = new Utf8String(clientInfo.Client.Root);
			string filterOrDefault = filter ?? "...";
			string queryPath = $"//{clientInfo.Client.Name}/{filterOrDefault}";

			RefName refName = GetRefName(stream.Id, change, filter, revisionsOnly);
			RefName incRefName = new RefName($"{refName}_inc");

			// Get the current sync state for this change
			CommitNode? syncNode = await _treeStore.TryReadTreeAsync<CommitNode>(incRefName, cancellationToken);
			bool deleteIncRef = syncNode != null;
			if (syncNode == null)
			{
				DirectoryNode contents = await baseContents.Contents.ExpandAsync(cancellationToken);
				syncNode = new CommitNode(contents);
			}

			// Get the contents of the current tree
			DirectoryNode root = await syncNode.Contents.ExpandAsync(cancellationToken);

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

					FileEntry entry = await root.AddFileByPathAsync(path, FileEntryFlags.PerforceDepotPathAndRevision, cancellationToken);
					await entry.AppendAsync(dataStream, Options.Chunking, cancellationToken);

					if (++count > 1000)
					{
						await _treeStore.WriteTreeAsync(refName, syncNode, false, cancellationToken);
						count = 0;
					}
				}
			}
			else
			{
				// Replay the files that have already been synced
				foreach (Utf8String path in syncNode.Paths)
				{
					string flushPath = $"//{clientInfo.Client.Name}/{path}@{change}";
					_logger.LogInformation("Flushing {FlushPath}", flushPath);
					await perforce.SyncQuietAsync(SyncOptions.Force | SyncOptions.KeepWorkspaceFiles, -1, flushPath, cancellationToken);
				}

				// Do a sync preview to find everything that's left, and sort the remaining list of paths
				List<(Utf8String Path, long Size)> files = new List<(Utf8String, long)>();
				await foreach (PerforceResponse<SyncRecord> response in perforce.StreamCommandAsync<SyncRecord>("sync", new[] { "-n" }, new string[] { $"{queryPath}@{change}" }, null, cancellationToken))
				{
					PerforceError? error = response.Error;
					if (error != null)
					{
						_logger.LogWarning("Perforce: {Message}", error.Data);
						continue;
					}

					Utf8String file = response.Data.Path.Clone();
					if (!file.StartsWith(clientRoot, Utf8StringComparer.Ordinal))
					{
						throw new ArgumentException($"Unable to make path {file} relative to client root {clientRoot}");
					}

					byte[] path = new byte[file.Length - clientRoot.Length];
					for (int idx = clientRoot.Length; idx < file.Length; idx++)
					{
						path[idx - clientRoot.Length] = (file[idx] == '\\') ? (byte)'/' : file[idx];
					}

					files.Add((new Utf8String(path), response.Data.FileSize));
				}
				files.SortBy(x => x.Path, FolderFirstSorter.Instance);

				// Sync incrementally
				long totalSize = files.Sum(x => x.Size);
				long syncedSize = 0;
				while (files.Count > 0)
				{
					// Save the incremental state
					if (syncedSize > 0)
					{
						await _treeStore.WriteTreeAsync(incRefName, syncNode, cancellationToken: cancellationToken);
						deleteIncRef = true;
					}

					// Find the next path to sync
					const long MaxBatchSize = 1024 * 1024 * 1024;
					(int idx, Utf8String path, long size) = GetSyncBatch(files, MaxBatchSize);
					syncedSize += size;

					const long TrimInterval = 1024 * 1024 * 256;

					long dataSize = 0;
					long trimDataSize = TrimInterval;

					string syncPath = $"//{clientInfo.Client.Name}/{path}@{change}";
					double syncPct = (syncedSize * 100.0) / Math.Max(totalSize, 1L);
					_logger.LogInformation("Syncing {StreamId} to {Change} [{SyncPct:n1}%]: {Path} ({Size:n0} bytes)", stream.Id, change, syncPct, path, size);

					Dictionary<int, FileEntry> handles = new Dictionary<int, FileEntry>();
					await foreach (PerforceResponse response in perforce.StreamCommandAsync("sync", Array.Empty<string>(), new string[] { syncPath }, null, typeof(SyncRecord), true, default))
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
								Utf8String file = GetClientRelativePath(io.Payload, clientInfo.Client.Root);
								handles[io.File] = await root.AddFileByPathAsync(file, FileEntryFlags.None, cancellationToken);
							}
							else if (io.Command == PerforceIoCommand.Write)
							{
								FileEntry entry = handles[io.File];
								await entry.AppendAsync(io.Payload, Options.Chunking, cancellationToken);
								dataSize += io.Payload.Length;
							}
							else if (io.Command == PerforceIoCommand.Close)
							{
								handles.Remove(io.File);
							}
							else if (io.Command == PerforceIoCommand.Unlink)
							{
								Utf8String file = GetClientRelativePath(io.Payload, clientInfo.Client.Root);
								await root.DeleteFileByPathAsync(file, cancellationToken);
							}
							else
							{
								_logger.LogWarning("Unhandled command code {Code}", io.Command);
							}

							if (dataSize > trimDataSize)
							{
								_logger.LogInformation("Trimming working set after receiving {NumBytes:n0}mb...", dataSize / (1024 * 1024));
								await _treeStore.WriteTreeAsync(refName, syncNode, false, cancellationToken);
								GC.Collect();
								trimDataSize = dataSize + TrimInterval;
								_logger.LogInformation("Trimming complete. Next trim at {NextNumBytes:n0}mb.", trimDataSize / (1024 * 1024));
							}
						}
					}

					// Update the root sync node
					syncNode.Paths.Add(path);
					files.RemoveRange(idx, files.Count - idx);
				}
			}

			clientInfo.Change = change;

			// Return the new root object
			_logger.LogInformation("Writing ref {RefId} for {StreamId} change {Change}", refName, stream.Id, change);
			await _treeStore.WriteTreeAsync(refName, syncNode, true, cancellationToken);

			// Delete the incremental state
			if (deleteIncRef)
			{
				await _treeStore.DeleteTreeAsync(incRefName, cancellationToken);
			}
			return syncNode;
		}

		static (int, Utf8String, long) GetSyncBatch(List<(Utf8String Path, long Size)> files, long maxSize)
		{
			int idx = files.Count - 1;
			Utf8String path = files[idx].Path;
			long size = files[idx].Size;

			Utf8String prefix = path;
			while (prefix.Length > 0)
			{
				// Get the length of the parent directory
				int endIdx = prefix.Length - 1;
				while (endIdx > 0 && path[endIdx - 1] != (byte)'/')
				{
					endIdx--;
				}

				// Get the parent directory prefix, ending with a slash
				prefix = prefix.Substring(0, endIdx);

				// Include all files with this prefix
				int nextIdx = idx;
				long nextSize = size;
				for (; nextIdx > 0 && files[nextIdx - 1].Path.StartsWith(prefix); nextIdx--)
				{
					nextSize += files[nextIdx - 1].Size;
					if (nextSize > maxSize)
					{
						return (idx, path, size);
					}
				}
				size = nextSize;
				idx = nextIdx;

				// Update the sync path to be the prefixed directory
				path = prefix + "...";
			}

			return (idx, path, size);
		}

		async Task FlushWorkspaceAsync(ReplicationClient clientInfo, IPerforceConnection perforce, int change)
		{
			if (clientInfo.Change != change)
			{
				clientInfo.Change = -1;
				if (change == 0)
				{
					_logger.LogInformation("Flushing have table for {Client}", clientInfo.Client.Name);
					await perforce.SyncQuietAsync(SyncOptions.Force | SyncOptions.KeepWorkspaceFiles, -1, $"//{clientInfo.Client.Name}/...#0");
				}
				else
				{
					_logger.LogInformation("Flushing have table for {Client} to change {Change}", clientInfo.Client.Name, change);
					await perforce.SyncQuietAsync(SyncOptions.Force | SyncOptions.KeepWorkspaceFiles, -1, $"//{clientInfo.Client.Name}/...@{change}");
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

		#endregion
	}
}
