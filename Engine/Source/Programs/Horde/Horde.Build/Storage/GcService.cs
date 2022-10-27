// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Storage;
using Microsoft.Extensions.Logging;
using Horde.Build.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using Microsoft.Extensions.Hosting;
using Horde.Build.Server;
using HordeCommon;
using EpicGames.Redis.Utility;
using StackExchange.Redis;
using MongoDB.Driver;
using System.Runtime.CompilerServices;
using System.IO;
using System.Diagnostics;
using System.Buffers;

namespace Horde.Build.Storage
{
	[DebuggerDisplay("Index={BaseIndex}, Count={Hashes.Count}")]
	class GcPage
	{
		public const int MaxItems = 1000;

		[BsonId, BsonIgnoreIfDefault]
		public ObjectId Id { get; set; }

		[BsonElement("cyc")]
		public int Cycle { get; set; }

		[BsonElement("base")]
		public int BaseIndex { get; set; }

		[BsonElement("read")]
		public int ReadIndex { get; set; }

		[BsonElement("hash")]
		public List<IoHash> Hashes { get; set; } = new List<IoHash>();

		public int HeadIndex => BaseIndex + Hashes.Count;

		public GcPage()
		{
		}

		public GcPage(int cycle, int baseIndex, int readIndex)
		{
			Cycle = cycle;
			BaseIndex = baseIndex;
			ReadIndex = readIndex;
		}
	}

	[DebuggerDisplay("{Id}: {Locator}")]
	class GcNode
	{
		// Hash of the namespace id and blob id, as a string.
		public IoHash Id { get; set; }

		[BsonElement("l")]
		public BlobLocator Locator { get; set; }

		[BsonElement("r")]
		public List<IoHash>? Imports { get; set; }

		[BsonElement("t")]
		public DateTime Time { get; set; }
	}

	class GcSession
	{
		readonly IStorageClientImpl _store;
		readonly NamespaceId _namespaceId;
		readonly Utf8String _namespaceIdUtf8;
		readonly IMongoCollection<GcPage> _pageCollection;
		readonly IMongoCollection<GcNode> _nodeCollection;
		readonly int _cycle;
		readonly ILogger _logger;
		readonly HashSet<IoHash> _reachable = new HashSet<IoHash>();
		readonly DateTime _startTimeUtc;
		readonly DateTime _deleteTimeUtc;

		GcPage _writePage;

		public GcSession(IStorageClientImpl store, NamespaceId namespaceId, IMongoCollection<GcPage> pageCollection, IMongoCollection<GcNode> nodeCollection, int cycle, DateTime startTimeUtc, ILogger logger)
		{
			_store = store;
			_namespaceId = namespaceId;
			_namespaceIdUtf8 = new Utf8String(namespaceId.ToString());
			_pageCollection = pageCollection;
			_nodeCollection = nodeCollection;
			_cycle = cycle;
			_startTimeUtc = startTimeUtc;
			_deleteTimeUtc = startTimeUtc - TimeSpan.FromHours(store.Config.GcDelayHrs);
			_logger = logger;

			_writePage = new GcPage(_cycle, 0, -1);
		}

		public async Task RecoverAsync(CancellationToken cancellationToken)
		{
			int expectedBaseIndex = 0;
			await foreach (GcPage page in _pageCollection.Find(x => x.Cycle == _cycle, new FindOptions { BatchSize = 1 }).SortBy(x => x.BaseIndex).ToAsyncEnumerable(cancellationToken))
			{
				if (page.BaseIndex != expectedBaseIndex)
				{
					break;
				}

				_reachable.UnionWith(page.Hashes);
				_writePage = new GcPage(_cycle, page.BaseIndex + page.Hashes.Count, page.ReadIndex);

				expectedBaseIndex = page.BaseIndex + page.Hashes.Count;
			}
		}

