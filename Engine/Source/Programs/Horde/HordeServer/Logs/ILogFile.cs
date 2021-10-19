// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Api;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Models
{
	using JobId = ObjectId<IJob>;
	using LogId = ObjectId<ILogFile>;

	/// <summary>
	/// Information about a log file chunk
	/// </summary>
	public interface ILogChunk
	{
		/// <summary>
		/// Offset of the chunk within the log
		/// </summary>
		long Offset { get; }

		/// <summary>
		/// Length of this chunk. If zero, the chunk is still being written to.
		/// </summary>
		int Length { get; }

		/// <summary>
		/// Index of the first line within this chunk. If a line straddles two chunks, this is the index of the split line.
		/// </summary>
		int LineIndex { get; }

		/// <summary>
		/// If the chunk has yet to be pushed to persistent storage, includes the name of the server that is currently storing it.
		/// </summary>
		string? Server { get; }
	}

	/// <summary>
	/// Information about a log file
	/// </summary>
	public interface ILogFile
	{
		/// <summary>
		/// Identifier for the LogFile. Randomly generated.
		/// </summary>
		public LogId Id { get; }

		/// <summary>
		/// Unique id of the job containing this log
		/// </summary>
		public JobId JobId { get; }

		/// <summary>
		/// The session allowed to write to this log
		/// </summary>
		public ObjectId? SessionId { get; }

		/// <summary>
		/// Maximum line index in the file
		/// </summary>
		public int? MaxLineIndex { get; }

		/// <summary>
		/// Length of the file which is indexed
		/// </summary>
		public long? IndexLength { get; }

		/// <summary>
		/// Type of data stored in this log 
		/// </summary>
		public LogType Type { get; }

		/// <summary>
		/// Chunks within this file
		/// </summary>
		public IReadOnlyList<ILogChunk> Chunks { get; }
	}

	/// <summary>
	/// Extension methods for log files
	/// </summary>
	public static class LogFileExtensions
	{
		/// <summary>
		/// Gets the chunk index containing the given offset.
		/// </summary>
		/// <param name="Chunks">The chunks to search</param>
		/// <param name="Offset">The offset to search for</param>
		/// <returns>The chunk index containing the given offset</returns>
		public static int GetChunkForOffset(this IReadOnlyList<ILogChunk> Chunks, long Offset)
		{
			int ChunkIndex = Chunks.BinarySearch(x => x.Offset, Offset);
			if (ChunkIndex < 0)
			{
				ChunkIndex = ~ChunkIndex - 1;
			}
			return ChunkIndex;
		}

		/// <summary>
		/// Gets the starting chunk index for the given line
		/// </summary>
		/// <param name="Chunks">The chunks to search</param>
		/// <param name="LineIndex">Index of the line to query</param>
		/// <returns>Index of the chunk to fetch</returns>
		public static int GetChunkForLine(this IReadOnlyList<ILogChunk> Chunks, int LineIndex)
		{
			int ChunkIndex = Chunks.BinarySearch(x => x.LineIndex, LineIndex);
			if(ChunkIndex < 0)
			{
				ChunkIndex = ~ChunkIndex - 1;
			}
			return ChunkIndex;
		}
	}
}
