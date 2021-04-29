// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Controllers;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

namespace HordeServer.Logs
{
	/// <summary>
	/// A read-only trie of long values
	/// </summary>
	public class ReadOnlyTrie : IEnumerable<ulong>
	{
		/// <summary>
		/// Stack item for traversing the tree
		/// </summary>
		struct StackItem
		{
			/// <summary>
			/// The current node index
			/// </summary>
			public int Index;

			/// <summary>
			/// Value in the current node (0-15)
			/// </summary>
			public int Value;
		}

		/// <summary>
		/// Delegate for filtering values during a tree traversal
		/// </summary>
		/// <param name="Value">The current value</param>
		/// <param name="Mask">Mask for which bits in the value are valid</param>
		/// <returns>True if values matching the given mask should be enumerated</returns>
		public delegate bool VisitorDelegate(ulong Value, ulong Mask);

		/// <summary>
		/// Height of the tree
		/// </summary>
		const int Height = sizeof(ulong) * 2;

		/// <summary>
		/// Array of bitmasks for each node in the tree
		/// </summary>
		public ushort[] NodeData { get; }

		/// <summary>
		/// Array of child offsets for each node. Excludes the last layer of the tree.
		/// </summary>
		int[] FirstChildIndex;

		/// <summary>
		/// Empty index definition
		/// </summary>
		public static ReadOnlyTrie Empty { get; } = new ReadOnlyTrie(new ushort[1]);

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="NodeData">Node data</param>
		public ReadOnlyTrie(ushort[] NodeData)
		{
			this.NodeData = NodeData;
			this.FirstChildIndex = CreateChildLookup(NodeData);
		}

		/// <summary>
		/// Tests whether the given value is in the trie
		/// </summary>
		/// <param name="Value">The value to check for</param>
		/// <returns>True if the value is in the trie</returns>
		public bool Contains(ulong Value)
		{
			int Index = 0;
			for (int Shift = (sizeof(ulong) * 8) - 4; Shift >= 0; Shift -= 4)
			{
				int Mask = NodeData[Index];
				int Flag = 1 << (int)((Value >> Shift) & 15);
				if ((Mask & Flag) == 0)
				{
					return false;
				}

				Index = FirstChildIndex[Index];
				for (; ; )
				{
					Mask &= (Mask - 1);
					if ((Mask & Flag) == 0)
					{
						break;
					}
					Index++;
				}
			}
			return true;
		}

		/// <inheritdoc/>
		public IEnumerator<ulong> GetEnumerator()
		{
			return EnumerateRange(ulong.MinValue, ulong.MaxValue).GetEnumerator();
		}

		/// <inheritdoc/>
		IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

		/// <summary>
		/// Enumerate all values matching a given filter
		/// </summary>
		/// <param name="Predicate">Predicate for which values to include</param>
		/// <returns>Values satisfying the given predicate</returns>
		public IEnumerable<ulong> EnumerateValues(VisitorDelegate Predicate)
		{
			int Depth = 0;
			ulong Value = 0;

			StackItem[] Stack = new StackItem[Height];
			Stack[1].Index = FirstChildIndex[0];

			for (; ; )
			{
				StackItem Current = Stack[Depth];
				if (Current.Value >= 16)
				{
					// Move up the tree if we've enumerated all the branches at the current level
					Depth--;
					if (Depth < 0)
					{
						yield break;
					}
					Stack[Depth].Value++;

					// Increment the child index too. These are stored sequentially for a given parent. The value will be cleared when we recurse into it.
					Stack[Depth + 1].Index++;
				}
				else if ((NodeData[Current.Index] & (1 << Current.Value)) == 0)
				{
					// This branch does not exist. Skip it.
					Stack[Depth].Value++;
				}
				else
				{
					// Get the value and mask for the current node
					int Shift = (Stack.Length - Depth - 1) * 4;
					ulong Mask = ~((1UL << Shift) - 1);
					Value = (Value & (Mask << 4)) | ((ulong)(uint)Current.Value << Shift);

					if (!Predicate(Value, Mask))
					{
						// This node is excluded, skip it
						Stack[Depth].Value++;
						if (Depth + 1 < Stack.Length)
						{
							Stack[Depth + 1].Index++;
						}
					}
					else if(Depth + 1 < Stack.Length)
					{
						// Move down the tree
						Depth++;
						Stack[Depth].Value = 0;
						if (Depth + 1 < Stack.Length)
						{
							Stack[Depth + 1].Index = FirstChildIndex[Stack[Depth].Index];
						}
					}
					else
					{
						// Yield the current value
						yield return Value;
						Stack[Depth].Value++;
					}
				}
			}
		}

