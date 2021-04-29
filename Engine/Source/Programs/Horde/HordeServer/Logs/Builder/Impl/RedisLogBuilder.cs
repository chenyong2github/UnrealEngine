using Datadog.Trace;
using EpicGames.Core;
using HordeServer.Api;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Primitives;
using MongoDB.Bson;
using StackExchange.Redis;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using HordeServer.Utilities;

namespace HordeServer.Logs.Builder
{
	/// <summary>
	/// Redis-based cache for log file chunks
	/// </summary>
	class RedisLogBuilder : ILogBuilder
	{
		const string ItemsKey = "log-items";

		struct ChunkKeys
		{
			public string Prefix { get; }
			public string Type => $"{Prefix}-type";
			public string LineIndex => $"{Prefix}-lineindex";
			public string Length => $"{Prefix}-length";
			public string ChunkData => $"{Prefix}-chunk";
			public string SubChunkData => $"{Prefix}-subchunk";
			public string Complete => $"{Prefix}-complete";

			public ChunkKeys(ObjectId LogId, long Offset)
			{
				Prefix = $"log-{LogId}-chunk-{Offset}-builder";
			}

			public static bool TryParse(string Prefix, out ObjectId LogId, out long Offset)
			{
				Match Match = Regex.Match(Prefix, "log-([^-]+)-chunk-([^-]+)-builder");
				if (!Match.Success || !ObjectId.TryParse(Match.Groups[1].Value, out LogId) || !long.TryParse(Match.Groups[2].Value, out Offset))
				{
					LogId = new ObjectId();
					Offset = 0;
					return false;
				}
				return true;
			}
		}

		/// <inheritdoc/>
		public bool FlushOnShutdown => false;

		/// <summary>
		/// The Redis database connection pool
		/// </summary>
		RedisConnectionPool RedisConnectionPool;

		/// <summary>
		/// Logger for debug output
		/// </summary>
		ILogger Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="RedisConnectionPool">The redis database singleton</param>
		/// <param name="Logger">Logger for debug output</param>
		public RedisLogBuilder(RedisConnectionPool RedisConnectionPool, ILogger Logger)
		{
			this.RedisConnectionPool = RedisConnectionPool;
			this.Logger = Logger;
		}

		/// <inheritdoc/>
		public async Task<bool> AppendAsync(ObjectId LogId, long ChunkOffset, long WriteOffset, int WriteLineIndex, int WriteLineCount, ReadOnlyMemory<byte> Data, LogType Type)
		{
			IDatabase RedisDb = RedisConnectionPool.GetDatabase();
			ChunkKeys Keys = new ChunkKeys(LogId, ChunkOffset);

			if(ChunkOffset == WriteOffset)
			{
				using Scope Scope = Tracer.Instance.StartActive("Redis.CreateChunk");
				Scope.Span.SetTag("LogId", LogId.ToString());
				Scope.Span.SetTag("Offset", ChunkOffset.ToString(CultureInfo.InvariantCulture));
				Scope.Span.SetTag("WriteOffset", WriteOffset.ToString(CultureInfo.InvariantCulture));

				ITransaction? CreateTransaction = RedisDb.CreateTransaction();
				CreateTransaction.AddCondition(Condition.SortedSetNotContains(ItemsKey, Keys.Prefix));
				_ = CreateTransaction.SortedSetAddAsync(ItemsKey, Keys.Prefix, DateTime.UtcNow.Ticks);
				_ = CreateTransaction.StringSetAsync(Keys.Type, (int)Type);
				_ = CreateTransaction.StringSetAsync(Keys.Length, Data.Length);
				_ = CreateTransaction.StringSetAsync(Keys.LineIndex, WriteLineIndex + WriteLineCount);
				_ = CreateTransaction.StringSetAsync(Keys.SubChunkData, Data);
				if (await CreateTransaction.ExecuteAsync())
				{
					Logger.LogTrace("Created {Size} bytes in {Key}", Data.Length, Keys.SubChunkData);
					return true;
				}
			}

			{
				using Scope Scope = Tracer.Instance.StartActive("Redis.AppendChunk");
				Scope.Span.SetTag("LogId", LogId.ToString());
				Scope.Span.SetTag("Offset", ChunkOffset.ToString(CultureInfo.InvariantCulture));
				Scope.Span.SetTag("WriteOffset", WriteOffset.ToString(CultureInfo.InvariantCulture));

				ITransaction? AppendTransaction = RedisDb.CreateTransaction();
				AppendTransaction.AddCondition(Condition.SortedSetContains(ItemsKey, Keys.Prefix));
				AppendTransaction.AddCondition(Condition.KeyNotExists(Keys.Complete));
				AppendTransaction.AddCondition(Condition.StringEqual(Keys.Type, (int)Type));
				AppendTransaction.AddCondition(Condition.StringEqual(Keys.Length, (int)(WriteOffset - ChunkOffset)));
				_ = AppendTransaction.StringAppendAsync(Keys.SubChunkData, Data);
				_ = AppendTransaction.StringSetAsync(Keys.Length, (int)(WriteOffset - ChunkOffset) + Data.Length);
				_ = AppendTransaction.StringSetAsync(Keys.LineIndex, WriteLineIndex + WriteLineCount);
				if (await AppendTransaction.ExecuteAsync())
				{
					Logger.LogTrace("Appended {Size} to {Key}", Data.Length, Keys.SubChunkData);
					return true;
				}
			}

			return false;
		}

