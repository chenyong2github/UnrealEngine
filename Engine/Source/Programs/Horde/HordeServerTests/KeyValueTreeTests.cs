using HordeServer.Storage;
using HordeServer.Storage.Impl;
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
				: base(BlobRef<LeafBlob>.Type, BlobHash.NumBytes, new[] { (0, (IBlobType)BlobRef<LeafBlob>.Type) }, 2, 2)
			{
			}
		}

		class TestTreeMax256 : KeyValueTree
		{
			public TestTreeMax256()
				: base(BlobRef<LeafBlob>.Type, BlobHash.NumBytes, new[] { (0, (IBlobType)BlobRef<LeafBlob>.Type) }, 256, 256)
			{
			}
		}

		[TestMethod]
		public void TestChildKeys()
		{
			Assert.AreEqual(256, KeyValueTreeExtensions.GetChildKeyLength(128));
			Assert.AreEqual(128, KeyValueTreeExtensions.GetChildKeyLength(64));
			Assert.AreEqual(64, KeyValueTreeExtensions.GetChildKeyLength(32));

			Assert.AreEqual(256, KeyValueTreeExtensions.GetChildKeyLength(128));

			Assert.AreEqual(128, KeyValueTreeExtensions.GetChildKeyLength(64));
			Assert.AreEqual(64, KeyValueTreeExtensions.GetChildKeyLength(56));
			Assert.AreEqual(64, KeyValueTreeExtensions.GetChildKeyLength(48));
			Assert.AreEqual(48, KeyValueTreeExtensions.GetChildKeyLength(40));

			Assert.AreEqual(128 + 128, KeyValueTreeExtensions.GetChildKeyLength(128 + 64));
			Assert.AreEqual(128 + 64, KeyValueTreeExtensions.GetChildKeyLength(128 + 56));
			Assert.AreEqual(128 + 64, KeyValueTreeExtensions.GetChildKeyLength(128 + 48));
			Assert.AreEqual(128 + 48, KeyValueTreeExtensions.GetChildKeyLength(128 + 40));

			Assert.AreEqual(64, KeyValueTreeExtensions.GetChildKeyLength(32));
		}

		static KeyValueItem MakeUpdate(BlobHash HashValue)
		{
			byte[] Buffer = new byte[BlobHash.NumBytes];
			new Random().NextBytes(Buffer);
			return new KeyValueItem(HashValue, Buffer);
		}

		static KeyValueItem MakeItem(string HashString)
		{
			return MakeUpdate(BlobHash.Parse(HashString));
		}

		[TestMethod]
		public async Task TestBasicInsertion()
		{
			TransientStorageBackend Backend = new TransientStorageBackend();
			SimpleStorageService Storage = new SimpleStorageService(Backend);

			BlobRef<TestTreeMax2> Tree = await Storage.CreateKeyValueTreeAsync<TestTreeMax2>();
			{
				ReadOnlyKeyValueNode RootNode = await Storage.GetKeyValueNodeAsync(Tree.Hash);
				Assert.AreEqual(BlobHash.NumBits, RootNode.NumKeyBits);
				Assert.AreEqual(0, RootNode.NumItems);
				Assert.AreEqual(0, RootNode.NumItemsIfMerged);
			}

			KeyValueItem Item1 = MakeItem("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
			Tree = await Storage.UpdateKeyValueTreeAsync(Tree, new[] { Item1 });
			{
				ReadOnlyKeyValueNode RootNode = await Storage.GetKeyValueNodeAsync(Tree.Hash);
				Assert.AreEqual(BlobHash.NumBits, RootNode.NumKeyBits);
				Assert.AreEqual(1, RootNode.NumItems);
				Assert.AreEqual(1, RootNode.NumItemsIfMerged);
			}

			KeyValueItem Item2 = MakeItem("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaac");
			Tree = await Storage.UpdateKeyValueTreeAsync(Tree, new[] { Item2 });
			{
				ReadOnlyKeyValueNode RootNode = await Storage.GetKeyValueNodeAsync(Tree.Hash);
				Assert.AreEqual(BlobHash.NumBits, RootNode.NumKeyBits);
				Assert.AreEqual(2, RootNode.NumItems);
				Assert.AreEqual(2, RootNode.NumItemsIfMerged);
			}

			KeyValueItem Item3 = MakeItem("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab");
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

			TransientStorageBackend Backend = new TransientStorageBackend();
			SimpleStorageService Storage = new SimpleStorageService(Backend);

			BlobRef<TestTreeMax256> Tree = await Storage.CreateKeyValueTreeAsync<TestTreeMax256>();

			Dictionary<BlobHash, BlobHash> Items = new Dictionary<BlobHash, BlobHash>();
			for (int OuterIdx = 0; OuterIdx < 128; OuterIdx++)
			{
				List<KeyValueItem> NewItems = new List<KeyValueItem>();
				for (int Idx = 0; Idx < 128; Idx++)
				{
					byte[] KeyBytes = new byte[BlobHash.NumBytes];
					Random.NextBytes(KeyBytes);
					BlobHash Key = new BlobHash(KeyBytes);

					byte[] ValueBytes = new byte[BlobHash.NumBytes];
					Random.NextBytes(ValueBytes);
					BlobHash Value = new BlobHash(ValueBytes);

					NewItems.Add(new KeyValueItem(Key, ValueBytes));
					Items[Key] = Value;
				}
				Tree = await Storage.UpdateKeyValueTreeAsync(Tree, NewItems);
			}

			int NumItems = await CountItemsAsync(Storage, Tree.Hash);
			Assert.AreEqual(Items.Count, NumItems);

			foreach (KeyValuePair<BlobHash, BlobHash> Pair in Items)
			{
				ReadOnlyMemory<byte>? Value = await Storage.SearchKeyValueTreeAsync(Tree, Pair.Key);
				Assert.IsNotNull(Value);
				Assert.IsTrue(Value.Value.Span.SequenceEqual(Pair.Value.Span));
			}
		}

		static async Task<int> CountItemsAsync(IStorageService Storage, BlobHash Hash)
		{
			ReadOnlyKeyValueNode Node = await Storage.GetKeyValueNodeAsync(Hash);
			if (Node.NumKeyBits == BlobHash.NumBits)
			{
				return Node.NumItems;
			}
			else
			{
				int NumItems = 0;

				ReadOnlyBlobHashArray Children = new ReadOnlyBlobHashArray(Node.Values);
				foreach (BlobHash Child in Children)
				{
					NumItems += await CountItemsAsync(Storage, Child);
				}

				return NumItems;
			}
		}
	}
}
