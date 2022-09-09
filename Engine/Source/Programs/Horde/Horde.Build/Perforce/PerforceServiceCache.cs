// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Perforce;
using Horde.Build.Server;
using Horde.Build.Streams;
using Horde.Build.Users;
using Horde.Build.Utilities;
using HordeCommon;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Bson.Serialization.Options;
using MongoDB.Driver;

namespace Horde.Build.Perforce
{
	using StreamId = StringId<IStream>;
	using UserId = ObjectId<IUser>;

	/// <summary>
	/// Options for the commit service
	/// </summary>
	public class CommitServiceOptions
	{
		/// <summary>
		/// Whether to mirror commit metadata to the database
		/// </summary>
		public bool Enable { get; set; } = true;
	}

	/// <summary>
	/// Service which mirrors changes from Perforce
	/// </summary>
	class PerforceServiceCache : PerforceService, IHostedService
	{
		[SingletonDocument("commit-cache")]
		class CacheState : SingletonBase
		{
			[BsonDictionaryOptions(DictionaryRepresentation.ArrayOfDocuments)]
			public Dictionary<string, ClusterState> Clusters { get; set; } = new Dictionary<string, ClusterState>();
		}

		class ClusterState
		{
			public int MaxChange { get; set; }

			[BsonDictionaryOptions(DictionaryRepresentation.ArrayOfDocuments)]
			public Dictionary<StreamId, int> MinChanges { get; set; } = new Dictionary<StreamId, int>();
		}

		class CommitTagInfo
		{
			public CommitTag Name { get; }
			public FileFilter Filter { get; }

			public CommitTagInfo(CommitTagConfig config)
			{
				Name = config.Name;
				Filter = config.CreateFileFilter();
			}
		}

		class StreamInfo
		{
			public IStream Stream { get; set; }
			public PerforceViewMap View { get; }
			public List<CommitTagInfo> CommitTags { get; set; }

			public StreamInfo(IStream stream, PerforceViewMap view)
			{
				Stream = stream;
				View = view;
				CommitTags = stream.Config.GetAllCommitTags().Select(x => new CommitTagInfo(x)).ToList();
			}
		}

		class ClusterTicker
		{
			public string ClusterName { get; }
			public Task<ClusterState?>? Task { get; set; }
			public Stopwatch Timer { get; }

			public ClusterTicker(string name)
			{
				ClusterName = name;
				Timer = Stopwatch.StartNew();
			}
		}

		class CachedCommitDoc : ICommit
		{
			[BsonIgnore]
			PerforceServiceCache _owner = null!;

			[BsonIgnore]
			IStream _stream = null!;

			[BsonIgnoreIfDefault] // Allow upserts
			public ObjectId Id { get; set; }

			public StreamId StreamId { get; set; }
			public int Number { get; set; }
			public int OriginalChange { get; set; }
			public UserId AuthorId { get; set; }
			public UserId OwnerId { get; set; }
			public string Description { get; set; }
			public string BasePath { get; set; }
			public DateTime DateUtc { get; set; }

			public List<CommitTag> CommitTags { get; set; } = new List<CommitTag>();

			[BsonConstructor]
			CachedCommitDoc()
			{
				Description = null!;
				BasePath = null!;

				CommitTags = new List<CommitTag>();
			}

			public CachedCommitDoc(ICommit commit, List<CommitTag> commitTags)
			{
				StreamId = commit.StreamId;
				Number = commit.Number;
				OriginalChange = commit.OriginalChange;
				AuthorId = commit.AuthorId;
				OwnerId = commit.OwnerId;
				Description = commit.Description;
				BasePath = commit.BasePath;
				DateUtc = commit.DateUtc;

				CommitTags = commitTags;
			}

			public void PostLoad(PerforceServiceCache owner, IStream stream)
			{
				_owner = owner;
				_stream = stream;
			}

			public ValueTask<IReadOnlyList<CommitTag>> GetTagsAsync(CancellationToken cancellationToken)
			{
				return new ValueTask<IReadOnlyList<CommitTag>>(CommitTags);
			}

			public async ValueTask<IReadOnlyList<string>> GetFilesAsync(CancellationToken cancellationToken)
			{
				ICommit? other = await _owner!.GetChangeDetailsAsync(_stream, Number, cancellationToken);
				if (other == null)
				{
					return Array.Empty<string>();
				}
				return await other.GetFilesAsync(cancellationToken);
			}
		}

