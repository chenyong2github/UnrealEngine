// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Storage.Primitives
{
	/// <summary>
	/// Base type definition for key/value trees. Handles both internal and leaf nodes.
	/// </summary>
	abstract class KeyValueTree : IBlobType
	{
		/// <summary>
		/// Default maximum node size
		/// </summary>
		const int DefaultMaxNodeSize = 1024 * 1024;

		/// <summary>
		/// The key type
		/// </summary>
		public IBlobType KeyType;

		/// <summary>
		/// Length of each value
		/// </summary>
		public int LeafValueSize { get; }

		/// <summary>
		/// Offset within each value item of blob references, and their types
		/// </summary>
		public (int, IBlobType)[] LeafValueRefTypes { get; }

		/// <summary>
		/// Number of items for an internal node
		/// </summary>
		public int MaxItemsPerInternalNode { get; }

		/// <summary>
		/// Number of items for a leaf node
		/// </summary>
		public int MaxItemsPerLeafNode { get; }

		/// <inheritdoc/>
		public string Name => "KeyValueTree";

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="KeyType">Type of key</param>
		/// <param name="LeafValueSize">Length of each value</param>
		/// <param name="LeafValueRefTypes">References from each value</param>
		public KeyValueTree(IBlobType KeyType, int LeafValueSize, params (int, IBlobType)[] LeafValueRefTypes)
			: this(KeyType, LeafValueSize, LeafValueRefTypes, DefaultMaxNodeSize / (IoHash.NumBytes * 2), DefaultMaxNodeSize / (IoHash.NumBytes + LeafValueSize))
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="KeyType">Type of key</param>
		/// <param name="LeafValueSize">Length of each value</param>
		/// <param name="LeafValueRefTypes">References from each value</param>
		/// <param name="MaxItemsPerInternalNode">Maximum number of items to store in each internal node</param>
		/// <param name="MaxItemsPerLeafNode">Maximum number of items to store in each leaf node</param>
		public KeyValueTree(IBlobType KeyType, int LeafValueSize, (int, IBlobType)[] LeafValueRefTypes, int MaxItemsPerInternalNode, int MaxItemsPerLeafNode)
		{
			this.KeyType = KeyType;
			this.LeafValueSize = LeafValueSize;
			this.LeafValueRefTypes = LeafValueRefTypes;
			this.MaxItemsPerInternalNode = MaxItemsPerInternalNode;
			this.MaxItemsPerLeafNode = MaxItemsPerLeafNode;
		}

		/// <summary>
		/// Get references from the given data
		/// </summary>
		/// <param name="Data">Blob of data</param>
		/// <returns>Sequence of blob references</returns>
		public IEnumerable<BlobRef> GetRefs(ReadOnlyMemory<byte> Data)
		{
			ReadOnlyKeyValueNode Node = new ReadOnlyKeyValueNode(Data);
			if (Node.NumKeyBits == IoHash.NumBits)
			{
				// Leaf node: keys are references to other blobs, values follow type definition
				ReadOnlyMemory<byte> Keys = Node.Keys;
				while (!Keys.IsEmpty)
				{
					yield return new BlobRef(KeyType, new IoHash(Keys.Slice(0, IoHash.NumBytes)));
					Keys = Keys.Slice(IoHash.NumBytes);
				}

				ReadOnlyMemory<byte> Values = Node.Values;
				while (!Values.IsEmpty)
				{
					foreach ((int Offset, IBlobType Type) in LeafValueRefTypes)
					{
						yield return new BlobRef(Type, new IoHash(Values.Slice(Offset, IoHash.NumBytes)));
					}
					Values = Values.Slice(LeafValueSize);
				}
			}
			else
			{
				// Internal node: keys are just prefixes, and not concrete references. Values are always just references to other trees.
				ReadOnlyHashArray Values = new ReadOnlyHashArray(Node.Values);
				foreach(IoHash Value in Values)
				{
					yield return new BlobRef(this, Value);
				}
			}
		}
	}

	/// <summary>
	/// Base type definition for key/value trees with a static key type
	/// </summary>
	abstract class KeyValueTree<TKey> : KeyValueTree where TKey : IBlobType, new()
	{
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="ValueSize"></param>
		/// <param name="ValueRefTypes"></param>
		public KeyValueTree(int ValueSize, params (int, IBlobType)[] ValueRefTypes)
			: base(BlobRef<TKey>.Type, ValueSize, ValueRefTypes)
		{
		}
	}

	/// <summary>
	/// Base type definition for key/value trees with a static key type
	/// </summary>
	sealed class KeyValueTree<TKey, TValue> : KeyValueTree where TKey : IBlobType, new() where TValue : IBlobType, new()
	{
		/// <summary>
		/// Constructor
		/// </summary>
		public KeyValueTree()
			: base(BlobRef<TKey>.Type, IoHash.NumBytes, (0, BlobRef<TValue>.Type))
		{
		}
	}

	/// <summary>
	/// Extension methods for manipulating key/value trees
	/// </summary>
	static class KeyValueTreeExtensions
	{
		/// <summary>
		/// Creates a new, empty key value tree
		/// </summary>
		/// <typeparam name="TTree">The tree type</typeparam>
		/// <param name="Storage">Storage service</param>
		/// <returns>Reference to the new tree</returns>
		public static async Task<BlobRef<TTree>> CreateKeyValueTreeAsync<TTree>(this IStorageService Storage) where TTree : KeyValueTree, new()
		{
			KeyValueNode Node = new KeyValueNode(0, 0, IoHash.NumBits, IoHash.NumBits, 0);
			return new BlobRef<TTree>(await Storage.PutKeyValueNodeAsync(Node));
		}

		public static async Task<BlobRef<TValue>?> SearchKeyValueTreeAsync<TKey, TValue>(this IStorageService Storage, BlobRef<KeyValueTree<TKey, TValue>> Tree, IoHash FindHash) where TKey : IBlobType, new() where TValue : IBlobType, new()
		{
			ReadOnlyMemory<byte>? Result = await SearchKeyValueTreeInternalAsync(Storage, Tree, FindHash);
			if(Result == null)
			{
				return null;
			}
			return new BlobRef<TValue>(new IoHash(Result.Value));
		}

		public static Task<ReadOnlyMemory<byte>?> SearchKeyValueTreeAsync<TTree>(this IStorageService Storage, BlobRef<TTree> Tree, IoHash FindHash) where TTree : KeyValueTree, new()
		{
			return SearchKeyValueTreeInternalAsync(Storage, Tree, FindHash);
		}

		static async Task<ReadOnlyMemory<byte>?> SearchKeyValueTreeInternalAsync<TTree>(this IStorageService Storage, BlobRef<TTree> Tree, IoHash FindHash) where TTree : KeyValueTree, new()
		{
			IoHash Hash = Tree.Hash;
			for (; ; )
			{
				ReadOnlyKeyValueNode Node = await Storage.GetKeyValueNodeAsync(Hash);

				IoHash MaskHash = MaskLeft(FindHash, Node.NumKeyBits);

				ReadOnlyHashArray Keys = new ReadOnlyHashArray(Node.Keys);

				int KeyIdx = Keys.BinarySearch(MaskHash);
				if (KeyIdx < 0)
				{
					return null;
				}
				else if (Node.NumKeyBits == IoHash.NumBits)
				{
					return Node.Values.Slice(KeyIdx * BlobRef<TTree>.Type.LeafValueSize, BlobRef<TTree>.Type.LeafValueSize);
				}
				else
				{
					Hash = new ReadOnlyHashArray(Node.Values)[KeyIdx];
				}
			}
		}

		/// <summary>
		/// Updates a key value tree with the given entries
		/// </summary>
		/// <typeparam name="TTree">The tree to update</typeparam>
		/// <param name="Storage">Storage service instance</param>
		/// <param name="Tree">The tree to update</param>
		/// <param name="NewItems">New items to add</param>
		/// <returns></returns>
		public static async Task<BlobRef<TTree>> UpdateKeyValueTreeAsync<TTree>(this IStorageService Storage, BlobRef<TTree> Tree, IEnumerable<KeyValueItem> NewItems) where TTree : KeyValueTree, new()
		{
			IoHash NewRootHash = await UpdateKeyValueTreeAsync(Storage, BlobRef<TTree>.Type, Tree.Hash, NewItems);
			return new BlobRef<TTree>(NewRootHash);
		}

		/// <summary>
		/// Updates a key/value tree with a set of items
		/// </summary>
		/// <param name="Storage">The storage service</param>
		/// <param name="Tree">The tree to update</param>
		/// <param name="RootHash">Hash of the root node of the tree</param>
		/// <param name="NewItems">New items to add</param>
		/// <returns>Hash for the new tree root</returns>
		static async Task<IoHash> UpdateKeyValueTreeAsync(this IStorageService Storage, KeyValueTree Tree, IoHash RootHash, IEnumerable<KeyValueItem> NewItems)
		{
			// Sort all the items and remove duplicates
			ArraySegment<KeyValueItem> NewSortedItems = NewItems.OrderBy(x => x.Key).ToArray();
			if (NewSortedItems.Count > 0)
			{
				int OutputCount = 1;
				for (int InputCount = 1; InputCount < NewSortedItems.Count; InputCount++)
				{
					if (NewSortedItems[InputCount].Key != NewSortedItems[OutputCount - 1].Key)
					{
						NewSortedItems[OutputCount++] = NewSortedItems[InputCount];
					}
				}
				NewSortedItems = NewSortedItems.Slice(0, OutputCount);
			}

			// Get the root node
			ReadOnlyKeyValueNode RootNode;
			if (RootHash == IoHash.Zero)
			{
				RootNode = new KeyValueNode(0, 0, IoHash.NumBits, IoHash.NumBits, Tree.LeafValueSize);
			}
			else
			{
				RootNode = await Storage.GetKeyValueNodeAsync(RootHash);
			}

			// Get the new node
			KeyValueNode NewRootNode = await UpdateKeyValueNodeAsync(Storage, Tree, 0, RootNode, NewSortedItems);
			return await Storage.PutKeyValueNodeAsync(NewRootNode);
		}

		/// <summary>
		/// Update a key value tree node
		/// </summary>
		/// <param name="Storage">Storage service</param>
		/// <param name="Tree">The tree definition</param>
		/// <param name="ParentKeyBits">Number of bits in the parent node</param>
		/// <param name="Node">The current node state</param>
		/// <param name="NewItems">The items to modify</param>
		/// <returns>Updated node</returns>
		static async Task<KeyValueNode> UpdateKeyValueNodeAsync(IStorageService Storage, KeyValueTree Tree, int ParentKeyBits, ReadOnlyKeyValueNode Node, ArraySegment<KeyValueItem> NewItems)
		{
			// Create a list wrapper for the keys, so we can perform binary searches on it
			ReadOnlyHashArray Keys = new ReadOnlyHashArray(Node.Keys);

			// Keep track of the total number of child items underneath this node in order to update the merge count
			int NewNumChildItems = Node.NumItemsIfMerged - Node.NumItems;

			// Find the necessary updates to the node
			List<(int KeyIdx, KeyValueItem? NewItem)> Updates = new List<(int, KeyValueItem?)>(NewItems.Count);
			if (Node.NumKeyBits == IoHash.NumBits)
			{
				foreach (KeyValueItem NewItem in NewItems)
				{
					int KeyIdx = Keys.BinarySearch(NewItem.Key);
					if (KeyIdx >= 0 || !NewItem.Value.IsEmpty)
					{
						Updates.Add((KeyIdx, NewItem));
					}
				}
			}
			else
			{
				ReadOnlyHashArray Children = new ReadOnlyHashArray(Node.Values);
				for (int Idx = 0; Idx < NewItems.Count;)
				{
					IoHash KeyPrefix = MaskLeft(NewItems[Idx].Key, Node.NumKeyBits);

					// Find all the operations which start with the same prefix
					int MinIdx = Idx++;
					while (Idx < NewItems.Count && StartsWith(NewItems[Idx].Key, KeyPrefix, Node.NumKeyBits))
					{
						Idx++;
					}

					// Check if it already exists within the tree
					int KeyIdx = Keys.BinarySearch(KeyPrefix);

					// Get the old child node
					ReadOnlyKeyValueNode OldChildNode;
					if (KeyIdx >= 0)
					{
						OldChildNode = await Storage.GetKeyValueNodeAsync(Children[KeyIdx]);
					}
					else
					{
						OldChildNode = CreateEmptyChildNode(Node.NumKeyBits);
					}

					// Create the new item
					KeyValueNode NewChildNode = await UpdateKeyValueNodeAsync(Storage, Tree, Node.NumKeyBits, OldChildNode, NewItems.Slice(MinIdx, Idx - MinIdx));
					if (NewChildNode.NumItems == 0)
					{
						// Only remove it if it already exists
						if (KeyIdx >= 0)
						{
							Updates.Add((KeyIdx, null));
						}
					}
					else
					{
						// Add the new item to the update list
						IoHash NewChildHash = await Storage.PutKeyValueNodeAsync(NewChildNode);
						KeyValueItem NewItem = new KeyValueItem(KeyPrefix, NewChildHash.Memory);
						Updates.Add((KeyIdx, NewItem));

						// Update the number of merged items
						NewNumChildItems += NewChildNode.NumItemsIfMerged - OldChildNode.NumItemsIfMerged;
					}
				}
			}

			// Create a new buffer for the keys
			int NewNumItems = Node.NumItems + Updates.Count(x => x.KeyIdx < 0) - Updates.Count(x => x.NewItem == null);
			KeyValueNode NewNode = CreateNode(Tree, NewNumItems, NewNumItems + NewNumChildItems, Node.NumKeyBits, Node.NumKeyBitsIfMerged);

			// Copy all the data across
			int ReadIdx = 0;
			KeyValueItemReader Reader = new KeyValueItemReader(Tree, Node);
			KeyValueItemWriter Writer = new KeyValueItemWriter(NewNode);
			foreach ((int KeyIdx, KeyValueItem? NewItem) in Updates)
			{
				// Copy all the data before the new item
				if (KeyIdx < 0)
				{
					int Count = ~KeyIdx - ReadIdx;
					Reader.CopyTo(Writer, Count);
					ReadIdx += Count;
				}
				else
				{
					int Count = KeyIdx - ReadIdx;
					Reader.CopyTo(Writer, Count);
					Reader.MoveNext();
					ReadIdx += Count + 1;
				}

				// Copy the new item
				if (NewItem != null)
				{
					Writer.WriteItem(NewItem.Value.Key, NewItem.Value.Value);
				}
			}
			Reader.CopyTo(Writer);

			// Merge together layers of the tree
			while (NewNode.NumKeyBitsIfMerged > NewNode.NumKeyBits && NewNode.NumItemsIfMerged < Tree.MaxItemsPerInternalNode)
			{
				NewNode = await MergeAsync(Storage, Tree, NewNode);
			}

			// If it's larger than a threshold, we need to split this node into two
			while (NewNode.NumItems > ((NewNode.NumKeyBits == IoHash.NumBits)? Tree.MaxItemsPerLeafNode : Tree.MaxItemsPerInternalNode))
			{
				NewNode = await SplitAsync(Storage, Tree, NewNode, ParentKeyBits);
			}

			// Return the updated node
			return NewNode;
		}

		/// <summary>
		/// Merges a node iwth its children
		/// </summary>
		/// <param name="Storage"></param>
		/// <param name="Tree"></param>
		/// <param name="Node"></param>
		/// <returns></returns>
		public static async Task<KeyValueNode> MergeAsync(IStorageService Storage, KeyValueTree Tree, ReadOnlyKeyValueNode Node)
		{
			KeyValueNode NewNode = CreateNode(Tree, Node.NumItemsIfMerged, Node.NumItemsIfMerged, Node.NumKeyBitsIfMerged, Node.NumKeyBitsIfMerged);
			KeyValueItemWriter Writer = new KeyValueItemWriter(NewNode);

			ReadOnlyHashArray Values = new ReadOnlyHashArray(Node.Values);
			for (int Idx = 0; Idx < Values.Count; Idx++)
			{
				ReadOnlyKeyValueNode ChildNode = await Storage.GetKeyValueNodeAsync(Values[Idx]);
				Writer.WriteItems(ChildNode.Keys, ChildNode.Values);
			}

			return NewNode;
		}

		/// <summary>
		/// Splits a node in the key value tree
		/// </summary>
		/// <param name="Storage"></param>
		/// <param name="Tree">The tree definition</param>
		/// <param name="Node"></param>
		/// <param name="ParentKeyBits"></param>
		/// <returns></returns>
		public static async Task<KeyValueNode> SplitAsync(IStorageService Storage, KeyValueTree Tree, ReadOnlyKeyValueNode Node, int ParentKeyBits)
		{
			// Given an even distribution of keys across the hash space, keeping the tree balanced requires that we keep the 
			// number of items in the parent node approximately equal to the average number of items in each child node.
			// 
			// If h is the number of bits in the hash, n is the number of bits in the parent node, and m is the number of 
			// child nodes, it follows that:
			// 
			//    Items in parent node = 2 ^ n
			//    Items in each child node = (2 ^ (h - n)) / m
			// 
			// Thus: n = (h - log2(m)) / 2
			int MaxChildren = Node.IsLeafNode ? Tree.MaxItemsPerLeafNode : Tree.MaxItemsPerInternalNode;
			int NewMaxKeyBits = ParentKeyBits + Math.Max(1, (int)(((Node.NumKeyBits - ParentKeyBits) - Math.Log2(MaxChildren)) * 0.5));

			List<KeyValueItem> NewItems = new List<KeyValueItem>();

			KeyValueItemReader Reader = new KeyValueItemReader(Tree, Node);

			ReadOnlyHashArray Keys = new ReadOnlyHashArray(Node.Keys);
			for (int KeyIdx = 0; KeyIdx < Keys.Count; )
			{
				// Mask off the first key prefix
				IoHash KeyPrefix = MaskLeft(Keys[KeyIdx], NewMaxKeyBits);

				// Count the number of keys with the same prefix
				int Count = 1;
				while (KeyIdx + Count < Keys.Count && StartsWith(Keys[KeyIdx + Count], KeyPrefix, NewMaxKeyBits))
				{
					Count++;
				}

				// Create the new child node
				KeyValueNode NewChildNode = CreateNode(Tree, Count, Count, Node.NumKeyBits, Node.NumKeyBits);
				Reader.CopyTo(new KeyValueItemWriter(NewChildNode), Count);
				KeyIdx += Count;

				// If the child node is still above the max number of children, split it again
				while (NewChildNode.NumItems > MaxChildren)
				{
					NewChildNode = await SplitAsync(Storage, Tree, NewChildNode, NewMaxKeyBits);
				}

				// Finally, store the child node and add it to its new parent
				IoHash NewChildHash = await Storage.PutKeyValueNodeAsync(NewChildNode);
				NewItems.Add(new KeyValueItem(KeyPrefix, NewChildHash.Memory));
			}

			KeyValueNode NewNode = new KeyValueNode(NewItems.Count, Node.NumItems, NewMaxKeyBits, Node.NumKeyBits, IoHash.NumBytes);

			KeyValueItemWriter Writer = new KeyValueItemWriter(NewNode);
			foreach (KeyValueItem NewItem in NewItems)
			{
				Writer.WriteItem(NewItem.Key, NewItem.Value);
			}

			return NewNode;
		}

		public static KeyValueNode CreateNode(KeyValueTree Tree, int NumItems, int NumItemsIfMerged, int NumKeyBits, int NumKeyBitsIfMerged)
		{
			int NewValueSize = (NumKeyBits == IoHash.NumBits) ? Tree.LeafValueSize : IoHash.NumBytes;
			return new KeyValueNode(NumItems, NumItemsIfMerged, NumKeyBits, NumKeyBitsIfMerged, NewValueSize);
		}

		public static ReadOnlyKeyValueNode CreateEmptyChildNode(int MaxKeyBits)
		{
			int ChildKeyBits = GetChildKeyLength(MaxKeyBits);
			return new KeyValueNode(0, 0, ChildKeyBits, ChildKeyBits, 0);
		}

		public static int GetChildKeyLength(int KeyLength)
		{
			// Find the largest power of two boundary that the current key length is on
			int KeyStep = 1;
			while (KeyLength + KeyStep < IoHash.NumBits)
			{
				int NextKeyStep = KeyStep << 1;
				if ((KeyLength & (NextKeyStep - 1)) != 0)
				{
					break;
				}
				KeyStep = NextKeyStep;
			}
			return KeyLength + KeyStep;
		}

		/// <summary>
		/// Check if one hash starts with the same bits as another
		/// </summary>
		/// <param name="This"></param>
		/// <param name="Other">The hash to test against</param>
		/// <param name="Length">Number of bits to compare</param>
		/// <returns>True if the hashes match, false otherwise</returns>
		static bool StartsWith(IoHash This, IoHash Other, int Length)
		{
			ReadOnlySpan<byte> ThisSpan = This.Span;
			ReadOnlySpan<byte> OtherSpan = Other.Span;

			int Idx = 0;
			int RemainingLength = Length;

			for (; RemainingLength > 8; RemainingLength -= 8, Idx++)
			{
				if (ThisSpan[Idx] != OtherSpan[Idx])
				{
					return false;
				}
			}

			int Mask = ~((1 << (8 - RemainingLength)) - 1);
			return (ThisSpan[Idx] & Mask) == (OtherSpan[Idx] & Mask);
		}

		/// <summary>
		/// Mask off the left number of bits from the hash
		/// </summary>
		/// <param name="Hash"></param>
		/// <param name="NumBits">Number of bits to include</param>
		/// <returns>Masked hash value</returns>
		static IoHash MaskLeft(IoHash Hash, int NumBits)
		{
			ReadOnlySpan<byte> OldData = Hash.Span;
			byte[] NewData = new byte[IoHash.NumBytes];

			int Idx = 0;
			int RemainingBits = NumBits;
			for (; RemainingBits > 8; RemainingBits -= 8, Idx++)
			{
				NewData[Idx] = OldData[Idx];
			}

			NewData[Idx] = (byte)(OldData[Idx] & ~((1 << (8 - RemainingBits)) - 1));
			return new IoHash(NewData);
		}

		/// <summary>
		/// Adds a key/value tree node to a storage provider
		/// </summary>
		/// <param name="Provider"></param>
		/// <param name="Node"></param>
		/// <returns></returns>
		public static async Task<IoHash> PutKeyValueNodeAsync(this IStorageService Provider, ReadOnlyKeyValueNode Node)
		{
			return await Provider.PutBlobAsync(Node.Memory);
		}

		/// <summary>
		/// Gets a key/value tree node from a storage provider
		/// </summary>
		/// <param name="Provider"></param>
		/// <param name="Hash"></param>
		/// <returns></returns>
		public static async Task<ReadOnlyKeyValueNode> GetKeyValueNodeAsync(this IStorageService Provider, IoHash Hash)
		{
			return new ReadOnlyKeyValueNode(await Provider.GetBlobAsync(Hash));
		}
	}
}
