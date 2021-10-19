// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Utilities;
using MongoDB.Bson;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Logs.Builder
{
	using LogId = ObjectId<ILogFile>;

	/// <summary>
	/// In-memory implementation of a log write buffer
	/// </summary>
	class LocalLogBuilder : ILogBuilder
	{
		/// <summary>
		/// Stores information about a sub-chunk which is still being written to
		/// </summary>
		class PendingSubChunk
		{
			/// <summary>
			/// The type of data stored in this sub chunk
			/// </summary>
			public LogType Type { get; }

			/// <summary>
			/// Data for the sub-chunk
			/// </summary>
			public MemoryStream Stream { get; } = new MemoryStream(4096);

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="Type">Type of data stored in this subchunk</param>
			public PendingSubChunk(LogType Type)
			{
				this.Type = Type;
			}

			/// <summary>
			/// Converts this pending sub-chunk to a concrete LogSubChunkData object
			/// </summary>
			/// <param name="Offset">Offset of this sub-chunk within the file</param>
			/// <param name="LineIndex">The line index</param>
			/// <returns>New sub-chunk data object</returns>
			public LogSubChunkData ToSubChunkData(long Offset, int LineIndex)
			{
				return new LogSubChunkData(Type, Offset, LineIndex, new ReadOnlyLogText(Stream.ToArray()));
			}
		}

		/// <summary>
		/// Stores information about a pending log chunk
		/// </summary>
		class PendingChunk
		{
			/// <summary>
			/// Time that this chunk was created
			/// </summary>
			public DateTime CreateTimeUtc { get; set; } = DateTime.UtcNow;

			/// <summary>
			/// Array of sub chunks for this chunk
			/// </summary>
			public List<PendingSubChunk> SubChunks = new List<PendingSubChunk>();

			/// <summary>
			/// The next sub-chunk
			/// </summary>
			public PendingSubChunk NextSubChunk;

			/// <summary>
			/// Total length of this chunk
			/// </summary>
			public long Length;

			/// <summary>
			/// Whether the chunk is complete
			/// </summary>
			public bool Complete;

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="Type">Type of data to store in this file</param>
			public PendingChunk(LogType Type)
			{
				NextSubChunk = new PendingSubChunk(Type);
			}

			/// <summary>
			/// Complete the current sub-chunk
			/// </summary>
			public void CompleteSubChunk()
			{
				if (NextSubChunk.Stream.Length > 0)
				{
					SubChunks.Add(NextSubChunk);
					NextSubChunk = new PendingSubChunk(NextSubChunk.Type);
				}
			}
		}

		/// <summary>
		/// Current log chunk state
		/// </summary>
		ConcurrentDictionary<(LogId, long), PendingChunk> PendingChunks = new ConcurrentDictionary<(LogId, long), PendingChunk>();

		/// <inheritdoc/>
		public bool FlushOnShutdown => true;

		/// <inheritdoc/>
		public Task<bool> AppendAsync(LogId LogId, long ChunkOffset, long WriteOffset, int WriteLineIndex, int WriteLineCount, ReadOnlyMemory<byte> Data, LogType Type)
		{
			PendingChunk? PendingChunk;
			while (!PendingChunks.TryGetValue((LogId, ChunkOffset), out PendingChunk))
			{
				if(WriteOffset != ChunkOffset)
				{
					return Task.FromResult(false);
				}

				PendingChunk = new PendingChunk(Type);

				if (PendingChunks.TryAdd((LogId, ChunkOffset), PendingChunk))
				{
					break;
				}
			}

			lock (PendingChunk)
			{
				if (!PendingChunk.Complete && ChunkOffset + PendingChunk.Length == WriteOffset)
				{
					PendingChunk.NextSubChunk.Stream.Write(Data.Span);
					PendingChunk.Length += Data.Span.Length;
					return Task.FromResult(true);
				}
			}
			return Task.FromResult(false);
		}

		/// <inheritdoc/>
		public Task CompleteSubChunkAsync(LogId LogId, long Offset)
		{
			PendingChunk? PendingChunk;
			if (PendingChunks.TryGetValue((LogId, Offset), out PendingChunk))
			{
				lock (PendingChunk)
				{
					PendingChunk.CompleteSubChunk();
				}
			}
			return Task.CompletedTask;
		}

		/// <inheritdoc/>
		public Task CompleteChunkAsync(LogId LogId, long Offset)
		{
			PendingChunk? PendingChunk;
			if (PendingChunks.TryGetValue((LogId, Offset), out PendingChunk))
			{
				lock (PendingChunk)
				{
					if (!PendingChunk.Complete)
					{
						PendingChunk.Complete = true;
						PendingChunk.CompleteSubChunk();
					}
				}
			}
			return Task.CompletedTask;
		}

		/// <inheritdoc/>
		public Task RemoveChunkAsync(LogId LogId, long Offset)
		{
			PendingChunks.TryRemove((LogId, Offset), out _);
			return Task.CompletedTask;
		}

		/// <inheritdoc/>
		public Task<LogChunkData?> GetChunkAsync(LogId LogId, long Offset, int LineIndex)
		{
			PendingChunk? PendingChunk;
			if (PendingChunks.TryGetValue((LogId, Offset), out PendingChunk))
			{
				long SubChunkOffset = Offset;
				int SubChunkLineIndex = LineIndex;

				List<LogSubChunkData> SubChunks = new List<LogSubChunkData>();
				foreach(PendingSubChunk PendingSubChunk in PendingChunk.SubChunks)
				{
					LogSubChunkData SubChunk = PendingSubChunk.ToSubChunkData(SubChunkOffset, SubChunkLineIndex);
					SubChunkOffset += SubChunk.Length;
					SubChunkLineIndex += SubChunk.LineCount;
					SubChunks.Add(SubChunk);
				}

				PendingSubChunk NextSubChunk = PendingChunk.NextSubChunk;
				if (NextSubChunk.Stream.Length > 0)
				{
					LogSubChunkData SubChunkData = NextSubChunk.ToSubChunkData(SubChunkOffset, SubChunkLineIndex);
					SubChunks.Add(SubChunkData);
				}

				return Task.FromResult<LogChunkData?>(new LogChunkData(Offset, LineIndex, SubChunks));
			}

			return Task.FromResult<LogChunkData?>(null);
		}

		/// <inheritdoc/>
		public Task<List<(LogId, long)>> TouchChunksAsync(TimeSpan MinAge)
		{
			DateTime UtcNow = DateTime.UtcNow;

			List<(LogId, long)> Chunks = new List<(LogId, long)>();
			foreach (KeyValuePair<(LogId, long), PendingChunk> PendingChunk in PendingChunks.ToArray())
			{
				lock (PendingChunk.Value)
				{
					if (PendingChunk.Value.CreateTimeUtc < UtcNow - MinAge)
					{
						Chunks.Add(PendingChunk.Key);
						PendingChunk.Value.CreateTimeUtc = UtcNow;
					}
				}
			}
			return Task.FromResult(Chunks);
		}
	}
}