		public CommitServiceOptions Options { get; }
		readonly MongoService _mongoService;
		readonly IMongoCollection<CachedCommitDoc> _commits;
		readonly IStreamCollection _streamCollection;
		readonly ILogger _logger;
		readonly ITicker _updateCommitsTicker;

		/// <summary>
		/// Constructor
		/// </summary>
		public PerforceServiceCache(PerforceLoadBalancer loadBalancer, MongoService mongoService, IUserCollection userCollection, IStreamCollection streamCollection, IClock clock, IOptions<ServerSettings> settings, IOptions<CommitServiceOptions> options, ILogger<PerforceServiceCache> logger)
			: base(loadBalancer, mongoService, userCollection, settings, logger)
		{
			Options = options.Value;

			_mongoService = mongoService;

			List<MongoIndex<CachedCommitDoc>> indexes = new List<MongoIndex<CachedCommitDoc>>();
			indexes.Add(MongoIndex.Create<CachedCommitDoc>(keys => keys.Ascending(x => x.StreamId).Descending(x => x.Number), true));
			indexes.Add(MongoIndex.Create<CachedCommitDoc>(keys => keys.Ascending(x => x.StreamId).Ascending(x => x.CommitTags).Descending(x => x.Number), true));
			_commits = mongoService.GetCollection<CachedCommitDoc>("CommitsV2", indexes);

			_streamCollection = streamCollection;
			_logger = logger;
			_updateCommitsTicker = clock.AddSharedTicker<PerforceServiceCache>(TimeSpan.FromSeconds(10.0), UpdateCommitsAsync, logger);
		}

		/// <inheritdoc/>
		public override void Dispose()
		{
			base.Dispose();

			_updateCommitsTicker.Dispose();
		}

		/// <inheritdoc/>
		public async Task StartAsync(CancellationToken cancellationToken)
		{
			if (Options.Enable)
			{
				await _updateCommitsTicker.StartAsync();
			}
		}

		/// <inheritdoc/>
		public async Task StopAsync(CancellationToken cancellationToken)
		{
			if (Options.Enable)
			{
				await _updateCommitsTicker.StopAsync();
			}
		}

		#region Commit updates

		/// <summary>
		/// Polls Perforce for submitted changes
		/// </summary>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		async ValueTask UpdateCommitsAsync(CancellationToken cancellationToken)
		{
			CacheState state = await _mongoService.GetSingletonAsync<CacheState>();

			// Get the current list of streams and their views
			Dictionary<string, List<StreamInfo>> clusters = await CreateStreamInfoAsync(cancellationToken);

			// Task for updating the list of clusters periodically
			Task<Dictionary<string, List<StreamInfo>>>? clusterTask = null;
			Stopwatch clusterTimer = Stopwatch.StartNew();

			// Poll each cluster
			List<ClusterTicker> tickers = new List<ClusterTicker>();

			for (; ; )
			{
				// Update the background task for refreshing the list of clusters
				if(clusterTask != null && clusterTask.IsCompleted)
				{
					clusters = await clusterTask;
					clusterTask = null;
				}
				if (clusterTask == null && clusterTimer.Elapsed > TimeSpan.FromSeconds(30.0))
				{
					clusterTask = Task.Run(() => CreateStreamInfoAsync(cancellationToken), cancellationToken);
					clusterTimer.Restart();
				}

				// Remove any state for clusters that are no longer valid
				bool updateState = false;
				foreach (string clusterName in state.Clusters.Keys)
				{
					if (!clusters.ContainsKey(clusterName))
					{
						state.Clusters.Remove(clusterName);
						updateState = true;
					}
				}

				// Make sure there's a ticker for every cluster
				foreach (string clusterName in clusters.Keys)
				{
					if (!tickers.Any(x => x.ClusterName.Equals(clusterName, StringComparison.OrdinalIgnoreCase)))
					{
						ClusterTicker ticker = new ClusterTicker(clusterName);
						tickers.Add(ticker);
					}
				}

				// Check if it's time to update any tickers
				for (int idx = 0; idx < tickers.Count; idx++)
				{
					ClusterTicker ticker = tickers[idx];
					if (ticker.Task != null && ticker.Task.IsCompleted)
					{
						ClusterState? clusterState = await ticker.Task;
						if (clusterState != null)
						{
							state.Clusters[ticker.ClusterName] = clusterState;
							updateState = true;
						}
						ticker.Task = null;
					}
					if (ticker.Task == null)
					{
						List<StreamInfo>? streams;
						if (!clusters.TryGetValue(ticker.ClusterName, out streams))
						{
							tickers.RemoveAt(idx--);
							continue;
						}

						ClusterState? clusterState;
						if (!state.Clusters.TryGetValue(ticker.ClusterName, out clusterState))
						{
							clusterState = new ClusterState();
						}

						ticker.Task = Task.Run(() => UpdateClusterAsync(ticker.ClusterName, streams, clusterState, cancellationToken));
					}
				}

				// Apply any updates to the global state
				if (updateState)
				{
					if (!await _mongoService.TryUpdateSingletonAsync(state))
					{
						state = await _mongoService.GetSingletonAsync<CacheState>();
					}
				}

				// Wait before performing the next poll
				await Task.Delay(TimeSpan.FromSeconds(2.0), cancellationToken);
			}
		}

