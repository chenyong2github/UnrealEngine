// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Storage.Backends
{
	/// <summary>
	/// In-memory implementation of ILogFileStorage
	/// </summary>
	public class TransientStorageBackend : IStorageBackend
	{
		/// <summary>
		/// Data storage
		/// </summary>
		ConcurrentDictionary<string, byte[]> PathToData = new ConcurrentDictionary<string, byte[]>();

		/// <inheritdoc/>
		public Task<Stream?> ReadAsync(string Path)
		{
			byte[]? Data;
			if (PathToData.TryGetValue(Path, out Data))
			{
				return Task.FromResult<Stream?>(new MemoryStream(Data, false));
			}
			else
			{
				return Task.FromResult<Stream?>(null);
			}
		}

		/// <inheritdoc/>
		public async Task WriteAsync(string Path, Stream Stream)
		{
			using (MemoryStream Buffer = new MemoryStream())
			{
				await Stream.CopyToAsync(Buffer);
				PathToData[Path] = Buffer.ToArray();
			}
		}

		/// <inheritdoc/>
		public Task<bool> ExistsAsync(string Path)
		{
			return Task.FromResult(PathToData.ContainsKey(Path));
		}

		/// <inheritdoc/>
		public Task DeleteAsync(string Path)
		{
			PathToData.TryRemove(Path, out _);
			return Task.CompletedTask;
		}
	}
}
