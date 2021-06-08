// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Storage.Primitives
{
	/// <summary>
	/// A key/value pair for insertion into a KeyValueTree
	/// </summary>
	struct KeyValueItem
	{
		/// <summary>
		/// The key
		/// </summary>
		public IoHash Key { get; }

		/// <summary>
		/// Block of memory containing the value for this item
		/// </summary>
		public ReadOnlyMemory<byte> Value { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Key"></param>
		/// <param name="Value"></param>
		public KeyValueItem(IoHash Key, ReadOnlyMemory<byte> Value)
		{
			this.Key = Key;
			this.Value = Value;
		}
	}

	class KeyValueItemReader
	{
		ReadOnlyMemory<byte> Keys;
		ReadOnlyMemory<byte> Values;
		int ValueSize;

		public KeyValueItemReader(KeyValueTree Tree, ReadOnlyKeyValueNode Node)
		{
			this.Keys = Node.Keys;
			this.Values = Node.Values;
			this.ValueSize = Node.IsLeafNode ? Tree.LeafValueSize : IoHash.NumBytes;
		}

		public bool IsEmpty => Keys.Length == 0;

		public void MoveNext()
		{
			Keys = Keys.Slice(IoHash.NumBytes);
			Values = Values.Slice(ValueSize);
		}

		public void CopyTo(KeyValueItemWriter Writer)
		{
			Writer.WriteItems(Keys, Values);

			Keys = ReadOnlyMemory<byte>.Empty;
			Values = ReadOnlyMemory<byte>.Empty;
		}

		public void CopyTo(KeyValueItemWriter Writer, int Count)
		{
			ReadOnlyMemory<byte> CopyKeys = Keys.Slice(0, Count * IoHash.NumBytes);
			ReadOnlyMemory<byte> CopyValues = Values.Slice(0, Count * ValueSize);

			Writer.WriteItems(CopyKeys, CopyValues);

			Keys = Keys.Slice(CopyKeys.Length);
			Values = Values.Slice(CopyValues.Length);
		}
	}

	class KeyValueItemWriter
	{
		public Memory<byte> Keys { get; private set; }
		public Memory<byte> Values { get; private set; }

		public KeyValueItemWriter(KeyValueNode Node)
		{
			this.Keys = Node.Keys;
			this.Values = Node.Values;
		}

		public void WriteItem(IoHash Key, ReadOnlyMemory<byte> Value)
		{
			WriteItems(Key.Memory, Value);
		}

		public void WriteItems(ReadOnlyMemory<byte> NewKeys, ReadOnlyMemory<byte> NewValues)
		{
			NewKeys.CopyTo(Keys);
			Keys = Keys.Slice(NewKeys.Length);

			NewValues.CopyTo(Values);
			Values = Values.Slice(NewValues.Length);
		}
	}
}
