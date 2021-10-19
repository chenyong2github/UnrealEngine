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
	/// Empty implementation of log storage
	/// </summary>
	public sealed class NullLogStorage : ILogStorage
	{
		/// <inheritdoc/>
		public void Dispose()
		{
		}

		/// <inheritdoc/>
		public Task<LogIndexData?> ReadIndexAsync(LogId LogId, long Length)
		{
			return Task.FromResult<LogIndexData?>(null);
		}

		/// <inheritdoc/>
		public Task WriteIndexAsync(LogId LogId, long Length, LogIndexData Index)
		{
			return Task.CompletedTask;
		}

		/// <inheritdoc/>
		public Task<LogChunkData?> ReadChunkAsync(LogId LogId, long Offset, int LineIndex)
		{
			return Task.FromResult<LogChunkData?>(null);
		}

		/// <inheritdoc/>
		public Task WriteChunkAsync(LogId LogId, long Offset, LogChunkData ChunkData)
		{
			return Task.CompletedTask;
		}
	}
}
