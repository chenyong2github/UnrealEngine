// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading.Tasks;
using Horde.Build.Utilities;
using System.Collections.Generic;
using System.Threading;
using System;
using EpicGames.Core;
using Horde.Build.Server;
using EpicGames.Redis;
using StackExchange.Redis;
using System.Collections.Concurrent;
using Microsoft.Extensions.Hosting;
using HordeCommon;
using Microsoft.Extensions.Logging;
using System.Globalization;

namespace Horde.Build.Logs
{
	using LogId = ObjectId<ILogFile>;

	/// <summary>
	/// Stores data for logs which has not yet been flushed to persistent storage.
	/// 
	/// * Once a log is retrieved, server adds an entry with this log id to a sorted set in Redis (see <see cref="_expireQueue"/>, scored by 
	///   expiry time (defaulting to <see cref="ExpireAfter"/> from present) and broadcasts it to any other server instances.
	/// * If a log is being tailed, server keeps the total number of lines in a Redis key (<see cref="RedisTailNext"/>, and appends chunks of log 
	///   data to a sorted set for that log scored by index of the first line (see <see cref="RedisTailData"/>).
	/// * Chunks are split on fixed boundaries, in order to allow older entries to be purged by score without having to count the number of lines
	///   they contain first.
	/// * Agent polls for requests to provide tail data via <see cref="LogRpcService.UpdateLogTail"/>, which calls <see cref="WaitForTailNextAsync"/>.
	///   <see cref="WaitForTailNextAsync"/> returns the index of the total line count for this log if it is being tailed (indicating the index of
	///   the next line that the agent should send to the server), or blocks until the log is being tailed.
	/// * Once data is flushed to persistent storage, the number of flushed lines is added to <see cref="_trimQueue"/> and the line data is removed
	///   from Redis after <see cref="TrimAfter"/>.
	/// * <see cref="EpicGames.Horde.Logs.LogNode"/> contains a "complete" flag indicating whether it is necessary to check for tail data.
	///   
	/// </summary>
	public class LogTailService : IHostedService
	{
		/// <summary>
		/// Amount of time to keep tail data in the cache after it has been flushed to persistent storage. This gives some wiggle room with order of operations when sequencing persisted data with tail data.
		/// </summary>
		public static TimeSpan TrimAfter { get; } = TimeSpan.FromMinutes(0.5);

		/// <summary>
		/// Amount of time to continue tailing a log after the last time it was updated.
		/// </summary>
		public static TimeSpan ExpireAfter { get; } = TimeSpan.FromMinutes(2.0);

		readonly RedisService _redisService;

		readonly ConcurrentDictionary<LogId, AsyncEvent> _notifyLogEvents = new ConcurrentDictionary<LogId, AsyncEvent>();
		readonly RedisChannel<LogId> _tailStartChannel;
		readonly RedisSortedSet<string> _trimQueue;
		readonly RedisSortedSet<LogId> _expireQueue;

		readonly IClock _clock;
		readonly ITicker _expireTailsTicker;

		readonly ILogger _logger;

		RedisChannelSubscription<LogId>? _tailStartSubscription;

		internal RedisString<int> RedisTailNext(LogId logId) => new RedisString<int>(_redisService.ConnectionPool, $"logs:{logId}:tail-next");
		internal RedisSortedSet<ReadOnlyMemory<byte>> RedisTailData(LogId logId) => new RedisSortedSet<ReadOnlyMemory<byte>>(_redisService.ConnectionPool, $"logs:{logId}:tail-data");

