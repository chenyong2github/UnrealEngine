using EpicGames.Core;
using EpicGames.Serialization;
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
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
		public IoHash RefId { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public RefException(NamespaceId NamespaceId, BucketId BucketId, IoHash RefId, string Message, Exception? InnerException = null)
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
		public RefNotFoundException(NamespaceId NamespaceId, BucketId BucketId, IoHash RefId, Exception? InnerException = null)
			: base(NamespaceId, BucketId, RefId, $"Ref {NamespaceId}/{BucketId}/{RefId} not found", InnerException)
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
		IoHash RefId { get; }

		/// <summary>
		/// The value stored for this ref
		/// </summary>
		CbObject Value { get; }
	}

	/// <summary>
	/// Interface for a collection of ref documents
	/// </summary>
	public interface IStorageClient
	{
		#region Blobs

		/// <summary>
		/// Opens a blob read stream
		/// </summary>
		/// <param name="NamespaceId">Namespace to operate on</param>
		/// <param name="Hash">Hash of the blob</param>
		/// <returns>Stream for the blob, or null if it does not exist</returns>
		Task<Stream> ReadBlobAsync(NamespaceId NamespaceId, IoHash Hash);

		/// <summary>
		/// Writes a blob to storage
		/// </summary>
		/// <param name="NamespaceId">Namespace to operate on</param>
		/// <param name="Hash">Hash of the blob</param>
		/// <param name="Stream">The stream to write</param>
		Task WriteBlobAsync(NamespaceId NamespaceId, IoHash Hash, Stream Stream);

		#endregion

		#region Refs

		/// <summary>
		/// Gets the given reference
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <param name="RefId">Name of the reference</param>
		/// <returns>The reference data if the ref exists</returns>
		Task<IRef> GetRefAsync(NamespaceId NamespaceId, BucketId BucketId, IoHash RefId);

		/// <summary>
		/// Gets the given reference
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <param name="RefId">Ref identifier</param>
		/// <returns>The reference data if the ref exists</returns>
		Task<bool> HasRefAsync(NamespaceId NamespaceId, BucketId BucketId, IoHash RefId);

		/// <summary>
		/// Determines which refs are missing
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <param name="RefIds">Names of the references</param>
		/// <returns>List of missing references</returns>
		Task<List<IoHash>> FindMissingRefsAsync(NamespaceId NamespaceId, BucketId BucketId, List<IoHash> RefIds);

		/// <summary>
		/// Sets the given reference
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <param name="RefId">Ref identifier</param>
		/// <param name="Value">New value for the reference</param>
		/// <returns>List of missing references</returns>
		Task<List<IoHash>> SetRefAsync(NamespaceId NamespaceId, BucketId BucketId, IoHash RefId, CbObject Value);

		/// <summary>
		/// Attempts to finalize a reference, turning its references into hard references
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <param name="RefId">Ref identifier</param>
		/// <param name="Hash">Hash of the referenced object</param>
		/// <returns></returns>
		Task<List<IoHash>> FinalizeRefAsync(NamespaceId NamespaceId, BucketId BucketId, IoHash RefId, IoHash Hash);

		/// <summary>
		/// Removes the given reference
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <param name="RefId">Ref identifier</param>
		/// <returns>True if the ref was deleted, false if it did not exist</returns>
		Task<bool> DeleteRefAsync(NamespaceId NamespaceId, BucketId BucketId, IoHash RefId);

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
		/// <param name="StorageClient"></param>
		/// <param name="NamespaceId"></param>
		/// <param name="Hash"></param>
		/// <returns></returns>
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
		/// Gets a blob as a byte array
		/// </summary>
		/// <param name="StorageClient"></param>
		/// <param name="NamespaceId"></param>
		/// <param name="Data">The data to be written</param>
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
		/// <param name="StorageClient"></param>
		/// <param name="NamespaceId"></param>
		/// <param name="Hash">Hash of the data</param>
		/// <param name="Data">The data to be written</param>
		/// <returns></returns>
		public static async Task WriteBlobFromMemoryAsync(this IStorageClient StorageClient, NamespaceId NamespaceId, IoHash Hash, ReadOnlyMemory<byte> Data)
		{
			using ReadOnlyMemoryStream Stream = new ReadOnlyMemoryStream(Data);
			await StorageClient.WriteBlobAsync(NamespaceId, Hash, Stream);
		}
	}
}