		async Task<Dictionary<string, List<StreamInfo>>> CreateStreamInfoAsync(CancellationToken cancellationToken)
		{
			List<IStream> streams = await _streamCollection.FindAllAsync();

			Dictionary<string, List<StreamInfo>> clusters = new Dictionary<string, List<StreamInfo>>(StringComparer.OrdinalIgnoreCase);
			foreach (IGrouping<string, IStream> group in streams.GroupBy(x => x.Config.ClusterName, StringComparer.OrdinalIgnoreCase))
			{
				List<StreamInfo> streamInfoList = await CreateStreamInfoForClusterAsync(group.Key, group, cancellationToken);
				clusters[group.Key] = streamInfoList;
			}

			return clusters;
		}

		async Task<List<StreamInfo>> CreateStreamInfoForClusterAsync(string clusterName, IEnumerable<IStream> streams, CancellationToken cancellationToken)
		{
			using (IPooledPerforceConnection perforce = await ConnectAsync(clusterName, null, cancellationToken))
			{
				List<StreamInfo> streamInfoList = new List<StreamInfo>();
				foreach (IStream stream in streams)
				{
					StreamRecord record = await perforce.GetStreamAsync(stream.Name, true, cancellationToken);
					PerforceViewMap view = PerforceViewMap.Parse(record.View);
					streamInfoList.Add(new StreamInfo(stream, view));
				}
				return streamInfoList;
			}
		}

		async Task<ClusterState?> UpdateClusterAsync(string clusterName, List<StreamInfo> streamInfos, ClusterState state, CancellationToken cancellationToken)
		{
			const int MaxChanges = 100;

			using (IPooledPerforceConnection perforce = await ConnectAsync(clusterName, null, cancellationToken))
			{
				// Get the changelist range to query
				FileSpecList spec = FileSpecList.Any;
				if (state.MaxChange > 0)
				{
					spec = $"@{state.MaxChange + 1},@now";
				}

				// Find the changes within that range, and abort if there's nothing new
				List<ChangesRecord> changes = await perforce.GetChangesAsync(ChangesOptions.None, MaxChanges, ChangeStatus.Submitted, spec, cancellationToken);
				if (changes.Count == 0)
				{
					return null;
				}

				// Get the server information
				InfoRecord info = await perforce.GetInfoAsync(cancellationToken);

				// Create a buffer for files
				List<string> files = new List<string>();

				// Describe the changes and create records for them
				List<DescribeRecord> describeRecords = await perforce.DescribeAsync(changes.Select(x => x.Number).ToArray(), cancellationToken);
				foreach (StreamInfo streamInfo in streamInfos)
				{
					foreach (DescribeRecord describeRecord in describeRecords)
					{
						files.Clear();
						foreach (DescribeFileRecord describeFile in describeRecord.Files)
						{
							string relativePath;
							if (streamInfo.View.TryMapFile(describeFile.DepotFile, info.PathComparison, out relativePath))
							{
								files.Add(relativePath);
							}
						}

						if (files.Count > 0)
						{
							ICommit commit = await CreateCommitAsync(streamInfo.Stream, describeRecord, info, cancellationToken);

							List<CommitTag> commitTags = new List<CommitTag>();
							foreach (CommitTagInfo commitTagInfo in streamInfo.CommitTags)
							{
								if (commitTagInfo.Filter.ApplyTo(files.Select(x => "/" + x)).Any())
								{
									commitTags.Add(commitTagInfo.Name);
								}
							}

							CachedCommitDoc commitDoc = new CachedCommitDoc(commit, commitTags);
							FilterDefinition<CachedCommitDoc> filter = Builders<CachedCommitDoc>.Filter.Expr(x => x.StreamId == commit.StreamId && x.Number == commit.Number);
							await _commits.ReplaceOneAsync(filter, commitDoc, new ReplaceOptions { IsUpsert = true }, cancellationToken);
						}
					}
				}

				// Update the cache state
				Dictionary<StreamId, int> minChanges = new Dictionary<StreamId, int>(streamInfos.Count);
				foreach (StreamInfo streamInfo in streamInfos)
				{
					int minChange;
					if (!state.MinChanges.TryGetValue(streamInfo.Stream.Id, out minChange))
					{
						minChange = changes[^1].Number;
					}
					minChanges.Add(streamInfo.Stream.Id, minChange);
				}
				state.MinChanges = minChanges;
				state.MaxChange = changes[0].Number;
				return state;
			}
		}

