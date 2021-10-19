// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Storage;
using HordeServer.Utilities;
using Microsoft.Extensions.Caching.Memory;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Logs
{
	using LogId = ObjectId<ILogFile>;

	/// <summary>
	/// Caching interface for reading and writing log data.
	/// </summary>
	public interface ILogStorage : IDisposable
	{
		/// <summary>
		/// Attempts to read an index for the given log file
		/// </summary>
		/// <param name="LogId">Unique id of the log file</param>
		/// <param name="Length">Length of the file covered by the index</param>
		/// <returns>Index for the log file</returns>
		Task<LogIndexData?> ReadIndexAsync(LogId LogId, long Length);

		/// <summary>
		/// Log file to write an index for
		/// </summary>
		/// <param name="LogId">Unique id of the log file</param>
		/// <param name="Length">Length of the file covered by the index</param>
		/// <param name="Index">The log file index</param>
		/// <returns>Async task</returns>
		Task WriteIndexAsync(LogId LogId, long Length, LogIndexData Index);

		/// <summary>
		/// Retrieves an item from the cache
		/// </summary>
		/// <param name="LogId">Unique id of the log file</param>
		/// <param name="Offset">Offset of the chunk to read</param>
		/// <param name="LineIndex">First line of the chunk</param>
		/// <returns>Data for the given key, or null if it's not present</returns>
		Task<LogChunkData?> ReadChunkAsync(LogId LogId, long Offset, int LineIndex);

		/// <summary>
		/// Writes a chunk to storage
		/// </summary>
		/// <param name="LogId">Unique id of the log file</param>
		/// <param name="Offset">Offset of the chunk to write</param>
		/// <param name="ChunkData">Information about the chunk data</param>
		/// <returns>Async task</returns>
		Task WriteChunkAsync(LogId LogId, long Offset, LogChunkData ChunkData);
	}
}
