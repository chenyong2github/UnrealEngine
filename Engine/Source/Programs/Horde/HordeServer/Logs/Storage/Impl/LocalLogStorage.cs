// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.Extensions.Caching.Memory;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Logs.Storage
{
	using LogId = ObjectId<ILogFile>;

	/// <summary>
	/// In-memory cache for chunk and index data
	/// </summary>
	class LocalLogStorage : ILogStorage
	{
		/// <summary>
		/// The memory cache instance
		/// </summary>
		MemoryCache Cache;

		/// <summary>
		/// The inner storage
		/// </summary>
		ILogStorage Inner;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="NumItems">Maximum size for the memory cache</param>
		/// <param name="Inner">The inner storage provider</param>
		public LocalLogStorage(int NumItems, ILogStorage Inner)
		{
			MemoryCacheOptions Options = new MemoryCacheOptions();
			Options.SizeLimit = NumItems;

			this.Cache = new MemoryCache(Options);
			this.Inner = Inner;
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			Cache.Dispose();
			Inner.Dispose();
		}

		/// <summary>
		/// Gets the cache key for a particular index
		/// </summary>
		/// <param name="LogId">The log file to retrieve an index for</param>
		/// <param name="Length">Length of the file covered by the index</param>
		/// <returns>Cache key for the index</returns>
		static string IndexKey(LogId LogId, long Length)
		{
			return $"{LogId}/index-{Length}";
		}

		/// <summary>
		/// Gets the cache key for a particular chunk
		/// </summary>
		/// <param name="LogId">The log file to retrieve an index for</param>
		/// <param name="Offset">The chunk offset</param>
		/// <returns>Cache key for the chunk</returns>
		static string ChunkKey(LogId LogId, long Offset)
		{
			return $"{LogId}/chunk-{Offset}";
		}

		/// <summary>
		/// Adds an entry to the cache
		/// </summary>
		/// <param name="Key">The cache key</param>
		/// <param name="Value">Value to store</param>
		void AddEntry(string Key, object? Value)
		{
			using (ICacheEntry Entry = Cache.CreateEntry(Key))
			{
				if (Value == null)
				{
					Entry.SetAbsoluteExpiration(TimeSpan.FromSeconds(30.0));
					Entry.SetSize(0);
				}
				else
				{
					Entry.SetSlidingExpiration(TimeSpan.FromHours(4));
					Entry.SetSize(1);
				}
				Entry.SetValue(Value);
			}
		}

		/// <summary>
		/// Reads a value from the cache, or from the inner storage provider, adding the result to the cache
		/// </summary>
		/// <typeparam name="T">Type of object to read</typeparam>
		/// <param name="Key">The cache key</param>
		/// <param name="ReadInner">Delegate to read from the inner storage provider</param>
		/// <returns>The retrieved value</returns>
		async Task<T?> ReadValueAsync<T>(string Key, Func<Task<T?>> ReadInner) where T : class
		{
			T? Value;
			if (!Cache.TryGetValue(Key, out Value))
			{
				try
				{
					Value = await ReadInner();
				}
				finally
				{
					AddEntry(Key, Value);
				}
			}
			return Value;
		}

		/// <inheritdoc/>
		public Task<LogIndexData?> ReadIndexAsync(LogId LogId, long Length)
		{
			return ReadValueAsync(IndexKey(LogId, Length), () => Inner.ReadIndexAsync(LogId, Length));
		}

		/// <inheritdoc/>
		public Task WriteIndexAsync(LogId LogId, long Length, LogIndexData IndexData)
		{
			AddEntry(IndexKey(LogId, Length), IndexData);
			return Inner.WriteIndexAsync(LogId, Length, IndexData);
		}

		/// <inheritdoc/>
		public Task<LogChunkData?> ReadChunkAsync(LogId LogId, long Offset, int LineIndex)
		{
			return ReadValueAsync(ChunkKey(LogId, Offset), () => Inner.ReadChunkAsync(LogId, Offset, LineIndex));
		}

		/// <inheritdoc/>
		public Task WriteChunkAsync(LogId LogId, long Offset, LogChunkData ChunkData)
		{
			AddEntry(ChunkKey(LogId, Offset), ChunkData);
			return Inner.WriteChunkAsync(LogId, Offset, ChunkData);
		}
	}
}
