// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Bundles;
using EpicGames.Horde.Bundles.Nodes;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Impl;
using EpicGames.Serialization;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;

namespace HordeServerTests
{
	[TestClass]
	public class BundleTests
	{
		MemoryStorageClient StorageClient = new MemoryStorageClient();
		NamespaceId NamespaceId = new NamespaceId("namespace");
		BucketId BucketId = new BucketId("bucket");

		[TestMethod]
		public void SerializationTests()
		{
			DateTime UtcNow = DateTime.UtcNow;

			List<BundleImport> Imports = new List<BundleImport>();
			Imports.Add(new BundleImport(IoHash.Compute(Encoding.UTF8.GetBytes("import1")), 0, 123));
			Imports.Add(new BundleImport(IoHash.Compute(Encoding.UTF8.GetBytes("import2")), 1, 456));

			BundleObject OldObject = new BundleObject();
			OldObject.CreationTimeUtc = DateTime.UtcNow;
			OldObject.ImportObjects.Add(new BundleImportObject(UtcNow - TimeSpan.FromMinutes(5.0), new CbObjectAttachment(IoHash.Compute(new byte[] { 1, 2, 3 })), 12345, Imports));
			OldObject.Exports.Add(new BundleExport(IoHash.Compute(Encoding.UTF8.GetBytes("export1")), 2, 5, new BundleCompressionPacket(0) { EncodedLength = 20, DecodedLength = 40 }, 0, 40, new int[] { 1, 2 }));
			OldObject.Exports.Add(new BundleExport(IoHash.Compute(Encoding.UTF8.GetBytes("export2")), 3, 6, new BundleCompressionPacket(20) { EncodedLength = 10, DecodedLength = 20 }, 0, 20, new int[] { -1 }));

			CbObject SerializedObject = CbSerializer.Serialize(OldObject);
			ReadOnlyMemory<byte> SerializedData = SerializedObject.GetView();

			BundleObject NewObject = CbSerializer.Deserialize<BundleObject>(SerializedData);

			Assert.AreEqual(OldObject.CreationTimeUtc, NewObject.CreationTimeUtc);

			Assert.AreEqual(OldObject.ImportObjects.Count, NewObject.ImportObjects.Count);
			for (int Idx = 0; Idx < OldObject.ImportObjects.Count; Idx++)
			{
				BundleImportObject OldObjectImport = OldObject.ImportObjects[Idx];
				BundleImportObject NewObjectImport = NewObject.ImportObjects[Idx];

				Assert.AreEqual(OldObjectImport.CreationTimeUtc, NewObjectImport.CreationTimeUtc);
				Assert.AreEqual(OldObjectImport.Object, NewObjectImport.Object);
				Assert.AreEqual(OldObjectImport.TotalCost, NewObjectImport.TotalCost);
				Assert.AreEqual(OldObjectImport.Imports.Count, NewObjectImport.Imports.Count);

				for (int ImportIdx = 0; ImportIdx < OldObjectImport.Imports.Count; ImportIdx++)
				{
					BundleImport OldImport = OldObjectImport.Imports[ImportIdx];
					BundleImport NewImport = NewObjectImport.Imports[ImportIdx];

					Assert.AreEqual(OldImport.Hash, NewImport.Hash);
					Assert.AreEqual(OldImport.Rank, NewImport.Rank);
					Assert.AreEqual(OldImport.Cost, NewImport.Cost);
				}
			}

			Assert.AreEqual(OldObject.Exports.Count, NewObject.Exports.Count);
			for (int Idx = 0; Idx < OldObject.Exports.Count; Idx++)
			{
				BundleExport OldExport = OldObject.Exports[Idx];
				BundleExport NewExport = NewObject.Exports[Idx];

				Assert.AreEqual(OldExport.Hash, NewExport.Hash);
				Assert.AreEqual(OldExport.Rank, NewExport.Rank);
				Assert.AreEqual(OldExport.Cost, NewExport.Cost);
				Assert.AreEqual(OldExport.Packet.Offset, NewExport.Packet.Offset);
				Assert.AreEqual(OldExport.Packet.EncodedLength, NewExport.Packet.EncodedLength);
				Assert.AreEqual(OldExport.Packet.DecodedLength, NewExport.Packet.DecodedLength);
				Assert.AreEqual(OldExport.Offset, NewExport.Offset);
				Assert.AreEqual(OldExport.Length, NewExport.Length);
				Assert.IsTrue(OldExport.References.AsSpan().SequenceEqual(NewExport.References.AsSpan()));
			}
		}