		#endregion

		#region ICommitSource implementation

		/// <summary>
		/// Returns commits within the cached range
		/// </summary>
		class CachedCommitSource : CommitSource
		{
			readonly PerforceServiceCache _owner;

			public CachedCommitSource(PerforceServiceCache owner, IStream stream)
				: base(owner, stream)
			{
				_owner = owner;
			}

			public override async IAsyncEnumerable<ICommit> FindAsync(int? minChange, int? maxChange, int? maxResults, IReadOnlyList<CommitTag>? tags, [EnumeratorCancellation] CancellationToken cancellationToken = default)
			{
				CacheState state = await _owner._mongoService.GetSingletonAsync<CacheState>();
				if (state.Clusters.TryGetValue(_stream.Config.ClusterName, out ClusterState? clusterState))
				{
					int minReplicatedChange;
					if (clusterState.MinChanges.TryGetValue(_stream.Id, out minReplicatedChange) && (maxChange == null || maxChange > minReplicatedChange))
					{
						FilterDefinition<CachedCommitDoc> filter = Builders<CachedCommitDoc>.Filter.Eq(x => x.StreamId, _stream.Id);

						if (tags != null && tags.Count > 0)
						{
							if (tags.Count == 1)
							{
								filter &= Builders<CachedCommitDoc>.Filter.AnyEq(x => x.CommitTags, tags[0]);
							}
							else
							{
								filter &= Builders<CachedCommitDoc>.Filter.AnyIn(x => x.CommitTags, tags);
							}
						}

						if (maxChange != null)
						{
							filter &= Builders<CachedCommitDoc>.Filter.Lte(x => x.Number, Math.Min(maxChange.Value, clusterState.MaxChange));
						}
						else
						{
							filter &= Builders<CachedCommitDoc>.Filter.Lte(x => x.Number, clusterState.MaxChange);
						}

						if (minChange != null)
						{
							filter &= Builders<CachedCommitDoc>.Filter.Gte(x => x.Number, Math.Max(minChange.Value, minReplicatedChange));
						}
						else
						{
							filter &= Builders<CachedCommitDoc>.Filter.Gte(x => x.Number, minReplicatedChange);
						}

						int numResults = 0;
						using (IAsyncCursor<CachedCommitDoc> cursor = await _owner._commits.Find(filter).SortByDescending(x => x.Number).Limit(maxResults).ToCursorAsync(cancellationToken))
						{
							while (await cursor.MoveNextAsync(cancellationToken))
							{
								foreach (CachedCommitDoc commit in cursor.Current)
								{
									commit.PostLoad(_owner, _stream);
									yield return commit;
									numResults++;
								}
							}
						}

						if (maxResults != null)
						{
							maxResults = maxResults.Value - numResults;
						}
					}
				}

				if (maxResults == null || maxResults > 0)
				{
					await foreach (ICommit commit in base.FindAsync(minChange, maxChange, maxResults, tags, cancellationToken))
					{
						yield return commit;
					}
				}
			}

			public override async Task<ICommit> GetAsync(int changeNumber, CancellationToken cancellationToken = default)
			{
				CachedCommitDoc? commit = await _owner._commits.Find(x => x.StreamId == _stream.Id && x.Number == changeNumber).FirstOrDefaultAsync(cancellationToken);
				if(commit != null)
				{
					commit.PostLoad(_owner, _stream);
					return commit;
				}
				return await base.GetAsync(changeNumber, cancellationToken);
			}
		}

		/// <inheritdoc/>
		public override ICommitCollection GetCommits(IStream stream)
		{
			return new CachedCommitSource(this, stream);
		}

		#endregion
	}
}
