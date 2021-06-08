// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Storage.Primitives;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Storage
{
	/// <summary>
	/// Combined interface for replicated content addressable storage and ref table.
	/// </summary>
	public interface IStorageService
	{
		/// <summary>
		/// Attempts to retrieve a blob from the store. 
		/// 
		/// Note that this call may succeed even though the object is not in persistent storage; it may be retrieved
		/// from a volatile cache. Call <see cref="ShouldPutBlobAsync(IoHash)"/> to determine if a blob needs to
		/// be put to the store.
		/// 
		/// This method takes a timestamp indicating the required replication consistency. The method may return null
		/// if an object was put to the store after this time but it has not replicated down yet.
		/// </summary>
		/// <param name="Hash">Hash of the blob</param>
		/// <param name="Deadline">Deadline for returning a result. If zero, the storage provider should use a default value.</param>
		/// <returns>The blob data. May return ReadOnlyMemory<byte>.Empty if it does not exist.</byte></returns>
		Task<ReadOnlyMemory<byte>?> TryGetBlobAsync(IoHash Hash, DateTime Deadline = default(DateTime));

		/// <summary>
		/// Adds a blob to the store.
		/// </summary>
		/// <param name="Hash">Hash of the value. Used as a hint for possible sharding; the server will verify this hash and fail if it does not match.</param>
		/// <param name="Value">The blob data</param>
		/// <returns>Async task</returns>
		Task PutBlobAsync(IoHash Hash, ReadOnlyMemory<byte> Value);

		/// <summary>
		/// Determines if the blob should be written to the store. The server may return true even if it exists in a store
		/// at its discretion (eg. perhaps the client -> store link is faster than the store -> parent store link, or a GC
		/// operation is in progress)
		/// </summary>
		/// <param name="Hash">Hash of the blob to write</param>
		/// <returns>True if the blob should be written to the store</returns>
		Task<bool> ShouldPutBlobAsync(IoHash Hash);

		/// <summary>
		/// Gets a reference from the storage layer
		/// </summary>
		/// <param name="Key">Key for the reference</param>
		/// <param name="MaxDrift">Required replication consistency</param>
		/// <returns>Hash of the result</returns>
		Task<IoHash?> GetRefAsync(IoHash Key, TimeSpan MaxDrift = default(TimeSpan));

		/// <summary>
		/// Sets the value associated with a key. Writes are propagated as a "last writer wins" policy.
		/// </summary>
		/// <param name="Key">The key to set</param>
		/// <param name="Value">Value for the key. If null, the key is deleted.</param>
		/// <returns>Async task</returns>
		Task SetRefAsync(IoHash Key, IoHash? Value);

		/// <summary>
		/// Updates the last modified timestamp of the given key. Possibly equivalent to doing a SET() on the same key/value pair, but does not 
		/// need to force a value write to propagate up the tree. Mainly used for GC.
		/// </summary>
		/// <param name="Key">The key to touch</param>
		/// <returns>Async task</returns>
		Task TouchRefAsync(IoHash Key);
	}

	/// <summary>
	/// Extension methods for the storage service
	/// </summary>
	static class StorageServiceExtensions
	{
		/// <summary>
		/// Gets a blob by hash, throwing an exception if it does not exist.
		/// </summary>
		/// <param name="StorageService">The storage provider</param>
		/// <param name="Hash">Hash of the blob to retrieve</param>
		/// <param name="Deadline">Deadline for retreving the data</param>
		/// <returns>Memory for the blob</returns>
		public static async Task<ReadOnlyMemory<byte>> GetBlobAsync(this IStorageService StorageService, IoHash Hash, DateTime Deadline = default(DateTime))
		{
			ReadOnlyMemory<byte>? Blob = await StorageService.TryGetBlobAsync(Hash, Deadline);
			if (Blob == null)
			{
				throw new KeyNotFoundException($"Unable to read key {Hash}");
			}
			return Blob.Value;
		}

		/// <summary>
		/// Hashes a blob, then adds it to the storage service. If the hash is already known, prefer <see cref="IStorageService.PutBlobAsync(IoHash, ReadOnlyMemory{byte})"/>
		/// </summary>
		/// <param name="StorageService">The storage service</param>
		/// <param name="Data">The data to add</param>
		/// <returns>Hash of the data that was added</returns>
		public static async Task<IoHash> PutBlobAsync(this IStorageService StorageService, ReadOnlyMemory<byte> Data)
		{
			IoHash Hash = IoHash.Compute(Data.Span);
			await StorageService.PutBlobAsync(Hash, Data);
			return Hash;
		}
	}
}