		[TestMethod]
		public async Task BasicTestDirectory()
		{
			Bundle<DirectoryNode> Bundle = new Bundle<DirectoryNode>(StorageClient, NamespaceId, new BundleOptions(), null);
			DirectoryNode Node = await Bundle.Root.AddDirectoryAsync("hello");
			DirectoryNode Node2 = await Node.AddDirectoryAsync("world");

			RefId RefId = new RefId("testref");
			await Bundle.WriteAsync(BucketId, RefId, false, DateTime.MinValue);

			// Should be stored inline
			Assert.AreEqual(1, StorageClient.Refs.Count);
			Assert.AreEqual(0, StorageClient.Blobs.Count);

			// Check the ref
			BundleObject Root = await StorageClient.GetRefAsync<BundleObject>(NamespaceId, BucketId, RefId);
			Assert.AreEqual(3, Root.Exports.Count);
			Assert.AreEqual(0, Root.Exports[0].Rank);
			Assert.AreEqual(1, Root.Exports[1].Rank);
			Assert.AreEqual(2, Root.Exports[2].Rank);

			// Create a new bundle and read it back in again
			Bundle = new Bundle<DirectoryNode>(StorageClient, NamespaceId, new BundleOptions(), null);
			await Bundle.ReadAsync(BucketId, RefId);

			Assert.AreEqual(1, Bundle.Root.Entries.Count);
			DirectoryNode? OutputNode = await Bundle.Root.FindDirectoryAsync("hello");
			Assert.IsNotNull(OutputNode);

			Assert.AreEqual(1, OutputNode!.Entries.Count);
			DirectoryNode? OutputNode2 = await OutputNode!.FindDirectoryAsync("world");
			Assert.IsNotNull(OutputNode2);

			Assert.AreEqual(0, OutputNode2!.Entries.Count);
		}



		// caching
		// hierarchical chunking

		[TestMethod]
		public async Task DedupTests()
		{
			BundleOptions Options = new BundleOptions();
			Options.MaxBlobSize = 1;
			Options.MaxInlineBlobSize = 1;

			Bundle<DirectoryNode> Bundle = new Bundle<DirectoryNode>(StorageClient, NamespaceId, Options, null);

			await Bundle.Root.AddDirectoryAsync("node1");
			await Bundle.Root.AddDirectoryAsync("node2");
			await Bundle.Root.AddDirectoryAsync("node3");

			RefId RefId = new RefId("ref");
			await Bundle.WriteAsync(BucketId, RefId, false, DateTime.UtcNow);

			Assert.AreEqual(1, StorageClient.Refs.Count);
			Assert.AreEqual(1, StorageClient.Blobs.Count);
		}