		public async Task RunAsync(CancellationToken cancellationToken)
		{
			// Attempt to recover the current state
			await RecoverAsync(cancellationToken);

			// Find the root set
			if (_writePage.ReadIndex < 0)
			{
				// Add all the nodes to the root set
				await using (IAsyncEnumerator<NodeLocator> enumerator = _store.Refs.EnumerateRefs(_namespaceId, cancellationToken).GetAsyncEnumerator(cancellationToken))
				{
					Task<bool> moveNextTask = enumerator.MoveNextAsync(cancellationToken).AsTask();
					while (await moveNextTask)
					{
						List<WriteModel<GcNode>> requests = new List<WriteModel<GcNode>>();
						while (requests.Count < 20 && await moveNextTask)
						{
							NodeLocator locator = enumerator.Current;

							IoHash hash = GetBlobDataId(locator.Blob.BlobId);
							if (await WriteHashAsync(hash, cancellationToken))
							{
								requests.Add(CreateGcNodeUpsert(hash, locator.Blob));
							}

							moveNextTask = enumerator.MoveNextAsync(cancellationToken).AsTask();
						}

						if (requests.Count > 0)
						{
							await _nodeCollection.BulkWriteAsync(requests, new BulkWriteOptions { IsOrdered = false }, cancellationToken);
						}
					}
				}

				// Write the last page with a read index of zero to indicate that the root set is complete
				_writePage.ReadIndex = 0;
				await FlushPageAsync(cancellationToken);
			}

			// Find the reachable set
			int readIndex = _writePage.ReadIndex;
			await foreach (ListSegment<GcNode> batch in ReadNodeBatchesAsync(readIndex, 20, cancellationToken).Prefetch(cancellationToken))
			{
				if (batch.Count == 0 && readIndex == _writePage.HeadIndex)
				{
					Debug.Assert(readIndex == _writePage.HeadIndex);
					break;
				}

				foreach (GcNode node in batch)
				{
					foreach (IoHash import in node.Imports!)
					{
						await WriteHashAsync(import, cancellationToken);
					}
					_writePage.ReadIndex = ++readIndex;
				}
			}
			await FlushPageAsync(cancellationToken);

			// Delete any unreferenced nodes
			await foreach (string path in _store.Backend.EnumerateAsync(cancellationToken))
			{
				BlobId blobId;
				if (StorageBackend.TryGetBlobIdFromPath(path, out blobId))
				{
					IoHash hash = GetBlobDataId(blobId);
					if (!_reachable.Contains(hash))
					{
						DeleteResult result = await _nodeCollection.DeleteOneAsync(x => x.Id == hash && x.Time < _deleteTimeUtc, cancellationToken);
						if (result.DeletedCount > 0 || _deleteTimeUtc == _startTimeUtc)
						{
							await _store.Backend.DeleteAsync(path, cancellationToken);
						}
						else
						{
							await _nodeCollection.BulkWriteAsync(new[] { CreateGcNodeUpsert(hash, new BlobLocator(HostId.Empty, blobId)) }, cancellationToken: cancellationToken);
						}
					}
				}
			}
		}

		public async IAsyncEnumerable<ListSegment<GcNode>> ReadNodeBatchesAsync(int readIndex, int maxBatchCount, [EnumeratorCancellation] CancellationToken cancellationToken)
		{
			await foreach (ListSegment<IoHash> batch in ReadHashBatchesAsync(readIndex, maxBatchCount, cancellationToken))
			{
				List<GcNode> nodes = await _nodeCollection.Find(Builders<GcNode>.Filter.In(x => x.Id, batch)).ToListAsync(cancellationToken);
				Dictionary<IoHash, GcNode> hashToNode = nodes.ToDictionary(x => x.Id, x => x);

				List<GcNode> results = new List<GcNode>();
				foreach (IoHash hash in batch)
				{
					GcNode node = hashToNode[hash];
					if (node.Imports == null)
					{
						await FindImportsAsync(node, cancellationToken);
					}
					results.Add(node);
				}

				yield return results;
			}
		}

		public async IAsyncEnumerable<ListSegment<IoHash>> ReadHashBatchesAsync(int readIndex, int maxBatchCount, [EnumeratorCancellation] CancellationToken cancellationToken)
		{
			for (; ; )
			{
				GcPage page = await ReadPageAsync(readIndex, cancellationToken);
				if (readIndex == page.HeadIndex)
				{
					yield return ListSegment.Empty<IoHash>();
				}
				else
				{
					while (readIndex < page.HeadIndex)
					{
						int start = readIndex - page.BaseIndex;
						int count = Math.Min(maxBatchCount, page.Hashes.Count - start);
						yield return page.Hashes.AsSegment(start, count);
						readIndex += count;
					}
				}
			}
		}

