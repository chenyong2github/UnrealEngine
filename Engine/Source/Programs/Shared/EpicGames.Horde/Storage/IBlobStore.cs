// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Base class for exceptions related to the blob store
	/// </summary>
	public class BlobException : Exception
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
	/// A referenced namespace for a blob was not found
	/// </summary>
	public class BlobNamespaceNotFoundException : BlobException
	{
		/// <summary>
		/// Constructor
		/// </summary>
		public BlobNamespaceNotFoundException(NamespaceId NamespaceId, IoHash Hash, Exception? InnerException = null)
			: base(NamespaceId, Hash, $"Unable to find namespace {NamespaceId} for blob {Hash}", InnerException)
		{
		}
	}

	/// <summary>
	/// Interface for a collection of blobs
	/// </summary>
	public interface IBlobStore
	{
		/// <summary>
		/// Determines whether a blob exists
		/// </summary>
		/// <param name="NamespaceId">Namespace to operate on</param>
		/// <param name="Hash"></param>
		/// <returns></returns>
		Task<bool> ExistsAsync(NamespaceId NamespaceId, IoHash Hash);

		/// <summary>
		/// Determines which blobs the storage collection needs
		/// </summary>
		/// <param name="NamespaceId">Namespace to operate on</param>
		/// <param name="Hashes"></param>
		/// <returns></returns>
		Task<List<IoHash>> NeedsAsync(NamespaceId NamespaceId, IEnumerable<IoHash> Hashes);

		/// <summary>
		/// Opens a blob read stream
		/// </summary>
		/// <param name="NamespaceId">Namespace to operate on</param>
		/// <param name="Hash">Hash of the blob</param>
		/// <returns>Stream for the blob, or null if it does not exist</returns>
		Task<Stream> ReadAsync(NamespaceId NamespaceId, IoHash Hash);

		/// <summary>
		/// Adds an item to storage
		/// </summary>
		/// <param name="NamespaceId">Namespace to operate on</param>
		/// <param name="Hash">Hash of the blob</param>
		/// <param name="Stream">The stream to write</param>
		Task WriteAsync(NamespaceId NamespaceId, IoHash Hash, Stream Stream);
	}

	/// <summary>
	/// Extension methods for <see cref="IBlobStore"/>
	/// </summary>
	public static class BlobStoreExtensions
	{
		const int DefaultMaxInMemoryBlobLength = 50 * 1024 * 1024;

		/// <summary>
		/// Gets a blob as a byte array
		/// </summary>
		/// <param name="Collection"></param>
		/// <param name="NamespaceId"></param>
		/// <param name="Hash"></param>
		/// <returns></returns>
		public static async Task<byte[]> ReadBytesAsync(this IBlobStore Collection, NamespaceId NamespaceId, IoHash Hash, int MaxInMemoryBlobLength = DefaultMaxInMemoryBlobLength)
		{
			using Stream Stream = await Collection.ReadAsync(NamespaceId, Hash);

			long Length = Stream.Length;
			if (Length > MaxInMemoryBlobLength)
			{
				throw new BlobException(NamespaceId, Hash, $"Blob {Hash} is too large ({Length} > {MaxInMemoryBlobLength})");
			}

			byte[] Buffer = new byte[Length];
			for(int Offset = 0; Offset < Length; )
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
		/// <param name="Collection"></param>
		/// <param name="NamespaceId"></param>
		/// <param name="Data">The data to be written</param>
		/// <returns></returns>
		public static async Task<IoHash> WriteBytesAsync(this IBlobStore Collection, NamespaceId NamespaceId, ReadOnlyMemory<byte> Data)
		{
			IoHash Hash = IoHash.Compute(Data.Span);
			await WriteBytesAsync(Collection, NamespaceId, Hash, Data);
			return Hash;
		}

		/// <summary>
		/// Gets a blob as a byte array
		/// </summary>
		/// <param name="Collection"></param>
		/// <param name="NamespaceId"></param>
		/// <param name="Hash">Hash of the data</param>
		/// <param name="Data">The data to be written</param>
		/// <returns></returns>
		public static async Task WriteBytesAsync(this IBlobStore Collection, NamespaceId NamespaceId, IoHash Hash, ReadOnlyMemory<byte> Data)
		{
			using ReadOnlyMemoryStream Stream = new ReadOnlyMemoryStream(Data);
			await Collection.WriteAsync(NamespaceId, Hash, Stream);
		}
	}
}
