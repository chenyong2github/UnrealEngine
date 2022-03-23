// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Bundles;
using EpicGames.Horde.Bundles.Nodes;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Impl;
using EpicGames.Serialization;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Logging;

namespace Horde.Build.Commands.Bundles
{
	abstract class BundleCommandBase : Command
	{
		[CommandLine("-StorageDir=", Description = "Overrides the default storage server with a local directory")]
		public DirectoryReference? _storageDir = null;

		protected IStorageClient CreateStorageClient(ILogger logger)
		{
			_storageDir ??= DirectoryReference.Combine(Program.DataDir, "Storage");
			return new FileStorageClient(_storageDir, logger);
		}
	}

	[Command("bundle", "create", "Creates a bundle from a folder on the local hard drive")]
	class CreateCommand : BundleCommandBase
	{
		[CommandLine("-Namespace=")]
		public NamespaceId NamespaceId { get; set; } = new NamespaceId("default-ns");

		[CommandLine("-Bucket=")]
		public BucketId BucketId { get; set; } = new BucketId("default-bkt");

		[CommandLine("-Ref=")]
		public RefId RefId { get; set; } = new RefId("default-ref");

		[CommandLine("-InputDir=", Required = true)]
		public DirectoryReference InputDir { get; set; } = null!;

		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			IStorageClient storageClient = base.CreateStorageClient(logger);
			using (MemoryCache cache = new MemoryCache(new MemoryCacheOptions { SizeLimit = 50 * 1024 * 1024 }))
			{
				using (Bundle<DirectoryNode> newBundle = Bundle.Create<DirectoryNode>(storageClient, NamespaceId, new BundleOptions(), cache))
				{
					await newBundle.Root.CopyFromDirectoryAsync(InputDir.ToDirectoryInfo(), new ChunkingOptions(), logger);
					await newBundle.WriteAsync(BucketId, RefId, CbObject.Empty, false);
				}
			}

			return 0;
		}
	}
}
