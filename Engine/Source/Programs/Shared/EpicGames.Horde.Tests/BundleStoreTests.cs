// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Backends;
using EpicGames.Horde.Storage.Bundles;
using EpicGames.Horde.Storage.Nodes;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using System;
using System.Buffers;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using System.Threading;
using System.Text;
using System.IO;
using System.Security.Cryptography;
using EpicGames.Core;

namespace EpicGames.Horde.Tests
{
	[TestClass]
	public class BundleStoreTests
	{
		[TestMethod]
		public async Task TestTreeAsync()
		{
			InMemoryBlobStore blobStore = new InMemoryBlobStore();
			await TestTreeAsync(blobStore, new BundleOptions { MaxBlobSize = 1024 * 1024 });

			Assert.AreEqual(0, blobStore.Blobs.Count);
			Assert.AreEqual(1, blobStore.Refs.Count);
		}

		[TestMethod]
		public async Task TestTreeSeparateBlobsAsync()
		{
			InMemoryBlobStore blobStore = new InMemoryBlobStore();
			await TestTreeAsync(blobStore, new BundleOptions { MaxBlobSize = 1 });

			Assert.AreEqual(4, blobStore.Blobs.Count);
			Assert.AreEqual(1, blobStore.Refs.Count);
		}

		static async Task TestTreeAsync(IBlobStore blobStore, BundleOptions options)
		{
			using IMemoryCache cache = new MemoryCache(new MemoryCacheOptions());

			// Generate a tree
			using (BundleStore collection = new BundleStore(blobStore, options, cache))
			{
				ITreeBlob node1 = await collection.WriteBlobAsync(new ReadOnlySequence<byte>(new byte[] { 1 }), Array.Empty<ITreeBlob>(), CancellationToken.None);
				ITreeBlob node2 = await collection.WriteBlobAsync(new ReadOnlySequence<byte>(new byte[] { 2 }), new[] { node1 }, CancellationToken.None);
				ITreeBlob node3 = await collection.WriteBlobAsync(new ReadOnlySequence<byte>(new byte[] { 3 }), new[] { node2 }, CancellationToken.None);
				ITreeBlob node4 = await collection.WriteBlobAsync(new ReadOnlySequence<byte>(new byte[] { 4 }), Array.Empty<ITreeBlob>(), CancellationToken.None);
				ITreeBlob node5 = await collection.WriteBlobAsync(new ReadOnlySequence<byte>(new byte[] { 5 }), new[] { node4, node3 }, CancellationToken.None);

				await collection.WriteTreeAsync(new RefId("test"), node5);

				ITreeBlob root = node5;
				await CheckTree(root);
			}

			// Check we can read it back in
			using (BundleStore collection = new BundleStore(blobStore, options, cache))
			{
				ITreeBlob root = await collection.ReadTreeAsync(new RefId("test"));
				await CheckTree(root);
			}
		}

		static async Task CheckTree(ITreeBlob root)
		{
			ITreeBlob node5 = root;
			byte[] data5 = (await node5.GetDataAsync()).ToArray();
			Assert.IsTrue(data5.SequenceEqual(new byte[] { 5 }));
			IReadOnlyList<ITreeBlob> refs5 = await node5.GetReferencesAsync();
			Assert.AreEqual(2, refs5.Count);

			ITreeBlob node4 = refs5[0];
			byte[] data4 = (await node4.GetDataAsync()).ToArray();
			Assert.IsTrue(data4.SequenceEqual(new byte[] { 4 }));
			IReadOnlyList<ITreeBlob> refs4 = await node4.GetReferencesAsync();
			Assert.AreEqual(0, refs4.Count);

			ITreeBlob node3 = refs5[1];
			byte[] data3 = (await node3.GetDataAsync()).ToArray();
			Assert.IsTrue(data3.SequenceEqual(new byte[] { 3 }));
			IReadOnlyList<ITreeBlob> refs3 = await node3.GetReferencesAsync();
			Assert.AreEqual(1, refs3.Count);

			ITreeBlob node2 = refs3[0];
			byte[] data2 = (await node2.GetDataAsync()).ToArray();
			Assert.IsTrue(data2.SequenceEqual(new byte[] { 2 }));
			IReadOnlyList<ITreeBlob> refs2 = await node2.GetReferencesAsync();
			Assert.AreEqual(1, refs2.Count);

			ITreeBlob node1 = refs2[0];
			byte[] data1 = (await node1.GetDataAsync()).ToArray();
			Assert.IsTrue(data1.SequenceEqual(new byte[] { 1 }));
			IReadOnlyList<ITreeBlob> refs1 = await node1.GetReferencesAsync();
			Assert.AreEqual(0, refs1.Count);
		}

