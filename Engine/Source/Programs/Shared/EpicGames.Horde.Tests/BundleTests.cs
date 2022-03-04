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
using System.Linq;
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
		public void BuzHashTests()
		{
			byte[] Data = new byte[4096];
			new Random(0).NextBytes(Data);

			const int WindowSize = 128;

			uint RollingHash = 0;
			for (int MaxIdx = 0; MaxIdx < Data.Length + WindowSize; MaxIdx++)
			{
				int MinIdx = MaxIdx - WindowSize;

				if (MaxIdx < Data.Length)
				{
					RollingHash = BuzHash.Add(RollingHash, Data[MaxIdx]);
				}

				int Length = Math.Min(MaxIdx + 1, Data.Length) - Math.Max(MinIdx, 0);
				uint CleanHash = BuzHash.Add(0, Data.AsSpan(Math.Max(MinIdx, 0), Length));
				Assert.AreEqual(RollingHash, CleanHash);

				if (MinIdx >= 0)
				{
					RollingHash = BuzHash.Sub(RollingHash, Data[MinIdx], Length);
				}
			}
		}

		[TestMethod]
		public void BasicChunkingTests()
		{
			ChunkingOptions Options = new ChunkingOptions();
			Options.LeafOptions = new ChunkingOptionsForNodeType(8, 8, 8);

			FileNode Node = new FileNode();
			Node.Append(new byte[7], Options);
			Assert.AreEqual(0, Node.Depth);
			Assert.AreEqual(7, Node.Payload.Length);

			Node = new FileNode();
			Node.Append(new byte[8], Options);
			Assert.AreEqual(0, Node.Depth);
			Assert.AreEqual(8, Node.Payload.Length);

			Node = new FileNode();
			Node.Append(new byte[9], Options);
			Assert.AreEqual(1, Node.Depth);
			Assert.AreEqual(2, Node.Children.Count);

			FileNode? ChildNode1 = Node.Children[0].Node;
			Assert.IsNotNull(ChildNode1);
			Assert.AreEqual(0, ChildNode1!.Depth);
			Assert.AreEqual(8, ChildNode1!.Payload.Length);

			FileNode? ChildNode2 = Node.Children[1].Node;
			Assert.IsNotNull(ChildNode2);
			Assert.AreEqual(0, ChildNode2!.Depth);
			Assert.AreEqual(1, ChildNode2!.Payload.Length);
		}

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
			Bundle<DirectoryNode> NewBundle = new Bundle<DirectoryNode>(StorageClient, NamespaceId, new DirectoryNode(), new BundleOptions(), null);
			DirectoryNode Node = NewBundle.Root.AddDirectory("hello");
			DirectoryNode Node2 = Node.AddDirectory("world");

			RefId RefId = new RefId("testref");
			await NewBundle.WriteAsync(BucketId, RefId, false, DateTime.MinValue);

			// Should be stored inline
			Assert.AreEqual(1, StorageClient.Refs.Count);
			Assert.AreEqual(0, StorageClient.Blobs.Count);

			// Check the ref
			BundleObject RootObject = await StorageClient.GetRefAsync<BundleObject>(NamespaceId, BucketId, RefId);
			Assert.AreEqual(3, RootObject.Exports.Count);
			Assert.AreEqual(0, RootObject.Exports[0].Rank);
			Assert.AreEqual(1, RootObject.Exports[1].Rank);
			Assert.AreEqual(2, RootObject.Exports[2].Rank);

			// Create a new bundle and read it back in again
			NewBundle = await Bundle.ReadAsync<DirectoryNode>(StorageClient, NamespaceId, BucketId, RefId, new BundleOptions(), null);

			Assert.AreEqual(0, NewBundle.Root.Files.Count);
			Assert.AreEqual(1, NewBundle.Root.Directories.Count);
			DirectoryNode? OutputNode = await NewBundle.Root.FindDirectoryAsync("hello");
			Assert.IsNotNull(OutputNode);

			Assert.AreEqual(0, OutputNode!.Files.Count);
			Assert.AreEqual(1, OutputNode!.Directories.Count);
			DirectoryNode? OutputNode2 = await OutputNode!.FindDirectoryAsync("world");
			Assert.IsNotNull(OutputNode2);

			Assert.AreEqual(0, OutputNode2!.Files.Count);
			Assert.AreEqual(0, OutputNode2!.Directories.Count);
		}

		[TestMethod]
		public async Task DedupTests()
		{
			BundleOptions Options = new BundleOptions();
			Options.MaxBlobSize = 1;
			Options.MaxInlineBlobSize = 1;

			Bundle<DirectoryNode> Bundle = new Bundle<DirectoryNode>(StorageClient, NamespaceId, new DirectoryNode(), Options, null);

			Bundle.Root.AddDirectory("node1");
			Bundle.Root.AddDirectory("node2");
			Bundle.Root.AddDirectory("node3");

			Assert.AreEqual(0, StorageClient.Refs.Count);
			Assert.AreEqual(0, StorageClient.Blobs.Count);

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

			Bundle<DirectoryNode> InitialBundle = new Bundle<DirectoryNode>(StorageClient, NamespaceId, new DirectoryNode(), Options, null);

			DirectoryNode Node1 = InitialBundle.Root.AddDirectory("node1");
			DirectoryNode Node2 = Node1.AddDirectory("node2");
			DirectoryNode Node3 = Node2.AddDirectory("node3");
			DirectoryNode Node4 = Node3.AddDirectory("node4");

			RefId RefId = new RefId("ref");
			await InitialBundle.WriteAsync(BucketId, RefId, false, DateTime.UtcNow);

			Assert.AreEqual(1, StorageClient.Refs.Count);
			Assert.AreEqual(4, StorageClient.Blobs.Count);

			Bundle<DirectoryNode> NewBundle = await Bundle.ReadAsync<DirectoryNode>(StorageClient, NamespaceId, BucketId, RefId, Options, null);

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

			Bundle<DirectoryNode> Bundle = new Bundle<DirectoryNode>(StorageClient, NamespaceId, new DirectoryNode(), Options, null);

			DirectoryNode Node1 = Bundle.Root.AddDirectory("node1");
			DirectoryNode Node2 = Node1.AddDirectory("node2");
			DirectoryNode Node3 = Node2.AddDirectory("node3");
			DirectoryNode Node4 = Bundle.Root.AddDirectory("node4"); // same contents as node 3

			RefId RefId1 = new RefId("ref1");
			await Bundle.WriteAsync(BucketId, RefId1, false, Time);

			Assert.AreEqual(1, StorageClient.Refs.Count);
			Assert.AreEqual(1, StorageClient.Blobs.Count);

			IRef Ref1 = StorageClient.Refs[(NamespaceId, BucketId, RefId1)];

			BundleObject RootObject1 = CbSerializer.Deserialize<BundleObject>(Ref1.Value);
			Assert.AreEqual(1, RootObject1.Exports.Count);
			Assert.AreEqual(1, RootObject1.ImportObjects.Count);

			IoHash LeafHash1 = RootObject1.ImportObjects[0].Object.Hash;
			BundleObject LeafObject1 = CbSerializer.Deserialize<BundleObject>(StorageClient.Blobs[(NamespaceId, LeafHash1)]);
			Assert.AreEqual(3, LeafObject1.Exports.Count); // node1 + node2 + node3 (== node4)
			Assert.AreEqual(0, LeafObject1.ImportObjects.Count);

			// Remove one of the nodes from the root without compacting. the existing blob should be reused.
			Bundle.Root.DeleteDirectory("node1");

			RefId RefId2 = new RefId("ref2");
			await Bundle.WriteAsync(BucketId, RefId2, false, Time);

			IRef Ref2 = StorageClient.Refs[(NamespaceId, BucketId, RefId2)];

			BundleObject RootObject2 = CbSerializer.Deserialize<BundleObject>(Ref2.Value);
			Assert.AreEqual(1, RootObject2.Exports.Count);
			Assert.AreEqual(1, RootObject2.ImportObjects.Count);

			IoHash LeafHash2 = RootObject2.ImportObjects[0].Object.Hash;
			Assert.AreEqual(LeafHash1, LeafHash2);
			Assert.AreEqual(3, LeafObject1.Exports.Count); // unused: node1 + node2 + node3, used: node4 (== node3)
			Assert.AreEqual(0, LeafObject1.ImportObjects.Count);

			// Repack it and check that we make a new object
			RefId RefId3 = new RefId("ref3");
			await Bundle.WriteAsync(BucketId, RefId3, true, Time + TimeSpan.FromDays(7));

			IRef Ref3 = StorageClient.Refs[(NamespaceId, BucketId, RefId3)];

			BundleObject RootObject3 = CbSerializer.Deserialize<BundleObject>(Ref3.Value);
			Assert.AreEqual(1, RootObject3.Exports.Count);
			Assert.AreEqual(1, RootObject3.ImportObjects.Count);

			IoHash LeafHash3 = RootObject3.ImportObjects[0].Object.Hash;
			Assert.AreNotEqual(LeafHash1, LeafHash3);

			BundleObject LeafObject3 = CbSerializer.Deserialize<BundleObject>(StorageClient.Blobs[(NamespaceId, LeafHash3)]);
			Assert.AreEqual(1, LeafObject3.Exports.Count);
			Assert.AreEqual(0, LeafObject3.ImportObjects.Count);
		}

		[TestMethod]
		public async Task CoreAppendTest()
		{
			byte[] Data = new byte[4096];
			new Random(0).NextBytes(Data);

			ChunkingOptions Options = new ChunkingOptions();
			Options.LeafOptions = new ChunkingOptionsForNodeType(16, 64, 256);

			FileNode Node = new FileNode();
			for (int Idx = 0; Idx < Data.Length; Idx++)
			{
				Node.Append(Data.AsMemory(Idx, 1), Options);

				byte[] OutputData = await Node.ToByteArrayAsync();
				Assert.IsTrue(Data.AsMemory(0, Idx + 1).Span.SequenceEqual(OutputData.AsSpan(0, Idx + 1)));
			}
		}

		[TestMethod]
		public async Task FixedSizeChunkingTests()
		{
			ChunkingOptions Options = new ChunkingOptions();
			Options.LeafOptions = new ChunkingOptionsForNodeType(64, 64, 64);
			Options.InteriorOptions = new ChunkingOptionsForNodeType(IoHash.NumBytes * 4, IoHash.NumBytes * 4, IoHash.NumBytes * 4);

			await ChunkingTests(Options);
		}

		[TestMethod]
		public async Task VariableSizeChunkingTests()
		{
			ChunkingOptions Options = new ChunkingOptions();
			Options.LeafOptions = new ChunkingOptionsForNodeType(32, 64, 96);
			Options.InteriorOptions = new ChunkingOptionsForNodeType(IoHash.NumBytes * 1, IoHash.NumBytes * 4, IoHash.NumBytes * 12);

			await ChunkingTests(Options);
		}

		async Task ChunkingTests(ChunkingOptions Options)
		{
			byte[] Data = new byte[4096];
			new Random(0).NextBytes(Data);

			for (int Idx = 0; Idx < Data.Length; Idx++)
			{
				Data[Idx] = (byte)Idx;
			}

			Bundle<FileNode> Bundle = new Bundle<FileNode>(StorageClient, NamespaceId, new FileNode(), new BundleOptions(), null);
			FileNode Root = Bundle.Root;

			const int NumIterations = 100;
			for (int Idx = 0; Idx < NumIterations; Idx++)
			{
				Root.Append(Data, Options);
			}

			byte[] Result = await Root.ToByteArrayAsync();
			Assert.AreEqual(NumIterations * Data.Length, Result.Length);

			for (int Idx = 0; Idx < NumIterations; Idx++)
			{
				ReadOnlyMemory<byte> SpanData = Result.AsMemory(Idx * Data.Length, Data.Length);
				Assert.IsTrue(SpanData.Span.SequenceEqual(Data));
			}

			await CheckSizes(Root, Options, true);
		}

		async Task CheckSizes(FileNode Node, ChunkingOptions Options, bool Rightmost)
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

				int ChildCount = Node.Children.Count;
				for (int Idx = 0; Idx < ChildCount; Idx++)
				{
					FileNode ChildNode = await Node.Children[Idx].GetAsync();
					await CheckSizes(ChildNode, Options, Idx == ChildCount - 1);
				}
			}
		}

		[TestMethod]
		public async Task SpillTestAsync()
		{
			BundleOptions Options = new BundleOptions();
			Options.MaxBlobSize = 1;

			Bundle<DirectoryNode> Bundle = new Bundle<DirectoryNode>(StorageClient, NamespaceId, new DirectoryNode(), Options, null);
			DirectoryNode Root = Bundle.Root;

			long TotalLength = 0;
			for (int IdxA = 0; IdxA < 10; IdxA++)
			{
				DirectoryNode NodeA = Root.AddDirectory($"{IdxA}");
				for (int IdxB = 0; IdxB < 10; IdxB++)
				{
					DirectoryNode NodeB = NodeA.AddDirectory($"{IdxB}");
					for (int IdxC = 0; IdxC < 10; IdxC++)
					{
						DirectoryNode NodeC = NodeB.AddDirectory($"{IdxC}");
						for (int IdxD = 0; IdxD < 10; IdxD++)
						{
							FileNode File = NodeC.CreateFile($"{IdxD}", 0);
							byte[] Data = Encoding.UTF8.GetBytes($"This is file {IdxA}/{IdxB}/{IdxC}/{IdxD}");
							TotalLength += Data.Length;
							File.Append(Data, new ChunkingOptions());
						}
					}

					int OldWorkingSetSize = GetWorkingSetSize(Bundle.Root);
					await Bundle.TrimAsync(20);
					int NewWorkingSetSize = GetWorkingSetSize(Bundle.Root);
					Assert.IsTrue(NewWorkingSetSize <= OldWorkingSetSize);
					Assert.IsTrue(NewWorkingSetSize <= 20);
				}
			}

			Assert.IsTrue(StorageClient.Blobs.Count > 0);
			Assert.IsTrue(StorageClient.Refs.Count == 0);

			RefId RefId = new RefId("ref");
			await Bundle.WriteAsync(BucketId, RefId, true, DateTime.UtcNow);

			Assert.AreEqual(TotalLength, Root.Length);

			Assert.IsTrue(StorageClient.Blobs.Count > 0);
			Assert.IsTrue(StorageClient.Refs.Count == 1);
		}

		int GetWorkingSetSize(BundleNode Node)
		{
			int Size = 0;
			foreach (BundleNodeRef NodeRef in Node.GetReferences())
			{
				if (NodeRef.Node != null)
				{
					Size += 1 + GetWorkingSetSize(NodeRef.Node);
				}
			}
			return Size;
		}
	}
}