		[TestMethod]
		public async Task ReloadTests()
		{
			BundleOptions Options = new BundleOptions();
			Options.MaxBlobSize = 1;
			Options.MaxInlineBlobSize = 1;

			Bundle<DirectoryNode> Bundle = new Bundle<DirectoryNode>(StorageClient, NamespaceId, Options, null);

			DirectoryNode Node1 = await Bundle.Root.AddDirectoryAsync("node1");
			DirectoryNode Node2 = await Node1.AddDirectoryAsync("node2");
			DirectoryNode Node3 = await Node2.AddDirectoryAsync("node3");
			DirectoryNode Node4 = await Node3.AddDirectoryAsync("node4");

			RefId RefId = new RefId("ref");
			await Bundle.WriteAsync(BucketId, RefId, false, DateTime.UtcNow);

			Assert.AreEqual(1, StorageClient.Refs.Count);
			Assert.AreEqual(4, StorageClient.Blobs.Count);

			Bundle<DirectoryNode> NewBundle = new Bundle<DirectoryNode>(StorageClient, NamespaceId, Options, null);
			await NewBundle.ReadAsync(BucketId, RefId);

			DirectoryNode? NewNode1 = await NewBundle.Root.FindDirectoryAsync("node1");
			Assert.IsNotNull(NewNode1);

			DirectoryNode? NewNode2 = await NewNode1!.FindDirectoryAsync("node2");
			Assert.IsNotNull(NewNode2);

			DirectoryNode? NewNode3 = await NewNode2!.FindDirectoryAsync("node3");
			Assert.IsNotNull(NewNode3);

			DirectoryNode? NewNode4 = await NewNode3!.FindDirectoryAsync("node4");
			Assert.IsNotNull(NewNode4);
		}

		[TestMethod]
		public async Task CompactTest()
		{
			DateTime Time = DateTime.UtcNow;

			BundleOptions Options = new BundleOptions();
			Options.MaxBlobSize = 1024 * 1024;
			Options.MaxInlineBlobSize = 1;

			Bundle<DirectoryNode> Bundle = new Bundle<DirectoryNode>(StorageClient, NamespaceId, Options, null);

			DirectoryNode Node1 = await Bundle.Root.AddDirectoryAsync("node1");
			DirectoryNode Node2 = await Node1.AddDirectoryAsync("node2");
			DirectoryNode Node3 = await Node2.AddDirectoryAsync("node3");
			DirectoryNode Node4 = await Bundle.Root.AddDirectoryAsync("node4"); // same contents as node 3

			RefId RefId1 = new RefId("ref1");
			await Bundle.WriteAsync(BucketId, RefId1, false, Time);

			Assert.AreEqual(1, StorageClient.Refs.Count);
			Assert.AreEqual(1, StorageClient.Blobs.Count);

			IRef Ref1 = StorageClient.Refs[(NamespaceId, BucketId, RefId1)];

			IoHash RootHash1 = Bundle.RootHash;
			BundleObject RootObject1 = CbSerializer.Deserialize<BundleObject>(Ref1.Value);
			Assert.AreEqual(1, RootObject1.Exports.Count);
			Assert.AreEqual(1, RootObject1.ImportObjects.Count);
			Assert.AreEqual(RootHash1, RootObject1.Exports[0].Hash);

			IoHash LeafHash1 = RootObject1.ImportObjects[0].Object.Hash;
			BundleObject LeafObject1 = CbSerializer.Deserialize<BundleObject>(StorageClient.Blobs[(NamespaceId, LeafHash1)]);
			Assert.AreEqual(3, LeafObject1.Exports.Count); // node4 == node3
			Assert.AreEqual(0, LeafObject1.ImportObjects.Count);

			// Remove one of the nodes from the root without compacting. the existing blob should be reused.
			Bundle.Root.Delete("node1");

			RefId RefId2 = new RefId("ref2");
			await Bundle.WriteAsync(BucketId, RefId2, false, Time);

			IRef Ref2 = StorageClient.Refs[(NamespaceId, BucketId, RefId2)];

			IoHash RootHash2 = Bundle.RootHash;
			BundleObject RootObject2 = CbSerializer.Deserialize<BundleObject>(Ref2.Value);
			Assert.AreEqual(1, RootObject2.Exports.Count);
			Assert.AreEqual(1, RootObject2.ImportObjects.Count);
			Assert.AreEqual(RootHash2, RootObject2.Exports[0].Hash);

			IoHash LeafHash2 = RootObject2.ImportObjects[0].Object.Hash;
			Assert.AreEqual(LeafHash1, LeafHash2);

			// Repack it and check that we make a new object
			RefId RefId3 = new RefId("ref3");
			await Bundle.WriteAsync(BucketId, RefId3, true, Time + TimeSpan.FromDays(7));

			IRef Ref3 = StorageClient.Refs[(NamespaceId, BucketId, RefId3)];

			IoHash RootHash3 = Bundle.RootHash;
			BundleObject RootObject3 = CbSerializer.Deserialize<BundleObject>(Ref3.Value);
			Assert.AreEqual(1, RootObject3.Exports.Count);
			Assert.AreEqual(1, RootObject3.ImportObjects.Count);
			Assert.AreEqual(RootHash3, RootObject3.Exports[0].Hash);

			IoHash LeafHash3 = RootObject3.ImportObjects[0].Object.Hash;
			Assert.AreNotEqual(LeafHash1, LeafHash3);

			BundleObject LeafObject3 = CbSerializer.Deserialize<BundleObject>(StorageClient.Blobs[(NamespaceId, LeafHash3)]);
			Assert.AreEqual(1, LeafObject3.Exports.Count);
			Assert.AreEqual(0, LeafObject3.ImportObjects.Count);
		}