		async ValueTask<GcPage> ReadPageAsync(int readIndex, CancellationToken cancellationToken)
		{
			cancellationToken.ThrowIfCancellationRequested();

			GcPage page;
			if (readIndex >= _writePage.BaseIndex)
			{
				page = _writePage;
			}
			else
			{
				page = await _pageCollection.Find(x => x.Cycle == _cycle && x.BaseIndex <= readIndex).SortByDescending(x => x.BaseIndex).FirstAsync(cancellationToken);
			}

			int pageBaseIndex = page.BaseIndex;
			if (readIndex > page.BaseIndex + page.Hashes.Count)
			{
				throw new InvalidDataException();
			}
			return page;
		}

		async Task FindImportsAsync(GcNode node, CancellationToken cancellationToken)
		{
			List<WriteModel<GcNode>> updates = new List<WriteModel<GcNode>>();
			node.Imports = new List<IoHash>();

			BundleHeader? header = await ReadHeaderAsync(_store, node.Locator, cancellationToken);
			if (header != null)
			{
				foreach (BundleImport import in header.Imports)
				{
					IoHash importId = GetBlobDataId(import.Locator.BlobId);
					node.Imports.Add(importId);

					if (!_reachable.Contains(importId))
					{
						updates.Add(CreateGcNodeUpsert(importId, import.Locator));
					}
				}
			}

			updates.Add(new UpdateOneModel<GcNode>(Builders<GcNode>.Filter.Eq(x => x.Id, node.Id), Builders<GcNode>.Update.Set(x => x.Imports, node.Imports)));

			await _nodeCollection.BulkWriteAsync(updates, new BulkWriteOptions { IsOrdered = false }, cancellationToken);
		}

		protected async Task<BundleHeader?> ReadHeaderAsync(IStorageClient store, BlobLocator locator, CancellationToken cancellationToken)
		{
			int fetchSize = 64 * 1024;
			for (; ; )
			{
				using (IMemoryOwner<byte> owner = MemoryPool<byte>.Shared.Rent(fetchSize))
				{
					using (Stream? stream = await store.ReadBlobRangeAsync(locator, 0, fetchSize, cancellationToken))
					{
						if (stream == null)
						{
							_logger.LogError("Unable to read blob {Blob}", locator);
							return null;
						}

						// Read the start of the blob
						Memory<byte> memory = owner.Memory.Slice(0, fetchSize);

						int length = await stream.ReadGreedyAsync(memory, cancellationToken);
						if (length < BundleHeader.PreludeLength)
						{
							_logger.LogError("Blob {Blob} does not have a valid prelude", locator);
							return null;
						}

						memory = memory.Slice(0, length);

						// Make sure it's large enough to hold the header
						int headerSize = BundleHeader.ReadPrelude(memory);
						if (headerSize <= fetchSize)
						{
							return new BundleHeader(new MemoryReader(memory));
						}

						// Increase the fetch size and retry
						fetchSize = headerSize;
					}
				}
			}
		}

		WriteModel<GcNode> CreateGcNodeUpsert(IoHash hash, BlobLocator locator)
		{
			FilterDefinition<GcNode> filter = Builders<GcNode>.Filter.Eq(x => x.Id, hash);
			UpdateDefinition<GcNode> update = Builders<GcNode>.Update.SetOnInsert(x => x.Locator, locator).SetOnInsert(x => x.Time, _startTimeUtc);
			return new UpdateOneModel<GcNode>(filter, update) { IsUpsert = true };
		}

		async ValueTask<bool> WriteHashAsync(IoHash hash, CancellationToken cancellationToken)
		{
			if (_reachable.Add(hash))
			{
				_writePage.Hashes.Add(hash);
				if (_writePage.Hashes.Count == GcPage.MaxItems)
				{
					await FlushPageAsync(cancellationToken);
				}
				return true;
			}
			return false;
		}

