// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using System;
using System.Buffers;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Storage.Bundles
{
	/// <summary>
	/// Bundle version number
	/// </summary>
	[System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1027:Mark enums with FlagsAttribute")]
	public enum BundleVersion
	{
		/// <summary>
		/// Initial version number
		/// </summary>
		Initial = 0,

		/// <summary>
		/// The current version number
		/// </summary>
		Current = Initial,
	}

	/// <summary>
	/// Header for the contents of a bundle. May contain an inlined payload object containing the object data itself.
	/// </summary>
	public class Bundle
	{
		/// <summary>
		/// Header for the bundle
		/// </summary>
		public BundleHeader Header { get; }

		/// <summary>
		/// Packet data as described in the header
		/// </summary>
		public ReadOnlyMemory<byte> Payload { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public Bundle(BundleHeader header, ReadOnlyMemory<byte> payload)
		{
			Header = header;
			Payload = payload;
		}

		/// <summary>
		/// Reads a bundle from a block of memory
		/// </summary>
		public Bundle(IMemoryReader reader)
		{
			Header = new BundleHeader(reader);
			int length = (int)reader.ReadUnsignedVarInt();
			Payload = reader.ReadFixedLengthBytes(length);
		}

		/// <summary>
		/// Serializes the bundle to a sequence of bytes
		/// </summary>
		/// <returns>Sequence for the bundle</returns>
		public ReadOnlySequence<byte> AsSequence()
		{
			ByteArrayBuilder builder = new ByteArrayBuilder();
			Header.Write(builder);
			builder.WriteUnsignedVarInt(Payload.Length);

			ReadOnlySequenceBuilder<byte> sequence = new ReadOnlySequenceBuilder<byte>();
			sequence.Append(builder.AsSequence());
			sequence.Append(Payload);

			return sequence.Construct();
		}
	}

	/// <summary>
	/// Header for the contents of a bundle.
	/// </summary>
	public class BundleHeader
	{
		/// <summary>
		/// References to nodes in other bundles
		/// </summary>
		public IReadOnlyList<BundleImport> Imports { get; } = new List<BundleImport>();

		/// <summary>
		/// Nodes exported from this bundle
		/// </summary>
		public IReadOnlyList<BundleExport> Exports { get; } = new List<BundleExport>();

		/// <summary>
		/// List of data packets within this bundle
		/// </summary>
		public IReadOnlyList<BundlePacket> Packets { get; } = new List<BundlePacket>();

		/// <summary>
		/// Constructs a new bundle header
		/// </summary>
		/// <param name="imports">Imports from other bundles</param>
		/// <param name="exports">Exports for nodes</param>
		/// <param name="packets">Compression packets within the bundle</param>
		public BundleHeader(IReadOnlyList<BundleImport> imports, IReadOnlyList<BundleExport> exports, IReadOnlyList<BundlePacket> packets)
		{
			Imports = imports;
			Exports = exports;
			Packets = packets;
		}

		/// <summary>
		/// Reads a bundle header from memory
		/// </summary>
		/// <param name="reader">Reader to deserialize from</param>
		/// <returns>New header object</returns>
		public BundleHeader(IMemoryReader reader)
		{
			BundleVersion version = (BundleVersion)reader.ReadUnsignedVarInt();
			if (version != BundleVersion.Current)
			{
				throw new InvalidDataException();
			}

			Imports = reader.ReadVariableLengthArray(() => new BundleImport(reader));
			Exports = reader.ReadVariableLengthArray(() => new BundleExport(reader));
			Packets = reader.ReadVariableLengthArray(() => new BundlePacket(reader));
		}

		/// <summary>
		/// Serializes a header to memory
		/// </summary>
		/// <param name="writer">Writer to serialize to</param>
		public void Write(IMemoryWriter writer)
		{
			writer.WriteUnsignedVarInt((ulong)BundleVersion.Current);

			writer.WriteVariableLengthArray(Imports, x => x.Write(writer));
			writer.WriteVariableLengthArray(Exports, x => x.Write(writer));
			writer.WriteVariableLengthArray(Packets, x => x.Write(writer));
		}
	}

	/// <summary>
	/// Reference to another tree pack object
	/// </summary>
	public class BundleImport
	{
		/// <summary>
		/// Blob containing the bundle data.
		/// </summary>
		public BlobId BlobId { get; }

		/// <summary>
		/// Number of exports from this blob.
		/// </summary>
		public int ExportCount { get; }

		/// <summary>
		/// Indexes of referenced nodes exported from this bundle
		/// </summary>
		public IReadOnlyList<(int, IoHash)> Exports { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BundleImport(BlobId blobId, int exportCount, IReadOnlyList<(int, IoHash)> exports)
		{
			BlobId = blobId;
			ExportCount = exportCount;
			Exports = exports;
		}

		/// <summary>
		/// Deserialize a bundle import
		/// </summary>
		/// <param name="reader">Reader to deserialize from</param>
		internal BundleImport(IMemoryReader reader)
		{
			BlobId = reader.ReadBlobId();

			ExportCount = (int)reader.ReadUnsignedVarInt();

			int count = (int)reader.ReadUnsignedVarInt();

			(int, IoHash)[] exports = new (int, IoHash)[count];
			for (int idx = 0; idx < count; idx++)
			{
				int index = (int)reader.ReadUnsignedVarInt();
				IoHash hash = reader.ReadIoHash();
				exports[idx] = (index, hash);
			}
			Exports = exports;
		}

		/// <summary>
		/// Serializes a bundle import
		/// </summary>
		/// <param name="writer">Writer to serialize to</param>
		public void Write(IMemoryWriter writer)
		{
			writer.WriteBlobId(BlobId);
			writer.WriteUnsignedVarInt(ExportCount);

			writer.WriteUnsignedVarInt(Exports.Count);
			for(int idx = 0; idx < Exports.Count; idx++)
			{
				(int index, IoHash hash) = Exports[idx];
				writer.WriteUnsignedVarInt(index);
				writer.WriteIoHash(hash);
			}
		}
	}

	/// <summary>
	/// Descriptor for a compression packet
	/// </summary>
	public class BundlePacket
	{
		/// <summary>
		/// Encoded length of the packet
		/// </summary>
		public int EncodedLength { get; set; }

		/// <summary>
		/// Decoded length of the packet
		/// </summary>
		public int DecodedLength { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="encodedLength">Size of the encoded data</param>
		/// <param name="decodedLength">Size of the decoded data</param>
		public BundlePacket(int encodedLength, int decodedLength)
		{
			EncodedLength = encodedLength;
			DecodedLength = decodedLength;
		}

		/// <summary>
		/// Deserialize a packet header from a memory reader
		/// </summary>
		/// <param name="reader">Reader to deserialize from</param>
		public BundlePacket(IMemoryReader reader)
		{
			EncodedLength = (int)reader.ReadUnsignedVarInt();
			DecodedLength = (int)reader.ReadUnsignedVarInt();
		}

		/// <summary>
		/// Serializes this packet header 
		/// </summary>
		/// <param name="writer"></param>
		public void Write(IMemoryWriter writer)
		{
			writer.WriteUnsignedVarInt(EncodedLength);
			writer.WriteUnsignedVarInt(DecodedLength);
		}
	}

	/// <summary>
	/// Entry for a node exported from an object
	/// </summary>
	public class BundleExport
	{
		/// <summary>
		/// Hash of the node
		/// </summary>
		public IoHash Hash { get; }

		/// <summary>
		/// Uncompressed length of this node
		/// </summary>
		public int Length { get; }
		
		/// <summary>
		/// Nodes referenced by this export. Indices in this array correspond to a lookup table consisting
        /// of the imported nodes in the order they are declared in the header, followed by nodes listed in the
        /// export table itself.
        /// </summary>
		public IReadOnlyList<int> References { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BundleExport(IoHash hash, int length, IReadOnlyList<int> references)
		{
			Hash = hash;
			Length = length;
			References = references;
		}

		/// <summary>
		/// Deserialize an export
		/// </summary>
		/// <param name="reader">Reader to deserialize from</param>
		public BundleExport(IMemoryReader reader)
		{
			Hash = reader.ReadIoHash();
			Length = (int)reader.ReadUnsignedVarInt();

			int numReferences = (int)reader.ReadUnsignedVarInt();
			int[] references = new int[numReferences];

			for (int idx = 0; idx < numReferences; idx++)
			{
				references[idx] = (int)reader.ReadUnsignedVarInt();
			}

			References = references;
		}

		/// <summary>
		/// Serializes an export
		/// </summary>
		/// <param name="writer">Writer to serialize to</param>
		public void Write(IMemoryWriter writer)
		{
			writer.WriteIoHash(Hash);
			writer.WriteUnsignedVarInt(Length);

			writer.WriteUnsignedVarInt(References.Count);
			for(int idx = 0; idx < References.Count; idx++)
			{
				writer.WriteUnsignedVarInt(References[idx]);
			}
		}
	}

	/// <summary>
	/// Extension methods for serializing bundles
	/// </summary>
	public static class BundleExtensions
	{
		/// <summary>
		/// Read a bundle from a blob store
		/// </summary>
		/// <param name="store">Blob store to read from</param>
		/// <param name="id">Identifier for the blob</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The bundle that was read</returns>
		public static async Task<Bundle> ReadBundleAsync(this IBlobStore store, BlobId id, CancellationToken cancellationToken)
		{
			IBlob blob = await store.ReadBlobAsync(id, cancellationToken);
			return await ReadBundleAsync(blob);
		}

		/// <summary>
		/// Read a bundle from a blob store
		/// </summary>
		/// <param name="store">Blob store to read from</param>
		/// <param name="id">Identifier for the blob</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The bundle that was read</returns>
		public static async Task<Bundle> ReadBundleAsync(this IBlobStore store, RefId id, CancellationToken cancellationToken)
		{
			IBlob blob = await store.ReadRefAsync(id, cancellationToken);
			return await ReadBundleAsync(blob);
		}

		/// <summary>
		/// Writes a bundle to a blob store
		/// </summary>
		/// <param name="store">Blob store to write to</param>
		/// <param name="id">Identifier for the blob</param>
		/// <param name="bundle">Bundle to write</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static Task WriteBundleAsync(this IBlobStore store, RefId id, Bundle bundle, CancellationToken cancellationToken)
		{
			ReadOnlySequence<byte> data = bundle.AsSequence();
			List<BlobId> imports = bundle.Header.Imports.Select(x => x.BlobId).ToList();
			return store.WriteRefAsync(id, data, imports, cancellationToken);
		}

		/// <summary>
		/// Writes a bundle to a blob store
		/// </summary>
		/// <param name="store">Blob store to write to</param>
		/// <param name="bundle">Bundle to write</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Identifier for the blob</returns>
		public static Task<BlobId> WriteBundleAsync(this IBlobStore store, Bundle bundle, CancellationToken cancellationToken)
		{
			ReadOnlySequence<byte> data = bundle.AsSequence();
			List<BlobId> imports = bundle.Header.Imports.Select(x => x.BlobId).ToList();
			return store.WriteBlobAsync(data, imports, cancellationToken);
		}

		static async Task<Bundle> ReadBundleAsync(IBlob blob)
		{
			ReadOnlyMemory<byte> data = await blob.GetDataAsync();
			MemoryReader reader = new MemoryReader(data);
			Bundle bundle = new Bundle(reader);
			reader.CheckEmpty();
			return bundle;
		}
	}
}