		/// <inheritdoc/>
		public async Task CompleteSubChunkAsync(ObjectId LogId, long Offset)
		{
			IDatabase RedisDb = RedisConnectionPool.GetDatabase();
			ChunkKeys Keys = new ChunkKeys(LogId, Offset);
			for (; ; )
			{
				using Scope Scope = Tracer.Instance.StartActive("Redis.CompleteSubChunk");
				Scope.Span.SetTag("LogId", LogId.ToString());
				Scope.Span.SetTag("Offset", Offset.ToString(CultureInfo.InvariantCulture));

				LogType Type = (LogType)(int)await RedisDb.StringGetAsync(Keys.Type);
				int Length = (int)await RedisDb.StringGetAsync(Keys.Length);
				int LineIndex = (int)await RedisDb.StringGetAsync(Keys.LineIndex);
				long ChunkDataLength = await RedisDb.StringLengthAsync(Keys.ChunkData);

				RedisValue SubChunkTextValue = await RedisDb.StringGetAsync(Keys.SubChunkData);
				if (SubChunkTextValue.IsNullOrEmpty)
				{
					break;
				}

				ReadOnlyLogText SubChunkText = new ReadOnlyLogText(SubChunkTextValue);
				LogSubChunkData SubChunkData = new LogSubChunkData(Type, Offset + Length - SubChunkText.Data.Length, LineIndex - SubChunkText.LineCount, SubChunkText);
				byte[] SubChunkDataBytes = SubChunkData.ToByteArray();

				ITransaction WriteTransaction = RedisDb.CreateTransaction();

				WriteTransaction.AddCondition(Condition.StringLengthEqual(Keys.ChunkData, ChunkDataLength));
				WriteTransaction.AddCondition(Condition.StringLengthEqual(Keys.SubChunkData, SubChunkText.Length));
				WriteTransaction.AddCondition(Condition.StringEqual(Keys.Length, Length));
				WriteTransaction.AddCondition(Condition.StringEqual(Keys.LineIndex, LineIndex));
				Task<long> NewLength = WriteTransaction.StringAppendAsync(Keys.ChunkData, SubChunkDataBytes);
				_ = WriteTransaction.KeyDeleteAsync(Keys.SubChunkData);

				if (await WriteTransaction.ExecuteAsync())
				{
					Logger.LogDebug("Completed sub-chunk for log {LogId} chunk offset {Offset} -> sub-chunk size {SubChunkSize}, chunk size {ChunkSize}", LogId, Offset, SubChunkDataBytes.Length, await NewLength);
					break;
				}
			}
		}

		/// <inheritdoc/>
		public async Task CompleteChunkAsync(ObjectId LogId, long Offset)
		{
			IDatabase RedisDb = RedisConnectionPool.GetDatabase();
			using Scope Scope = Tracer.Instance.StartActive("Redis.CompleteChunk");
			Scope.Span.SetTag("LogId", LogId.ToString());
			Scope.Span.SetTag("Offset", Offset.ToString(CultureInfo.InvariantCulture));

			ChunkKeys Keys = new ChunkKeys(LogId, Offset);

			ITransaction Transaction = RedisDb.CreateTransaction();
			Transaction.AddCondition(Condition.SortedSetContains(ItemsKey, Keys.Prefix));
			_ = Transaction.StringSetAsync(Keys.Complete, true);
			if(!await Transaction.ExecuteAsync())
			{
				Logger.LogDebug("Log {LogId} chunk offset {Offset} is not in Redis builder", LogId, Offset);
				return;
			}

			await CompleteSubChunkAsync(LogId, Offset);
		}

