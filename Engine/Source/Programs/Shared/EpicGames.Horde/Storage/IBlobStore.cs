// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;

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
		ReadOnlyMemory<byte> Data { get; }

		/// <summary>
		/// Find the outward references for a node
		/// </summary>
		/// <returns></returns>
		IReadOnlyList<BlobId> References { get; }
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
		Task<IBlob?> TryReadBlobAsync(BlobId id, CancellationToken cancellationToken = default);

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
		Task<IBlob?> TryReadRefAsync(RefId id, CancellationToken cancellationToken = default);

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
		Task DeleteRefAsync(RefId id, CancellationToken cancellationToken = default);

		#endregion
	}

	/// <summary>
	/// Extension methods for <see cref="IBlobStore"/>
	/// </summary>
	public static class BlobStoreExtensions
	{
		/// <summary>
		/// Reads a blob from the store, throwing an exception if it does not exist
		/// </summary>
		/// <param name="store">The store instance to read from</param>
		/// <param name="id">Id for the blob</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The blob instance</returns>
		public static async Task<IBlob> ReadBlobAsync(this IBlobStore store, BlobId id, CancellationToken cancellationToken = default)
		{
			IBlob? blob = await store.TryReadBlobAsync(id, cancellationToken);
			if (blob == null)
			{
				throw new KeyNotFoundException($"Unable to find blob {id}");
			}
			return blob;
		}

		/// <summary>
		/// Reads a ref from the store, throwing an exception if it does not exist
		/// </summary>
		/// <param name="store">The store instance to read from</param>
		/// <param name="id">Id for the ref</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The blob instance</returns>
		public static async Task<IBlob> ReadRefAsync(this IBlobStore store, RefId id, CancellationToken cancellationToken = default)
		{
			IBlob? blob = await store.TryReadRefAsync(id, cancellationToken);
			if (blob == null)
			{
				throw new KeyNotFoundException($"Unable to find ref {id}");
			}
			return blob;
		}
	}

	/// <summary>
	/// Utility methods for blobs
	/// </summary>
	public static class BlobUtils
	{
		class InMemoryBlob : IBlob
		{
			/// <inheritdoc/>
			public ReadOnlyMemory<byte> Data { get; }

			/// <inheritdoc/>
			public IReadOnlyList<BlobId> References { get; }

			public InMemoryBlob(ReadOnlyMemory<byte> data, IReadOnlyList<BlobId> references)
			{
				Data = data;
				References = references;
			}
		}

		/// <summary>
		/// Create a blob from memory
		/// </summary>
		/// <param name="data">Payload for the blob</param>
		/// <param name="references">References to other blobs</param>
		/// <returns>Blob instance</returns>
		public static IBlob FromMemory(ReadOnlyMemory<byte> data, IReadOnlyList<BlobId> references) => new InMemoryBlob(data, references);

		/// <summary>
		/// Serialize a blob into a flat memory buffer
		/// </summary>
		/// <param name="blob">The blob to serialize</param>
		/// <returns>Serialized data</returns>
		public static ReadOnlySequence<byte> Serialize(IBlob blob)
		{
			return Serialize(blob.Data, blob.References);
		}

		/// <summary>
		/// Serialize a blob into a flat memory buffer
		/// </summary>
		/// <param name="data">Data for the blob</param>
		/// <param name="references">List of references to other blobs</param>
		/// <returns>Serialized data</returns>
		public static ReadOnlySequence<byte> Serialize(ReadOnlyMemory<byte> data, IReadOnlyList<BlobId> references)
		{
			return Serialize(new ReadOnlySequence<byte>(data), references);
		}

		/// <summary>
		/// Serialize a blob into a flat memory buffer
		/// </summary>
		/// <param name="data">Data for the blob</param>
		/// <param name="references">List of references to other blobs</param>
		/// <returns>Serialized data</returns>
		public static ReadOnlySequence<byte> Serialize(ReadOnlySequence<byte> data, IReadOnlyList<BlobId> references)
		{
			ByteArrayBuilder writer = new ByteArrayBuilder();
			writer.WriteVariableLengthArray(references, x => writer.WriteBlobId(x));
			writer.WriteUnsignedVarInt((ulong)data.Length);

			ReadOnlySequenceBuilder<byte> builder = new ReadOnlySequenceBuilder<byte>();
			builder.Append(writer.ToByteArray());
			builder.Append(data);

			return builder.Construct();
		}

		/// <summary>
		/// Deserialize a blob from a block of memory
		/// </summary>
		/// <param name="memory">Memory to deserialize from</param>
		/// <returns>Deserialized blob data. May reference the supplied memory.</returns>
		public static IBlob Deserialize(ReadOnlyMemory<byte> memory) => Deserialize(new ReadOnlySequence<byte>(memory));

		/// <summary>
		/// Deserialize a blob from a block of memory
		/// </summary>
		/// <param name="sequence">Sequence to deserialize from</param>
		/// <returns>Deserialized blob data. May reference the supplied memory.</returns>
		public static IBlob Deserialize(ReadOnlySequence<byte> sequence)
		{
			MemoryReader reader = new MemoryReader(sequence.First);
			IReadOnlyList<BlobId> references = reader.ReadVariableLengthArray(() => reader.ReadBlobId());
			long length = (long)reader.ReadUnsignedVarInt();

			ReadOnlyMemory<byte> data = sequence.Slice(sequence.First.Length - reader.Memory.Length).AsSingleSegment();
			return new InMemoryBlob(data, references);
		}
	}
}