		async Task FlushPageAsync(CancellationToken cancellationToken)
		{
			await _pageCollection.ReplaceOneAsync(x => x.Cycle == _writePage.Cycle && x.BaseIndex == _writePage.BaseIndex, _writePage, new ReplaceOptions { IsUpsert = true }, cancellationToken);
			if (_writePage.Hashes.Count > 0)
			{
				_writePage = new GcPage(_cycle, _writePage.BaseIndex + _writePage.Hashes.Count, _writePage.ReadIndex);
			}
		}

		public static IoHash GetBlobDataId(Utf8String namespaceIdUtf8, BlobId blobId)
		{
			Span<byte> buffer = stackalloc byte[namespaceIdUtf8.Length + 1 + blobId.Inner.Length];
			namespaceIdUtf8.Span.CopyTo(buffer);
			blobId.Inner.Span.CopyTo(buffer.Slice(namespaceIdUtf8.Length + 1));
			return IoHash.Compute(buffer);
		}

		IoHash GetBlobDataId(BlobId blobId) => GetBlobDataId(_namespaceIdUtf8, blobId);
	}

	class GcService : IHostedService
	{
		[SingletonDocument("gc")]
		class GcState : SingletonBase
		{
			public int NextCycle { get; set; } = 1;
			public List<GcNamespaceState> Namespaces { get; set; } = new List<GcNamespaceState>();
		}

		class GcNamespaceState
		{
			public NamespaceId Id { get; set; }
			public int Cycle { get; set; }
			public DateTime StartTime { get; set; }
			public DateTime LastStartTime { get; set; }
		}

		readonly MongoService _mongoService;
		readonly RedisService _redisService;
		readonly StorageService _storageService;
		readonly SingletonDocument<GcState> _gcState;
		readonly IClock _clock;
		readonly ITicker _ticker;
		readonly ILogger _logger;
		readonly IMongoCollection<GcNode> _nodeCollection;
		readonly IMongoCollection<GcPage> _pageCollection;

		public GcService(MongoService mongoService, RedisService redisService, StorageService storageService, IClock clock, ILogger<GcService> logger)
		{
			_mongoService = mongoService;
			_redisService = redisService;
			_storageService = storageService;
			_gcState = new SingletonDocument<GcState>(mongoService);
			_clock = clock;
			_ticker = clock.AddTicker<GcService>(TimeSpan.FromMinutes(5.0), TickAsync, logger);
			_logger = logger;
			_nodeCollection = _mongoService.GetCollection<GcNode>("Storage.GcNodes", builder => builder.Ascending(x => x.Time));
			_pageCollection = _mongoService.GetCollection<GcPage>("Storage.GcPages", builder => builder.Ascending(x => x.Cycle).Ascending(x => x.BaseIndex), unique: true);
		}

		/// <inheritdoc/>
		public async Task StartAsync(CancellationToken cancellationToken)
		{
			await _ticker.StartAsync();
		}

		/// <inheritdoc/>
		public async Task StopAsync(CancellationToken cancellationToken)
		{
			await _ticker.StopAsync();
		}

