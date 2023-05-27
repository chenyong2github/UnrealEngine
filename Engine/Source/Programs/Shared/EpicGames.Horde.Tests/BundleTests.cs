// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Backends;
using EpicGames.Horde.Storage.Nodes;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection.Metadata;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Tests
{
	[TestClass]
	public sealed class BundleTests : IDisposable
	{
		readonly IMemoryCache _cache;
		readonly MemoryStorageClient _storage;

		public BundleTests()
		{
			_cache = new MemoryCache(new MemoryCacheOptions());
			_storage = new MemoryStorageClient();
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
		public async Task BasicChunkingTests()
		{
			RefName refName = new RefName("test");
			TreeReader reader = new TreeReader(_storage, null, NullLogger.Instance);
			using TreeWriter writer = new TreeWriter(_storage, new TreeOptions(), refName.Text);

			ChunkingOptions options = new ChunkingOptions();
			options.LeafOptions = new ChunkingOptionsForNodeType(8, 8, 8);

			ChunkedDataWriter fileNodeWriter = new ChunkedDataWriter(writer, options);

			NodeHandle handle;
			ChunkedDataNode node;
			byte[] data = CreateBuffer(1024);

			handle = await fileNodeWriter.CreateAsync(data.AsMemory(0, 7), CancellationToken.None);
			node = await reader.ReadNodeAsync<ChunkedDataNode>(handle.Locator);
			Assert.IsTrue(node is LeafChunkedDataNode);
			Assert.AreEqual(7, ((LeafChunkedDataNode)node).Data.Length);
			await TestBufferlessReadsAsync(reader, handle.Locator, data.AsMemory(0, 7));

			handle = await fileNodeWriter.CreateAsync(data.AsMemory(0, 8), CancellationToken.None);
			node = await reader.ReadNodeAsync<ChunkedDataNode>(handle.Locator);
			Assert.IsTrue(node is LeafChunkedDataNode);
			Assert.AreEqual(8, ((LeafChunkedDataNode)node).Data.Length);
			await TestBufferlessReadsAsync(reader, handle.Locator, data.AsMemory(0, 8));

			handle = await fileNodeWriter.CreateAsync(data.AsMemory(0, 9), CancellationToken.None);
			node = await reader.ReadNodeAsync<ChunkedDataNode>(handle.Locator);
			Assert.IsTrue(node is InteriorChunkedDataNode);
			Assert.AreEqual(2, ((InteriorChunkedDataNode)node).Children.Count);
			await TestBufferlessReadsAsync(reader, handle.Locator, data.AsMemory(0, 9));

			ChunkedDataNode? childNode1 = await ((InteriorChunkedDataNode)node).Children[0].ExpandAsync(reader);
			Assert.IsNotNull(childNode1);
			Assert.IsTrue(childNode1 is LeafChunkedDataNode);
			Assert.AreEqual(8, ((LeafChunkedDataNode)childNode1!).Data.Length);

			ChunkedDataNode? childNode2 = await ((InteriorChunkedDataNode)node).Children[1].ExpandAsync(reader);
			Assert.IsNotNull(childNode2);
			Assert.IsTrue(childNode2 is LeafChunkedDataNode);
			Assert.AreEqual(1, ((LeafChunkedDataNode)childNode2!).Data.Length);

			handle = await fileNodeWriter.CreateAsync(data, CancellationToken.None);
			node = await reader.ReadNodeAsync<ChunkedDataNode>(handle.Locator);
			Assert.IsTrue(node is InteriorChunkedDataNode);
			await TestBufferlessReadsAsync(reader, handle.Locator, data);
		}

		private static byte[] CreateBuffer(int length)
		{
			byte[] output = GC.AllocateUninitializedArray<byte>(length);
			for (int i = 0; i < length; i++)
			{
				output[i] = (byte)i;
			}
			return output;
		}

		private static async Task TestBufferlessReadsAsync(TreeReader reader, NodeLocator locator, ReadOnlyMemory<byte> expected)
		{
			using MemoryStream memoryStream = new MemoryStream();
			await ChunkedDataNode.CopyToStreamAsync(reader, locator, memoryStream, default);
			ReadOnlyMemory<byte> read = memoryStream.ToArray().AsMemory();
			Assert.IsTrue(read.Span.SequenceEqual(expected.Span));			
		}

		[TestMethod]
		public void SerializationTests()
		{
			BundleHeader oldHeader;
			{
				List<NodeType> types = new List<NodeType>();
				types.Add(new NodeType(Guid.NewGuid(), 0));

				List<BlobLocator> imports = new List<BlobLocator>();
				imports.Add(new BlobLocator("import1"));
				imports.Add(new BlobLocator("import2"));

				List<BundleExport> exports = new List<BundleExport>();
				exports.Add(new BundleExport(0, IoHash.Compute(Encoding.UTF8.GetBytes("export1")), 0, 0, 2, new BundleExportRef[] { new BundleExportRef(0, 5), new BundleExportRef(0, 6) }));
				exports.Add(new BundleExport(0, IoHash.Compute(Encoding.UTF8.GetBytes("export2")), 1, 0, 3, new BundleExportRef[] { new BundleExportRef(-1, 0) }));

				List<BundlePacket> packets = new List<BundlePacket>();
				packets.Add(new BundlePacket(BundleCompressionFormat.LZ4, 0, 20, 40));
				packets.Add(new BundlePacket(BundleCompressionFormat.LZ4, 20, 10, 20));

				oldHeader = BundleHeader.Create(types, imports, exports, packets);
			}

			byte[] serializedData = oldHeader.Data.ToArray();

			BundleHeader newHeader = BundleHeader.Read(serializedData);
				
			Assert.AreEqual(oldHeader.Imports.Count, newHeader.Imports.Count);
			for (int idx = 0; idx < oldHeader.Imports.Count; idx++)
			{
				Assert.AreEqual(oldHeader.Imports[idx], newHeader.Imports[idx]);
			}

			Assert.AreEqual(oldHeader.Exports.Count, newHeader.Exports.Count);
			for (int idx = 0; idx < oldHeader.Exports.Count; idx++)
			{
				BundleExport oldExport = oldHeader.Exports[idx];
				BundleExport newExport = newHeader.Exports[idx];

				Assert.AreEqual(oldExport.Hash, newExport.Hash);
				Assert.AreEqual(oldExport.Length, newExport.Length);
				Assert.IsTrue(oldExport.References.SequenceEqual(newExport.References));
			}

			Assert.AreEqual(oldHeader.Packets.Count, newHeader.Packets.Count);
			for (int idx = 0; idx < oldHeader.Packets.Count; idx++)
			{
				BundlePacket oldPacket = oldHeader.Packets[idx];
				BundlePacket newPacket = newHeader.Packets[idx];
				Assert.AreEqual(oldPacket.DecodedLength, newPacket.DecodedLength);
				Assert.AreEqual(oldPacket.EncodedLength, newPacket.EncodedLength);
			}
		}

		[TestMethod]
		public async Task BasicTestDirectory()
		{
			MemoryStorageClient store = _storage;
			TreeReader reader = new TreeReader(store, null, NullLogger.Instance);

			DirectoryNode root = new DirectoryNode(DirectoryFlags.None);
			DirectoryNode node = root.AddDirectory("hello");
			DirectoryNode node2 = node.AddDirectory("world");

			RefName refName = new RefName("testref");
			await store.WriteNodeAsync(refName, root);

			// Should be stored inline
			Assert.AreEqual(1, store.Refs.Count);
			Assert.AreEqual(1, store.Blobs.Count);

			// Check the ref
			NodeHandle refTarget = await store.ReadRefTargetAsync(refName);
			Bundle bundle = await store.ReadBundleAsync(refTarget.Locator.Blob);
			Assert.AreEqual(0, bundle.Header.Imports.Count);
			Assert.AreEqual(3, bundle.Header.Exports.Count);

			// Create a new bundle and read it back in again
			DirectoryNode newRoot = await reader.ReadNodeAsync<DirectoryNode>(refName);

			Assert.AreEqual(0, newRoot.Files.Count);
			Assert.AreEqual(1, newRoot.Directories.Count);
			DirectoryNode? outputNode = await newRoot.FindDirectoryAsync(reader, "hello", CancellationToken.None);
			Assert.IsNotNull(outputNode);

			Assert.AreEqual(0, outputNode!.Files.Count);
			Assert.AreEqual(1, outputNode!.Directories.Count);
			DirectoryNode? outputNode2 = await outputNode.FindDirectoryAsync(reader, "world", CancellationToken.None);
			Assert.IsNotNull(outputNode2);

			Assert.AreEqual(0, outputNode2!.Files.Count);
			Assert.AreEqual(0, outputNode2!.Directories.Count);
		}

		[TestMethod]
		public async Task DedupTests()
		{
			TreeOptions options = new TreeOptions();
			options.MaxBlobSize = 1;

			DirectoryNode root = new DirectoryNode(DirectoryFlags.None);
			root.AddDirectory("node1");
			root.AddDirectory("node2");
			root.AddDirectory("node3");

			Assert.AreEqual(0, _storage.Refs.Count);
			Assert.AreEqual(0, _storage.Blobs.Count);

			RefName refName = new RefName("ref");
			await _storage.WriteNodeAsync(refName, root, options);

			Assert.AreEqual(1, _storage.Refs.Count);
			Assert.AreEqual(2, _storage.Blobs.Count);
		}

		[TestMethod]
		public async Task ReloadTests()
		{
			TreeOptions options = new TreeOptions();
			options.MaxBlobSize = 1;

			RefName refName = new RefName("ref");

			{
				DirectoryNode root = new DirectoryNode(DirectoryFlags.None);

				DirectoryNode node1 = root.AddDirectory("node1");
				DirectoryNode node2 = node1.AddDirectory("node2");
				DirectoryNode node3 = node2.AddDirectory("node3");
				DirectoryNode node4 = node3.AddDirectory("node4");

				await _storage.WriteNodeAsync(refName, root, options);

				Assert.AreEqual(1, _storage.Refs.Count);
				Assert.AreEqual(5, _storage.Blobs.Count);
			}

			{
				TreeReader reader = new TreeReader(_storage, null, NullLogger.Instance);

				DirectoryNode root = await reader.ReadNodeAsync<DirectoryNode>(refName);

				DirectoryNode? newNode1 = await root.FindDirectoryAsync(reader, "node1", CancellationToken.None);
				Assert.IsNotNull(newNode1);

				DirectoryNode? newNode2 = await newNode1!.FindDirectoryAsync(reader, "node2", CancellationToken.None);
				Assert.IsNotNull(newNode2);

				DirectoryNode? newNode3 = await newNode2!.FindDirectoryAsync(reader, "node3", CancellationToken.None);
				Assert.IsNotNull(newNode3);

				DirectoryNode? newNode4 = await newNode3!.FindDirectoryAsync(reader, "node4", CancellationToken.None);
				Assert.IsNotNull(newNode4);
			}
		}
	}
}
