// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Bundles.Nodes;
using EpicGames.Serialization;
using K4os.Compression.LZ4;
using Microsoft.Extensions.Logging;
using System;
using System.Buffers.Binary;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;

namespace EpicGames.Horde.Bundles
{
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
		/// Time that this object was minted. Used to evaluate how likely it will be that a client will already have it.
		/// </summary>
		[CbField("time")]
		public DateTime CreationTimeUtc { get; set; }

		/// <summary>
		/// Other objects that are referenced.
		/// </summary>
		[CbField("imports")]
		public List<BundleImportObject> ImportObjects { get; set; } = new List<BundleImportObject>();

		/// <summary>
		/// Exported items in the data 
		/// </summary>
		[CbField("exports")]
		[CbConverter(typeof(BundleExportListConverter))]
		public List<BundleExport> Exports { get; set; } = new List<BundleExport>();

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
		/// Time that the blob was created.
		/// </summary>
		[CbField("time")]
		public DateTime CreationTimeUtc { get; set; }

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
		public List<BundleImport> Imports { get; set; } = new List<BundleImport>();

		/// <summary>
		/// Default constructor, for serialization
		/// </summary>
		private BundleImportObject()
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public BundleImportObject(DateTime CreationTimeUtc, CbObjectAttachment Object, int TotalCost, List<BundleImport> Imports)
		{
			this.CreationTimeUtc = CreationTimeUtc;
			this.Object = Object;
			this.TotalCost = TotalCost;
			this.Imports = Imports;
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
		public int Cost { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BundleImport(IoHash Hash, int Rank, int Cost)
		{
			this.Hash = Hash;
			this.Rank = Rank;
			this.Cost = Cost;
		}
	}

	class BundleImportListConverter : CbConverterBase<List<BundleImport>>
	{
		public override List<BundleImport> Read(CbField Field) => Deserialize(Field.AsBinary().Span);
		public override void Write(CbWriter Writer, List<BundleImport> Value) => Writer.WriteBinaryArrayValue(Serialize(Value));
		public override void WriteNamed(CbWriter Writer, Utf8String Name, List<BundleImport> Value) => Writer.WriteBinaryArray(Name, Serialize(Value));

		static List<BundleImport> Deserialize(ReadOnlySpan<byte> Span)
		{
			List<BundleImport> Imports = new List<BundleImport>();
			while (Span.Length > 0)
			{
				IoHash Hash = new IoHash(Span);
				Span = Span.Slice(IoHash.NumBytes);

				int Rank = (int)VarInt.Read(Span, out int RankBytes);
				Span = Span.Slice(RankBytes);

				int Cost = (int)VarInt.Read(Span, out int CostBytes);
				Span = Span.Slice(CostBytes);

				Imports.Add(new BundleImport(Hash, Rank, Cost));
			}
			return Imports;
		}

		static byte[] Serialize(List<BundleImport> Imports)
		{
			byte[] Data = new byte[Imports.Sum(x => IoHash.NumBytes + VarInt.Measure(x.Rank) + VarInt.Measure(x.Cost))];

			Span<byte> Span = Data;
			foreach (BundleImport Import in Imports)
			{
				Import.Hash.CopyTo(Span);
				Span = Span.Slice(IoHash.NumBytes);

				int RankBytes = VarInt.Write(Span, Import.Rank);
				Span = Span.Slice(RankBytes);

				int CostBytes = VarInt.Write(Span, Import.Cost);
				Span = Span.Slice(CostBytes);
			}

			return Data;
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
		/// <param name="Offset"></param>
		public BundleCompressionPacket(int Offset)
		{
			this.Offset = Offset;
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
		/// Length of the node within the decompressed packet
		/// </summary>
		public int Length { get; }

		/// <summary>
		/// References to other nodes.
		/// </summary>
		public int[] References { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BundleExport(IoHash Hash, int Rank, int Cost, BundleCompressionPacket Packet, int Offset, int Length, int[] References)
			: base(Hash, Rank, Cost)
		{
			this.Packet = Packet;
			this.Offset = Offset;
			this.Length = Length;
			this.References = References;
		}
	}

	class BundleExportListConverter : CbConverterBase<List<BundleExport>>
	{
		public override List<BundleExport> Read(CbField Field) => Deserialize(Field.AsBinary().Span);
		public override void Write(CbWriter Writer, List<BundleExport> Value) => Writer.WriteBinaryArrayValue(Serialize(Value));
		public override void WriteNamed(CbWriter Writer, Utf8String Name, List<BundleExport> Value) => Writer.WriteBinaryArray(Name, Serialize(Value));

		static List<BundleExport> Deserialize(ReadOnlySpan<byte> Span)
		{
			List<BundleExport> Exports = new List<BundleExport>();

			int PacketOffset = 0;
			while (Span.Length > 0)
			{
				BundleCompressionPacket Packet = new BundleCompressionPacket(PacketOffset);

				Packet.EncodedLength = (int)VarInt.Read(Span, out int EncodedLengthBytes);
				Span = Span.Slice(EncodedLengthBytes);

				Packet.DecodedLength = (int)VarInt.Read(Span, out int DecodedLengthBytes);
				Span = Span.Slice(DecodedLengthBytes);

				PacketOffset += Packet.EncodedLength;

				int Offset = 0;
				while (Offset < Packet.DecodedLength)
				{
					IoHash Hash = new IoHash(Span);
					Span = Span.Slice(IoHash.NumBytes);

					int Rank = (int)VarInt.Read(Span, out int RankBytes);
					Span = Span.Slice(RankBytes);

					int Cost = (int)VarInt.Read(Span, out int CostBytes);
					Span = Span.Slice(CostBytes);

					int Length = (int)VarInt.Read(Span, out int LengthBytes);
					Span = Span.Slice(LengthBytes);

					int NumReferences = (int)VarInt.Read(Span, out int NumReferencesBytes);
					Span = Span.Slice(NumReferencesBytes);

					int[] References = new int[NumReferences];
					for (int ReferenceIdx = 0; ReferenceIdx < NumReferences; ReferenceIdx++)
					{
						References[ReferenceIdx] = (int)VarInt.Read(Span, out int ReferenceBytes);
						Span = Span.Slice(ReferenceBytes);
					}

					Exports.Add(new BundleExport(Hash, Rank, Cost, Packet, Offset, Length, References));
					Offset += Length;
				}
			}

			return Exports;
		}

		static byte[] Serialize(List<BundleExport> Exports)
		{
			int Length = Measure(Exports);

			byte[] Data = new byte[Length];
			Span<byte> Span = Data;

			BundleCompressionPacket? PrevPacket = null;
			foreach (BundleExport Export in Exports)
			{
				BundleCompressionPacket Packet = Export.Packet;
				if (PrevPacket == null || Packet.Offset != PrevPacket.Offset)
				{
					CheckPacketSequence(PrevPacket, Packet);

					int EncodedLengthBytes = VarInt.Write(Span, Packet.EncodedLength);
					Span = Span.Slice(EncodedLengthBytes);

					int DecodedLengthBytes = VarInt.Write(Span, Packet.DecodedLength);
					Span = Span.Slice(DecodedLengthBytes);

					PrevPacket = Packet;
				}

				Export.Hash.CopyTo(Span);
				Span = Span.Slice(IoHash.NumBytes);

				int RankBytes = VarInt.Write(Span, Export.Rank);
				Span = Span.Slice(RankBytes);

				int CostBytes = VarInt.Write(Span, Export.Cost);
				Span = Span.Slice(CostBytes);

				int LengthBytes = VarInt.Write(Span, Export.Length);
				Span = Span.Slice(LengthBytes);

				int NumReferencesBytes = VarInt.Write(Span, Export.References.Length);
				Span = Span.Slice(NumReferencesBytes);

				foreach (int Reference in Export.References)
				{
					int ReferenceBytes = VarInt.Write(Span, Reference);
					Span = Span.Slice(ReferenceBytes);
				}
			}

			Debug.Assert(Span.Length == 0);
			return Data;
		}

		static int Measure(List<BundleExport> Exports)
		{
			int Length = 0;

			BundleCompressionPacket? PrevPacket = null;
			foreach (BundleExport Export in Exports)
			{
				BundleCompressionPacket Packet = Export.Packet;
				if (PrevPacket == null || Packet.Offset != PrevPacket.Offset)
				{
					CheckPacketSequence(PrevPacket, Packet);
					Length += VarInt.Measure(Packet.EncodedLength) + VarInt.Measure(Packet.DecodedLength);
					PrevPacket = Packet;
				}
				Length += IoHash.NumBytes + VarInt.Measure(Export.Rank) + VarInt.Measure(Export.Cost) + VarInt.Measure(Export.Length) + VarInt.Measure(Export.References.Length) + Export.References.Sum(x => VarInt.Measure(x));
			}

			return Length;
		}

		static void CheckPacketSequence(BundleCompressionPacket? PrevPacket, BundleCompressionPacket Packet)
		{
			int ExpectedOffset = (PrevPacket == null) ? 0 : (PrevPacket.Offset + PrevPacket.EncodedLength);
			if (Packet.Offset != ExpectedOffset)
			{
				throw new InvalidOperationException("Bundle compression packets are not sequential");
			}
		}
	}
}