		[TestMethod]
		public async Task FixedSizeChunkingTests()
		{
			ChunkOptions Options = new ChunkOptions();
			Options.LeafOptions = new TypedChunkOptions(64, 64, 64);
			Options.InteriorOptions = new TypedChunkOptions(IoHash.NumBytes * 4, IoHash.NumBytes * 4, IoHash.NumBytes * 4);

			await ChunkingTests(Options);
		}

		[TestMethod]
		public async Task VariableSizeChunkingTests()
		{
			ChunkOptions Options = new ChunkOptions();
			Options.LeafOptions = new TypedChunkOptions(32, 64, 96);
			Options.InteriorOptions = new TypedChunkOptions(IoHash.NumBytes * 1, IoHash.NumBytes * 4, IoHash.NumBytes * 12);

			await ChunkingTests(Options);
		}

		async Task ChunkingTests(ChunkOptions Options)
		{
			byte[] Data = new byte[4096];
			new Random(0).NextBytes(Data);

			for (int Idx = 0; Idx < Data.Length; Idx++)
			{
				Data[Idx] = (byte)Idx;
			}

			Bundle<ChunkNode> Bundle = new Bundle<ChunkNode>(StorageClient, NamespaceId, new BundleOptions(), null);

			const int NumIterations = 100;
			for (int Idx = 0; Idx < NumIterations; Idx++)
			{
				Bundle.Root.Append(Data, Options);
			}

			byte[] Result = await Bundle.Root.ToByteArrayAsync();
			Assert.AreEqual(NumIterations * Data.Length, Result.Length);

			for (int Idx = 0; Idx < NumIterations; Idx++)
			{
				ReadOnlyMemory<byte> SpanData = Result.AsMemory(Idx * Data.Length, Data.Length);
				Assert.IsTrue(SpanData.Span.SequenceEqual(Data));
			}

			await CheckSizes(Bundle.Root, Options, true);
		}

		async Task CheckSizes(ChunkNode Node, ChunkOptions Options, bool Rightmost)
		{
			if (Node.Depth == 0)
			{
				Assert.IsTrue(Rightmost || Node.Payload.Length >= Options.LeafOptions.MinSize);
				Assert.IsTrue(Node.Payload.Length <= Options.LeafOptions.MaxSize);
			}
			else
			{
				Assert.IsTrue(Rightmost || Node.Payload.Length >= Options.InteriorOptions.MinSize);
				Assert.IsTrue(Node.Payload.Length <= Options.InteriorOptions.MaxSize);

				int ChildCount = Node.GetChildCount();
				for (int Idx = 0; Idx < ChildCount; Idx++)
				{
					ChunkNode ChildNode = await Node.GetChildNodeAsync(Idx);
					await CheckSizes(ChildNode, Options, Idx == ChildCount - 1);
				}
			}
		}
	}
}