		/// <summary>
		/// Enumerates all values in the trie between the given ranges
		/// </summary>
		/// <param name="MinValue">Minimum value to enumerate</param>
		/// <param name="MaxValue">Maximum value to enumerate</param>
		/// <returns>Sequence of values</returns>
		public IEnumerable<ulong> EnumerateRange(ulong MinValue, ulong MaxValue)
		{
			return EnumerateValues((Value, Mask) => (Value >= (MinValue & Mask) && Value <= (MaxValue & Mask)));
		}

		/// <summary>
		/// Creates a lookup for child node offsets from raw node data
		/// </summary>
		/// <param name="NodeData">Array of masks for each node</param>
		/// <returns>Array of offsets</returns>
		static int[] CreateChildLookup(ushort[] NodeData)
		{
			List<int> ChildOffsets = new List<int>();
			if (NodeData.Length > 0)
			{
				int NodeCount = 1;

				int Index = 0;
				int ChildIndex = NodeCount;

				for (int Level = 0; Level < Height; Level++)
				{
					int NextNodeCount = 0;
					for (int Idx = 0; Idx < NodeCount; Idx++)
					{
						ushort Node = NodeData[Index++];

						int NumChildren = CountBits(Node);
						ChildOffsets.Add(ChildIndex);
						ChildIndex += NumChildren;

						NextNodeCount += NumChildren;
					}
					NodeCount = NextNodeCount;
				}
			}
			return ChildOffsets.ToArray();
		}

		/// <summary>
		/// Count the number of set bits in the given value
		/// </summary>
		/// <param name="Value">Value to test</param>
		/// <returns>Number of set bits</returns>
		static int CountBits(ushort Value)
		{
			int Count = Value;
			Count = (Count & 0b0101010101010101) + ((Count >> 1) & 0b0101010101010101);
			Count = (Count & 0b0011001100110011) + ((Count >> 2) & 0b0011001100110011);
			Count = (Count & 0b0000111100001111) + ((Count >> 4) & 0b0000111100001111);
			Count = (Count & 0b0000000011111111) + ((Count >> 8) & 0b0000000011111111);
			return Count;
		}

		/// <summary>
		/// Read a trie from the given buffer
		/// </summary>
		/// <param name="Reader">Reader to read from</param>
		/// <returns>New trie</returns>
		public static ReadOnlyTrie Read(MemoryReader Reader)
		{
			ReadOnlyMemory<byte> Nodes = Reader.ReadVariableLengthBytes();
			ushort[] NodeData = MemoryMarshal.Cast<byte, ushort>(Nodes.Span).ToArray();
			return new ReadOnlyTrie(NodeData);
		}

		/// <summary>
		/// Write this trie to the given buffer
		/// </summary>
		/// <param name="Writer">Writer to output to</param>
		public void Write(MemoryWriter Writer)
		{
			Writer.WriteVariableLengthBytes(MemoryMarshal.AsBytes<ushort>(NodeData));
		}

		/// <summary>
		/// Gets the serialized size of this trie
		/// </summary>
		/// <returns></returns>
		public int GetSerializedSize()
		{
			return (sizeof(int) + NodeData.Length * sizeof(ushort));
		}
	}

	/// <summary>
	/// Extension methods for serializing tries
	/// </summary>
	public static class LogTokenSetExtensions
	{
		/// <summary>
		/// Read a trie from the given buffer
		/// </summary>
		/// <param name="Reader">Reader to read from</param>
		/// <returns>New trie</returns>
		public static ReadOnlyTrie ReadTrie(this MemoryReader Reader)
		{
			return ReadOnlyTrie.Read(Reader);
		}

		/// <summary>
		/// Write this trie to the given buffer
		/// </summary>
		/// <param name="Writer">Writer to output to</param>
		/// <param name="Trie">Trie to write</param>
		public static void WriteTrie(this MemoryWriter Writer, ReadOnlyTrie Trie)
		{
			Trie.Write(Writer);
		}
	}
}
