// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using EpicGames.Core;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using EpicGames.Horde.Storage;
using Horde.Build.Storage;
using System.Threading;
using Horde.Build.Server;
using Horde.Build.Utilities;
using Microsoft.Extensions.DependencyInjection;

namespace Horde.Build.Tests
{
	using BackendId = StringId<IStorageBackend>;

	[TestClass]
	public sealed class GcServiceTests : TestSetup
	{
		async Task SetupNamespaceAsync()
		{
			GlobalConfig globalConfig = new GlobalConfig();
			globalConfig.Storage.Backends.Add(new BackendConfig { Id = new BackendId("default-backend"), Type = StorageBackendType.Memory });
			globalConfig.Storage.Namespaces.Add(new NamespaceConfig { Id = new NamespaceId("default"), Backend = new BackendId("default-backend"), GcDelayHrs = 0.0 });
			await ConfigCollection.AddConfigAsync("globals", globalConfig);
			Assert.IsNotNull(await GlobalsService.TryUpdateAsync(await GlobalsService.GetAsync(), "globals"));
		}

		[TestMethod]
		public async Task CreateBasicTree()
		{
			BlobId.UseDeterministicIds();
			await SetupNamespaceAsync();
			IStorageClientImpl store = await StorageService.GetClientAsync(new NamespaceId("default"), CancellationToken.None);

			Random random = new Random(0);
			BlobLocator[] blobs = await CreateTestDataAsync(store, 2000, 4000, 2000, 10, random);

			HashSet<BlobLocator> roots = new HashSet<BlobLocator>();
			for (int idx = 0; idx < 10; idx++)
			{
				int blobIdx = (int)(random.NextDouble() * blobs.Length);
				if (roots.Add(blobs[blobIdx]))
				{
					await store.WriteRefTargetAsync(new RefName($"ref-{idx}"), new NodeLocator(blobs[blobIdx], 0));
				}
			}

			HashSet<BlobLocator> nodes = await FindNodes(store, roots);

			await Clock.AdvanceAsync(TimeSpan.FromDays(1.0));

			GcService gc = ServiceProvider.GetRequiredService<GcService>();
			await gc.RunAsync(new NamespaceId("default"), CancellationToken.None);

			BlobLocator[] remaining = await store.Backend.EnumerateAsync().Select(x => new BlobLocator(HostId.Empty, StorageBackend.GetBlobIdFromPath(x))).ToArrayAsync();
			Assert.AreEqual(nodes.Count, remaining.Length);
			Assert.IsTrue(remaining.All(x => nodes.Contains(x)));
		}

		async Task<HashSet<BlobLocator>> FindNodes(IStorageClient store, IEnumerable<BlobLocator> roots)
		{
			HashSet<BlobLocator> nodes = new HashSet<BlobLocator>();
			await FindNodes(store, roots, nodes);
			return nodes;
		}

		async Task FindNodes(IStorageClient store, IEnumerable<BlobLocator> roots, HashSet<BlobLocator> nodes)
		{
			foreach (BlobLocator root in roots)
			{
				if (nodes.Add(root))
				{
					Bundle bundle = await store.ReadBundleAsync(root);
					await FindNodes(store, bundle.Header.Imports.Select(x => x.Locator), nodes);
				}
			}
		}

		static async ValueTask<BlobLocator[]> CreateTestDataAsync(IStorageClient store, int numRoots, int numInterior, int numLeaves, int avgChildren, Random random)
		{
			int firstRoot = 0;
			int firstInterior = firstRoot + numRoots;
			int firstLeaf = firstInterior + numInterior;
			int numNodes = firstLeaf + numLeaves;

			List<int>[] children = new List<int>[numNodes];
			for (int idx = 0; idx < numNodes; idx++)
			{
				children[idx] = new List<int>();
			}

			double maxParents = ((numRoots + numInterior) * avgChildren) / (numInterior + numLeaves);
			for (int idx = numRoots; idx < numNodes; idx++)
			{
				int numParents = 1 + (int)(random.NextDouble() * maxParents);
				for (; numParents > 0; numParents--)
				{
					int parentIdx = (int)(random.NextDouble() * Math.Min(idx, numRoots + numInterior - 1));
					children[parentIdx].Add(idx);
				}
			}

			BlobLocator[] locators = new BlobLocator[children.Length];
			for (int idx = numNodes - 1; idx >= 0; idx--)
			{
				List<BundleImport> imports = children[idx].ConvertAll(x => new BundleImport(locators[x], Array.Empty<(int, IoHash)>()));
				BundleHeader header = new BundleHeader(BundleCompressionFormat.None, imports, Array.Empty<BundleExport>(), Array.Empty<BundlePacket>());
				Bundle bundle = new Bundle(header, Array.Empty<ReadOnlyMemory<byte>>());
				locators[idx] = await store.WriteBundleAsync(bundle, prefix: "gctest");
			}

			return locators;
		}
	}
}
