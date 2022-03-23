// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Bundles;
using EpicGames.Horde.Bundles.Nodes;
using EpicGames.Horde.Storage;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging;

namespace Horde.Build.Commands.Bundles
{
	[Command("bundle", "extract", "Extracts data from a bundle to the local hard drive")]
	class ExtractCommand : BundleCommandBase
	{
		[CommandLine("-Namespace=")]
		public NamespaceId NamespaceId { get; set; } = new NamespaceId("default-ns");

		[CommandLine("-Bucket=")]
		public BucketId BucketId { get; set; } = new BucketId("default-bkt");

		[CommandLine("-Ref=")]
		public RefId RefId { get; set; } = new RefId("default-ref");

		[CommandLine("-OutputDir=", Required = true)]
		public DirectoryReference OutputDir { get; set; } = null!;

		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			IStorageClient storageClient = CreateStorageClient(logger);
			using (IMemoryCache cache = new MemoryCache(new MemoryCacheOptions()))
			{
				using Bundle<DirectoryNode> newBundle = await Bundle.ReadAsync<DirectoryNode>(storageClient, NamespaceId, BucketId, RefId, new BundleOptions(), cache);
				await newBundle.Root.CopyToDirectoryAsync(newBundle, OutputDir.ToDirectoryInfo(), logger);
			}
			return 0;
		}
	}
}
