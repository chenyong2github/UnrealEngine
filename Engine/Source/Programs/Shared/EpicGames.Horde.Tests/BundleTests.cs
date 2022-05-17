// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Bundles;
using EpicGames.Horde.Bundles.Nodes;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Impl;
using EpicGames.Serialization;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Horde.Tests
{
	[TestClass]
	public sealed class BundleTests : IDisposable
	{
		readonly MemoryCache _cache;
		readonly MemoryStorageClient _storageClient = new MemoryStorageClient();
		readonly NamespaceId _namespaceId = new NamespaceId("namespace");
		readonly BucketId _bucketId = new BucketId("bucket");

		public BundleTests()
		{
			_cache = new MemoryCache(new MemoryCacheOptions { SizeLimit = 50 * 1024 * 1024 });
		}

		public void Dispose()
		{
			_cache.Dispose();
		}

		[TestMethod]
		public void BuzHashTests()
		{
			byte[] data = new byte[4096];
			new Random(0).NextBytes(data);

			const int WindowSize = 128;

			uint rollingHash = 0;
			for (int maxIdx = 0; maxIdx < data.Length + WindowSize; maxIdx++)
			{
				int minIdx = maxIdx - WindowSize;

				if (maxIdx < data.Length)
				{
					rollingHash = BuzHash.Add(rollingHash, data[maxIdx]);
				}

				int length = Math.Min(maxIdx + 1, data.Length) - Math.Max(minIdx, 0);
				uint cleanHash = BuzHash.Add(0, data.AsSpan(Math.Max(minIdx, 0), length));
				Assert.AreEqual(rollingHash, cleanHash);

				if (minIdx >= 0)
				{
					rollingHash = BuzHash.Sub(rollingHash, data[minIdx], length);
				}
			}
		}

		[TestMethod]
		public void BasicChunkingTests()
		{
			ChunkingOptions options = new ChunkingOptions();
			options.LeafOptions = new ChunkingOptionsForNodeType(8, 8, 8);

			FileNode node = new FileNode();
			node.Append(new byte[7], options);
			Assert.AreEqual(0, node.Depth);
			Assert.AreEqual(7, node.Payload.Length);

			node = new FileNode();
			node.Append(new byte[8], options);
			Assert.AreEqual(0, node.Depth);
			Assert.AreEqual(8, node.Payload.Length);

			node = new FileNode();
			node.Append(new byte[9], options);
			Assert.AreEqual(1, node.Depth);
			Assert.AreEqual(2, node.Children.Count);

			FileNode? childNode1 = node.Children[0].Node;
			Assert.IsNotNull(childNode1);
			Assert.AreEqual(0, childNode1!.Depth);
			Assert.AreEqual(8, childNode1!.Payload.Length);

			FileNode? childNode2 = node.Children[1].Node;
			Assert.IsNotNull(childNode2);
			Assert.AreEqual(0, childNode2!.Depth);
			Assert.AreEqual(1, childNode2!.Payload.Length);
		}

		[TestMethod]
		public void SerializationTests()
		{
			List<BundleImport> imports = new List<BundleImport>();
			imports.Add(new BundleImport(IoHash.Compute(Encoding.UTF8.GetBytes("import1")), 0, 123));
			imports.Add(new BundleImport(IoHash.Compute(Encoding.UTF8.GetBytes("import2")), 1, 456));

			BundleObject oldObject = new BundleObject();
			oldObject.ImportObjects.Add(new BundleImportObject(new CbObjectAttachment(IoHash.Compute(new byte[] { 1, 2, 3 })), 12345, imports));
			oldObject.Exports.Add(new BundleExport(IoHash.Compute(Encoding.UTF8.GetBytes("export1")), 2, new BundleCompressionPacket(0) { EncodedLength = 20, DecodedLength = 40 }, 0, 40, new int[] { 1, 2 }));
			oldObject.Exports.Add(new BundleExport(IoHash.Compute(Encoding.UTF8.GetBytes("export2")), 3, new BundleCompressionPacket(20) { EncodedLength = 10, DecodedLength = 20 }, 0, 20, new int[] { -1 }));

			CbObject serializedObject = CbSerializer.Serialize(oldObject);
			ReadOnlyMemory<byte> serializedData = serializedObject.GetView();

			BundleObject newObject = CbSerializer.Deserialize<BundleObject>(serializedData);

			Assert.AreEqual(oldObject.ImportObjects.Count, newObject.ImportObjects.Count);
			for (int idx = 0; idx < oldObject.ImportObjects.Count; idx++)
			{
				BundleImportObject oldObjectImport = oldObject.ImportObjects[idx];
				BundleImportObject newObjectImport = newObject.ImportObjects[idx];

				Assert.AreEqual(oldObjectImport.Object, newObjectImport.Object);
				Assert.AreEqual(oldObjectImport.TotalCost, newObjectImport.TotalCost);
				Assert.AreEqual(oldObjectImport.Imports.Count, newObjectImport.Imports.Count);

				for (int importIdx = 0; importIdx < oldObjectImport.Imports.Count; importIdx++)
				{
					BundleImport oldImport = oldObjectImport.Imports[importIdx];
					BundleImport newImport = newObjectImport.Imports[importIdx];

					Assert.AreEqual(oldImport.Hash, newImport.Hash);
					Assert.AreEqual(oldImport.Rank, newImport.Rank);
					Assert.AreEqual(oldImport.Length, newImport.Length);
				}
			}

			Assert.AreEqual(oldObject.Exports.Count, newObject.Exports.Count);
			for (int idx = 0; idx < oldObject.Exports.Count; idx++)
			{
				BundleExport oldExport = oldObject.Exports[idx];
				BundleExport newExport = newObject.Exports[idx];

				Assert.AreEqual(oldExport.Hash, newExport.Hash);
				Assert.AreEqual(oldExport.Rank, newExport.Rank);
				Assert.AreEqual(oldExport.Length, newExport.Length);
				Assert.AreEqual(oldExport.Packet.Offset, newExport.Packet.Offset);
				Assert.AreEqual(oldExport.Packet.EncodedLength, newExport.Packet.EncodedLength);
				Assert.AreEqual(oldExport.Packet.DecodedLength, newExport.Packet.DecodedLength);
				Assert.AreEqual(oldExport.Offset, newExport.Offset);
				Assert.AreEqual(oldExport.Length, newExport.Length);
				Assert.IsTrue(oldExport.References.SequenceEqual(newExport.References));
			}
		}

		[TestMethod]
		public async Task BasicTestDirectory()
		{
			using Bundle<DirectoryNode> newBundle = Bundle.Create<DirectoryNode>(_storageClient, _namespaceId, new DirectoryNode(), new BundleOptions(), _cache);
			DirectoryNode node = newBundle.Root.AddDirectory("hello");
			DirectoryNode node2 = node.AddDirectory("world");

			RefId refId = new RefId("testref");
			await newBundle.WriteAsync(_bucketId, refId, CbObject.Empty, false);

			// Should be stored inline
			Assert.AreEqual(1, _storageClient.Refs.Count);
			Assert.AreEqual(0, _storageClient.Blobs.Count);

			// Check the ref
			BundleRoot root = await _storageClient.GetRefAsync<BundleRoot>(_namespaceId, _bucketId, refId);
			BundleObject rootObject = root.Object;
			Assert.AreEqual(3, rootObject.Exports.Count);
			Assert.AreEqual(0, rootObject.Exports[0].Rank);
			Assert.AreEqual(1, rootObject.Exports[1].Rank);
			Assert.AreEqual(2, rootObject.Exports[2].Rank);

			// Create a new bundle and read it back in again
			using Bundle<DirectoryNode> newBundle2 = await Bundle.ReadAsync<DirectoryNode>(_storageClient, _namespaceId, _bucketId, refId, new BundleOptions(), _cache);

			Assert.AreEqual(0, newBundle2.Root.Files.Count);
			Assert.AreEqual(1, newBundle2.Root.Directories.Count);
			DirectoryNode? outputNode = await newBundle.FindDirectoryAsync(newBundle2.Root, "hello");
			Assert.IsNotNull(outputNode);

			Assert.AreEqual(0, outputNode!.Files.Count);
			Assert.AreEqual(1, outputNode!.Directories.Count);
			DirectoryNode? outputNode2 = await newBundle.FindDirectoryAsync(outputNode, "world");
			Assert.IsNotNull(outputNode2);

			Assert.AreEqual(0, outputNode2!.Files.Count);
			Assert.AreEqual(0, outputNode2!.Directories.Count);
		}

		[TestMethod]
		public async Task DedupTests()
		{
			BundleOptions options = new BundleOptions();
			options.MaxBlobSize = 1;
			options.MaxInlineBlobSize = 1;

			using Bundle<DirectoryNode> newBundle = Bundle.Create<DirectoryNode>(_storageClient, _namespaceId, new DirectoryNode(), options, _cache);

			newBundle.Root.AddDirectory("node1");
			newBundle.Root.AddDirectory("node2");
			newBundle.Root.AddDirectory("node3");

			Assert.AreEqual(0, _storageClient.Refs.Count);
			Assert.AreEqual(0, _storageClient.Blobs.Count);

			RefId refId = new RefId("ref");
			await newBundle.WriteAsync(_bucketId, refId, CbObject.Empty, false);

			Assert.AreEqual(1, _storageClient.Refs.Count);
			Assert.AreEqual(1, _storageClient.Blobs.Count);
		}

		[TestMethod]
		public async Task ReloadTests()
		{
			BundleOptions options = new BundleOptions();
			options.MaxBlobSize = 1;
			options.MaxInlineBlobSize = 1;

			using Bundle<DirectoryNode> initialBundle = Bundle.Create<DirectoryNode>(_storageClient, _namespaceId, new DirectoryNode(), options, _cache);

			DirectoryNode node1 = initialBundle.Root.AddDirectory("node1");
			DirectoryNode node2 = node1.AddDirectory("node2");
			DirectoryNode node3 = node2.AddDirectory("node3");
			DirectoryNode node4 = node3.AddDirectory("node4");

			RefId refId = new RefId("ref");
			await initialBundle.WriteAsync(_bucketId, refId, CbObject.Empty, false);

			Assert.AreEqual(1, _storageClient.Refs.Count);
			Assert.AreEqual(4, _storageClient.Blobs.Count);

			using Bundle<DirectoryNode> newBundle = await Bundle.ReadAsync<DirectoryNode>(_storageClient, _namespaceId, _bucketId, refId, options, _cache);

			DirectoryNode? newNode1 = await newBundle.FindDirectoryAsync(newBundle.Root, "node1");
			Assert.IsNotNull(newNode1);

			DirectoryNode? newNode2 = await newBundle.FindDirectoryAsync(newNode1!, "node2");
			Assert.IsNotNull(newNode2);

			DirectoryNode? newNode3 = await newBundle.FindDirectoryAsync(newNode2!, "node3");
			Assert.IsNotNull(newNode3);

			DirectoryNode? newNode4 = await newBundle.FindDirectoryAsync(newNode3!, "node4");
			Assert.IsNotNull(newNode4);
		}

		[TestMethod]
		public async Task CompactTest()
		{
			BundleOptions options = new BundleOptions();
			options.MaxBlobSize = 1024 * 1024;
			options.MaxInlineBlobSize = 1;

			using Bundle<DirectoryNode> newBundle = Bundle.Create<DirectoryNode>(_storageClient, _namespaceId, new DirectoryNode(), options, _cache);

			DirectoryNode node1 = newBundle.Root.AddDirectory("node1");
			DirectoryNode node2 = node1.AddDirectory("node2");
			DirectoryNode node3 = node2.AddDirectory("node3");
			DirectoryNode node4 = newBundle.Root.AddDirectory("node4"); // same contents as node 3

			RefId refId1 = new RefId("ref1");
			await newBundle.WriteAsync(_bucketId, refId1, CbObject.Empty, false);

			Assert.AreEqual(1, _storageClient.Refs.Count);
			Assert.AreEqual(1, _storageClient.Blobs.Count);

			IRef ref1 = _storageClient.Refs[(_namespaceId, _bucketId, refId1)];

			BundleRoot root1 = CbSerializer.Deserialize<BundleRoot>(ref1.Value);
			BundleObject rootObject1 = root1.Object;
			Assert.AreEqual(1, rootObject1.Exports.Count);
			Assert.AreEqual(1, rootObject1.ImportObjects.Count);

			IoHash leafHash1 = rootObject1.ImportObjects[0].Object.Hash;
			BundleObject leafObject1 = CbSerializer.Deserialize<BundleObject>(_storageClient.Blobs[(_namespaceId, leafHash1)]);
			Assert.AreEqual(3, leafObject1.Exports.Count); // node1 + node2 + node3 (== node4)
			Assert.AreEqual(0, leafObject1.ImportObjects.Count);

			// Remove one of the nodes from the root without compacting. the existing blob should be reused.
			newBundle.Root.DeleteDirectory("node1");

			RefId refId2 = new RefId("ref2");
			await newBundle.WriteAsync(_bucketId, refId2, CbObject.Empty, false);

			IRef ref2 = _storageClient.Refs[(_namespaceId, _bucketId, refId2)];

			BundleRoot root2 = CbSerializer.Deserialize<BundleRoot>(ref2.Value);
			BundleObject rootObject2 = root2.Object;
			Assert.AreEqual(1, rootObject2.Exports.Count);
			Assert.AreEqual(1, rootObject2.ImportObjects.Count);

			IoHash leafHash2 = rootObject2.ImportObjects[0].Object.Hash;
			Assert.AreEqual(leafHash1, leafHash2);
			Assert.AreEqual(3, leafObject1.Exports.Count); // unused: node1 + node2 + node3, used: node4 (== node3)
			Assert.AreEqual(0, leafObject1.ImportObjects.Count);

			// Repack it and check that we make a new object
			RefId refId3 = new RefId("ref3");
			await newBundle.WriteAsync(_bucketId, refId3, CbObject.Empty, true);

			IRef ref3 = _storageClient.Refs[(_namespaceId, _bucketId, refId3)];

			BundleRoot root3 = CbSerializer.Deserialize<BundleRoot>(ref3.Value);
			BundleObject rootObject3 = root3.Object;
			Assert.AreEqual(1, rootObject3.Exports.Count);
			Assert.AreEqual(1, rootObject3.ImportObjects.Count);

			IoHash leafHash3 = rootObject3.ImportObjects[0].Object.Hash;
			Assert.AreNotEqual(leafHash1, leafHash3);

			BundleObject leafObject3 = CbSerializer.Deserialize<BundleObject>(_storageClient.Blobs[(_namespaceId, leafHash3)]);
			Assert.AreEqual(1, leafObject3.Exports.Count);
			Assert.AreEqual(0, leafObject3.ImportObjects.Count);
		}

		[TestMethod]
		public async Task CoreAppendTest()
		{
			byte[] data = new byte[4096];
			new Random(0).NextBytes(data);

			ChunkingOptions options = new ChunkingOptions();
			options.LeafOptions = new ChunkingOptionsForNodeType(16, 64, 256);

			FileNode node = new FileNode();
			for (int idx = 0; idx < data.Length; idx++)
			{
				node.Append(data.AsMemory(idx, 1), options);

				byte[] outputData = await node.ToByteArrayAsync(null!);
				Assert.IsTrue(data.AsMemory(0, idx + 1).Span.SequenceEqual(outputData.AsSpan(0, idx + 1)));
			}
		}

		[TestMethod]
		public async Task FixedSizeChunkingTests()
		{
			ChunkingOptions options = new ChunkingOptions();
			options.LeafOptions = new ChunkingOptionsForNodeType(64, 64, 64);
			options.InteriorOptions = new ChunkingOptionsForNodeType(IoHash.NumBytes * 4, IoHash.NumBytes * 4, IoHash.NumBytes * 4);

			await ChunkingTests(options);
		}

		[TestMethod]
		public async Task VariableSizeChunkingTests()
		{
			ChunkingOptions options = new ChunkingOptions();
			options.LeafOptions = new ChunkingOptionsForNodeType(32, 64, 96);
			options.InteriorOptions = new ChunkingOptionsForNodeType(IoHash.NumBytes * 1, IoHash.NumBytes * 4, IoHash.NumBytes * 12);

			await ChunkingTests(options);
		}

		async Task ChunkingTests(ChunkingOptions options)
		{
			byte[] data = new byte[4096];
			new Random(0).NextBytes(data);

			for (int idx = 0; idx < data.Length; idx++)
			{
				data[idx] = (byte)idx;
			}

			using Bundle<FileNode> newBundle = Bundle.Create<FileNode>(_storageClient, _namespaceId, new FileNode(), new BundleOptions(), _cache);
			FileNode root = newBundle.Root;

			const int NumIterations = 100;
			for (int idx = 0; idx < NumIterations; idx++)
			{
				root.Append(data, options);
			}

			byte[] result = await root.ToByteArrayAsync(null!);
			Assert.AreEqual(NumIterations * data.Length, result.Length);

			for (int idx = 0; idx < NumIterations; idx++)
			{
				ReadOnlyMemory<byte> spanData = result.AsMemory(idx * data.Length, data.Length);
				Assert.IsTrue(spanData.Span.SequenceEqual(data));
			}

			await CheckSizes(newBundle, root, options, true);
		}

		async Task CheckSizes(Bundle bundle, FileNode node, ChunkingOptions options, bool rightmost)
		{
			if (node.Depth == 0)
			{
				Assert.IsTrue(rightmost || node.Payload.Length >= options.LeafOptions.MinSize);
				Assert.IsTrue(node.Payload.Length <= options.LeafOptions.MaxSize);
			}
			else
			{
				Assert.IsTrue(rightmost || node.Payload.Length >= options.InteriorOptions.MinSize);
				Assert.IsTrue(node.Payload.Length <= options.InteriorOptions.MaxSize);

				int childCount = node.Children.Count;
				for (int idx = 0; idx < childCount; idx++)
				{
					FileNode childNode = await bundle.GetAsync(node.Children[idx]);
					await CheckSizes(bundle, childNode, options, idx == childCount - 1);
				}
			}
		}

		[TestMethod]
		public async Task SpillTestAsync()
		{
			BundleOptions options = new BundleOptions();
			options.MaxBlobSize = 1;

			using Bundle<DirectoryNode> newBundle = Bundle.Create<DirectoryNode>(_storageClient, _namespaceId, new DirectoryNode(), options, _cache);
			DirectoryNode root = newBundle.Root;

			long totalLength = 0;
			for (int idxA = 0; idxA < 10; idxA++)
			{
				DirectoryNode nodeA = root.AddDirectory($"{idxA}");
				for (int idxB = 0; idxB < 10; idxB++)
				{
					DirectoryNode nodeB = nodeA.AddDirectory($"{idxB}");
					for (int idxC = 0; idxC < 10; idxC++)
					{
						DirectoryNode nodeC = nodeB.AddDirectory($"{idxC}");
						for (int idxD = 0; idxD < 10; idxD++)
						{
							FileNode file = nodeC.AddFile($"{idxD}", 0);
							byte[] data = Encoding.UTF8.GetBytes($"This is file {idxA}/{idxB}/{idxC}/{idxD}");
							totalLength += data.Length;
							file.Append(data, new ChunkingOptions());
						}
					}

					int oldWorkingSetSize = GetWorkingSetSize(newBundle.Root);
					await newBundle.TrimAsync(20);
					int newWorkingSetSize = GetWorkingSetSize(newBundle.Root);
					Assert.IsTrue(newWorkingSetSize <= oldWorkingSetSize);
					Assert.IsTrue(newWorkingSetSize <= 20);
				}
			}

			Assert.IsTrue(_storageClient.Blobs.Count > 0);
			Assert.IsTrue(_storageClient.Refs.Count == 0);

			RefId refId = new RefId("ref");
			await newBundle.WriteAsync(_bucketId, refId, CbObject.Empty, true);

			Assert.AreEqual(totalLength, root.Length);

			Assert.IsTrue(_storageClient.Blobs.Count > 0);
			Assert.IsTrue(_storageClient.Refs.Count == 1);
		}

		int GetWorkingSetSize(BundleNode node)
		{
			int size = 0;
			foreach (BundleNodeRef nodeRef in node.GetReferences())
			{
				if (nodeRef.Node != null)
				{
					size += 1 + GetWorkingSetSize(nodeRef.Node);
				}
			}
			return size;
		}
	}
}