		/// <inheritdoc/>
		public async Task RemoveChunkAsync(ObjectId LogId, long Offset)
		{
			IDatabase RedisDb = RedisConnectionPool.GetDatabase();
			using Scope Scope = Tracer.Instance.StartActive("Redis.RemoveChunk");
			Scope.Span.SetTag("LogId", LogId.ToString());
			Scope.Span.SetTag("Offset", Offset.ToString(CultureInfo.InvariantCulture));

			ChunkKeys Keys = new ChunkKeys(LogId, Offset);

			ITransaction Transaction = RedisDb.CreateTransaction();
			_ = Transaction.KeyDeleteAsync(Keys.Type);
			_ = Transaction.KeyDeleteAsync(Keys.LineIndex);
			_ = Transaction.KeyDeleteAsync(Keys.Length);
			_ = Transaction.KeyDeleteAsync(Keys.ChunkData);
			_ = Transaction.KeyDeleteAsync(Keys.SubChunkData);
			_ = Transaction.KeyDeleteAsync(Keys.Complete);

			_ = Transaction.SortedSetRemoveAsync(ItemsKey, Keys.Prefix);
			await Transaction.ExecuteAsync();
		}

		/// <inheritdoc/>
		public async Task<LogChunkData?> GetChunkAsync(ObjectId LogId, long Offset, int LineIndex)
		{
			IDatabase RedisDb = RedisConnectionPool.GetDatabase();
			ChunkKeys Keys = new ChunkKeys(LogId, Offset);
			for (; ; )
			{
				using Scope Scope = Tracer.Instance.StartActive("Redis.GetChunk");
				Scope.Span.SetTag("LogId", LogId.ToString());
				Scope.Span.SetTag("Offset", Offset.ToString(CultureInfo.InvariantCulture));

				RedisValue ChunkDataValue = await RedisDb.StringGetAsync(Keys.ChunkData);

				ReadOnlyMemory<byte> ChunkData = ChunkDataValue;

				ITransaction Transaction = RedisDb.CreateTransaction();
				Transaction.AddCondition(Condition.StringLengthEqual(Keys.ChunkData, ChunkData.Length));
				Task<RedisValue> TypeTask = Transaction.StringGetAsync(Keys.Type);
				Task<RedisValue> LastSubChunkDataTask = Transaction.StringGetAsync(Keys.SubChunkData);

				if (await Transaction.ExecuteAsync())
				{
					MemoryReader Reader = new MemoryReader(ChunkData);

					long SubChunkOffset = Offset;
					int SubChunkLineIndex = LineIndex;

					List<LogSubChunkData> SubChunks = new List<LogSubChunkData>();
					while (Reader.Offset < Reader.Length)
					{
						LogSubChunkData SubChunkData = Reader.ReadLogSubChunkData(SubChunkOffset, SubChunkLineIndex);
						SubChunkOffset += SubChunkData.Length;
						SubChunkLineIndex += SubChunkData.LineCount;
						SubChunks.Add(SubChunkData);
					}

					RedisValue Type = await TypeTask;

					RedisValue LastSubChunkData = await LastSubChunkDataTask;
					if (LastSubChunkData.Length() > 0)
					{
						ReadOnlyLogText SubChunkDataText = new ReadOnlyLogText(LastSubChunkData);
						SubChunks.Add(new LogSubChunkData((LogType)(int)Type, SubChunkOffset, SubChunkLineIndex, SubChunkDataText));
					}

					if (SubChunks.Count == 0)
					{
						break;
					}

					return new LogChunkData(Offset, LineIndex, SubChunks);
				}
			}
			return null;
		}

		/// <inheritdoc/>
		public async Task<List<(ObjectId, long)>> TouchChunksAsync(TimeSpan MinAge)
		{
			IDatabase RedisDb = RedisConnectionPool.GetDatabase();
			
			// Find all the chunks that are suitable for expiry
			DateTime UtcNow = DateTime.UtcNow;
			SortedSetEntry[] Entries = await RedisDb.SortedSetRangeByScoreWithScoresAsync(ItemsKey, stop: (UtcNow - MinAge).Ticks);

			// Update the score for each element in a transaction. If it succeeds, we can write the chunk. Otherwise another pod has beat us to it.
			List<(ObjectId, long)> Results = new List<(ObjectId, long)>();
			foreach (SortedSetEntry Entry in Entries)
			{
				if (ChunkKeys.TryParse(Entry.Element.ToString(), out ObjectId LogId, out long Offset))
				{
					ITransaction Transaction = RedisDb.CreateTransaction();
					Transaction.AddCondition(Condition.SortedSetEqual(ItemsKey, Entry.Element, Entry.Score));
					_ = Transaction.SortedSetAddAsync(ItemsKey, Entry.Element, UtcNow.Ticks);

					if (!await Transaction.ExecuteAsync())
					{
						break;
					}

					Results.Add((LogId, Offset));
				}
			}
			return Results;
		}
	}
}
