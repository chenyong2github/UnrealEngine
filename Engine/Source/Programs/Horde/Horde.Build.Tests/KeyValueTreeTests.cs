// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Services;
using HordeServer.Storage;
using HordeServer.Storage.Backends;
using HordeServer.Storage.Primitives;
using HordeServer.Storage.Services;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace HordeServerTests
{
	[TestClass]
	public class KeyValueTreeTests
	{
		class TestTreeMax2 : KeyValueTree
		{
			public TestTreeMax2()
				: base(BlobRef<LeafBlob>.Type, IoHash.NumBytes, new[] { (0, (IBlobType)BlobRef<LeafBlob>.Type) }, 2, 2)
			{
			}
		}

		class TestTreeMax256 : KeyValueTree
		{
			public TestTreeMax256()
				: base(BlobRef<LeafBlob>.Type, IoHash.NumBytes, new[] { (0, (IBlobType)BlobRef<LeafBlob>.Type) }, 256, 256)
			{
			}
		}

		[TestMethod]
		public void TestChildKeys()
		{
			Assert.AreEqual(160, KeyValueTreeExtensions.GetChildKeyLength(128));
			Assert.AreEqual(128, KeyValueTreeExtensions.GetChildKeyLength(64));
			Assert.AreEqual(64, KeyValueTreeExtensions.GetChildKeyLength(32));

			Assert.AreEqual(160, KeyValueTreeExtensions.GetChildKeyLength(128));

			Assert.AreEqual(128, KeyValueTreeExtensions.GetChildKeyLength(64));
			Assert.AreEqual(64, KeyValueTreeExtensions.GetChildKeyLength(56));
			Assert.AreEqual(64, KeyValueTreeExtensions.GetChildKeyLength(48));
			Assert.AreEqual(48, KeyValueTreeExtensions.GetChildKeyLength(40));

			Assert.AreEqual(64, KeyValueTreeExtensions.GetChildKeyLength(32));
		}

		static KeyValueItem MakeUpdate(IoHash HashValue)
		{
			byte[] Buffer = new byte[IoHash.NumBytes];
			new Random().NextBytes(Buffer);
			return new KeyValueItem(HashValue, Buffer);
		}

		static KeyValueItem MakeItem(string HashString)
		{
			return MakeUpdate(IoHash.Parse(HashString));
		}

		[TestMethod]
		public async Task TestBasicInsertion()
		{
			IStorageBackend<ArtifactCollection> Backend = new TransientStorageBackend().ForType<ArtifactCollection>();
			SimpleStorageService Storage = new SimpleStorageService(Backend);

			BlobRef<TestTreeMax2> Tree = await Storage.CreateKeyValueTreeAsync<TestTreeMax2>();
			{
				ReadOnlyKeyValueNode RootNode = await Storage.GetKeyValueNodeAsync(Tree.Hash);
				Assert.AreEqual(IoHash.NumBits, RootNode.NumKeyBits);
				Assert.AreEqual(0, RootNode.NumItems);
				Assert.AreEqual(0, RootNode.NumItemsIfMerged);
			}

			KeyValueItem Item1 = MakeItem("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
			Tree = await Storage.UpdateKeyValueTreeAsync(Tree, new[] { Item1 });
			{
				ReadOnlyKeyValueNode RootNode = await Storage.GetKeyValueNodeAsync(Tree.Hash);
				Assert.AreEqual(IoHash.NumBits, RootNode.NumKeyBits);
				Assert.AreEqual(1, RootNode.NumItems);
				Assert.AreEqual(1, RootNode.NumItemsIfMerged);
			}

			KeyValueItem Item2 = MakeItem("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaac");
			Tree = await Storage.UpdateKeyValueTreeAsync(Tree, new[] { Item2 });
			{
				ReadOnlyKeyValueNode RootNode = await Storage.GetKeyValueNodeAsync(Tree.Hash);
				Assert.AreEqual(IoHash.NumBits, RootNode.NumKeyBits);
				Assert.AreEqual(2, RootNode.NumItems);
				Assert.AreEqual(2, RootNode.NumItemsIfMerged);
			}

			KeyValueItem Item3 = MakeItem("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab");
			Tree = await Storage.UpdateKeyValueTreeAsync(Tree, new[] { Item3 });
			{
				ReadOnlyKeyValueNode Node = await Storage.GetKeyValueNodeAsync(Tree.Hash);
				Assert.IsTrue(Node.NumItems <= 2);
				Assert.AreEqual(3, Node.NumItemsIfMerged);
			}

			ReadOnlyMemory<byte>? FindItem1 = await Storage.SearchKeyValueTreeAsync(Tree, Item1.Key);
			Assert.IsNotNull(FindItem1);
			Assert.IsTrue(FindItem1.Value.Span.SequenceEqual(Item1.Value.Span));

			ReadOnlyMemory<byte>? FindItem2 = await Storage.SearchKeyValueTreeAsync(Tree, Item2.Key);
			Assert.IsNotNull(FindItem2);
			Assert.IsTrue(FindItem2.Value.Span.SequenceEqual(Item2.Value.Span));

			ReadOnlyMemory<byte>? FindItem3 = await Storage.SearchKeyValueTreeAsync(Tree, Item3.Key);
			Assert.IsNotNull(FindItem3);
			Assert.IsTrue(FindItem3.Value.Span.SequenceEqual(Item3.Value.Span));
		}

		[TestMethod]
		public async Task TestLoad()
		{
			Random Random = new Random(0x12345678);

			IStorageBackend<ArtifactCollection> Backend = new TransientStorageBackend().ForType<ArtifactCollection>();
			SimpleStorageService Storage = new SimpleStorageService(Backend);

			BlobRef<TestTreeMax256> Tree = await Storage.CreateKeyValueTreeAsync<TestTreeMax256>();

			Dictionary<IoHash, IoHash> Items = new Dictionary<IoHash, IoHash>();
			for (int OuterIdx = 0; OuterIdx < 128; OuterIdx++)
			{
				List<KeyValueItem> NewItems = new List<KeyValueItem>();
				for (int Idx = 0; Idx < 128; Idx++)
				{
					byte[] KeyBytes = new byte[IoHash.NumBytes];
					Random.NextBytes(KeyBytes);
					IoHash Key = new IoHash(KeyBytes);

					byte[] ValueBytes = new byte[IoHash.NumBytes];
					Random.NextBytes(ValueBytes);
					IoHash Value = new IoHash(ValueBytes);

					NewItems.Add(new KeyValueItem(Key, ValueBytes));
					Items[Key] = Value;
				}
				Tree = await Storage.UpdateKeyValueTreeAsync(Tree, NewItems);
			}

			int NumItems = await CountItemsAsync(Storage, Tree.Hash);
			Assert.AreEqual(Items.Count, NumItems);

			foreach (KeyValuePair<IoHash, IoHash> Pair in Items)
			{
				ReadOnlyMemory<byte>? Value = await Storage.SearchKeyValueTreeAsync(Tree, Pair.Key);
				Assert.IsNotNull(Value);
				Assert.IsTrue(Value.Value.Span.SequenceEqual(Pair.Value.Span));
			}
		}

		static async Task<int> CountItemsAsync(IStorageService Storage, IoHash Hash)
		{
			ReadOnlyKeyValueNode Node = await Storage.GetKeyValueNodeAsync(Hash);
			if (Node.NumKeyBits == IoHash.NumBits)
			{
				return Node.NumItems;
			}
			else
			{
				int NumItems = 0;

				ReadOnlyHashArray Children = new ReadOnlyHashArray(Node.Values);
				foreach (IoHash Child in Children)
				{
					NumItems += await CountItemsAsync(Storage, Child);
				}

				return NumItems;
			}
		}
	}
}
