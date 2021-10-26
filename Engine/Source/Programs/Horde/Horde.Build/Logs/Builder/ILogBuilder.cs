// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Utilities;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Logs
{
	using LogId = ObjectId<ILogFile>;

	/// <summary>
	/// Interface for the log file write cache
	/// </summary>
	public interface ILogBuilder
	{
		/// <summary>
		/// Whether the cache should be flushed on shutdown
		/// </summary>
		bool FlushOnShutdown { get; }

		/// <summary>
		/// Append data to a key
		/// </summary>
		/// <param name="LogId">The log file id</param>
		/// <param name="ChunkOffset">Offset of the chunk within the log file</param>
		/// <param name="WriteOffset">Offset to write to</param>
		/// <param name="WriteLineIndex">Line index being written</param>
		/// <param name="WriteLineCount">Line count being written</param>
		/// <param name="Data">Data to be appended</param>
		/// <param name="Type">Type of data stored in this log file</param>
		/// <returns>True if the data was appended to the given chunk. False if the chunk has been completed.</returns>
		Task<bool> AppendAsync(LogId LogId, long ChunkOffset, long WriteOffset, int WriteLineIndex, int WriteLineCount, ReadOnlyMemory<byte> Data, LogType Type);

		/// <summary>
		/// Finish the current sub chunk
		/// </summary>
		/// <param name="LogId">The log file id</param>
		/// <param name="Offset">Offset of the chunk within the log file</param>
		/// <returns>Async task</returns>
		Task CompleteSubChunkAsync(LogId LogId, long Offset);

		/// <summary>
		/// Finish the current chunk
		/// </summary>
		/// <param name="LogId">The log file id</param>
		/// <param name="Offset">Offset of the chunk within the log file</param>
		/// <returns>Async task</returns>
		Task CompleteChunkAsync(LogId LogId, long Offset);

		/// <summary>
		/// Remove a complete chunk from the builder
		/// </summary>
		/// <param name="LogId">The log file id</param>
		/// <param name="Offset">Offset of the chunk within the log file</param>
		/// <returns>Async task</returns>
		Task RemoveChunkAsync(LogId LogId, long Offset);

		/// <summary>
		/// Gets the current chunk for the given log file
		/// </summary>
		/// <param name="LogId">The log file id</param>
		/// <param name="Offset">Offset of the chunk within the log file</param>
		/// <param name="LineIndex">Line index of the chunk within the log file</param>
		/// <returns></returns>
		Task<LogChunkData?> GetChunkAsync(LogId LogId, long Offset, int LineIndex);

		/// <summary>
		/// Touches the timestamps of all the chunks after the given age, and returns them. Used for flushing the builder.
		/// </summary>
		/// <param name="MinAge">Minimum age of the chunks to enumerate. If specified, only chunks last modified longer than this period will be returned.</param>
		/// <returns>List of chunks, identified by log id and chunk index</returns>
		Task<List<(LogId, long)>> TouchChunksAsync(TimeSpan MinAge);
	}
}
