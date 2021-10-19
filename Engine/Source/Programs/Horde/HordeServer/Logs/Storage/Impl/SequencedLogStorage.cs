// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using HordeServer.Utilities;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Logs.Storage.Impl
{
	using LogId = ObjectId<ILogFile>;

	/// <summary>
	/// Storage layer which caches pending read tasks, to avoid fetching the same item more than once
	/// </summary>
	class SequencedLogStorage : ILogStorage
	{
		/// <summary>
		/// Inner storage implementation
		/// </summary>
		ILogStorage Inner;

		/// <summary>
		/// Pending index reads
		/// </summary>
		Dictionary<(LogId, long), Task<LogIndexData?>> IndexReadTasks = new Dictionary<(LogId, long), Task<LogIndexData?>>();

		/// <summary>
		/// Pending index reads
		/// </summary>
		Dictionary<(LogId, long), Task> IndexWriteTasks = new Dictionary<(LogId, long), Task>();

		/// <summary>
		/// Pending chunk reads
		/// </summary>
		Dictionary<(LogId, long), Task<LogChunkData?>> ChunkReadTasks = new Dictionary<(LogId, long), Task<LogChunkData?>>();

		/// <summary>
		/// Pending chunk reads
		/// </summary>
		Dictionary<(LogId, long), Task> ChunkWriteTasks = new Dictionary<(LogId, long), Task>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Inner">The inner storage provider</param>
		public SequencedLogStorage(ILogStorage Inner)
		{
			this.Inner = Inner;
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			Inner.Dispose();
		}

		/// <inheritdoc/>
		public Task<LogIndexData?> ReadIndexAsync(LogId LogId, long Length)
		{
			Task<LogIndexData?>? Task;
			lock (IndexReadTasks)
			{
				if (!IndexReadTasks.TryGetValue((LogId, Length), out Task))
				{
					Task = InnerReadIndexAsync(LogId, Length);
					IndexReadTasks.Add((LogId, Length), Task);
				}
			}
			return Task;
		}

		/// <inheritdoc/>
		public Task WriteIndexAsync(LogId LogId, long Length, LogIndexData IndexData)
		{
			Task? Task;
			lock (IndexWriteTasks)
			{
				if (!IndexWriteTasks.TryGetValue((LogId, Length), out Task))
				{
					Task = InnerWriteIndexAsync(LogId, Length, IndexData);
					IndexWriteTasks.Add((LogId, Length), Task);
				}
			}
			return Task;
		}

		/// <inheritdoc/>
		public Task<LogChunkData?> ReadChunkAsync(LogId LogId, long Offset, int LineIndex)
		{
			Task<LogChunkData?>? Task;
			lock (ChunkReadTasks)
			{
				if (!ChunkReadTasks.TryGetValue((LogId, Offset), out Task))
				{
					Task = InnerReadChunkAsync(LogId, Offset, LineIndex);
					ChunkReadTasks[(LogId, Offset)] = Task;
				}
			}
			return Task;
		}

		/// <inheritdoc/>
		public Task WriteChunkAsync(LogId LogId, long Offset, LogChunkData ChunkData)
		{
			Task? Task;
			lock (ChunkWriteTasks)
			{
				if (!ChunkWriteTasks.TryGetValue((LogId, Offset), out Task))
				{
					Task = InnerWriteChunkAsync(LogId, Offset, ChunkData);
					ChunkWriteTasks[(LogId, Offset)] = Task;
				}
			}
			return Task;
		}

		/// <summary>
		/// Wrapper for reading an index from the inner storage provider
		/// </summary>
		/// <param name="LogId">The log file to read the index for</param>
		/// <param name="Length">Length of the file that's indexed</param>
		/// <returns>The index data</returns>
		async Task<LogIndexData?> InnerReadIndexAsync(LogId LogId, long Length)
		{
			await Task.Yield();

			LogIndexData? IndexData = await Inner.ReadIndexAsync(LogId, Length);
			lock (IndexReadTasks)
			{
				IndexReadTasks.Remove((LogId, Length));
			}
			return IndexData;
		}

		/// <summary>
		/// Wrapper for reading an index from the inner storage provider
		/// </summary>
		/// <param name="LogId">The log file to read the index for</param>
		/// <param name="Length">Length of the indexed data</param>
		/// <param name="IndexData">The index data to write</param>
		/// <returns>The index data</returns>
		async Task InnerWriteIndexAsync(LogId LogId, long Length, LogIndexData IndexData)
		{
			await Task.Yield();

			await Inner.WriteIndexAsync(LogId, Length, IndexData);

			lock (IndexWriteTasks)
			{
				IndexWriteTasks.Remove((LogId, Length));
			}
		}

		/// <summary>
		/// Wrapper for reading a chunk from the inner storage provider
		/// </summary>
		/// <param name="LogId">The log file to read the index for</param>
		/// <param name="Offset">Offset of the chunk to read</param>
		/// <param name="LineIndex">Index of the first line in this chunk</param>
		/// <returns>The index data</returns>
		async Task<LogChunkData?> InnerReadChunkAsync(LogId LogId, long Offset, int LineIndex)
		{
			await Task.Yield();

			LogChunkData? ChunkData = await Inner.ReadChunkAsync(LogId, Offset, LineIndex);
			lock (ChunkReadTasks)
			{
				ChunkReadTasks.Remove((LogId, Offset));
			}
			return ChunkData;
		}

		/// <summary>
		/// Wrapper for reading a chunk from the inner storage provider
		/// </summary>
		/// <param name="LogId">The log file to write the chunk for</param>
		/// <param name="Offset">Offset of the chunk within the log</param>
		/// <param name="ChunkData">The chunk data</param>
		/// <returns>The index data</returns>
		async Task<LogChunkData> InnerWriteChunkAsync(LogId LogId, long Offset, LogChunkData ChunkData)
		{
			await Task.Yield();

			await Inner.WriteChunkAsync(LogId, Offset, ChunkData);

			lock (ChunkWriteTasks)
			{
				ChunkWriteTasks.Remove((LogId, Offset));
			}
			return ChunkData;
		}
	}
}
