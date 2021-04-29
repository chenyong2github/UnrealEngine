// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Storage
{
	/// <summary>
	/// Interface for a log file storage implementation
	/// </summary>
	public interface IStorageBackend : IDisposable
	{
		/// <summary>
		/// Updates the timestamp on the given file. Touching the file ensures that it will not be deleted in a "reasonable" timeframe.
		/// </summary>
		/// <param name="Path">Path to update</param>
		/// <returns>True if the file exists, false otherwise</returns>
		Task<bool> TouchAsync(string Path);

		/// <summary>
		/// Gets a chunk from a storage device
		/// </summary>
		/// <param name="Path">The path to read from</param>
		/// <returns>The data to create from</returns>
		Task<ReadOnlyMemory<byte>?> ReadAsync(string Path);

		/// <summary>
		/// Tries to update a log chunk after verifying it's the correct version
		/// </summary>
		/// <param name="Path">The path to write to</param>
		/// <param name="Data">Buffer containing the data to write</param>
		/// <returns>Async task</returns>
		Task WriteAsync(string Path, ReadOnlyMemory<byte> Data);

		/// <summary>
		/// Deletes a file with the given path
		/// </summary>
		/// <param name="Path">Path of the file to delete</param>
		/// <returns>Async task</returns>
		Task DeleteAsync(string Path);
	}
}
