// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;

namespace EpicGames.Horde.Bundles
{
	/// <summary>
	/// Object stored within the ref for a bundle
	/// </summary>
	public class BundleRoot
	{
		/// <summary>
		/// Data hashed to produce the ref key; included in unhashed form for debugging purposes.
		/// </summary>
		[CbField("metadata")]
		public CbObject Metadata { get; set; } = CbObject.Empty;

		/// <summary>
		/// Embedded object data
		/// </summary>
		[CbField("object")]
		public BundleObject Object { get; set; } = new BundleObject();
	}

	/// <summary>
	/// Blob within the bundle.
	/// </summary>
	public class BundleObject
	{
		/// <summary>
		/// Current schema version
		/// </summary>
		public const int CurrentSchema = 1;

		/// <summary>
		/// Schema version
		/// </summary>
		[CbField("schema")]
		public int Schema { get; set; } = CurrentSchema;

		/// <summary>
		/// Other objects that are referenced.
		/// </summary>
		[CbField("imports")]
		public List<BundleImportObject> ImportObjects { get; } = new List<BundleImportObject>();

		/// <summary>
		/// Exported items in the data 
		/// </summary>
		[CbField("exports")]
		[CbConverter(typeof(BundleExportListConverter))]
		public List<BundleExport> Exports { get; } = new List<BundleExport>();

		/// <summary>
		/// Raw data for this exported entries in this blob.
		/// </summary>
		[CbField("data")]
		public ReadOnlyMemory<byte> Data { get; set; }
	}

	/// <summary>
	/// Reference to another tree pack object
	/// </summary>	
	[DebuggerDisplay("{Object}")]
	public class BundleImportObject
	{
		/// <summary>
		/// Imported object.
		/// </summary>
		[CbField("object")]
		public CbObjectAttachment Object { get; set; }

		/// <summary>
		/// Sum of the costs of nodes strored in this import.
		/// </summary>
		[CbField("cost")]
		public int TotalCost { get; set; }

		/// <summary>
		/// List of nodes imported from this object, and their sizes.
		/// </summary>
		[CbField("imports")]
		[CbConverter(typeof(BundleImportListConverter))]
		public List<BundleImport> Imports { get; } = new List<BundleImport>();

		/// <summary>
		/// Default constructor, for serialization
		/// </summary>
		private BundleImportObject()
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public BundleImportObject(CbObjectAttachment sourceObject, int totalCost, List<BundleImport> imports)
		{
			Object = sourceObject;
			TotalCost = totalCost;
			Imports = imports;
		}
	}

	/// <summary>
	/// Entry for a node imported from another object
	/// </summary>
	public class BundleImport
	{
		/// <summary>
		/// Hash of the node
		/// </summary>
		public IoHash Hash { get; }

		/// <summary>
		/// The node's rank, ie. its distance from a leaf node (where leaf nodes have a value of zero).
		/// </summary>
		public int Rank { get; }

		/// <summary>
		/// Decompressed size of this node
		/// </summary>
		public int Length { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BundleImport(IoHash hash, int rank, int length)
		{
			if (length <= 0)
			{
				throw new ArgumentException("Length must be greater than zero", nameof(length));
			}

			Hash = hash;
			Rank = rank;
			Length = length;
		}
	}

	class BundleImportListConverter : CbConverterBase<List<BundleImport>>
	{
		public override List<BundleImport> Read(CbField field) => Deserialize(field.AsBinary().Span);
		public override void Write(CbWriter writer, List<BundleImport> value) => writer.WriteBinaryArrayValue(Serialize(value));
		public override void WriteNamed(CbWriter writer, Utf8String name, List<BundleImport> value) => writer.WriteBinaryArray(name, Serialize(value));

		static List<BundleImport> Deserialize(ReadOnlySpan<byte> span)
		{
			List<BundleImport> imports = new List<BundleImport>();
			while (span.Length > 0)
			{
				IoHash hash = new IoHash(span);
				span = span.Slice(IoHash.NumBytes);

				int rank = (int)VarInt.ReadUnsigned(span, out int rankBytes);
				span = span.Slice(rankBytes);

				int cost = (int)VarInt.ReadUnsigned(span, out int costBytes);
				span = span.Slice(costBytes);

				imports.Add(new BundleImport(hash, rank, cost));
			}
			return imports;
		}

		static byte[] Serialize(List<BundleImport> imports)
		{
			byte[] data = new byte[imports.Sum(x => IoHash.NumBytes + VarInt.MeasureUnsigned(x.Rank) + VarInt.MeasureUnsigned(x.Length))];

			Span<byte> span = data;
			foreach (BundleImport import in imports)
			{
				import.Hash.CopyTo(span);
				span = span.Slice(IoHash.NumBytes);

				int rankBytes = VarInt.WriteUnsigned(span, import.Rank);
				span = span.Slice(rankBytes);

				int costBytes = VarInt.WriteUnsigned(span, import.Length);
				span = span.Slice(costBytes);
			}

			return data;
		}
	}

	/// <summary>
	/// Descriptor for a compression packet
	/// </summary>
	public class BundleCompressionPacket
	{
		/// <summary>
		/// Offset of the packet within the <see cref="BundleObject"/>'s data array
		/// </summary>
		public int Offset { get; }

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
		/// <param name="offset"></param>
		public BundleCompressionPacket(int offset)
		{
			Offset = offset;
		}
	}