		[TestMethod]
		public async Task DirectoryNodesAsync()
		{
			using IMemoryCache cache = new MemoryCache(new MemoryCacheOptions());
			InMemoryBlobStore blobStore = new InMemoryBlobStore();

			// Generate a tree
			using (BundleStore<DirectoryNode> store = new BundleStore<DirectoryNode>(blobStore, new BundleOptions(), cache))
			{
				DirectoryNode root = new DirectoryNode();
				DirectoryNode hello = root.AddDirectory("hello");
				DirectoryNode world = hello.AddDirectory("world");
				await store.WriteTreeAsync(new RefId("test"), root, true, CancellationToken.None);

				await CheckDirectoryTreeAsync(root);
			}

			// Check we can read it back in
			using (BundleStore<DirectoryNode> store = new BundleStore<DirectoryNode>(blobStore, new BundleOptions(), cache))
			{
				DirectoryNode root = await store.ReadTreeAsync(new RefId("test"));
				await CheckDirectoryTreeAsync(root);
			}
		}

		static async Task CheckDirectoryTreeAsync(DirectoryNode root)
		{
			Assert.AreEqual(1, root.Directories.Count);
			Assert.AreEqual("hello", root.Directories.First().Name);

			DirectoryNode hello = await root.Directories.First().ExpandAsync(CancellationToken.None);
			Assert.AreEqual(1, hello.Directories.Count);
			Assert.AreEqual("world", hello.Directories.First().Name);

			DirectoryNode world = await hello.Directories.First().ExpandAsync(CancellationToken.None);
			Assert.AreEqual(0, world.Directories.Count);
		}

		[TestMethod]
		public async Task FileNodesAsync()
		{
			using IMemoryCache cache = new MemoryCache(new MemoryCacheOptions());
			InMemoryBlobStore blobStore = new InMemoryBlobStore();

			// Generate a tree
			using (BundleStore<DirectoryNode> store = new BundleStore<DirectoryNode>(blobStore, new BundleOptions(), cache))
			{
				DirectoryNode root = new DirectoryNode();
				DirectoryNode hello = root.AddDirectory("hello");

				FileNode world = await hello.AddFileAsync("world", FileEntryFlags.None, Encoding.UTF8.GetBytes("world"), new ChunkingOptions(), CancellationToken.None);
				
				await store.WriteTreeAsync(new RefId("test"), root, true, CancellationToken.None);

				await CheckFileTreeAsync(root);
			}

			// Check we can read it back in
			using (BundleStore<DirectoryNode> store = new BundleStore<DirectoryNode>(blobStore, new BundleOptions(), cache))
			{
				DirectoryNode root = await store.ReadTreeAsync(new RefId("test"));
				await CheckFileTreeAsync(root);
			}
		}

		static async Task CheckFileTreeAsync(DirectoryNode root)
		{
			Assert.AreEqual(1, root.Directories.Count);
			Assert.AreEqual("hello", root.Directories.First().Name);

			DirectoryNode hello = await root.Directories.First().ExpandAsync(CancellationToken.None);
			Assert.AreEqual(0, hello.Directories.Count);
			Assert.AreEqual(1, hello.Files.Count);
			Assert.AreEqual("world", hello.Files.First().Name);

			FileNode world = await hello.Files.First().ExpandAsync(CancellationToken.None);

			byte[] worldData = await GetFileDataAsync(world);
			Assert.IsTrue(worldData.SequenceEqual(Encoding.UTF8.GetBytes("world")));
		}

