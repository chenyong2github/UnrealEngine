// Copyright Epic Games, Inc. All Rights Reserved.

using Amazon.Runtime;
using EpicGames.Core;
using HordeServer.Logs;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Routing.Constraints;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

namespace HordeServer.Logs
{
	/// <summary>
	/// Structure used for generating prefix trees
	/// </summary>
	public class ReadOnlyTrieBuilder
	{
		/// <summary>
		/// Node within the trie
		/// </summary>
		class Node
		{
			public Node?[]? Children;
		}

		/// <summary>
		/// The root node
		/// </summary>
		Node Root;

		/// <summary>
		/// Default constructor
		/// </summary>
		public ReadOnlyTrieBuilder()
		{
			Root = new Node();
		}

		/// <summary>
		/// Adds a value to the trie
		/// </summary>
		/// <param name="Value">Value to add</param>
		public void Add(ulong Value)
		{
			// Loop through the tree until we've added the item
			Node Leaf = Root;
			for (int Shift = (sizeof(ulong) * 8) - 4; Shift >= 0; Shift -= 4)
			{
				int Index = (int)(Value >> Shift) & 15;
				if (Leaf.Children == null)
				{
					Leaf.Children = new Node[16];
				}
				if (Leaf.Children[Index] == null)
				{
					Leaf.Children[Index] = new Node();
				}
				Leaf = Leaf.Children[Index]!;
			}
		}

		/// <summary>
		/// Searches for the given item in the trie
		/// </summary>
		/// <param name="Value">Value to add</param>
		public bool Contains(ulong Value)
		{
			// Loop through the tree until we've added the item
			Node Leaf = Root;
			for (int Shift = (sizeof(ulong) * 8) - 4; Shift >= 0; Shift -= 4)
			{
				int Index = (int)(Value >> Shift) & 15;
				if (Leaf.Children == null)
				{
					return false;
				}
				if (Leaf.Children[Index] == null)
				{
					return false;
				}
				Leaf = Leaf.Children[Index]!;
			}
			return true;
		}

		/// <summary>
		/// Creates a <see cref="ReadOnlyTrie"/> from this data
		/// </summary>
		/// <returns></returns>
		public ReadOnlyTrie Build()
		{
			List<ushort> Values = new List<ushort>();

			List<Node> Nodes = new List<Node>();
			Nodes.Add(Root);

			for(int Bits = 0; Bits < (sizeof(ulong) * 8); Bits += 4)
			{
				List<Node> NextNodes = new List<Node>();
				foreach(Node Node in Nodes)
				{
					ushort Value = 0;
					if (Node.Children != null)
					{
						for (int Idx = 0; Idx < Node.Children.Length; Idx++)
						{
							if(Node.Children[Idx] != null)
							{
								Value |= (ushort)(1 << Idx);
								NextNodes.Add(Node.Children[Idx]!);
							}
						}
					}
					Values.Add(Value);
				}
				Nodes = NextNodes;
			}

			return new ReadOnlyTrie(Values.ToArray());
		}
	}
}