		/// <summary>
		/// Boundary to split Redis chunks along. 
		/// </summary>
		public int ChunkLineCount { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public LogTailService(RedisService redisService, IClock clock, ILogger<LogTailService> logger)
			: this(redisService, clock, 32, logger)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public LogTailService(RedisService redisService, IClock clock, int chunkLineCount, ILogger<LogTailService> logger)
		{
			if ((chunkLineCount & (chunkLineCount - 1)) != 0)
			{
				throw new ArgumentException($"{nameof(ChunkLineCount)} must be a power of two", nameof(chunkLineCount));
			}

			ChunkLineCount = chunkLineCount;

			_redisService = redisService;
			_tailStartChannel = new RedisChannel<LogId>("logs:notify");
			_trimQueue = new RedisSortedSet<string>(_redisService.ConnectionPool, "logs:trim");
			_expireQueue = new RedisSortedSet<LogId>(_redisService.ConnectionPool, "logs:expire");

			_clock = clock;
			_expireTailsTicker = clock.AddSharedTicker<LogTailService>(TimeSpan.FromSeconds(30.0), ExpireTailsAsync, logger);
			_logger = logger;
		}

		/// <inheritdoc/>
		public async Task StartAsync(CancellationToken cancellationToken)
		{
			_tailStartSubscription = await _redisService.DatabaseSingleton.Multiplexer.GetSubscriber().SubscribeAsync(_tailStartChannel, (_, x) => OnTailStart(x));
			await _expireTailsTicker.StartAsync();
		}

		/// <inheritdoc/>
		public async Task StopAsync(CancellationToken cancellationToken)
		{
			await _expireTailsTicker.StopAsync();
			if (_tailStartSubscription != null)
			{
				await _tailStartSubscription.DisposeAsync();
			}
		}

		private async ValueTask ExpireTailsAsync(CancellationToken cancellationToken)
		{
			double maxScore = (_clock.UtcNow - DateTime.UnixEpoch).TotalSeconds;

			SortedSetEntry<string>[] trimEntries = await _trimQueue.RangeByScoreWithScoresAsync(stop: maxScore);
			foreach (SortedSetEntry<string> trimEntry in trimEntries)
			{
				string[] values = trimEntry.Element.Split('=');

				LogId logId = LogId.Parse(values[0]);
				int lineCount = Int32.Parse(values[1], NumberStyles.None, CultureInfo.InvariantCulture);

				int minLineIndex = (lineCount & ~(ChunkLineCount - 1)) - 1;
				_ = await RedisTailData(logId).RemoveRangeByScoreAsync(Double.NegativeInfinity, minLineIndex, Exclude.None, CommandFlags.FireAndForget);
			}

			SortedSetEntry<LogId>[] expireEntries = await _expireQueue.RangeByScoreWithScoresAsync(stop: maxScore);
			foreach (SortedSetEntry<LogId> expireEntry in expireEntries)
			{
				LogId logId = expireEntry.Element;

				ITransaction transaction = _redisService.GetDatabase().CreateTransaction();
				transaction.AddCondition(Condition.SortedSetEqual(_expireQueue.Key, expireEntry.ElementValue, expireEntry.Score));
				_ = transaction.KeyDeleteAsync(RedisTailNext(logId).Key, CommandFlags.FireAndForget);
				_ = transaction.KeyDeleteAsync(RedisTailData(logId).Key, CommandFlags.FireAndForget);
				_ = transaction.ExecuteAsync(CommandFlags.FireAndForget);
			}
		}

		private void OnTailStart(LogId logId)
		{
			if (_notifyLogEvents.TryGetValue(logId, out AsyncEvent? notifyEvent))
			{
				notifyEvent.Latch();
			}
		}

		/// <summary>
		/// Enable tailing for the given log file
		/// </summary>
		/// <param name="logId">Log file identifier</param>
		/// <param name="lineCount">The current flushed line count for the log file</param>
		public async ValueTask EnableTailingAsync(LogId logId, int lineCount)
		{
			if (await RedisTailNext(logId).SetAsync(lineCount, when: When.NotExists))
			{
				_logger.LogDebug("Enabled tailing for log {LogId}", logId);
			}

			await _redisService.GetDatabase().PublishAsync(_tailStartChannel, logId);

			double score = (_clock.UtcNow + ExpireAfter - DateTime.UnixEpoch).TotalSeconds;
			_ = _expireQueue.AddAsync(logId, score, flags: CommandFlags.FireAndForget);
		}

		/// <summary>
		/// Appends data to the log tail, starting at the given line index
		/// </summary>
		/// <param name="logId">Log file identifier</param>
		/// <param name="tailNext">Index of the first line to append</param>
		/// <param name="tailData">Data for the log starting at the given line number</param>
		/// <returns></returns>
		public async Task AppendAsync(LogId logId, int tailNext, ReadOnlyMemory<byte> tailData)
		{
			List<SortedSetEntry<ReadOnlyMemory<byte>>> entries = new List<SortedSetEntry<ReadOnlyMemory<byte>>>();
			int newLineCount = SplitLogDataToChunks(tailNext, tailData, ChunkLineCount, entries);

			if (entries.Count > 0)
			{
				RedisString<int> redisTailNext = RedisTailNext(logId);
				RedisSortedSet<ReadOnlyMemory<byte>> redisTailData = RedisTailData(logId);

				ITransaction transaction = _redisService.GetDatabase().CreateTransaction();
				transaction.AddCondition(Condition.StringEqual(RedisTailNext(logId).Key, tailNext));
				_ = transaction.With(redisTailData).AddAsync(entries.ToArray(), flags: CommandFlags.FireAndForget);
				_ = transaction.With(redisTailNext).SetAsync(newLineCount, flags: CommandFlags.FireAndForget);

				bool result = await transaction.ExecuteAsync();
				_logger.LogTrace("Tail data for {LogId}, line {TailNext} -> {NewTailNext}, result: {Result}", logId, tailNext, newLineCount, result);
			}
		}

		/// <summary>
		/// Reads data for the given log file, starting at the requested line index
		/// </summary>
		/// <param name="logId">The log to query</param>
		/// <param name="lineIndex">Index of the first line to return</param>
		/// <param name="lineCount">Maximum number of lines to return</param>
		/// <returns>List of lines for the file</returns>
		public async Task<List<Utf8String>> ReadAsync(LogId logId, int lineIndex, int lineCount)
		{
			List<Utf8String> lines = new List<Utf8String>();
			await ReadAsync(logId, lineIndex, lineCount, lines);
			return lines;
		}

		/// <summary>
		/// Reads data for the given log file, starting at the requested line index
		/// </summary>
		/// <param name="logId">The log to query</param>
		/// <param name="lineIndex">Index of the first line to return</param>
		/// <param name="lineCount">Maximum number of lines to return</param>
		/// <param name="lines">Buffer for the lines to be returned</param>
		/// <returns>List of lines for the file</returns>
		public async Task ReadAsync(LogId logId, int lineIndex, int lineCount, List<Utf8String> lines)
		{
			int lowerBound = lineIndex;
			int upperBound = lineIndex + lineCount;
			int chunkLowerBound = lowerBound & ~(ChunkLineCount - 1);
			int chunkUpperBound = (upperBound + (ChunkLineCount - 1)) & ~(ChunkLineCount - 1);

			int nextIndex = lineIndex;

			SortedSetEntry<ReadOnlyMemory<byte>>[] entries = await RedisTailData(logId).RangeByScoreWithScoresAsync(chunkLowerBound, chunkUpperBound);
			foreach (SortedSetEntry<ReadOnlyMemory<byte>> entry in entries)
			{
				ReadOnlyMemory<byte> memory = entry.Element;

				int index = (int)entry.Score;
				if (index > nextIndex)
				{
					break;
				}
				for (; ; index++)
				{
					int lineLength = memory.Span.IndexOf((byte)'\n');
					if (lineLength == -1)
					{
						break;
					}
					if (index >= nextIndex)
					{
						lines.Add(new Utf8String(memory.Slice(0, lineLength)));
						nextIndex++;
					}
					memory = memory.Slice(lineLength + 1);
				}
			}
		}

		/// <summary>
		/// Flushes the given log file to the given number of lines
		/// </summary>
		/// <param name="logId">Log to flush</param>
		/// <param name="lineCount">Number of lines in the flushed log file</param>
		/// <returns></returns>
		public async ValueTask FlushAsync(LogId logId, int lineCount)
		{
			string trimEntry = $"{logId}={lineCount}";
			double trimTime = (_clock.UtcNow + TrimAfter - DateTime.UnixEpoch).TotalSeconds;
			_ = await _trimQueue.AddAsync(trimEntry, trimTime, flags: CommandFlags.FireAndForget);
		}

		/// <summary>
		/// Return the desired next line to append for the given log file, or -1 if it is not being tailed.
		/// </summary>
		/// <param name="logId">Log to query</param>
		/// <returns>Number of lines for the log file</returns>
		public async Task<int> GetTailNextAsync(LogId logId)
		{
			int? tailNext = await RedisTailNext(logId).GetValueAsync();
			return tailNext ?? -1;
		}

		/// <summary>
		/// Waits for a request to read tail data
		/// </summary>
		/// <param name="logId">The log file to query</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public async Task<int> WaitForTailNextAsync(LogId logId, CancellationToken cancellationToken)
		{
			AsyncEvent notifyEvent = _notifyLogEvents.GetOrAdd(logId, new AsyncEvent());
			try
			{
				using (IDisposable registration = cancellationToken.Register(() => notifyEvent.Latch()))
				{
					for (; ; )
					{
						Task notifyTask = notifyEvent.Task;

						int? tailNext = await RedisTailNext(logId).GetValueAsync();
						if (tailNext != null)
						{
							return tailNext.Value;
						}
						else if (cancellationToken.IsCancellationRequested)
						{
							return -1;
						}

						await notifyTask;
					}
				}
			}
			finally
			{
				_notifyLogEvents.TryRemove(logId, out _);
			}
		}

		/// <summary>
		/// Splits data into chunks aligned onto <see cref="ChunkLineCount"/> line boundaries.
		/// </summary>
		static int SplitLogDataToChunks(int lineIdx, ReadOnlyMemory<byte> data, int chunkLineCount, List<SortedSetEntry<ReadOnlyMemory<byte>>> entries)
		{
			for (; ; )
			{
				int minLineIdx = lineIdx;

				// Find the next chunk
				int length = 0;
				for (; ; )
				{
					// Add the next line to the buffer
					int lineLength = data.Span.Slice(length).IndexOf((byte)'\n') + 1;
					if (lineLength == 0)
					{
						break;
					}
					length += lineLength;

					// Move to the next line
					lineIdx++;

					// If this is a chunk boundary, break here
					if ((lineIdx & (chunkLineCount - 1)) == 0)
					{
						break;
					}
				}

				// If we couldn't find any more lines, bail out
				if (length == 0)
				{
					break;
				}

				// Add this entry
				ReadOnlyMemory<byte> chunk = data.Slice(0, length);
				entries.Add(new SortedSetEntry<ReadOnlyMemory<byte>>(chunk, minLineIdx));
				data = data.Slice(length);
			}
			return lineIdx;
		}
	}
}
