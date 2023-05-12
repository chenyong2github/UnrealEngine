// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using K4os.Compression.LZ4;
using System;
using System.Buffers;
using System.Buffers.Binary;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Storage
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
		/// Added the BundleExport.Alias property
		/// </summary>
		ExportAliases = 1,

		/// <summary>
		/// Back out change to include aliases. Will likely do this through an API rather than baked into the data. 
		/// </summary>
		RemoveAliases = 2,

		/// <summary>
		/// Last item in the enum. Used for <see cref="Latest"/>
		/// </summary>
		LatestPlusOne,

#pragma warning disable CA1069 // Enums values should not be duplicated
		/// <summary>
		/// The current version number
		/// </summary>
		Latest = (int)LatestPlusOne - 1,
#pragma warning restore CA1069 // Enums values should not be duplicated
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
		public IReadOnlyList<ReadOnlyMemory<byte>> Packets { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public Bundle(BundleHeader header, IReadOnlyList<ReadOnlyMemory<byte>> packets)
		{
			Header = header;
			Packets = packets;
		}

		/// <summary>
		/// Reads a bundle from a block of memory
		/// </summary>
		public Bundle(IMemoryReader reader)
		{
			Header = new BundleHeader(reader);

			ReadOnlyMemory<byte>[] packets = new ReadOnlyMemory<byte>[Header.Packets.Count];
			for (int idx = 0; idx < Header.Packets.Count; idx++)
			{
				BundlePacket packet = Header.Packets[idx];
				packets[idx] = reader.ReadFixedLengthBytes(packet.EncodedLength);
			}

			Packets = packets;
		}

		/// <summary>
		/// Reads a bundle from the given stream
		/// </summary>
		/// <param name="stream">Stream to read from</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Bundle that was read</returns>
		public static async Task<Bundle> FromStreamAsync(Stream stream, CancellationToken cancellationToken)
		{
			BundleHeader header = await BundleHeader.FromStreamAsync(stream, cancellationToken);

			ReadOnlyMemory<byte>[] packets = new ReadOnlyMemory<byte>[header.Packets.Count];
			for (int idx = 0; idx < header.Packets.Count; idx++)
			{
				BundlePacket packet = header.Packets[idx];

				byte[] data = new byte[packet.EncodedLength];
				await stream.ReadFixedLengthBytesAsync(data, cancellationToken);

				packets[idx] = data;
			}

			return new Bundle(header, packets);
		}

		/// <summary>
		/// Serializes the bundle to a sequence of bytes
		/// </summary>
		/// <returns>Sequence for the bundle</returns>
		public ReadOnlySequence<byte> AsSequence()
		{
			ByteArrayBuilder builder = new ByteArrayBuilder();
			Header.Write(builder);

			ReadOnlySequenceBuilder<byte> sequence = new ReadOnlySequenceBuilder<byte>();
			sequence.Append(builder.AsSequence());

			foreach (ReadOnlyMemory<byte> packet in Packets)
			{
				sequence.Append(packet);
			}

			return sequence.Construct();
		}
	}

	/// <summary>
	/// Indicates the compression format in the bundle
	/// </summary>
	public enum BundleCompressionFormat
	{
		/// <summary>
		/// Packets are uncompressed
		/// </summary>
		None = 0,

		/// <summary>
		/// LZ4 compression
		/// </summary>
		LZ4 = 1,

		/// <summary>
		/// Gzip compression
		/// </summary>
		Gzip = 2,

		/// <summary>
		/// Oodle compression (Kraken)
		/// </summary>
		Oodle = 3,
	}

	/// <summary>
	/// Header for the contents of a bundle.
	/// </summary>
	public class BundleHeader
	{
		/// <summary>
		/// Signature bytes
		/// </summary>
		public static ReadOnlyMemory<byte> Signature { get; } = Encoding.UTF8.GetBytes("UEBN");

		/// <summary>
		/// Length of the prelude data
		/// </summary>
		public const int PreludeLength = 8;

		/// <summary>
		/// Types for exports within this bundle
		/// </summary>
		public IReadOnlyList<BundleType> Types { get; }

		/// <summary>
		/// Bundles that we reference nodes in
		/// </summary>
		public BundleImportCollection Imports { get; }

		/// <summary>
		/// Nodes exported from this bundle
		/// </summary>
		public BundleExportCollection Exports { get; }

		/// <summary>
		/// List of data packets within this bundle
		/// </summary>
		public BundlePacketCollection Packets { get; }

		/// <summary>
		/// Constructs a new bundle header
		/// </summary>
		/// <param name="types">Type array indexed by each export</param>
		/// <param name="imports">Imported bundles</param>
		/// <param name="exports">Exports for nodes</param>
		/// <param name="packets">Compression packets within the bundle</param>
		public BundleHeader(IReadOnlyList<BundleType> types, IReadOnlyList<BlobLocator> imports, IReadOnlyList<BundleExport> exports, IReadOnlyList<BundlePacket> packets)
			: this(types, new BundleImportCollection(imports), new BundleExportCollection(exports), new BundlePacketCollection(packets))
		{
		}

		/// <summary>
		/// Constructs a new bundle header
		/// </summary>
		/// <param name="types">Type array indexed by each export</param>
		/// <param name="imports">Imported bundles</param>
		/// <param name="exports">Exports for nodes</param>
		/// <param name="packets">Compression packets within the bundle</param>
		public BundleHeader(IReadOnlyList<BundleType> types, BundleImportCollection imports, BundleExportCollection exports, BundlePacketCollection packets)
		{
			Types = types;
			Imports = imports;
			Exports = exports;
			Packets = packets;
		}

		record class ExportInfo(int TypeIdx, IoHash Hash, int Length, List<BundleExportRef> References);

		/// <summary>
		/// Reads a bundle header from memory
		/// </summary>
		/// <param name="reader">Reader to deserialize from</param>
		/// <returns>New header object</returns>
		public BundleHeader(IMemoryReader reader)
		{
			ReadOnlyMemory<byte> prelude = reader.GetMemory(8);
			CheckPrelude(prelude);
			reader.Advance(8);

			BundleVersion version = (BundleVersion)reader.ReadUnsignedVarInt();
			if (version > BundleVersion.Latest)
			{
				throw new InvalidDataException($"Unknown bundle version {(int)version}. Max supported is {(int)BundleVersion.Latest}.");
			}

			BundleCompressionFormat compressionFormat = (BundleCompressionFormat)reader.ReadUnsignedVarInt();

			// Read the types
			int numTypes = (int)reader.ReadUnsignedVarInt();
			List<BundleType> types = new List<BundleType>(numTypes);

			for (int typeIdx = 0; typeIdx < numTypes; typeIdx++)
			{
				Guid guid = reader.ReadGuid();
				int serializerVersion = (int)reader.ReadUnsignedVarInt();

				types.Add(new BundleType(guid, serializerVersion));
			}

			Types = types;

			// Read the imports
			int numImports = (int)reader.ReadUnsignedVarInt();
			List<BlobLocator> imports = new List<BlobLocator>(numImports);
			List<BundleExportRef> allExportReferences = new List<BundleExportRef>();

			for (int importIdx = 0; importIdx < numImports; importIdx++)
			{
				BlobLocator locator = reader.ReadBlobLocator();
				imports.Add(locator);

				int[] exportIndexes = reader.ReadVariableLengthArray(() => (int)reader.ReadUnsignedVarInt());
				for (int exportIdx = 0; exportIdx < exportIndexes.Length; exportIdx++)
				{
					BundleExportRef exportReference = new BundleExportRef(imports.Count - 1, exportIndexes[exportIdx]);
					allExportReferences.Add(exportReference);
				}
			}

			Imports = new BundleImportCollection(imports);

			// Read the exports
			int numExports = (int)reader.ReadUnsignedVarInt();
			List<ExportInfo> exportInfos = new List<ExportInfo>(numExports);

			for (int exportIdx = 0; exportIdx < numExports; exportIdx++)
			{
				allExportReferences.Add(new BundleExportRef(-1, exportIdx));

				int typeIdx = (int)reader.ReadUnsignedVarInt();
				IoHash hash = reader.ReadIoHash();
				int length = (int)reader.ReadUnsignedVarInt();

				List<BundleExportRef> exportRefs = new List<BundleExportRef>();

				int numReferences = (int)reader.ReadUnsignedVarInt();
				if (numReferences > 0)
				{
					for (int idx = 0; idx < numReferences; idx++)
					{
						int referenceIdx = (int)reader.ReadUnsignedVarInt();
						exportRefs.Add(allExportReferences[referenceIdx]);
					}
				}

				if (version == BundleVersion.ExportAliases)
				{
					_ = reader.ReadUtf8String();
				}

				exportInfos.Add(new ExportInfo(typeIdx, hash, length, exportRefs));
			}

			// Read the compression packets
			if (compressionFormat == BundleCompressionFormat.None)
			{
				List<BundlePacket> packets = new List<BundlePacket>(exportInfos.Count);

				int encodedOffset = 0;
				foreach (ExportInfo export in exportInfos)
				{
					packets.Add(new BundlePacket(compressionFormat, encodedOffset, export.Length, export.Length));
					encodedOffset += export.Length;
				}

				Packets = new BundlePacketCollection(packets);
			}
			else
			{
				int numPackets = (int)reader.ReadUnsignedVarInt();
				List<BundlePacket> packets = new List<BundlePacket>(numPackets);

				int encodedOffset = 0;
				for (int packetIdx = 0; packetIdx < numPackets; packetIdx++)
				{
					int encodedLength = (int)reader.ReadUnsignedVarInt();
					int decodedLength = (int)reader.ReadUnsignedVarInt();
					packets.Add(new BundlePacket(compressionFormat, encodedOffset, encodedLength, decodedLength));
					encodedOffset += encodedLength;
				}

				Packets = new BundlePacketCollection(packets);
			}

			// Create the final export list
			List<BundleExport> exports = new List<BundleExport>();
			{
				int packetIdx = 0;
				int packetOffset = 0;

				foreach (ExportInfo exportInfo in exportInfos)
				{
					if (packetOffset + exportInfo.Length > Packets[packetIdx].DecodedLength)
					{
						packetIdx++;
						packetOffset = 0;
					}

					exports.Add(new BundleExport(exportInfo.TypeIdx, exportInfo.Hash, packetIdx, packetOffset, exportInfo.Length, exportInfo.References));
					packetOffset += exportInfo.Length;
				}
			}
			Exports = new BundleExportCollection(exports);
		}

		/// <summary>
		/// Reads the prelude bytes from a bundle, and returns the header size.
		/// </summary>
		/// <param name="prelude">The prelude data</param>
		/// <returns>Size of the header, including the prelude data</returns>
		public static int ReadPrelude(ReadOnlyMemory<byte> prelude)
		{
			CheckPrelude(prelude);
			return BinaryPrimitives.ReadInt32BigEndian(prelude.Span.Slice(4));
		}

		/// <summary>
		/// Reads a bundle header from a stream
		/// </summary>
		/// <param name="stream">Stream to read from</param>
		/// <param name="cancellationToken">Cancellation token for the stream</param>
		/// <returns>New header</returns>
		public static async Task<BundleHeader> FromStreamAsync(Stream stream, CancellationToken cancellationToken)
		{
			byte[] prelude = new byte[8];
			await stream.ReadFixedLengthBytesAsync(prelude, cancellationToken);
			CheckPrelude(prelude);

			int headerLength = BinaryPrimitives.ReadInt32BigEndian(prelude.AsSpan(4));
			using (IMemoryOwner<byte> owner = MemoryPool<byte>.Shared.Rent(headerLength))
			{
				Memory<byte> header = owner.Memory.Slice(0, headerLength);
				prelude.CopyTo(header);

				await stream.ReadFixedLengthBytesAsync(header.Slice(prelude.Length), cancellationToken);
				return new BundleHeader(new MemoryReader(header));
			}
		}

		/// <summary>
		/// Validates that the prelude bytes for a bundle header are correct
		/// </summary>
		/// <param name="prelude">The prelude bytes</param>
		static void CheckPrelude(ReadOnlyMemory<byte> prelude)
		{
			if (!prelude.Slice(0, 4).Span.SequenceEqual(Signature.Span))
			{
				throw new InvalidDataException("Invalid signature bytes for bundle. Corrupt data?");
			}
		}

		/// <summary>
		/// Serializes a header to memory
		/// </summary>
		/// <param name="writer">Writer to serialize to</param>
		public void Write(IMemoryWriter writer)
		{
			int initialLength = writer.Length;

			// Write the header prelude; a fixed-length block containing a signature and header size.
			Memory<byte> prelude = writer.GetMemory(8);
			Signature.CopyTo(prelude);
			writer.Advance(8);

			// Write the header data
			writer.WriteUnsignedVarInt((ulong)BundleVersion.Latest);

			BundleCompressionFormat compressionFormat = (Packets.Count > 0) ? Packets[0].CompressionFormat : BundleCompressionFormat.None;

			writer.WriteUnsignedVarInt((ulong)compressionFormat);
			writer.WriteVariableLengthArray(Types, x => x.Write(writer));

			// Find all the referenced nodes in each import
			SortedSet<int>[] nodes = new SortedSet<int>[Imports.Count];
			for (int idx = 0; idx < Imports.Count; idx++)
			{
				nodes[idx] = new SortedSet<int>();
			}
			foreach (BundleExportRef exportRef in Exports.SelectMany(x => x.References).Where(x => x.ImportIdx != -1))
			{
				nodes[exportRef.ImportIdx].Add(exportRef.NodeIdx);
			}

			// Map all the imports to an index
			Dictionary<BundleExportRef, int> exportRefToIndex = new Dictionary<BundleExportRef, int>();
			for (int importIdx = 0; importIdx < Imports.Count; importIdx++)
			{
				foreach (int nodeIdx in nodes[importIdx])
				{
					int index = exportRefToIndex.Count;
					exportRefToIndex[new BundleExportRef(importIdx, nodeIdx)] = index;
				}
			}
			for (int exportIdx = 0; exportIdx < Exports.Count; exportIdx++)
			{
				int index = exportRefToIndex.Count;
				exportRefToIndex[new BundleExportRef(-1, exportIdx)] = index;
			}

			// Write all the imports
			writer.WriteUnsignedVarInt(Imports.Count);
			for (int importIdx = 0; importIdx < Imports.Count; importIdx++)
			{
				Debug.Assert(Imports[importIdx].IsValid());
				writer.WriteBlobLocator(Imports[importIdx]);
				writer.WriteVariableLengthArray(nodes[importIdx].ToArray(), x => writer.WriteUnsignedVarInt(x));
			}

			// Write all the exports
			writer.WriteUnsignedVarInt(Exports.Count);
			foreach (BundleExport export in Exports)
			{
				writer.WriteUnsignedVarInt(export.TypeIdx);
				writer.WriteIoHash(export.Hash);
				writer.WriteUnsignedVarInt(export.Length);

				writer.WriteUnsignedVarInt(export.References.Count);
				for (int idx = 0; idx < export.References.Count; idx++)
				{
					writer.WriteUnsignedVarInt(exportRefToIndex[export.References[idx]]);
				}
			}

			// Write the packets
			if (compressionFormat != BundleCompressionFormat.None)
			{
				writer.WriteUnsignedVarInt(Packets.Count);
				foreach(BundlePacket packet in Packets)
				{
					writer.WriteUnsignedVarInt(packet.EncodedLength);
					writer.WriteUnsignedVarInt(packet.DecodedLength);
				}
			}

			// Go back and write the length of the header
			BinaryPrimitives.WriteInt32BigEndian(prelude.Slice(4).Span, writer.Length - initialLength);
		}
	}

	/// <summary>
	/// Information about a type within a bundle
	/// </summary>
	[DebuggerDisplay("{Guid}#{Version}")]
	public class BundleType : IEquatable<BundleType>
	{
		/// <summary>
		/// Number of bytes in a serialized object
		/// </summary>
		public const int NumBytes = 20;

		/// <summary>
		/// Nominal identifier for the type
		/// </summary>
		public Guid Guid { get; }

		/// <summary>
		/// Version number for the serializer
		/// </summary>
		public int Version { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="guid">Nominal identifier for the type</param>
		/// <param name="version">Version number for the serializer</param>
		public BundleType(Guid guid, int version)
		{
			Guid = guid;
			Version = version;
		}

		/// <summary>
		/// Serializes a type to storage
		/// </summary>
		/// <param name="writer">Writer for serialization</param>
		public void Write(IMemoryWriter writer)
		{
			writer.WriteGuid(Guid);
			writer.WriteUnsignedVarInt(Version);
		}

		/// <summary>
		/// Parse a span of characters as a bundle type
		/// </summary>
		/// <param name="text">Text to parse</param>
		/// <returns>The parsed bundle type</returns>
		public static BundleType Parse(ReadOnlySpan<char> text)
		{
			int hashIdx = text.IndexOf('#');
			Guid guid = Guid.Parse(text.Slice(0, hashIdx));
			int version = Int32.Parse(text.Slice(hashIdx + 1), NumberStyles.None, CultureInfo.InvariantCulture);
			return new BundleType(guid, version);
		}

		/// <inheritdoc/>
		public override bool Equals(object? obj) => obj is BundleType type && Equals(type);

		/// <inheritdoc/>
		public override int GetHashCode() => HashCode.Combine(Guid, Version);

		/// <inheritdoc/>
		public bool Equals(BundleType? type) => type is object && Guid == type.Guid && Version == type.Version;

		/// <inheritdoc/>
		public override string ToString() => $"{Guid}#{Version}";

		/// <inheritdoc/>
		public static bool operator ==(BundleType lhs, BundleType rhs) => lhs.Equals(rhs);

		/// <inheritdoc/>
		public static bool operator !=(BundleType lhs, BundleType rhs) => !lhs.Equals(rhs);
	}

	/// <summary>
	/// Collection of imported node references
	/// </summary>
	public struct BundleImportCollection : IReadOnlyList<BlobLocator>
	{
		readonly ReadOnlyMemory<byte> _data;

		/// <summary>
		/// Deserializing constructor
		/// </summary>
		public BundleImportCollection(ReadOnlyMemory<byte> data) => _data = data;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="locators">Locators to write to the </param>
		public BundleImportCollection(IReadOnlyCollection<BlobLocator> locators)
		{
			byte[] data = new byte[Measure(locators)];
			Write(data, locators);
			_data = data;
		}

		/// <summary>
		/// Retrieve a single import from the collection
		/// </summary>
		public BlobLocator this[int index]
		{
			get
			{
				ReadOnlySpan<byte> span = _data.Span;
				int offset = BinaryPrimitives.ReadInt32LittleEndian(span.Slice(index * sizeof(int)));
				int length = span.Slice(offset).IndexOf((byte)0);
				return new BlobLocator(new Utf8String(_data.Slice(offset, length)));
			}
		}

		/// <inheritdoc/>
		public int Count => (_data.Length == 0)? 0 : BinaryPrimitives.ReadInt32LittleEndian(_data.Span) / sizeof(int);

		/// <inheritdoc/>
		public IEnumerator<BlobLocator> GetEnumerator()
		{
			int count = Count;
			for(int idx = 0; idx < count; idx++)
			{
				yield return this[idx];
			}
		}

		/// <inheritdoc/>
		IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

		/// <summary>
		/// Measure the size of memory required to store a collection of import locators
		/// </summary>
		/// <param name="locators">Locators to write</param>
		/// <returns>Size in bytes of the output buffer</returns>
		public static int Measure(IReadOnlyCollection<BlobLocator> locators)
		{
			int length = 0;
			foreach (BlobLocator locator in locators)
			{
				length += sizeof(int) + locator.Inner.Length + 1;
			}
			return length;
		}

		/// <summary>
		/// Serialize a collection of locators to memory
		/// </summary>
		/// <param name="data">Output buffer for the serialized data</param>
		/// <param name="locators">Locators to write</param>
		public static void Write(Span<byte> data, IReadOnlyCollection<BlobLocator> locators)
		{
			Span<byte> next = data;

			int offset = locators.Count * sizeof(int);
			foreach(BlobLocator locator in locators)
			{
				BinaryPrimitives.WriteInt32LittleEndian(next, offset);
				offset += locator.Inner.Length + 1;
				next = next.Slice(sizeof(int));
			}

			foreach (BlobLocator locator in locators)
			{
				locator.Inner.Span.CopyTo(next);
				next = next.Slice(locator.Inner.Length);

				next[0] = 0;
				next = next.Slice(1);
			}
		}
	}

	/// <summary>
	/// Descriptor for a compression packet
	/// </summary>
	public struct BundlePacket
	{
		/// <summary>
		/// Size of this structure when serialized
		/// </summary>
		public const int NumBytes = 16;

		/// <summary>
		/// Compression format for this packet
		/// </summary>
		public BundleCompressionFormat CompressionFormat { get; }

		/// <summary>
		/// Offset of the packet within the payload stream
		/// </summary>
		public int EncodedOffset { get; }

		/// <summary>
		/// Encoded length of the packet
		/// </summary>
		public int EncodedLength { get; }

		/// <summary>
		/// Decoded length of the packet
		/// </summary>
		public int DecodedLength { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="compressionFormat">Compression format for the packet</param>
		/// <param name="encodedOffset">Offset of the data within the payload stream</param>
		/// <param name="encodedLength">Size of the encoded data</param>
		/// <param name="decodedLength">Size of the decoded data</param>
		public BundlePacket(BundleCompressionFormat compressionFormat, int encodedOffset, int encodedLength, int decodedLength)
		{
			CompressionFormat = compressionFormat;
			EncodedOffset = encodedOffset;
			EncodedLength = encodedLength;
			DecodedLength = decodedLength;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public BundlePacket(ReadOnlySpan<byte> span)
		{
			CompressionFormat = (BundleCompressionFormat)BinaryPrimitives.ReadInt32LittleEndian(span);
			EncodedOffset = BinaryPrimitives.ReadInt32LittleEndian(span.Slice(4));
			EncodedLength = BinaryPrimitives.ReadInt32LittleEndian(span.Slice(8));
			DecodedLength = BinaryPrimitives.ReadInt32LittleEndian(span.Slice(12));
		}

		/// <summary>
		/// Serialize the struct to memory
		/// </summary>
		public void CopyTo(Span<byte> span)
		{
			BinaryPrimitives.WriteInt32LittleEndian(span, (int)CompressionFormat);
			BinaryPrimitives.WriteInt32LittleEndian(span.Slice(4), EncodedOffset);
			BinaryPrimitives.WriteInt32LittleEndian(span.Slice(8), EncodedLength);
			BinaryPrimitives.WriteInt32LittleEndian(span.Slice(12), DecodedLength);
		}
	}

	/// <summary>
	/// Collection of information about packets in a bundle
	/// </summary>
	public struct BundlePacketCollection : IReadOnlyCollection<BundlePacket>
	{
		readonly ReadOnlyMemory<byte> _data;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="data"></param>
		public BundlePacketCollection(ReadOnlyMemory<byte> data) => _data = data;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="packets">Packets to include in this collection</param>
		public BundlePacketCollection(IReadOnlyCollection<BundlePacket> packets)
		{
			byte[] data = new byte[Measure(packets)];
			Write(data, packets);
			_data = data;
		}

		/// <inheritdoc/>
		public int Count => _data.Length / BundlePacket.NumBytes;

		/// <inheritdoc/>
		public BundlePacket this[int index] => new BundlePacket(_data.Slice(index * BundlePacket.NumBytes, BundlePacket.NumBytes).Span);

		/// <inheritdoc/>
		public IEnumerator<BundlePacket> GetEnumerator()
		{
			ReadOnlyMemory<byte> remaining = _data;
			while (remaining.Length > 0)
			{
				yield return new BundlePacket(remaining.Slice(0, BundlePacket.NumBytes).Span);
				remaining = remaining.Slice(BundlePacket.NumBytes);
			}
		}

		/// <inheritdoc/>
		IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

		/// <summary>
		/// Measure the size of memory required to store a collection of import locators
		/// </summary>
		/// <param name="packets">Locators to write</param>
		/// <returns>Size in bytes of the output buffer</returns>
		public static int Measure(IReadOnlyCollection<BundlePacket> packets) => BundlePacket.NumBytes * packets.Count;

		/// <summary>
		/// Serialize a collection of packets to memory
		/// </summary>
		/// <param name="data">Output buffer for the serialized data</param>
		/// <param name="packets">Packets to write</param>
		public static void Write(Span<byte> data, IReadOnlyCollection<BundlePacket> packets)
		{
			Span<byte> next = data;
			foreach (BundlePacket packet in packets)
			{
				packet.CopyTo(next);
				next = next.Slice(BundlePacket.NumBytes);
			}
		}
	}

	/// <summary>
	/// Entry for a node exported from an object
	/// </summary>
	public struct BundleExport
	{
		/// <summary>
		/// Number of bytes in a serialized export object
		/// </summary>
		public const int NumBytes = 32;

		/// <summary>
		/// Raw data for this export
		/// </summary>
		public ReadOnlyMemory<byte> Data { get; }

		/// <summary>
		/// Hash of the node data
		/// </summary>
		public IoHash Hash => new IoHash(Data.Slice(0, 20).Span);

		/// <summary>
		/// Type id of the node. Can be used to look up the type information from the bundle header.
		/// </summary>
		public int TypeIdx => BinaryPrimitives.ReadUInt16LittleEndian(Data.Slice(20).Span);

		/// <summary>
		/// Packet containing this export's data
		/// </summary>
		public int Packet => BinaryPrimitives.ReadUInt16LittleEndian(Data.Slice(22).Span);

		/// <summary>
		/// Offset within the packet of the node data
		/// </summary>
		public int Offset => BinaryPrimitives.ReadInt32LittleEndian(Data.Slice(24).Span);

		/// <summary>
		/// Length of the node
		/// </summary>
		public int Length => BinaryPrimitives.ReadInt32LittleEndian(Data.Slice(28).Span);

		/// <summary>
		/// References to other nodes
		/// </summary>
		public BundleExportRefCollection References { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BundleExport(ReadOnlyMemory<byte> data, BundleExportRefCollection references)
		{
			Data = data;
			References = references;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public BundleExport(int typeIdx, IoHash hash, int packetIdx, int offset, int length, IReadOnlyList<BundleExportRef> references)
		{
			byte[] data = new byte[NumBytes];
			Write(data, typeIdx, hash, packetIdx, offset, length);
			Data = data;
			References = new BundleExportRefCollection(references);
		}

		/// <summary>
		/// Writes a new export to a block of memory
		/// </summary>
		public static void Write(Span<byte> data, int typeIdx, IoHash hash, int packet, int offset, int length)
		{
			hash.CopyTo(data);
			BinaryPrimitives.WriteUInt16LittleEndian(data.Slice(20), (ushort)typeIdx);
			BinaryPrimitives.WriteUInt16LittleEndian(data.Slice(22), (ushort)packet);
			BinaryPrimitives.WriteInt32LittleEndian(data.Slice(24), offset);
			BinaryPrimitives.WriteInt32LittleEndian(data.Slice(28), length);
		}
	}

	/// <summary>
	/// Entry for a node exported from an object
	/// </summary>
	public class BundleExportCollection : IReadOnlyList<BundleExport>
	{
		readonly int _count;
		readonly ReadOnlyMemory<byte> _data;
		readonly ReadOnlyMemory<byte> _refIndexData;
		readonly BundleExportRefCollection _refs;

		/// <summary>
		/// Constructor
		/// </summary>
		public BundleExportCollection(ReadOnlyMemory<byte> data, ReadOnlyMemory<byte> refData)
		{
			_count = data.Length / BundleExport.NumBytes;
			_data = data;

			if (refData.Length > 0)
			{
				_refIndexData = refData.Slice(0, _count * sizeof(int));
				_refs = new BundleExportRefCollection(refData.Slice(_refIndexData.Length));
			}
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public BundleExportCollection(IReadOnlyList<BundleExport> exports)
		{
			int exportRefLength = 0;
			foreach (BundleExport export in exports)
			{
				exportRefLength += export.References.Data.Length;
			}

			if (exportRefLength > 0)
			{
				byte[] refData = new byte[exportRefLength];
				byte[] refIndexData = new byte[exports.Count * sizeof(int)];

				int refDataOffset = 0;
				int refIndexOffset = 0;

				foreach (BundleExport export in exports)
				{
					BinaryPrimitives.WriteInt32LittleEndian(refIndexData.AsSpan(refIndexOffset), refDataOffset);
					refIndexOffset += sizeof(int);

					export.References.Data.Span.CopyTo(refData.AsSpan(refDataOffset));
					refDataOffset += export.References.Data.Length;
				}

				_refIndexData = refIndexData;
				_refs = new BundleExportRefCollection(refData);
			}

			byte[] data = new byte[exports.Count * BundleExport.NumBytes];
			for (int idx = 0; idx < exports.Count; idx++)
			{
				exports[idx].Data.CopyTo(data.AsMemory(idx * BundleExport.NumBytes));
			}
			_data = data;
		}

		/// <inheritdoc/>
		public int Count => _data.Length / BundleExport.NumBytes;

		/// <inheritdoc/>
		public BundleExport this[int index]
		{
			get
			{
				ReadOnlyMemory<byte> exportData = _data.Slice(index * BundleExport.NumBytes);

				BundleExportRefCollection exportRefs = new BundleExportRefCollection();
				if (_refIndexData.Length > 0)
				{
					int minOffset = BinaryPrimitives.ReadInt32LittleEndian(_refIndexData.Span.Slice(index * sizeof(int)));
					int maxOffset = (index == Count - 1)? _refs.Data.Length : BinaryPrimitives.ReadInt32LittleEndian(_refIndexData.Span.Slice((index + 1) * sizeof(int)));
					exportRefs = new BundleExportRefCollection(_refs.Data.Slice(minOffset, maxOffset - minOffset));
				}

				return new BundleExport(exportData, exportRefs);
			}
		}

		/// <inheritdoc/>
		public IEnumerator<BundleExport> GetEnumerator()
		{
			int count = Count;
			for (int idx = 0; idx < count; idx++)
			{
				yield return this[idx];
			}
		}

		/// <inheritdoc/>
		IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
	}

	/// <summary>
	/// Reference to a node in another bundle
	/// </summary>
	/// <param name="ImportIdx">Index into the import table of the blob containing the referenced node. Can be -1 for references within the same bundle.</param>
	/// <param name="NodeIdx">Node imported from the bundle</param>
	public record struct BundleExportRef(int ImportIdx, int NodeIdx)
	{
		/// <summary>
		/// Number of bytes in the serialized object
		/// </summary>
		public const int NumBytes = 4;

		/// <summary>
		/// Deserialize this object from memory
		/// </summary>
		public static BundleExportRef Read(ReadOnlySpan<byte> data)
		{
			return new BundleExportRef(BinaryPrimitives.ReadInt16LittleEndian(data), BinaryPrimitives.ReadInt16LittleEndian(data.Slice(2)));
		}

		/// <summary>
		/// Serialize this object to memory
		/// </summary>
		public void CopyTo(Span<byte> data)
		{
			BinaryPrimitives.WriteInt16LittleEndian(data, (short)ImportIdx);
			BinaryPrimitives.WriteUInt16LittleEndian(data.Slice(2), (ushort)NodeIdx);
		}
	}

	/// <summary>
	/// Collection of information about exported nodes
	/// </summary>
	public struct BundleExportRefCollection : IReadOnlyList<BundleExportRef>
	{
		/// <summary>
		/// Data used to store this collection
		/// </summary>
		public ReadOnlyMemory<byte> Data { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BundleExportRefCollection(ReadOnlyMemory<byte> data) => Data = data;

		/// <summary>
		/// Constructor
		/// </summary>
		public BundleExportRefCollection(IReadOnlyList<BundleExportRef> exportRefs)
		{
			byte[] data = new byte[exportRefs.Count * BundleExportRef.NumBytes];
			for (int idx = 0; idx < exportRefs.Count; idx++)
			{
				exportRefs[idx].CopyTo(data.AsSpan(idx * BundleExportRef.NumBytes));
			}
			Data = data;
		}

		/// <inheritdoc/>
		public int Count => Data.Length / BundleExportRef.NumBytes;

		/// <inheritdoc/>
		public BundleExportRef this[int index] => BundleExportRef.Read(Data.Span.Slice(index * BundleExportRef.NumBytes));

		/// <inheritdoc/>
		public IEnumerator<BundleExportRef> GetEnumerator()
		{
			ReadOnlyMemory<byte> data = Data;
			while (data.Length > 0)
			{
				yield return BundleExportRef.Read(data.Span);
				data = data.Slice(BundleExportRef.NumBytes);
			}
		}

		/// <inheritdoc/>
		IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
	}

	/// <summary>
	/// Utility methods for bundles
	/// </summary>
	public static class BundleData
	{
		/// <summary>
		/// Compress a data packet
		/// </summary>
		/// <param name="format">Format for the compressed data</param>
		/// <param name="input">The data to compress</param>
		/// <param name="writer">Writer for output data</param>
		/// <returns>The compressed data</returns>
		public static int Compress(BundleCompressionFormat format, ReadOnlyMemory<byte> input, IMemoryWriter writer)
		{
			switch (format)
			{
				case BundleCompressionFormat.None:
					{
						writer.WriteFixedLengthBytes(input.Span);
						return input.Length;
					}
				case BundleCompressionFormat.LZ4:
					{
						int maxSize = LZ4Codec.MaximumOutputSize(input.Length);

						Span<byte> buffer = writer.GetSpan(maxSize);
						int encodedLength = LZ4Codec.Encode(input.Span, buffer);

						writer.Advance(encodedLength);
						return encodedLength;
					}
				case BundleCompressionFormat.Gzip:
					{
						using MemoryStream outputStream = new MemoryStream(input.Length);
						using GZipStream gzipStream = new GZipStream(outputStream, CompressionLevel.Fastest);
						gzipStream.Write(input.Span);

						writer.WriteFixedLengthBytes(outputStream.ToArray());
						return (int)outputStream.Length;
					}
				case BundleCompressionFormat.Oodle:
					{
#if WITH_OODLE
						int maxSize = Oodle.MaximumOutputSize(OodleCompressorType.Selkie, input.Length);

						Span<byte> outputSpan = builder.GetSpan(maxSize);
						int encodedLength = Oodle.Compress(OodleCompressorType.Selkie, input.Span, outputSpan, OodleCompressionLevel.HyperFast);
						builder.Advance(encodedLength);

						return encodedLength;
#else
						throw new NotSupportedException("Oodle is not compiled into this build.");
#endif
					}
				default:
					throw new InvalidDataException($"Invalid compression format '{(int)format}'");
			}
		}

		/// <summary>
		/// Decompress a packet of data
		/// </summary>
		/// <param name="format">Format of the compressed data</param>
		/// <param name="input">Compressed data</param>
		/// <param name="output">Buffer to receive the decompressed data</param>
		public static void Decompress(BundleCompressionFormat format, ReadOnlyMemory<byte> input, Memory<byte> output)
		{
			switch (format)
			{
				case BundleCompressionFormat.None:
					input.CopyTo(output);
					break;
				case BundleCompressionFormat.LZ4:
					LZ4Codec.Decode(input.Span, output.Span);
					break;
				case BundleCompressionFormat.Gzip:
					{
						using ReadOnlyMemoryStream inputStream = new ReadOnlyMemoryStream(input);
						using GZipStream outputStream = new GZipStream(new MemoryWriterStream(new MemoryWriter(output)), CompressionMode.Decompress, false);
						inputStream.CopyTo(outputStream);
						break;
					}
				case BundleCompressionFormat.Oodle:
#if WITH_OODLE
					Oodle.Decompress(input.Span, output.Span);
					break;
#else
					throw new NotSupportedException("Oodle is not compiled into this build.");
#endif
				default:
					throw new InvalidDataException($"Invalid compression format '{(int)format}'");
			}
		}
	}
}
