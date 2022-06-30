// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Interface for accessing an object's data
	/// </summary>
	public interface IBlob
	{
		/// <summary>
		/// Gets the data for this blob
		/// </summary>
		/// <returns></returns>
		ValueTask<ReadOnlyMemory<byte>> GetDataAsync();

		/// <summary>
		/// Find the outward references for a node
		/// </summary>
		/// <returns></returns>
		ValueTask<IReadOnlyList<BlobId>> GetReferencesAsync();
	}

	/// <summary>
	/// Base interface for a low-level storage backend. Blobs added to this store are not content addressed, but referenced by <see cref="BlobId"/>.
	/// </summary>
	public interface IBlobStore
	{
		#region Blobs

		/// <summary>
		/// Reads data for a blob from the store
		/// </summary>
		/// <param name="id">The blob identifier</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		Task<IBlob> ReadBlobAsync(BlobId id, CancellationToken cancellationToken = default);

		/// <summary>
		/// Writes a new object to the store
		/// </summary>
		/// <param name="data">Payload for the object</param>
		/// <param name="references">Object references</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Unique identifier for the bundle</returns>
		Task<BlobId> WriteBlobAsync(ReadOnlySequence<byte> data, IReadOnlyList<BlobId> references, CancellationToken cancellationToken = default);

		#endregion

		#region Refs

		/// <summary>
		/// Checks if the given ref exists
		/// </summary>
		/// <param name="id">Name of the reference to look for</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>True if the ref exists, false if it did not exist</returns>
		Task<bool> HasRefAsync(RefId id, CancellationToken cancellationToken = default);

		/// <summary>
		/// Reads data for a ref from the store
		/// </summary>
		/// <param name="id">The blob identifier</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		Task<IBlob> ReadRefAsync(RefId id, CancellationToken cancellationToken = default);

		/// <summary>
		/// Writes a new blob to the store
		/// </summary>
		/// <param name="id">Ref for which the blob is a part</param>
		/// <param name="data">Payload for the object</param>
		/// <param name="references">Object references</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Unique identifier for the blob</returns>
		Task WriteRefAsync(RefId id, ReadOnlySequence<byte> data, IReadOnlyList<BlobId> references, CancellationToken cancellationToken = default);

		/// <summary>
		/// Reads data for a ref from the store
		/// </summary>
		/// <param name="id">The ref identifier</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		Task<bool> DeleteRefAsync(RefId id, CancellationToken cancellationToken = default);

		#endregion
	}
}
