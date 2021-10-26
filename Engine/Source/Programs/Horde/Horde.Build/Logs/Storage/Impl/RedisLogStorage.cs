// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using StackExchange.Redis;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Logs.Storage.Impl
{
	using LogId = ObjectId<ILogFile>;

	/// <summary>
	/// Redis log file storage
	/// </summary>
	public sealed class RedisLogStorage : ILogStorage
	{
		/// <summary>
		/// The Redis database
		/// </summary>
		IDatabase RedisDb;

		/// <summary>
		/// Logging device
		/// </summary>
		ILogger Logger;

		/// <summary>
		/// Inner storage layer
		/// </summary>
		ILogStorage Inner;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="RedisDb">Redis database</param>
		/// <param name="Logger">Logger instance</param>
		/// <param name="Inner">Inner storage layer</param>
		public RedisLogStorage(IDatabase RedisDb, ILogger Logger, ILogStorage Inner)
		{
			this.RedisDb = RedisDb;
			this.Logger = Logger;
			this.Inner = Inner;
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			Inner.Dispose();
		}

		/// <summary>
		/// Gets the key for a log file's index
		/// </summary>
		/// <param name="LogId">The log file id</param>
		/// <param name="Length">Length of the file covered by the index</param>
		/// <returns>The index key</returns>
		static string IndexKey(LogId LogId, long Length)
		{
			return $"log-{LogId}-index-{Length}";
		}

		/// <summary>
		/// Gets the key for a chunk's data
		/// </summary>
		/// <param name="LogId">The log file id</param>
		/// <param name="Offset">Offset of the chunk within the log file</param>
		/// <returns>The chunk key</returns>
		static string ChunkKey(LogId LogId, long Offset)
		{
			return $"log-{LogId}-chunk-{Offset}";
		}

		/// <summary>
		/// Adds an index to the cache
		/// </summary>
		/// <param name="Key">Key for the item to store</param>
		/// <param name="Value">Value to be stored</param>
		/// <returns>Async task</returns>
		[SuppressMessage("Design", "CA1031:Do not catch general exception types")]
		async Task AddAsync(string Key, ReadOnlyMemory<byte> Value)
		{
			try
			{
				await RedisDb.StringSetAsync(Key, Value, expiry: TimeSpan.FromHours(1.0));
				Logger.LogDebug("Added key {Key} to Redis cache ({Size} bytes)", Key, Value.Length);
			}
			catch (Exception Ex)
			{
				Logger.LogError(Ex, "Error writing Redis key {Key}", Key);
			}
		}

		/// <summary>
		/// Adds an index to the cache
		/// </summary>
		/// <param name="Key">Key for the item to store</param>
		/// <param name="Deserialize">Delegate to deserialize the item</param>
		/// <returns>Async task</returns>
		[SuppressMessage("Design", "CA1031:Do not catch general exception types")]
		async Task<T?> GetAsync<T>(string Key, Func<ReadOnlyMemory<byte>, T> Deserialize) where T : class
		{
			try
			{
				RedisValue Value = await RedisDb.StringGetAsync(Key);
				if (Value.IsNullOrEmpty)
				{
					Logger.LogDebug("Redis cache miss for {Key}", Key);
					return null;
				}
				else
				{
					Logger.LogDebug("Redis cache hit for {Key}", Key);
					return Deserialize(Value);
				}
			}
			catch (Exception Ex)
			{
				Logger.LogError(Ex, "Error reading Redis key {Key}", Key);
				return null;
			}
		}

		/// <inheritdoc/>
		public async Task<LogIndexData?> ReadIndexAsync(LogId LogId, long Length)
		{
			string Key = IndexKey(LogId, Length);

			LogIndexData? IndexData = await GetAsync(Key, Memory => LogIndexData.FromMemory(Memory));
			if(IndexData == null)
			{
				IndexData = await Inner.ReadIndexAsync(LogId, Length);
				if(IndexData != null)
				{
					await AddAsync(Key, IndexData.ToByteArray());
				}
			}
			return IndexData;
		}

		/// <inheritdoc/>
		public async Task WriteIndexAsync(LogId LogId, long Length, LogIndexData Index)
		{
			await Inner.WriteIndexAsync(LogId, Length, Index);
		}

		/// <inheritdoc/>
		public async Task<LogChunkData?> ReadChunkAsync(LogId LogId, long Offset, int LineIndex)
		{
			string Key = ChunkKey(LogId, Offset);

			LogChunkData? ChunkData = await GetAsync(Key, Memory => LogChunkData.FromMemory(Memory, Offset, LineIndex));
			if (ChunkData == null)
			{
				ChunkData = await Inner.ReadChunkAsync(LogId, Offset, LineIndex);
				if (ChunkData != null)
				{
					await AddAsync(Key, ChunkData.ToByteArray());
				}
			}
			return ChunkData;
		}

		/// <inheritdoc/>
		public async Task WriteChunkAsync(LogId LogId, long Offset, LogChunkData ChunkData)
		{
			await Inner.WriteChunkAsync(LogId, Offset, ChunkData);
		}
	}
}
