// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Caching.Memory;
using MongoDB.Bson;
using MongoDB.Bson.Serialization;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Caches queries against a given collection
	/// </summary>
	/// <typeparam name="TDocument">The document type</typeparam>
	class MongoQueryCache<TDocument> : IDisposable
	{
		class QueryState
		{
			public Stopwatch Timer = Stopwatch.StartNew();
			public List<TDocument>? Results;
			public Task? QueryTask;
		}

		IMongoCollection<TDocument> Collection;
		MemoryCache Cache;
		TimeSpan MaxLatency;

		public MongoQueryCache(IMongoCollection<TDocument> Collection, TimeSpan MaxLatency)
		{
			this.Collection = Collection;

			MemoryCacheOptions Options = new MemoryCacheOptions();
			this.Cache = new MemoryCache(Options);

			this.MaxLatency = MaxLatency;
		}

		public void Dispose()
		{
			Cache.Dispose();
		}

		async Task RefreshAsync(QueryState State, FilterDefinition<TDocument> Filter)
		{
			State.Results = await Collection.Find(Filter).ToListAsync();
			State.Timer.Restart();
		}

		public async Task<List<TDocument>> FindAsync(FilterDefinition<TDocument> Filter, int Index, int Count)
		{
			BsonDocument Rendered = Filter.Render(BsonSerializer.LookupSerializer<TDocument>(), BsonSerializer.SerializerRegistry);
			BsonDocument Document = new BsonDocument { new BsonElement("filter", Rendered), new BsonElement("index", Index), new BsonElement("count", Count) };

			string FilterKey = Document.ToString();

			QueryState? State;
			if (!Cache.TryGetValue(FilterKey, out State) || State == null)
			{
				State = new QueryState();
				using (ICacheEntry CacheEntry = Cache.CreateEntry(FilterKey))
				{
					CacheEntry.SetSlidingExpiration(TimeSpan.FromMinutes(5.0));
					CacheEntry.SetValue(State);
				}
			}

			if(State.QueryTask != null && State.QueryTask.IsCompleted)
			{
				await State.QueryTask;
				State.QueryTask = null;
			}

			if (State.QueryTask == null && (State.Results == null || State.Timer.Elapsed > MaxLatency))
			{
				State.QueryTask = Task.Run(() => RefreshAsync(State, Filter));
			}
			if (State.QueryTask != null && (State.Results == null || State.Timer.Elapsed > MaxLatency))
			{
				await State.QueryTask;
			}

			return State.Results!;
		}
	}
}
