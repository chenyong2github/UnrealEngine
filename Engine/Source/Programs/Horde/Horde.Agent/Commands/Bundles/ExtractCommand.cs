// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Bundles;
using EpicGames.Horde.Bundles.Nodes;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Impl;
using EpicGames.Serialization;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;

namespace HordeAgent.Commands.Bundles
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

		public override async Task<int> ExecuteAsync(ILogger Logger)
		{
			IStorageClient StorageClient = CreateStorageClient(Logger);
			using (IMemoryCache Cache = new MemoryCache(new MemoryCacheOptions { SizeLimit = 1024 * 1024 * 1000 }))
			{
				Bundle<DirectoryNode> NewBundle = await Bundle.ReadAsync<DirectoryNode>(StorageClient, NamespaceId, BucketId, RefId, new BundleOptions(), Cache);
				await NewBundle.Root.CopyToDirectoryAsync(OutputDir.ToDirectoryInfo(), Logger);
			}
			return 0;
		}
	}
}
