// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Threading.Tasks;

namespace HordeServer.Collections
{
	using JobId = ObjectId<IJob>;
	using LogId = ObjectId<ILogFile>;

	/// <summary>
	/// Updates a log file chunk
	/// </summary>
	public class CompleteLogChunkUpdate
	{
		/// <summary>
		/// Index of the chunk
		/// </summary>
		public int Index { get; set; }

		/// <summary>
		/// New length for the chunk
		/// </summary>
		public int Length { get; set; }

		/// <summary>
		/// Number of lines in the chunk
		/// </summary>
		public int LineCount { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Index">Index of the chunk</param>
		/// <param name="Length">New length for the chunk</param>
		/// <param name="LineCount">Number of lines in the chunk</param>
		public CompleteLogChunkUpdate(int Index, int Length, int LineCount)
		{
			this.Index = Index;
			this.Length = Length;
			this.LineCount = LineCount;
		}
	}

	/// <summary>
	/// Wrapper around the jobs collection in a mongo DB
	/// </summary>
	public interface ILogFileCollection
	{
		/// <summary>
		/// Creates a new log
		/// </summary>
		/// <param name="JobId">Unique id of the job that owns this log file</param>
		/// <param name="SessionId">Agent session allowed to update the log</param>
		/// <param name="Type">Type of events to be stored in the log</param>
		/// <returns>The new log file document</returns>
		Task<ILogFile> CreateLogFileAsync(JobId JobId, ObjectId? SessionId, LogType Type);

		/// <summary>
		/// Adds a new chunk
		/// </summary>
		/// <param name="LogFileInterface">The current log file</param>
		/// <param name="Offset">Offset of the new chunk</param>
		/// <param name="LineIndex">Line index for the start of the chunk</param>
		/// <returns>The updated log file document</returns>
		Task<ILogFile?> TryAddChunkAsync(ILogFile LogFileInterface, long Offset, int LineIndex);

		/// <summary>
		/// Update the log file with final information about certain chunks
		/// </summary>
		/// <param name="LogFileInterface">The current log file</param>
		/// <param name="Chunks">Chunks to update. New chunks will be inserted</param>
		/// <returns>The updated log file document</returns>
		Task<ILogFile?> TryCompleteChunksAsync(ILogFile LogFileInterface, IEnumerable<CompleteLogChunkUpdate> Chunks);

		/// <summary>
		/// Update the log file with final information about the index
		/// </summary>
		/// <param name="LogFileInterface">The current log file</param>
		/// <param name="NewIndexLength">New length of the index</param>
		/// <returns>The updated log file document</returns>
		Task<ILogFile?> TryUpdateIndexAsync(ILogFile LogFileInterface, long NewIndexLength);

		/// <summary>
		/// Gets a logfile by ID
		/// </summary>
		/// <param name="LogFileId">Unique id of the log file</param>
		/// <returns>The logfile document</returns>
		Task<ILogFile?> GetLogFileAsync(LogId LogFileId);

		/// <summary>
		/// Gets all the log files
		/// </summary>
		/// <returns>List of log files</returns>
		Task<List<ILogFile>> GetLogFilesAsync(int? Index = null, int? Count = null);
	}
}
