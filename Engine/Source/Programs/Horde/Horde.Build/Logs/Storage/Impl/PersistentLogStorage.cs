// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Storage;
using HordeServer.Utilities;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Logs.Readers
{
	using LogId = ObjectId<ILogFile>;

	/// <summary>
	/// Bulk storage for log file data
	/// </summary>
	class PersistentLogStorage : ILogStorage
	{
		/// <summary>
		/// The bulk storage provider to use
		/// </summary>
		IStorageBackend StorageProvider;

		/// <summary>
		/// Log provider
		/// </summary>
		ILogger Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="StorageProvider">The storage provider</param>
		/// <param name="Logger">Logging provider</param>
		public PersistentLogStorage(IStorageBackend<PersistentLogStorage> StorageProvider, ILogger<PersistentLogStorage> Logger)
		{
			this.StorageProvider = StorageProvider;
			this.Logger = Logger;
		}

		/// <inheritdoc/>
		public void Dispose()
		{
		}

		/// <inheritdoc/>
		public async Task<LogIndexData?> ReadIndexAsync(LogId LogId, long Length)
		{
			Logger.LogDebug("Reading log {LogId} index length {Length} from persistent storage", LogId, Length);

			string Path = $"{LogId}/index_{Length}";
			ReadOnlyMemory<byte>? Data = await StorageProvider.ReadBytesAsync(Path);
			if (Data == null)
			{
				return null;
			}
			return LogIndexData.FromMemory(Data.Value);
		}

		/// <inheritdoc/>
		public Task WriteIndexAsync(LogId LogId, long Length, LogIndexData IndexData)
		{
			Logger.LogDebug("Writing log {LogId} index length {Length} to persistent storage", LogId, Length);

			string Path = $"{LogId}/index_{Length}";
			ReadOnlyMemory<byte> Data = IndexData.ToByteArray();
			return StorageProvider.WriteBytesAsync(Path, Data);
		}

		/// <inheritdoc/>
		public async Task<LogChunkData?> ReadChunkAsync(LogId LogId, long Offset, int LineIndex)
		{
			Logger.LogDebug("Reading log {LogId} chunk offset {Offset} from persistent storage", LogId, Offset);

			string Path = $"{LogId}/offset_{Offset}";
			ReadOnlyMemory<byte>? Data = await StorageProvider.ReadBytesAsync(Path);
			if(Data == null)
			{
				return null;
			}

			MemoryReader Reader = new MemoryReader(Data.Value);
			LogChunkData ChunkData = Reader.ReadLogChunkData(Offset, LineIndex);

			if (Reader.Offset != Data.Value.Length)
			{
				throw new Exception($"Serialization of persistent chunk {Path} is not at expected offset (expected {Data.Value.Length}, actual {Reader.Offset})");
			}

			return ChunkData;
		}

		/// <inheritdoc/>
		public Task WriteChunkAsync(LogId LogId, long Offset, LogChunkData ChunkData)
		{
			Logger.LogDebug("Writing log {LogId} chunk offset {Offset} to persistent storage", LogId, Offset);

			string Path = $"{LogId}/offset_{Offset}";
			byte[] Data = new byte[ChunkData.GetSerializedSize()];
			MemoryWriter Writer = new MemoryWriter(Data);
			Writer.WriteLogChunkData(ChunkData);
			Writer.CheckOffset(Data.Length);

			return StorageProvider.WriteBytesAsync(Path, Data);
		}
	}
}
