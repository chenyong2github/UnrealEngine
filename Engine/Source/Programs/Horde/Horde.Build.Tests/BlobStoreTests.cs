// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Threading.Tasks;
using EpicGames.Core;
using Horde.Build.Jobs.Graphs;
using Horde.Build.Jobs;
using Horde.Build.Issues;
using Horde.Build.Logs;
using Horde.Build.Users;
using Horde.Build.Projects;
using Horde.Build.Streams;
using Horde.Build.Server;
using Horde.Build.Tests.Stubs.Services;
using Horde.Build.Utilities;
using HordeCommon;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using MongoDB.Bson;
using MongoDB.Driver;
using Moq;
using Horde.Build.Issues.Handlers;
using System.Threading;
using EpicGames.Horde.Storage;
using Horde.Build.Storage;
using Horde.Build.Storage.Backends;
using Microsoft.Extensions.Caching.Memory;
using System.Buffers;

namespace Horde.Build.Tests
{
	[TestClass]
	public class BlobStoreTests : TestSetup
	{
		IStorageClient CreateBlobStore()
		{
			return new BasicBlobStore(MongoService, new TransientStorageBackend(), ServiceProvider.GetRequiredService<IMemoryCache>(), ServiceProvider.GetRequiredService<ILogger<BasicBlobStore>>());
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

		static Bundle CreateTestBundle(ReadOnlySequence<byte> data, IReadOnlyList<BlobLocator> refs)
		{
			List<BundleImport> imports = new List<BundleImport>();

			Dictionary<BlobLocator, int> locatorToIndex = new Dictionary<BlobLocator, int>();
			foreach (BlobLocator locator in refs)
			{
				int index;
				if (!locatorToIndex.TryGetValue(locator, out index))
				{
					BundleImport import = new BundleImport(locator, 1, new List<(int, IoHash)> { (0, IoHash.Zero) });
					imports.Add(import);
					locatorToIndex.Add(locator, imports.Count - 1);
				}
			}

			List<BundleExport> exports = new List<BundleExport>();
			exports.Add(new BundleExport(IoHash.Compute(data), (int)data.Length, refs.Select(x => locatorToIndex[x]).ToArray()));

			List<BundlePacket> packets = new List<BundlePacket>();
			packets.Add(new BundlePacket((int)data.Length, (int)data.Length));

			BundleHeader header = new BundleHeader(BundleCompressionFormat.None, imports, exports, packets);
			return new Bundle(header, data);
		}

		static async Task<Blob> ReadBlobAsync(IStorageClient store, BlobLocator locator)
		{
			Bundle bundle = await store.ReadBundleAsync(locator);
			return ExtractBlobFromBundle(bundle);
		}

		static Blob ExtractBlobFromBundle(Bundle bundle)
		{
			Blob blob = new Blob();
			blob.Data = bundle.Payload.AsSingleSegment();
			blob.References = bundle.Header.Exports[0].References.Select(x => bundle.Header.Imports[x].Locator).ToList();
			return blob;
		}

		[TestMethod]
		public async Task LeafTest()
		{
			IStorageClient store = CreateBlobStore();

			byte[] input = CreateTestData(256, 0);
			BlobLocator locator = await store.WriteBundleAsync(CreateTestBundle(new ReadOnlySequence<byte>(input), Array.Empty<BlobLocator>()));

			Bundle outputBundle = await store.ReadBundleAsync(locator);
			Assert.IsTrue(outputBundle.Payload.AsSingleSegment().Span.SequenceEqual(input));
		}

		[TestMethod]
		public async Task ReferenceTest()
		{
			IStorageClient store = CreateBlobStore();

			byte[] input1 = CreateTestData(256, 1);
			BlobLocator locator1 = await store.WriteBundleAsync(CreateTestBundle(new ReadOnlySequence<byte>(input1), Array.Empty<BlobLocator>()));
			Blob blob1 = await ReadBlobAsync(store, locator1);
			Assert.IsTrue(blob1.Data.Span.SequenceEqual(input1));
			Assert.IsTrue(blob1.References.SequenceEqual(Array.Empty<BlobLocator>()));

			byte[] input2 = CreateTestData(256, 2);
			BlobLocator locator2 = await store.WriteBundleAsync(CreateTestBundle(new ReadOnlySequence<byte>(input2), new BlobLocator[] { locator1 }));
			Blob blob2 = await ReadBlobAsync(store, locator2);
			Assert.IsTrue(blob2.Data.Span.SequenceEqual(input2));
			Assert.IsTrue(blob2.References.SequenceEqual(new BlobLocator[] { locator1 }));

			byte[] input3 = CreateTestData(256, 3);
			BlobLocator locator3 = await store.WriteBundleAsync(CreateTestBundle(new ReadOnlySequence<byte>(input3), new BlobLocator[] { locator1, locator2, locator1 }));
			Blob blob3 = await ReadBlobAsync(store, locator3);
			Assert.IsTrue(blob3.Data.Span.SequenceEqual(input3));
			Assert.IsTrue(blob3.References.SequenceEqual(new BlobLocator[] { locator1, locator2, locator1 }));

			for(int idx = 0; idx < 2; idx++)
			{
				RefName refName = new RefName("hello");
				await store.WriteRefTargetAsync(refName, locator3);
				BlobLocator refTargetId = await store.ReadRefTargetAsync(refName);
				Assert.AreEqual(locator3, refTargetId);
			}

			RefName refName2 = new RefName("hello2");

			BlobLocator refTargetId2 = await store.WriteRefAsync(refName2, CreateTestBundle(new ReadOnlySequence<byte>(input3), new BlobLocator[] { locator1, locator2 }));
			Blob refTarget2 = await ReadBlobAsync(store, refTargetId2);
			Assert.IsTrue(refTarget2.Data.Span.SequenceEqual(input3));
			Assert.IsTrue(refTarget2.References.SequenceEqual(new BlobLocator[] { locator1, locator2 }));
		}
	}
}
