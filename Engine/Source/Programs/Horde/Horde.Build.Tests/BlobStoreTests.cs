// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using EpicGames.Core;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using MongoDB.Driver;
using EpicGames.Horde.Storage;
using System.Buffers;
using EpicGames.Horde.Storage.Backends;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging.Abstractions;

namespace Horde.Build.Tests
{
	[TestClass]
	public class BlobStoreTests : TestSetup
	{
		IMemoryCache? _cache;
		InMemoryBlobStore? _store;

		IStorageClient CreateBlobStore()
		{
			_cache ??= new MemoryCache(new MemoryCacheOptions());
			_store ??= new InMemoryBlobStore(_cache, NullLogger.Instance);
			return _store;
		}

		protected override void Dispose(bool disposing)
		{
			base.Dispose(disposing);

			if (disposing)
			{
				_cache?.Dispose();
			}
		}

		static byte[] CreateTestData(int length, int seed)
		{
			byte[] data = new byte[length];
			new Random(seed).NextBytes(data);
			return data;
		}

		class Blob
		{
			public ReadOnlyMemory<byte> Data { get; set; } = ReadOnlyMemory<byte>.Empty;
			public List<BlobLocator> References { get; set; } = new List<BlobLocator>();
		}

		static Bundle CreateTestBundle(ReadOnlyMemory<byte> data, IReadOnlyList<BlobLocator> refs)
		{
			List<BundleImport> imports = new List<BundleImport>();

			Dictionary<BlobLocator, int> locatorToIndex = new Dictionary<BlobLocator, int>();
			foreach (BlobLocator locator in refs)
			{
				int index;
				if (!locatorToIndex.TryGetValue(locator, out index))
				{
					BundleImport import = new BundleImport(locator, new List<(int, IoHash)> { (0, IoHash.Zero) });
					imports.Add(import);
					locatorToIndex.Add(locator, imports.Count - 1);
				}
			}

			List<BundleExport> exports = new List<BundleExport>();
			exports.Add(new BundleExport(IoHash.Compute(data.Span), (int)data.Length, refs.Select(x => locatorToIndex[x]).ToArray()));

			List<BundlePacket> packets = new List<BundlePacket>();
			packets.Add(new BundlePacket((int)data.Length, (int)data.Length));

			BundleHeader header = new BundleHeader(BundleCompressionFormat.None, imports, exports, packets);
			return new Bundle(header, new[] { data });
		}

		static async Task<Blob> ReadBlobAsync(IStorageClient store, BlobLocator locator)
		{
			Bundle bundle = await store.ReadBundleAsync(locator);
			return ExtractBlobFromBundle(bundle);
		}

		static Blob ExtractBlobFromBundle(Bundle bundle)
		{
			Blob blob = new Blob();
			blob.Data = bundle.Packets[0];
			blob.References = bundle.Header.Exports[0].References.Select(x => bundle.Header.Imports[x].Locator).ToList();
			return blob;
		}

		[TestMethod]
		public async Task LeafTest()
		{
			IStorageClient store = CreateBlobStore();

			byte[] input = CreateTestData(256, 0);
			BlobLocator locator = await store.WriteBundleAsync(CreateTestBundle(input, Array.Empty<BlobLocator>()));

			Bundle outputBundle = await store.ReadBundleAsync(locator);
			Assert.IsTrue(outputBundle.Packets[0].Span.SequenceEqual(input));
		}

		[TestMethod]
		public async Task ReferenceTest()
		{
			IStorageClient store = CreateBlobStore();

			byte[] input1 = CreateTestData(256, 1);
			BlobLocator locator1 = await store.WriteBundleAsync(CreateTestBundle(input1, Array.Empty<BlobLocator>()));
			Blob blob1 = await ReadBlobAsync(store, locator1);
			Assert.IsTrue(blob1.Data.Span.SequenceEqual(input1));
			Assert.IsTrue(blob1.References.SequenceEqual(Array.Empty<BlobLocator>()));

			byte[] input2 = CreateTestData(256, 2);
			BlobLocator locator2 = await store.WriteBundleAsync(CreateTestBundle(input2, new BlobLocator[] { locator1 }));
			Blob blob2 = await ReadBlobAsync(store, locator2);
			Assert.IsTrue(blob2.Data.Span.SequenceEqual(input2));
			Assert.IsTrue(blob2.References.SequenceEqual(new BlobLocator[] { locator1 }));

			byte[] input3 = CreateTestData(256, 3);
			BlobLocator locator3 = await store.WriteBundleAsync(CreateTestBundle(input3, new BlobLocator[] { locator1, locator2, locator1 }));
			Blob blob3 = await ReadBlobAsync(store, locator3);
			Assert.IsTrue(blob3.Data.Span.SequenceEqual(input3));
			Assert.IsTrue(blob3.References.SequenceEqual(new BlobLocator[] { locator1, locator2, locator1 }));

			for(int idx = 0; idx < 2; idx++)
			{
				RefName refName = new RefName("hello");
				await store.WriteRefTargetAsync(refName, new NodeLocator(locator3, 0));
				NodeLocator refTarget = await store.ReadRefTargetAsync(refName);
				Assert.AreEqual(locator3, refTarget.Blob);
			}

			RefName refName2 = new RefName("hello2");

			NodeLocator refTargetId2 = await store.WriteRefAsync(refName2, CreateTestBundle(input3, new BlobLocator[] { locator1, locator2 }), 0);
			Blob refTarget2 = await ReadBlobAsync(store, refTargetId2.Blob);
			Assert.IsTrue(refTarget2.Data.Span.SequenceEqual(input3));
			Assert.IsTrue(refTarget2.References.SequenceEqual(new BlobLocator[] { locator1, locator2 }));
		}
	}
}