		[TestMethod]
		public void ChunkingTests()
		{
			byte[] chunk = RandomNumberGenerator.GetBytes(256);

			byte[] data = new byte[chunk.Length * 1024];
			for (int idx = 0; idx * chunk.Length < data.Length; idx++)
			{
				chunk.CopyTo(data.AsSpan(idx * chunk.Length));
			}

			ChunkingOptions options = new ChunkingOptions();
			options.LeafOptions.MinSize = 1;
			options.LeafOptions.TargetSize = 64;
			options.LeafOptions.MaxSize = 1024;

			ReadOnlyMemory<byte> remaining = data;

			Dictionary<IoHash, long> uniqueChunks = new Dictionary<IoHash, long>();

			List<LeafFileNode> nodes = new List<LeafFileNode>();
			while (remaining.Length > 0)
			{
				LeafFileNode node = new LeafFileNode();
				remaining = node.AppendData(remaining, options);
				nodes.Add(node);

				IoHash hash = IoHash.Compute(node.Data);
				uniqueChunks[hash] = node.Length;
			}

			long uniqueSize = uniqueChunks.Sum(x => x.Value);
			Assert.IsTrue(uniqueSize < data.Length / 3);
		}

		[TestMethod]
		public async Task LargeFileTestAsync()
		{
			using IMemoryCache cache = new MemoryCache(new MemoryCacheOptions());
			InMemoryBlobStore blobStore = new InMemoryBlobStore();

			const int length = 1024;
			const int copies = 4096;

			byte[] chunk = new byte[length];
			new Random(0).NextBytes(chunk);

			byte[] data = new byte[chunk.Length * copies];
			for (int idx = 0; idx < copies; idx++)
			{
				chunk.CopyTo(data.AsSpan(idx * chunk.Length));
			}

			// Generate a tree
			using (BundleStore<DirectoryNode> store = new BundleStore<DirectoryNode>(blobStore, new BundleOptions { MaxInlineBlobSize = 1024, MaxBlobSize = 1024 }, cache))
			{
				DirectoryNode root = new DirectoryNode();

				ChunkingOptions options = new ChunkingOptions();
				options.LeafOptions.MinSize = 1;
				options.LeafOptions.TargetSize = 128;
				options.LeafOptions.MaxSize = 64 * 1024;

				FileNode file = await root.AddFileAsync("test", FileEntryFlags.None, data, options, CancellationToken.None);

				await store.WriteTreeAsync(new RefId("test"), root, true, CancellationToken.None);

				await CheckLargeFileTreeAsync(root, data);
			}

			// Check we can read it back in
			using (BundleStore<DirectoryNode> store = new BundleStore<DirectoryNode>(blobStore, new BundleOptions(), cache))
			{
				DirectoryNode root = await store.ReadTreeAsync(new RefId("test"));
				await CheckLargeFileTreeAsync(root, data);

				TreeNodeRef<FileNode> file = root.GetFileEntry("test");
				ITreeBlob blob = await file.CollapseAsync(store, CancellationToken.None);

				Dictionary<ITreeBlob, long> uniqueBlobs = new Dictionary<ITreeBlob, long>();
				await FindUniqueBlobs(blob, uniqueBlobs);

				long uniqueSize = uniqueBlobs.Sum(x => x.Value);
				Assert.IsTrue(uniqueSize < data.Length / 3); // random fraction meaning "lots of dedupe happened"
			}
		}

		static async Task CheckLargeFileTreeAsync(DirectoryNode root, byte[] data)
		{
			Assert.AreEqual(0, root.Directories.Count);
			Assert.AreEqual(1, root.Files.Count);

			FileNode world = await root.Files.First().ExpandAsync(CancellationToken.None);

			byte[] worldData = await GetFileDataAsync(world);
			Assert.AreEqual(data.Length, worldData.Length);
			Assert.IsTrue(worldData.SequenceEqual(data));
		}

		static async Task FindUniqueBlobs(ITreeBlob blob, Dictionary<ITreeBlob, long> uniqueBlobs)
		{
			ReadOnlySequence<byte> data = await blob.GetDataAsync();
			uniqueBlobs[blob] = data.Length;

			IReadOnlyList<ITreeBlob> references = await blob.GetReferencesAsync();
			foreach (ITreeBlob reference in references)
			{
				await FindUniqueBlobs(reference, uniqueBlobs);
			}
		}

		static async Task<byte[]> GetFileDataAsync(FileNode fileNode)
		{
			using (MemoryStream stream = new MemoryStream())
			{
				await fileNode.CopyToStreamAsync(stream, CancellationToken.None);
				return stream.ToArray();
			}
		}
	}
}
