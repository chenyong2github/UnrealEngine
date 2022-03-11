// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Base class for exceptions related to store
	/// </summary>
	public class StorageException : Exception
	{
		/// <summary>
		/// Constructor
		/// </summary>
		public StorageException(string Message, Exception? InnerException)
			: base(Message, InnerException)
		{
		}
	}

	/// <summary>
	/// Base class for blob exceptions
	/// </summary>
	public class BlobException : StorageException
	{
		/// <summary>
		/// Namespace containing the blob
		/// </summary>
		public NamespaceId NamespaceId { get; }

		/// <summary>
		/// Hash of the blob
		/// </summary>
		public IoHash Hash { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BlobException(NamespaceId NamespaceId, IoHash Hash, string Message, Exception? InnerException = null)
			: base(Message, InnerException)
		{
			this.NamespaceId = NamespaceId;
			this.Hash = Hash;
		}
	}

	/// <summary>
	/// Exception thrown for missing blobs
	/// </summary>
	public sealed class BlobNotFoundException : BlobException
	{
		/// <summary>
		/// Constructor
		/// </summary>
		public BlobNotFoundException(NamespaceId NamespaceId, IoHash Hash, Exception? InnerException = null)
			: base(NamespaceId, Hash, $"Unable to find blob {Hash} in {NamespaceId}", InnerException)
		{
		}
	}

	/// <summary>
	/// Base class for ref exceptions
	/// </summary>
	public class RefException : StorageException
	{
		/// <summary>
		/// Namespace containing the ref
		/// </summary>
		public NamespaceId NamespaceId { get; }

		/// <summary>
		/// Bucket containing the ref
		/// </summary>
		public BucketId BucketId { get; }

		/// <summary>
		/// Identifier for the ref
		/// </summary>
		public RefId RefId { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public RefException(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, string Message, Exception? InnerException = null)
			: base(Message, InnerException)
		{
			this.NamespaceId = NamespaceId;
			this.BucketId = BucketId;
			this.RefId = RefId;
		}
	}

	/// <summary>
	/// Indicates that a named reference wasn't found
	/// </summary>
	public sealed class RefNotFoundException : RefException
	{
		/// <summary>
		/// Constructor
		/// </summary>
		public RefNotFoundException(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, Exception? InnerException = null)
			: base(NamespaceId, BucketId, RefId, $"Ref {NamespaceId}/{BucketId}/{RefId} not found", InnerException)
		{
		}
	}

	/// <summary>
	/// Indicates that a ref cannot be finalized due to a missing blob
	/// </summary>
	public sealed class RefMissingBlobException : RefException
	{
		/// <summary>
		/// Constructor
		/// </summary>
		public RefMissingBlobException(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, List<IoHash> MissingBlobs, Exception? InnerException = null)
			: base(NamespaceId, BucketId, RefId, $"Ref {NamespaceId}/{BucketId}/{RefId} cannot be finalized; missing {MissingBlobs.Count} blobs ({MissingBlobs[0]}...)", InnerException)
		{
		}
	}

	/// <summary>
	/// Interface for an object reference
	/// </summary>
	public interface IRef
	{
		/// <summary>
		/// Namespace identifier
		/// </summary>
		NamespaceId NamespaceId { get; }

		/// <summary>
		/// Bucket identifier
		/// </summary>
		BucketId BucketId { get; }

		/// <summary>
		/// Ref identifier
		/// </summary>
		RefId RefId { get; }

		/// <summary>
		/// The value stored for this ref
		/// </summary>
		CbObject Value { get; }
	}

	/// <summary>
	/// Base interface for a storage client that only records blobs.
	/// </summary>
	public interface IStorageClient
	{
		#region Blobs

		/// <summary>
		/// Opens a blob read stream
		/// </summary>
		/// <param name="NamespaceId">Namespace to operate on</param>
		/// <param name="Hash">Hash of the blob</param>
		/// <param name="CancellationToken">Cancellation token for the operation</param>
		/// <returns>Stream for the blob, or null if it does not exist</returns>
		Task<Stream> ReadBlobAsync(NamespaceId NamespaceId, IoHash Hash, CancellationToken CancellationToken = default);

		/// <summary>
		/// Writes a blob to storage
		/// </summary>
		/// <param name="NamespaceId">Namespace to operate on</param>
		/// <param name="Hash">Hash of the blob</param>
		/// <param name="Stream">The stream to write</param>
		/// <param name="CancellationToken">Cancellation token for the operation</param>
		Task WriteBlobAsync(NamespaceId NamespaceId, IoHash Hash, Stream Stream, CancellationToken CancellationToken = default);

		/// <summary>
		/// Checks if the given blob exists
		/// </summary>
		/// <param name="NamespaceId">Namespace to operate on</param>
		/// <param name="Hash">Hash of the blob</param>
		/// <param name="CancellationToken">Cancellation token for the operation</param>
		/// <returns>True if the blob exists, false if it did not exist</returns>
		Task<bool> HasBlobAsync(NamespaceId NamespaceId, IoHash Hash, CancellationToken CancellationToken = default);

		#endregion
		#region Refs

		/// <summary>
		/// Gets the given reference
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <param name="RefId">Name of the reference</param>
		/// <param name="CancellationToken">Cancellation token for the operation</param>
		/// <returns>The reference data if the ref exists</returns>
		Task<IRef> GetRefAsync(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, CancellationToken CancellationToken = default);

		/// <summary>
		/// Checks if the given reference exists
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <param name="RefId">Ref identifier</param>
		/// <param name="CancellationToken">Cancellation token for the operation</param>
		/// <returns>True if the ref exists, false if it did not exist</returns>
		Task<bool> HasRefAsync(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, CancellationToken CancellationToken = default);

		/// <summary>
		/// Determines which refs are missing
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <param name="RefIds">Names of the references</param>
		/// <param name="CancellationToken">Cancellation token for the operation</param>
		/// <returns>List of missing references</returns>
		Task<List<RefId>> FindMissingRefsAsync(NamespaceId NamespaceId, BucketId BucketId, List<RefId> RefIds, CancellationToken CancellationToken = default);

		/// <summary>
		/// Attempts to sets the given reference, returning a list of missing objects on failure.
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <param name="RefId">Ref identifier</param>
		/// <param name="Value">New value for the reference</param>
		/// <param name="CancellationToken">Cancellation token for the operation</param>
		/// <returns>List of missing references</returns>
		Task<List<IoHash>> TrySetRefAsync(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, CbObject Value, CancellationToken CancellationToken = default);

		/// <summary>
		/// Attempts to finalize a reference, turning its references into hard references
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <param name="RefId">Ref identifier</param>
		/// <param name="Hash">Hash of the referenced object</param>
		/// <param name="CancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		Task<List<IoHash>> TryFinalizeRefAsync(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, IoHash Hash, CancellationToken CancellationToken = default);

		/// <summary>
		/// Removes the given reference
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <param name="RefId">Ref identifier</param>
		/// <param name="CancellationToken">Cancellation token for the operation</param>
		/// <returns>True if the ref was deleted, false if it did not exist</returns>
		Task<bool> DeleteRefAsync(NamespaceId NamespaceId, BucketId BucketId, RefId RefId, CancellationToken CancellationToken = default);

		#endregion
	}

	/// <summary>
	/// Extension methods for <see cref="IStorageClient"/>
	/// </summary>
	public static class StorageClientExtensions
	{
		const int DefaultMaxInMemoryBlobLength = 50 * 1024 * 1024;

		/// <summary>
		/// Gets a blob as a byte array
		/// </summary>
		/// <param name="StorageClient">The storage interface</param>
		/// <param name="NamespaceId">Namespace containing the blob</param>
		/// <param name="Hash">Hash of the blob to read</param>
		/// <param name="MaxInMemoryBlobLength">Maximum allowed memory allocation to store the blob</param>
		/// <returns>Data for the blob that was read. Throws an exception if the blob was not found.</returns>
		public static async Task<byte[]> ReadBlobToMemoryAsync(this IStorageClient StorageClient, NamespaceId NamespaceId, IoHash Hash, int MaxInMemoryBlobLength = DefaultMaxInMemoryBlobLength)
		{
			using Stream Stream = await StorageClient.ReadBlobAsync(NamespaceId, Hash);

			long Length = Stream.Length;
			if (Length > MaxInMemoryBlobLength)
			{
				throw new BlobException(NamespaceId, Hash, $"Blob {Hash} is too large ({Length} > {MaxInMemoryBlobLength})");
			}

			byte[] Buffer = new byte[Length];
			for (int Offset = 0; Offset < Length;)
			{
				int Count = await Stream.ReadAsync(Buffer, Offset, (int)Length - Offset);
				if (Count == 0)
				{
					throw new BlobException(NamespaceId, Hash, $"Unexpected end of stream reading blob {Hash}");
				}
				Offset += Count;
			}

			return Buffer;
		}

		/// <summary>
		/// Reads a blob and decodes it as a compact binary object
		/// </summary>
		/// <param name="StorageClient">The storage interface</param>
		/// <param name="NamespaceId">Namespace containing the blob</param>
		/// <param name="Hash">Hash of the blob to read</param>
		/// <param name="MaxInMemoryBlobLength">Maximum allowed memory allocation to store the blob</param>
		/// <returns>The decoded object</returns>
		public static async Task<CbObject> ReadObjectAsync(this IStorageClient StorageClient, NamespaceId NamespaceId, IoHash Hash, int MaxInMemoryBlobLength = DefaultMaxInMemoryBlobLength)
		{
			return new CbObject(await ReadBlobToMemoryAsync(StorageClient, NamespaceId, Hash, MaxInMemoryBlobLength));
		}

		/// <summary>
		/// Reads a blob and deserializes it as the given compact-binary encoded type
		/// </summary>
		/// <typeparam name="T">Type of object to deserialize</typeparam>
		/// <param name="StorageClient">The storage interface</param>
		/// <param name="NamespaceId">Namespace containing the blob</param>
		/// <param name="Hash">Hash of the blob to read</param>
		/// <param name="MaxInMemoryBlobLength">Maximum allowed memory allocation to store the blob</param>
		/// <returns>The decoded object</returns>
		public static async Task<T> ReadObjectAsync<T>(this IStorageClient StorageClient, NamespaceId NamespaceId, IoHash Hash, int MaxInMemoryBlobLength = DefaultMaxInMemoryBlobLength)
		{
			return CbSerializer.Deserialize<T>(await ReadBlobToMemoryAsync(StorageClient, NamespaceId, Hash, MaxInMemoryBlobLength));
		}

		/// <summary>
		/// Gets a blob as a byte array
		/// </summary>
		/// <param name="StorageClient">The storage interface</param>
		/// <param name="NamespaceId">Namespace containing the blob</param>
		/// <param name="Data">Data to write</param>
		/// <returns></returns>
		public static async Task<IoHash> WriteBlobFromMemoryAsync(this IStorageClient StorageClient, NamespaceId NamespaceId, ReadOnlyMemory<byte> Data)
		{
			IoHash Hash = IoHash.Compute(Data.Span);
			await WriteBlobFromMemoryAsync(StorageClient, NamespaceId, Hash, Data);
			return Hash;
		}

		/// <summary>
		/// Gets a blob as a byte array
		/// </summary>
		/// <param name="StorageClient">The storage interface</param>
		/// <param name="NamespaceId">Namespace containing the blob</param>
		/// <param name="Object">The object to be written</param>
		/// <returns></returns>
		public static Task<IoHash> WriteObjectAsync(this IStorageClient StorageClient, NamespaceId NamespaceId, CbObject Object)
		{
			return WriteBlobFromMemoryAsync(StorageClient, NamespaceId, Object.GetView());
		}

		/// <summary>
		/// Gets a blob as a byte array
		/// </summary>
		/// <param name="StorageClient">The storage interface</param>
		/// <param name="NamespaceId">Namespace containing the blob</param>
		/// <param name="Object">The object to be written</param>
		/// <returns></returns>
		public static Task<IoHash> WriteObjectAsync<T>(this IStorageClient StorageClient, NamespaceId NamespaceId, T Object)
		{
			CbWriter Writer = new CbWriter();
			CbSerializer.Serialize<T>(Writer, Object);
			return WriteObjectAsync(StorageClient, NamespaceId, Writer);
		}

		/// <summary>
		/// Gets a blob as a byte array
		/// </summary>
		/// <param name="StorageClient">The storage interface</param>
		/// <param name="NamespaceId">Namespace containing the blob</param>
		/// <param name="Writer">The object to be written</param>
		/// <returns></returns>
		public static async Task<IoHash> WriteObjectAsync<T>(this IStorageClient StorageClient, NamespaceId NamespaceId, CbWriter Writer)
		{
			IoHash Hash = Writer.ComputeHash();
			await StorageClient.WriteBlobAsync(NamespaceId, Hash, Writer.AsStream());
			return Hash;
		}

		/// <summary>
		/// Gets a blob as a byte array
		/// </summary>
		/// <param name="StorageClient">The storage interface</param>
		/// <param name="NamespaceId">Namespace containing the blob</param>
		/// <param name="Hash">Hash of the data</param>
		/// <param name="Data">The data to be written</param>
		/// <returns></returns>
		public static async Task WriteBlobFromMemoryAsync(this IStorageClient StorageClient, NamespaceId NamespaceId, IoHash Hash, ReadOnlyMemory<byte> Data)
		{
			using ReadOnlyMemoryStream Stream = new ReadOnlyMemoryStream(Data);
			await StorageClient.WriteBlobAsync(NamespaceId, Hash, Stream);
		}

		/// <summary>
		/// Reads a reference as a specific type
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="StorageClient">The storage interface</param>
		/// <param name="NamespaceId">Namespace containing the ref</param>
		/// <param name="BucketId">Bucket containing the ref</param>
		/// <param name="RefId">The ref id</param>
		/// <returns>Deserialized object for the given ref</returns>
		public static async Task<T> GetRefAsync<T>(this IStorageClient StorageClient, NamespaceId NamespaceId, BucketId BucketId, RefId RefId)
		{
			IRef Ref = await StorageClient.GetRefAsync(NamespaceId, BucketId, RefId);
			return CbSerializer.Deserialize<T>(Ref.Value);
		}

		/// <summary>
		/// Attempts to set a ref to a particular value
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="StorageClient">The storage interface</param>
		/// <param name="NamespaceId">Namespace containing the ref</param>
		/// <param name="BucketId">Bucket containing the ref</param>
		/// <param name="RefId">The ref id</param>
		/// <param name="Value">The new object for the ref</param>
		/// <returns>List of missing blob hashes</returns>
		public static Task<List<IoHash>> TrySetRefAsync<T>(this IStorageClient StorageClient, NamespaceId NamespaceId, BucketId BucketId, RefId RefId, T Value)
		{
			CbObject Object = CbSerializer.Serialize<T>(Value);
			return StorageClient.TrySetRefAsync(NamespaceId, BucketId, RefId, Object);
		}

		/// <summary>
		/// Sets a ref to a particular value
		/// </summary>
		/// <param name="StorageClient">The storage interface</param>
		/// <param name="NamespaceId">Namespace containing the ref</param>
		/// <param name="BucketId">Bucket containing the ref</param>
		/// <param name="RefId">The ref id</param>
		/// <param name="Value">The new object for the ref</param>
		public static async Task SetRefAsync(this IStorageClient StorageClient, NamespaceId NamespaceId, BucketId BucketId, RefId RefId, CbObject Value)
		{
			List<IoHash> MissingHashes = await StorageClient.TrySetRefAsync(NamespaceId, BucketId, RefId, Value);
			if (MissingHashes.Count > 0)
			{
				throw new RefMissingBlobException(NamespaceId, BucketId, RefId, MissingHashes);
			}
		}

		/// <summary>
		/// Sets a ref to a particular value
		/// </summary>
		/// <param name="StorageClient">The storage interface</param>
		/// <param name="NamespaceId">Namespace containing the ref</param>
		/// <param name="BucketId">Bucket containing the ref</param>
		/// <param name="RefId">The ref id</param>
		/// <param name="Value">The new object for the ref</param>
		public static Task SetRefAsync<T>(this IStorageClient StorageClient, NamespaceId NamespaceId, BucketId BucketId, RefId RefId, T Value)
		{
			CbObject Object = CbSerializer.Serialize<T>(Value);
			return SetRefAsync(StorageClient, NamespaceId, BucketId, RefId, Object);
		}

		/// <summary>
		/// Finalize a ref, throwing an exception if finalization fails
		/// </summary>
		/// <param name="StorageClient">The storage interface</param>
		/// <param name="NamespaceId">Namespace containing the ref</param>
		/// <param name="BucketId">Bucket containing the ref</param>
		/// <param name="RefId">The ref id</param>
		/// <param name="ValueHash">Hash of the ref value</param>
		public static async Task FinalizeRefAsync(this IStorageClient StorageClient, NamespaceId NamespaceId, BucketId BucketId, RefId RefId, IoHash ValueHash)
		{
			List<IoHash> MissingHashes = await StorageClient.TryFinalizeRefAsync(NamespaceId, BucketId, RefId, ValueHash);
			if (MissingHashes.Count > 0)
			{
				throw new RefMissingBlobException(NamespaceId, BucketId, RefId, MissingHashes);
			}
		}
	}
}