	/// <summary>
	/// Entry for a node exported from an object
	/// </summary>
	public class BundleExport : BundleImport
	{
		/// <summary>
		/// Packet containing the node
		/// </summary>
		public BundleCompressionPacket Packet { get; }

		/// <summary>
		/// Offset of the node within the decompressed packet
		/// </summary>
		public int Offset { get; }

		/// <summary>
		/// References to other nodes.
		/// </summary>
		public IReadOnlyList<int> References { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BundleExport(IoHash hash, int rank, BundleCompressionPacket packet, int offset, int length, IReadOnlyList<int> references)
			: base(hash, rank, length)
		{
			Packet = packet;
			Offset = offset;
			References = references;
		}
	}

	class BundleExportListConverter : CbConverterBase<List<BundleExport>>
	{
		public override List<BundleExport> Read(CbField field) => Deserialize(field.AsBinary().Span);
		public override void Write(CbWriter writer, List<BundleExport> value) => writer.WriteBinaryArrayValue(Serialize(value));
		public override void WriteNamed(CbWriter writer, Utf8String name, List<BundleExport> value) => writer.WriteBinaryArray(name, Serialize(value));

		static List<BundleExport> Deserialize(ReadOnlySpan<byte> span)
		{
			List<BundleExport> exports = new List<BundleExport>();

			int packetOffset = 0;
			while (span.Length > 0)
			{
				BundleCompressionPacket packet = new BundleCompressionPacket(packetOffset);

				packet.EncodedLength = (int)VarInt.ReadUnsigned(span, out int encodedLengthBytes);
				span = span.Slice(encodedLengthBytes);

				packet.DecodedLength = (int)VarInt.ReadUnsigned(span, out int decodedLengthBytes);
				span = span.Slice(decodedLengthBytes);

				packetOffset += packet.EncodedLength;

				int offset = 0;
				while (offset < packet.DecodedLength)
				{
					IoHash hash = new IoHash(span);
					span = span.Slice(IoHash.NumBytes);

					int rank = (int)VarInt.ReadUnsigned(span, out int rankBytes);
					span = span.Slice(rankBytes);

					int length = (int)VarInt.ReadUnsigned(span, out int lengthBytes);
					span = span.Slice(lengthBytes);

					int numReferences = (int)VarInt.ReadUnsigned(span, out int numReferencesBytes);
					span = span.Slice(numReferencesBytes);

					int[] references = new int[numReferences];
					for (int referenceIdx = 0; referenceIdx < numReferences; referenceIdx++)
					{
						references[referenceIdx] = (int)VarInt.ReadUnsigned(span, out int referenceBytes);
						span = span.Slice(referenceBytes);
					}

					exports.Add(new BundleExport(hash, rank, packet, offset, length, references));
					offset += length;
				}
			}

			return exports;
		}

		static byte[] Serialize(List<BundleExport> exports)
		{
			int length = Measure(exports);

			byte[] data = new byte[length];
			Span<byte> span = data;

			BundleCompressionPacket? prevPacket = null;
			foreach (BundleExport export in exports)
			{
				Debug.Assert(export.Length > 0);

				BundleCompressionPacket packet = export.Packet;
				if (prevPacket == null || packet.Offset != prevPacket.Offset)
				{
					CheckPacketSequence(prevPacket, packet);

					int encodedLengthBytes = VarInt.WriteUnsigned(span, packet.EncodedLength);
					span = span.Slice(encodedLengthBytes);

					int decodedLengthBytes = VarInt.WriteUnsigned(span, packet.DecodedLength);
					span = span.Slice(decodedLengthBytes);

					prevPacket = packet;
				}

				export.Hash.CopyTo(span);
				span = span.Slice(IoHash.NumBytes);

				int rankBytes = VarInt.WriteUnsigned(span, export.Rank);
				span = span.Slice(rankBytes);

				int lengthBytes = VarInt.WriteUnsigned(span, export.Length);
				span = span.Slice(lengthBytes);

				int numReferencesBytes = VarInt.WriteUnsigned(span, export.References.Count);
				span = span.Slice(numReferencesBytes);

				foreach (int reference in export.References)
				{
					int referenceBytes = VarInt.WriteUnsigned(span, reference);
					span = span.Slice(referenceBytes);
				}
			}

			Debug.Assert(span.Length == 0);
			return data;
		}

		static int Measure(List<BundleExport> exports)
		{
			int length = 0;

			BundleCompressionPacket? prevPacket = null;
			foreach (BundleExport export in exports)
			{
				BundleCompressionPacket packet = export.Packet;
				if (prevPacket == null || packet.Offset != prevPacket.Offset)
				{
					CheckPacketSequence(prevPacket, packet);
					length += VarInt.MeasureUnsigned(packet.EncodedLength) + VarInt.MeasureUnsigned(packet.DecodedLength);
					prevPacket = packet;
				}

				length += IoHash.NumBytes + VarInt.MeasureUnsigned(export.Rank) + VarInt.MeasureUnsigned(export.Length) + VarInt.MeasureUnsigned(export.References.Count) + export.References.Sum(x => VarInt.MeasureUnsigned(x));
			}

			return length;
		}

		static void CheckPacketSequence(BundleCompressionPacket? prevPacket, BundleCompressionPacket packet)
		{
			int expectedOffset = (prevPacket == null) ? 0 : (prevPacket.Offset + prevPacket.EncodedLength);
			if (packet.Offset != expectedOffset)
			{
				throw new InvalidOperationException("Bundle compression packets are not sequential");
			}
		}
	}
}
