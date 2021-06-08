// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Buffers.Binary;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Storage.Primitives
{
	/// <summary>
	/// A node in a key value tree
	/// </summary>
	class KeyValueNode
	{
		/// <summary>
		/// The underlying memory storing the node data
		/// </summary>
		public Memory<byte> Memory { get; }

		/// <summary>
		/// Number of child items from this node
		/// </summary>
		public int NumItems { get; }

		/// <summary>
		/// Number of items if this node is merged with its children. This will be equal to NumItems if this is the child of a parent-child relation.
		/// Note that this is different from the total number of leaf nodes under this tree; it is only used for splitting/merging layers.
		/// </summary>
		public int NumItemsIfMerged { get; }

		/// <summary>
		/// The number of valid bits in keys for this node
		/// </summary>
		public int NumKeyBits { get; }

		/// <summary>
		/// The number of valid bits in the key if it is merged with its children
		/// </summary>
		public int NumKeyBitsIfMerged { get; }

		/// <summary>
		/// Data for the array of keys
		/// </summary>
		public Memory<byte> Keys { get; }

		/// <summary>
		/// Data for the array of values
		/// </summary>
		public Memory<byte> Values { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="NumItems"></param>
		/// <param name="NumItemsIfMerged"></param>
		/// <param name="NumKeyBits"></param>
		/// <param name="NumKeyBitsIfMerged"></param>
		/// <param name="ValueSize">Size of a value in this node</param>
		public KeyValueNode(int NumItems, int NumItemsIfMerged, int NumKeyBits, int NumKeyBitsIfMerged, int ValueSize)
		{
			this.NumItems = NumItems;
			this.NumItemsIfMerged = NumItemsIfMerged;
			this.NumKeyBits = NumKeyBits;
			this.NumKeyBitsIfMerged = NumKeyBitsIfMerged;

			int KeyDataLength = IoHash.NumBytes * NumItems;
			int ValueDataLength = ValueSize * NumItems;
			Memory = new byte[(sizeof(int) * 4) + KeyDataLength + ValueDataLength];

			Memory<byte> Output = Memory;

			BinaryPrimitives.WriteInt32LittleEndian(Output.Span, NumItems);
			Output = Output.Slice(sizeof(int));

			BinaryPrimitives.WriteInt32LittleEndian(Output.Span, NumItemsIfMerged);
			Output = Output.Slice(sizeof(int));

			BinaryPrimitives.WriteInt32LittleEndian(Output.Span, NumKeyBits);
			Output = Output.Slice(sizeof(int));

			BinaryPrimitives.WriteInt32LittleEndian(Output.Span, NumKeyBitsIfMerged);
			Output = Output.Slice(sizeof(int));

			Keys = Output.Slice(0, KeyDataLength);
			Output = Output.Slice(KeyDataLength);

			Values = Output;
		}
	}

	/// <summary>
	/// Read-only version of <see cref="KeyValueNode"/>
	/// </summary>
	class ReadOnlyKeyValueNode
	{
		/// <inheritdoc cref="KeyValueNode.Memory"/>
		public ReadOnlyMemory<byte> Memory { get; }

		/// <inheritdoc cref="KeyValueNode.NumItems"/>
		public int NumItems { get; }

		/// <inheritdoc cref="KeyValueNode.NumItemsIfMerged"/>
		public int NumItemsIfMerged { get; }

		/// <inheritdoc cref="KeyValueNode.NumKeyBits"/>
		public int NumKeyBits { get; }

		/// <inheritdoc cref="KeyValueNode.NumKeyBitsIfMerged"/>
		public int NumKeyBitsIfMerged { get; }

		/// <inheritdoc cref="KeyValueNode.Keys"/>
		public ReadOnlyMemory<byte> Keys { get; }

		/// <inheritdoc cref="KeyValueNode.Values"/>
		public ReadOnlyMemory<byte> Values { get; }

		/// <summary>
		/// Whether this node is a leaf node
		/// </summary>
		public bool IsLeafNode => NumKeyBits == IoHash.NumBits;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Memory">Memory backing this node</param>
		public ReadOnlyKeyValueNode(ReadOnlyMemory<byte> Memory)
		{
			this.Memory = Memory;

			ReadOnlyMemory<byte> Input = Memory;

			NumItems = BinaryPrimitives.ReadInt32LittleEndian(Input.Span);
			Input = Input.Slice(sizeof(int));

			NumItemsIfMerged = BinaryPrimitives.ReadInt32LittleEndian(Input.Span);
			Input = Input.Slice(sizeof(int));

			NumKeyBits = BinaryPrimitives.ReadInt32LittleEndian(Input.Span);
			Input = Input.Slice(sizeof(int));

			NumKeyBitsIfMerged = BinaryPrimitives.ReadInt32LittleEndian(Input.Span);
			Input = Input.Slice(sizeof(int));

			Keys = Input.Slice(0, NumItems * IoHash.NumBytes);
			Input = Input.Slice(Keys.Length);

			Values = Input;
		}

		/// <summary>
		/// Implicit conversion operator from a KeyValueNode
		/// </summary>
		/// <param name="Other"></param>
		public static implicit operator ReadOnlyKeyValueNode(KeyValueNode Other)
		{
			return new ReadOnlyKeyValueNode(Other.Memory);
		}
	}
}
