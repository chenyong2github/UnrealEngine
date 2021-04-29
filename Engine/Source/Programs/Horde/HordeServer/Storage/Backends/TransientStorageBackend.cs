// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Storage.Impl
{
	/// <summary>
	/// In-memory implementation of ILogFileStorage
	/// </summary>
	public sealed class TransientStorageBackend : IStorageBackend
	{
		/// <summary>
		/// Data storage
		/// </summary>
		Dictionary<string, byte[]> PathToData = new Dictionary<string, byte[]>();

		/// <inheritdoc/>
		public void Dispose()
		{
		}

		/// <inheritdoc/>
		public Task<bool> TouchAsync(string Path)
		{
			return Task.FromResult(PathToData.ContainsKey(Path));
		}

		/// <inheritdoc/>
		public Task<ReadOnlyMemory<byte>?> ReadAsync(string Path)
		{
			byte[]? Data;
			if (PathToData.TryGetValue(Path, out Data))
			{
				return Task.FromResult<ReadOnlyMemory<byte>?>(Data);
			}
			else
			{
				return Task.FromResult<ReadOnlyMemory<byte>?>(null);
			}
		}

		/// <inheritdoc/>
		public Task WriteAsync(string Path, ReadOnlyMemory<byte> Data)
		{
			PathToData[Path] = Data.Span.ToArray();
			return Task.CompletedTask;
		}

		/// <inheritdoc/>
		public Task DeleteAsync(string Path)
		{
			PathToData.Remove(Path);
			return Task.CompletedTask;
		}
	}
}
