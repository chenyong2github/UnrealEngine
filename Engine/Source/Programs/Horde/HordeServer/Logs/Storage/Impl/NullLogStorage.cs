using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Logs.Storage.Impl
{
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
		public Task<LogIndexData?> ReadIndexAsync(ObjectId LogId, long Length)
		{
			return Task.FromResult<LogIndexData?>(null);
		}

		/// <inheritdoc/>
		public Task WriteIndexAsync(ObjectId LogId, long Length, LogIndexData Index)
		{
			return Task.CompletedTask;
		}

		/// <inheritdoc/>
		public Task<LogChunkData?> ReadChunkAsync(ObjectId LogId, long Offset, int LineIndex)
		{
			return Task.FromResult<LogChunkData?>(null);
		}

		/// <inheritdoc/>
		public Task WriteChunkAsync(ObjectId LogId, long Offset, LogChunkData ChunkData)
		{
			return Task.CompletedTask;
		}
	}
}