		/// <summary>
		/// Find the next namespace to run GC on
		/// </summary>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		async ValueTask TickAsync(CancellationToken cancellationToken)
		{
			HashSet<NamespaceId> ranNamespaceIds = new HashSet<NamespaceId>();

			DateTime utcNow = DateTime.UtcNow;
			for (; ; )
			{
				// Synchronize the list of configured namespaces with the GC state object
				List<NamespaceConfig> namespaces = await _storageService.GetNamespacesAsync(cancellationToken);

				GcState state = await _gcState.GetAsync();
				if (!Enumerable.SequenceEqual(namespaces.Select(x => x.Id).OrderBy(x => x), state.Namespaces.Select(x => x.Id).OrderBy(x => x)))
				{
					state = await _gcState.UpdateAsync(s => SyncNamespaceList(s, namespaces));
				}

				// Find all the namespaces that need to have GC run on them
				List<(DateTime, GcNamespaceState)> pending = new List<(DateTime, GcNamespaceState)>();
				foreach (GcNamespaceState namespaceState in state.Namespaces)
				{
					NamespaceConfig? config = namespaces.FirstOrDefault(x => x.Id == namespaceState.Id);
					if (config != null)
					{
						DateTime time = namespaceState.LastStartTime + TimeSpan.FromHours(config.GcFrequencyHrs);
						if (time > utcNow)
						{
							pending.Add((time, namespaceState));
						}
					}
				}
				pending.SortBy(x => x.Item1);

				// Update the first one we can acquire a lock for
				foreach ((_, GcNamespaceState namespaceState) in pending)
				{
					NamespaceId namespaceId = namespaceState.Id;
					if (ranNamespaceIds.Add(namespaceId))
					{
						RedisKey key = $"gc/{namespaceState.Id}";
						using (RedisLock namespaceLock = new RedisLock(_redisService.GetDatabase(), key))
						{
							if (await namespaceLock.AcquireAsync(TimeSpan.FromMinutes(20.0)))
							{
								try
								{
									await RunAsync(namespaceState, cancellationToken);
								}
								catch (Exception ex)
								{
									_logger.LogError(ex, "Exception while running garbage collection: {Message}", ex.Message);
								}
								break;
							}
						}
					}
				}
			}
		}

		public async Task RunAsync(NamespaceId namespaceId, CancellationToken cancellationToken)
		{
			GcState state = await _gcState.GetAsync();
			await RunAsync(FindOrAddNamespace(state, namespaceId), cancellationToken);
		}

		async Task RunAsync(GcNamespaceState namespaceState, CancellationToken cancellationToken)
		{
			NamespaceId namespaceId = namespaceState.Id;

			int cycle = namespaceState.Cycle;
			if (cycle == 0)
			{
				GcState newState = await _gcState.UpdateAsync(x => cycle = StartCycle(x, namespaceId));
				namespaceState = FindOrAddNamespace(newState, namespaceId);
			}

			IStorageClientImpl store = await _storageService.GetClientAsync(namespaceId, cancellationToken);
			GcSession session = new GcSession(store, namespaceId, _pageCollection, _nodeCollection, cycle, namespaceState.StartTime, _logger);
			await session.RunAsync(cancellationToken);
			await _gcState.UpdateAsync(x => CompleteCycle(x, namespaceId));
			await _pageCollection.DeleteManyAsync(x => x.Cycle == cycle, cancellationToken);
		}

		static void SyncNamespaceList(GcState state, List<NamespaceConfig> namespaces)
		{
			HashSet<NamespaceId> validNamespaceIds = new HashSet<NamespaceId>(namespaces.Select(x => x.Id));
			state.Namespaces.RemoveAll(x => !validNamespaceIds.Contains(x.Id));

			HashSet<NamespaceId> currentNamespaceIds = new HashSet<NamespaceId>(state.Namespaces.Select(x => x.Id));
			foreach (NamespaceConfig config in namespaces)
			{
				if (!currentNamespaceIds.Contains(config.Id))
				{
					state.Namespaces.Add(new GcNamespaceState { Id = config.Id, LastStartTime = DateTime.UtcNow });
				}
			}

			state.Namespaces.SortBy(x => x.Id);
		}

		int StartCycle(GcState state, NamespaceId namespaceId)
		{
			GcNamespaceState namespaceState = FindOrAddNamespace(state, namespaceId);
			namespaceState.Cycle = ++state.NextCycle;
			namespaceState.StartTime = _clock.UtcNow;
			return namespaceState.Cycle;
		}

		static void CompleteCycle(GcState state, NamespaceId namespaceId)
		{
			GcNamespaceState namespaceState = FindOrAddNamespace(state, namespaceId);
			namespaceState.Cycle = 0;
			namespaceState.LastStartTime = namespaceState.StartTime;
		}

		static GcNamespaceState FindOrAddNamespace(GcState state, NamespaceId namespaceId)
		{
			GcNamespaceState? namespaceState = state.Namespaces.FirstOrDefault(x => x.Id == namespaceId);
			if (namespaceState == null)
			{
				namespaceState = new GcNamespaceState();
				namespaceState.Id = namespaceId;
				state.Namespaces.Add(namespaceState);
			}
			return namespaceState;
		}
	}
}
