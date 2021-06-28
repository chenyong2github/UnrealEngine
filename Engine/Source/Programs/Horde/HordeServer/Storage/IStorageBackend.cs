// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Storage
{
	/// <summary>
	/// Interface for a traditional location-addressed storage provider
	/// </summary>
	public interface IStorageBackend
	{
		/// <summary>
		/// Opens a read stream for the given path.
		/// </summary>
		/// <param name="Path">Relative path within the bucket</param>
		/// <returns></returns>
		Task<Stream?> ReadAsync(string Path);

		/// <summary>
		/// Writes a stream to the given path. If the stream throws an exception during read, the write will be aborted.
		/// </summary>
		/// <param name="Path">Relative path within the bucket</param>
		/// <param name="Stream">Stream to write</param>
		Task WriteAsync(string Path, Stream Stream);

		/// <summary>
		/// Tests whether the given path exists
		/// </summary>
		/// <param name="Path">Relative path within the bucket</param>
		/// <returns></returns>
		Task<bool> ExistsAsync(string Path);

		/// <summary>
		/// Deletes a file with the given path
		/// </summary>
		/// <param name="Path">Relative path within the bucket</param>
		/// <returns>Async task</returns>
		Task DeleteAsync(string Path);
	}

	/// <summary>
	/// Extension methods for <see cref="IStorageBackend"/>
	/// </summary>
	public static class StorageBackendExtensions
	{
		/// <summary>
		/// Writes a block of memory to storage
		/// </summary>
		/// <param name="StorageBackend"></param>
		/// <param name="Path"></param>
		/// <returns></returns>
		public static async Task<ReadOnlyMemory<byte>> ReadBytesAsync(this IStorageBackend StorageBackend, string Path)
		{
			using (Stream? InputStream = await StorageBackend.ReadAsync(Path))
			{
				using (MemoryStream OutputStream = new MemoryStream())
				{
					if (InputStream != null)
					{
						await InputStream.CopyToAsync(OutputStream);
					}
					return OutputStream.ToArray();
				}
			}
		}

		/// <summary>
		/// Writes a block of memory to storage
		/// </summary>
		/// <param name="StorageBackend"></param>
		/// <param name="Path"></param>
		/// <param name="Data"></param>
		/// <returns></returns>
		public static async Task WriteBytesAsync(this IStorageBackend StorageBackend, string Path, ReadOnlyMemory<byte> Data)
		{
			using (ReadOnlyMemoryStream Stream = new ReadOnlyMemoryStream(Data))
			{
				await StorageBackend.WriteAsync(Path, Stream);
			}
		}
	}
}
